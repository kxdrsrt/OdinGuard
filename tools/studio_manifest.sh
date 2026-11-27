#!/usr/bin/env bash
# Turns a folder of signed Studio builds (from every platform) into ONE signed update manifest.
#
#   tools/studio_manifest.sh <stable|beta> <https base url> <folder> <notes file or ""> [<signing key file>]
#
# The folder holds the updater artifacts with their .sig next to them: *.app.tar.gz (macOS arm64), *-setup.exe
# (Windows x64), *.AppImage (Linux x64). The manifest <channel>.json and <channel>.json.sig are written into the folder,
# and verified against the public key that is built into the Studio (tauri.conf.json), exactly like an installed Studio.
# Used by .github/workflows/studio-release.yml; the signing key arrives as TAURI_SIGNING_PRIVATE_KEY (file content).
set -euo pipefail

CHANNEL=${1:?channel}
BASE_URL=${2:?base url}
DIR=${3:?folder}
NOTES_FILE=${4:-}
KEY_FILE=${5:-}
case "$CHANNEL" in stable|beta) ;; *) echo "channel must be stable or beta" >&2; exit 2 ;; esac
case "$BASE_URL" in https://*) ;; *) echo "base url must be https" >&2; exit 2 ;; esac

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="$ROOT/studio/apps/studio"
CONF="$APP/src-tauri/tauri.conf.json"
VERSION=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$CONF")
NOTES=""
[ -n "$NOTES_FILE" ] && NOTES=$(cat "$NOTES_FILE")

ARGS=()
for sig in "$DIR"/*.sig; do
  f=${sig%.sig}
  case "$(basename "$f")" in
    *.json) continue ;;
    *.app.tar.gz) t=darwin-aarch64 ;;
    *-setup.exe) t=windows-x86_64 ;;
    *.AppImage|*.AppImage.tar.gz) t=linux-x86_64 ;;
    *) echo "skipping unknown artifact $(basename "$f")" >&2; continue ;;
  esac
  [ -f "$f" ] || { echo "$sig has no artifact" >&2; exit 1; }
  ARGS+=(--artifact "$t=$f")
done
[ ${#ARGS[@]} -gt 0 ] || { echo "no signed artifacts in $DIR" >&2; exit 1; }

(cd "$ROOT/studio" && cargo build -q -p og-cli)
OG="$ROOT/studio/target/debug/og"
"$OG" update-manifest --version "$VERSION" --notes "$NOTES" --base-url "$BASE_URL" "${ARGS[@]}" --out "$DIR/$CHANNEL.json"

rm -f "$DIR/$CHANNEL.json.sig"
if [ -n "$KEY_FILE" ]; then
  (cd "$APP" && env -u TAURI_SIGNING_PRIVATE_KEY npx tauri signer sign -f "$KEY_FILE" -p "${TAURI_SIGNING_PRIVATE_KEY_PASSWORD:-}" "$DIR/$CHANNEL.json" >/dev/null)
else
  (cd "$APP" && npx tauri signer sign "$DIR/$CHANNEL.json" >/dev/null)
fi

PUB=$(python3 -c 'import json,base64,sys; print(base64.b64decode(json.load(open(sys.argv[1]))["plugins"]["updater"]["pubkey"]).decode())' "$CONF")
"$OG" update-verify "$DIR/$CHANNEL.json" --pubkey "$PUB"
SUM=$(command -v sha256sum >/dev/null && echo sha256sum || echo "shasum -a 256")
( cd "$DIR" && rm -f SHA256SUMS && ls | xargs $SUM > SHA256SUMS )
echo "manifest for $VERSION ($CHANNEL): $((${#ARGS[@]} / 2)) platform artifact(s) in $DIR"
