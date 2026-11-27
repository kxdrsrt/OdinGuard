#!/usr/bin/env bash
# Builds and signs an OdinGuard Studio release for the current platform into dist/studio/<version>/<channel>/.
# Publishes only when you ask for it (OG_PUBLISH_TAG); without it you upload the folder yourself after checking it.
#
#   tools/release_studio.sh <stable|beta> <https base url> [notes file]
#   OG_PUBLISH_TAG=v1.0.7 tools/release_studio.sh stable https://github.com/kxdrsrt/OdinGuard/releases/latest/download
#
# OG_PUBLISH_TAG: after the checks, upload the folder (manifest, its signature, the signed artifact) to that GitHub
# release with `gh` (the release must exist; files are replaced). Without a notes file the release's text is used as
# the notes that Studio shows. Every release needs its stable.json: "latest/download/stable.json" is what every
# Studio asks for, and a release without it is invisible to them.
#
# Signing key: keys/studio-updater.key (minisign, created with `npx tauri signer generate`, never committed);
# override with OG_UPDATER_KEY / OG_UPDATER_KEY_PASSWORD. OG_SKIP_BUILD=1 reuses an existing bundle.
set -euo pipefail

CHANNEL=${1:?channel: stable or beta}
BASE_URL=${2:?https base url of the update host, e.g. https://updates.example.org/studio}
NOTES_FILE=${3:-}
case "$CHANNEL" in stable|beta) ;; *) echo "channel must be stable or beta" >&2; exit 2 ;; esac
case "$BASE_URL" in https://*) ;; *) echo "base url must be https" >&2; exit 2 ;; esac

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="$ROOT/studio/apps/studio"
KEY=${OG_UPDATER_KEY:-$ROOT/keys/studio-updater.key}
PW=${OG_UPDATER_KEY_PASSWORD:-}
[ -f "$KEY" ] || { echo "signing key $KEY missing" >&2; exit 1; }
VERSION=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$APP/src-tauri/tauri.conf.json")
NOTES=""
[ -n "$NOTES_FILE" ] && NOTES=$(cat "$NOTES_FILE")
PUBLISH_TAG=${OG_PUBLISH_TAG:-}
if [ -z "$NOTES" ] && [ -n "$PUBLISH_TAG" ]; then
  NOTES=$(gh release view "$PUBLISH_TAG" --json body -q .body)
fi

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) TARGET=darwin-aarch64; BUNDLES=app ;;
  Darwin-x86_64) TARGET=darwin-x86_64; BUNDLES=app ;;
  Linux-x86_64) TARGET=linux-x86_64; BUNDLES=appimage ;;
  MINGW*|MSYS*|CYGWIN*) TARGET=windows-x86_64; BUNDLES=nsis ;;
  *) echo "unsupported platform $(uname -sm)" >&2; exit 2 ;;
esac

export PATH="$HOME/.cargo/bin:$PATH"
SOURCE_DATE_EPOCH=$(git -C "$ROOT" log -1 --format=%ct)
export SOURCE_DATE_EPOCH

OUT="$ROOT/dist/studio/$VERSION/$CHANNEL"
mkdir -p "$OUT"

if [ "${OG_SKIP_BUILD:-0}" != 1 ]; then
  (cd "$APP" && TAURI_SIGNING_PRIVATE_KEY=$(cat "$KEY") TAURI_SIGNING_PRIVATE_KEY_PASSWORD="$PW" \
    npx tauri build --bundles "$BUNDLES" --ci)
fi

BUNDLE_DIR="$APP/src-tauri/target/release/bundle"
ART=""
while IFS= read -r sig; do
  f=${sig%.sig}
  case "$f" in *"_$VERSION"*|*.app.tar.gz) ART=$f ;; esac
done < <(find "$BUNDLE_DIR" -name '*.sig' -type f)
[ -n "$ART" ] && [ -f "$ART" ] || { echo "no signed updater artifact in $BUNDLE_DIR" >&2; exit 1; }

NAME=$(basename "$ART" | tr ' ' '_')
cp "$ART" "$OUT/$NAME"
cp "$ART.sig" "$OUT/$NAME.sig"

(cd "$ROOT/studio" && cargo build -q -p og-cli)
OG="$ROOT/studio/target/debug/og"
"$OG" update-manifest --version "$VERSION" --notes "$NOTES" --base-url "$BASE_URL" --artifact "$TARGET=$OUT/$NAME" --out "$OUT/$CHANNEL.json"
rm -f "$OUT/$CHANNEL.json.sig"
(cd "$APP" && npx tauri signer sign -f "$KEY" -p "$PW" "$OUT/$CHANNEL.json" >/dev/null)
"$OG" update-verify "$OUT/$CHANNEL.json" --pubkey "$KEY.pub"
( cd "$OUT" && shasum -a 256 ./* > SHA256SUMS )

if [ -n "$PUBLISH_TAG" ]; then
  gh release upload "$PUBLISH_TAG" "$OUT/$NAME" "$OUT/$NAME.sig" "$OUT/$CHANNEL.json" "$OUT/$CHANNEL.json.sig" --clobber
  echo "release $VERSION ($CHANNEL, $TARGET) published to $PUBLISH_TAG"
else
  echo "release $VERSION ($CHANNEL, $TARGET) ready in $OUT - not published"
fi
