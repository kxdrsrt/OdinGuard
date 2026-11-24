#!/usr/bin/env bash
# Builds the staging folder of the LGP skill sounds (asset group "lgp-sound" of odin.grf) from the operator's own client.
#
# The 2018 client plays a ground skill's sound together with its animation. LGP players do not get the animation, so the
# server sends the sound alone (packet 0x1d3, og_lgp_sound_skills). That packet only has 23 characters for the file name,
# but the original names are longer (effect\wizard_stormgust.wav), so every sound is copied once under a short name:
# data\wav\og_<skill id>.wav. The copies are made from the client's GRFs on the operator's machine and are never part of
# the repository (same model as the competitive pack).
#
# Usage: tools/pack/build_lgp_sounds.sh <client dir with DATA.INI> [out dir]     (default out: ~/Dev/OdinGuard-assets/lgp-sound)
# Then set  [assets.odin_grf] lgp-sound = "<out dir>"  in the profile and rebuild (og build).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TSV="$HERE/../../assets/lgp-sound/sounds.tsv"
CLIENT="${1:?client folder with DATA.INI}"
OUT="${2:-$HOME/Dev/OdinGuard-assets/lgp-sound}"
RGRF="${RGRF:-rgrf}"
grfs=()
while IFS= read -r g; do grfs+=("$g"); done < <(tr -d '\r' < "$CLIENT/DATA.INI" | awk -F= '/^[0-9]+=/{print $1"\t"$2}' | sort -n | cut -f2 | grep -viE '^odin(-dark)?\.grf$')
rm -rf "$OUT"; mkdir -p "$OUT/data/wav"
n=0
tail -n +2 "$TSV" | while IFS=$'\t' read -r id src note; do
  [ -n "$id" ] || continue
  found=0
  for g in "${grfs[@]}"; do   # DATA.INI order: the first GRF that has the file wins
    if "$RGRF" -q --names cp949 cat "$CLIENT/$g" "data\\wav\\effect\\$src" > "$OUT/data/wav/og_$id.wav" 2>/dev/null && [ -s "$OUT/data/wav/og_$id.wav" ]; then found=1; break; fi
  done
  if [ $found = 1 ]; then echo "og_$id.wav <- $src"; else rm -f "$OUT/data/wav/og_$id.wav"; echo "missing in the client: $src (skill $id)" >&2; fi
done
echo "done: $(ls "$OUT/data/wav" | wc -l | tr -d ' ') sounds in $OUT"
