#!/usr/bin/env bash
# Regenerates the staging folders of the OdinGuard competitive pack (assets/competitive/recipe.toml).
#
# Every staging folder is <OUT>/<folder>/data/... and is what the Studio copies into odin-dark.grf
# (profile: assets.competitive_source = <OUT>, assets.competitive = [<folder>, ...]).
#
# Required inputs (read only, never modified):
#   DARK_DIR    folder with dark.grf, dark2.grf, nodelay.grf (darkworld; sha256 checked below)
#   CLIENT_DIR  client folder with DATA.INI and its GRFs (flat maps and vanilla ground are made from it;
#               odin*.grf entries of its DATA.INI are ignored so the pack is never built from itself)
# Optional:
#   OUT         staging root            (default: $HOME/Dev/OdinGuard-assets/competitive)
#   WORK        cache for extractions   (default: $OUT/.work)
#   RGRF        rgrf CLI                (default: rgrf in PATH; build: cargo build --release -p rgrf-cli)
#   TEST_GRF_DIR  if set: build one test GRF per staging folder there and verify it
#   ALLOW_OTHER_SOURCES=1  accept source GRFs with another sha256 (entries are still MD5-checked)
# Tools: bash, python3 (3.9+, standard library), shasum or sha256sum.
#
# Usage: tools/pack/build_categories.sh [folder ...]
#   folders: floor trees barricades pneuma nodelay effects ui other
#            flat-maps-woe-bg-pvp flat-maps-all floor-flat-woe-bg-pvp floor-flat-all
#   (default: all). Idempotent: each selected folder is deleted and written again.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
MAN="$REPO/assets/competitive/manifests"
PY="python3 $HERE/competitive.py"
RGRF="${RGRF:-rgrf}"
OUT="${OUT:-$HOME/Dev/OdinGuard-assets/competitive}"
WORK="${WORK:-$OUT/.work}"
: "${DARK_DIR:?set DARK_DIR to the folder with dark.grf, dark2.grf, nodelay.grf}"
: "${CLIENT_DIR:?set CLIENT_DIR to the client folder with DATA.INI}"

# sha256 of the sources the manifests were made from (same values as in recipe.toml); bash 3.2 compatible
SOURCES="dark.grf:f7baefcdcb8475a4c37e268329cefedcb75357165b7cf1f069c662596ba906ae
dark2.grf:045c71de65804624e28fe0a7c4e8225a6408ead27831911a5f9e1f21daa1a748
nodelay.grf:0dba7a75f6a5f48c2588fb64387b6529d594e4c0ab04b27879eb4911508c8f11"
ALL=(floor trees barricades pneuma nodelay effects ui other flat-maps-woe-bg-pvp flat-maps-all floor-flat-woe-bg-pvp floor-flat-all)
SEL=("$@"); [ ${#SEL[@]} -eq 0 ] && SEL=("${ALL[@]}")
want() { local x; for x in "${SEL[@]}"; do [ "$x" = "$1" ] && return 0; done; return 1; }
for x in "${SEL[@]}"; do case " ${ALL[*]} " in *" $x "*) ;; *) echo "unknown folder: $x" >&2; exit 2;; esac; done

sha256() { if command -v shasum >/dev/null; then shasum -a 256 "$1" | cut -d' ' -f1; else sha256sum "$1" | cut -d' ' -f1; fi; }
rg() { "$RGRF" -q --names cp949 "$@"; }
rgx() { "$RGRF" -q --names cp949 extract "$@" >/dev/null; }   # extract prints a summary even with -q

"$RGRF" --version >/dev/null
for line in $SOURCES; do
  g=${line%%:*}; expect=${line#*:}
  [ -f "$DARK_DIR/$g" ] || { echo "missing $DARK_DIR/$g" >&2; exit 1; }
  h=$(sha256 "$DARK_DIR/$g")
  if [ "$h" != "$expect" ]; then
    if [ "${ALLOW_OTHER_SOURCES:-0}" = 1 ]; then echo "warning: $g sha256 $h differs from the recipe" >&2
    else echo "error: $g sha256 $h differs from the recipe (ALLOW_OTHER_SOURCES=1 to continue)" >&2; exit 1; fi
  fi
done
mkdir -p "$OUT" "$WORK"

# ---- client view without OdinGuard's own GRFs: $WORK/client/DATA.INI + links to the original GRFs
CL="$WORK/client"; rm -rf "$CL"; mkdir -p "$CL"
GRFS=()
while IFS= read -r g; do
  [ -f "$CLIENT_DIR/$g" ] || { echo "DATA.INI names a missing GRF: $g" >&2; exit 1; }
  ln -s "$CLIENT_DIR/$g" "$CL/$g"; GRFS+=("$g")
done < <(tr -d '\r' < "$CLIENT_DIR/DATA.INI" | awk -F= '/^[0-9]+=/{print $1"\t"$2}' | sort -n | cut -f2 | grep -viE '^odin(-dark)?\.grf$')
{ echo "[Data]"; i=1; for g in "${GRFS[@]}"; do echo "$i=$g"; i=$((i+1)); done; } > "$CL/DATA.INI"
echo "client GRFs (priority order): ${GRFS[*]}"

# ---- darkworld caches (non-map entries plus root-level files)
needs_cache=0
for c in floor trees barricades pneuma nodelay effects ui other floor-flat-woe-bg-pvp floor-flat-all; do want "$c" && needs_cache=1; done
CACHE="$WORK/cache"
if [ $needs_cache = 1 ]; then
  rm -rf "$CACHE"; mkdir -p "$CACHE"
  for g in dark dark2; do
    rgx "$DARK_DIR/$g.grf" "$CACHE/$g" 'data\*\*'
    for p in 'data\*.tga' 'data\*.txt' 'data\*.xml' 'data\*.act' 'data\*.spr'; do
      rgx --on-collision skip "$DARK_DIR/$g.grf" "$CACHE/$g" "$p" || true
    done
  done
  # nodelay.grf also holds placeholder entries named like folders; take only .act/.spr
  rgx "$DARK_DIR/nodelay.grf" "$CACHE/nodelay" 'data\sprite\*.act'
  rgx "$DARK_DIR/nodelay.grf" "$CACHE/nodelay" 'data\sprite\*.spr'
  # tile textures with lower-case names, used by the floor folders
  rm -rf "$WORK/tiles"; $PY stage "$MAN/floor.tsv" "$CACHE" "$WORK/tiles" >/dev/null
fi

# ---- manifest categories
for c in trees barricades pneuma nodelay effects ui other; do
  want "$c" || continue
  rm -rf "${OUT:?}/$c"; mkdir -p "$OUT/$c"
  $PY stage "$MAN/$c.tsv" "$CACHE" "$OUT/$c"
done

# ---- map lists of the client
LISTS="$WORK/lists"; rm -rf "$LISTS"; mkdir -p "$LISTS"; : > "$LISTS/rsw.txt"; : > "$LISTS/src.tsv"
for g in "${GRFS[@]}"; do
  rg ls "$CL/$g" 'data\*.rsw' >> "$LISTS/rsw.txt" || true
  { rg ls "$CL/$g" 'data\*.gnd' || true; rg ls "$CL/$g" 'data\*.gat' || true; rg ls "$CL/$g" 'data\*.rsw' || true; } | awk -v g="$g" '{print tolower($0)"\t"g}' >> "$LISTS/src.tsv"
done
# first GRF in DATA.INI order that has a file (what the client reads)
src_of() { K="$1" awk -F'\t' 'tolower($1)==ENVIRON["K"]{print $2; exit}' "$LISTS/src.tsv"; }
for s in woe-bg-pvp all; do $PY scope "$s" "$LISTS/rsw.txt" > "$LISTS/maps-$s.txt"; done
# ground files the WoE/BG/PvP maps really use (an .rsw may name another map's .gnd, e.g. rush_cas01 -> prtg_cas02)
while IFS= read -r m; do
  g=$(src_of "data\\$m.rsw"); [ -n "$g" ] || continue
  "$RGRF" -q --names cp949 --json map info "$CL/$g" "data\\$m.rsw" | python3 -c 'import json,sys; print(json.load(sys.stdin)["ground"].lower()[:-4])'
done < "$LISTS/maps-woe-bg-pvp.txt" | sort -u > "$LISTS/ground-woe-bg-pvp.txt"

# ---- flat maps (rgrf map flat = GRF Editor's flat maps maker: flatten, remove light and shadow)
for s in woe-bg-pvp all; do
  f="flat-maps-$s"
  if want "$f" || { want "floor-flat-$s" && [ ! -d "$OUT/$f/data" ]; }; then
    rm -rf "${OUT:?}/$f"; mkdir -p "$OUT/$f"
    if [ "$s" = all ]; then
      rg map flat --flatten --remove-light --remove-shadow --overwrite -o "$WORK/$f.grf" "$CL" || flat_rc=$?
    else
      # shellcheck disable=SC2046
      rg map flat --flatten --remove-light --remove-shadow --overwrite -o "$WORK/$f.grf" "$CL" $(cat "$LISTS/maps-$s.txt") || flat_rc=$?
    fi
    # exit 3 = some maps of the client lack files (listed above); they are left out
    case "${flat_rc:-0}" in 0|3) ;; *) echo "rgrf map flat failed ($flat_rc)" >&2; exit 1;; esac; flat_rc=0
    rgx "$WORK/$f.grf" "$OUT/$f"
    echo "$f: $(find "$OUT/$f" -type f | wc -l | tr -d ' ') files"
  fi
done

# ---- darkworld map files of the WoE/BG/PvP maps (only for their WoE marker tiles)
if want floor || want floor-flat-woe-bg-pvp || want floor-flat-all; then
  rm -rf "$WORK/darkmaps"; mkdir -p "$WORK/darkmaps"
  while IFS= read -r m; do rgx "$DARK_DIR/dark2.grf" "$WORK/darkmaps" "data\\$m.g??" 2>/dev/null || true; done < "$LISTS/maps-woe-bg-pvp.txt"
fi

# ---- floor on the original maps (heights, walls, models and shadows stay), WoE/BG/PvP maps
if want floor; then
  rm -rf "$WORK/van"; mkdir -p "$WORK/van"
  while IFS= read -r m; do
    for ext in gnd gat; do
      g=$(src_of "data\\$m.$ext")
      if [ -n "$g" ]; then rgx "$CL/$g" "$WORK/van" "data\\$m.$ext"; fi
    done
  done < "$LISTS/ground-woe-bg-pvp.txt"
  rm -rf "${OUT:?}/floor"
  $PY floor --maps-dir "$WORK/van" --tiles "$WORK/tiles/data/texture" --markers "$WORK/darkmaps" \
    --maps "$LISTS/ground-woe-bg-pvp.txt" --out "$OUT/floor"
fi

# ---- floor on the flat maps (replaces the .gnd of flat-maps-<scope>; list it after flat-maps)
for s in woe-bg-pvp all; do
  f="floor-flat-$s"; want "$f" || continue
  rm -rf "${OUT:?}/$f"
  $PY floor --maps-dir "$OUT/flat-maps-$s" --tiles "$WORK/tiles/data/texture" --markers "$WORK/darkmaps" \
    --fill-void --out "$OUT/$f"
done

# ---- sizes and optional test GRFs
dirs=(); for x in "${SEL[@]}"; do [ -d "$OUT/$x/data" ] && dirs+=("$OUT/$x"); done
$PY measure "${dirs[@]}" | tee "$WORK/sizes.tsv"
if [ -n "${TEST_GRF_DIR:-}" ]; then
  mkdir -p "$TEST_GRF_DIR"
  for d in "${dirs[@]}"; do
    n=$(basename "$d")
    rg build --overwrite -o "$TEST_GRF_DIR/odin-dark-$n.grf" "$d" >/dev/null
    rg verify "$TEST_GRF_DIR/odin-dark-$n.grf" >/dev/null && echo "verify ok: odin-dark-$n.grf ($(wc -c < "$TEST_GRF_DIR/odin-dark-$n.grf" | tr -d ' ') bytes)"
  done
fi
