#!/bin/sh
# Builds a release: dist/OdinGuard-<version>-client.zip and dist/OdinGuard-<version>-server.zip + SHA256SUMS.
# Usage: tools/make_release.sh [xathena dir]
# The client package never contains keys/server.seed; the server key goes into the server's
# conf/import/odinguard_conf.txt (og_key) or the ODINGUARD_KEY environment variable.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
VER=$(sed -n 's/^#define OG_VERSION_STR *"\(.*\)"/\1/p' client/src/og_version.h)
[ -n "$VER" ] || { echo "no OG_VERSION_STR in client/src/og_version.h" >&2; exit 1; }

sh tools/sync_server.sh "$@"
rm -f client/build/odinguard.dll client/build/odinguard_res.o client/build/ijl15.dll
make -C client build/odinguard.dll build/ijl15.dll >/dev/null

D=dist/OdinGuard-$VER
rm -rf "$D-client" "$D-server" "$D-client.zip" "$D-server.zip"
mkdir -p "$D-client" "$D-server"
cp client/build/odinguard.dll client/build/ijl15.dll client/package/* "$D-client/"
cp -R server/README.md server/HOOKS.txt server/sql server/src server/conf "$D-server/"
cp tools/check_auras.py "$D-server/"
cp third_party/LICENCE-monocypher.md "$D-client/LICENSE-monocypher.md"
cp third_party/minhook/LICENSE.txt "$D-client/LICENSE-minhook.txt"

(cd dist && zip -qr "OdinGuard-$VER-client.zip" "OdinGuard-$VER-client" && zip -qr "OdinGuard-$VER-server.zip" "OdinGuard-$VER-server" \
	&& shasum -a 256 "OdinGuard-$VER-client.zip" "OdinGuard-$VER-server.zip" "OdinGuard-$VER-client/"*.dll > "SHA256SUMS-$VER.txt")
ls -la dist/*"$VER"*.zip
cat "dist/SHA256SUMS-$VER.txt"
