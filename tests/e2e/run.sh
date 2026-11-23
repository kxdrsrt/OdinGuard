#!/bin/sh
# End-to-end tests of a Studio release against local test servers (tests/e2e/server_env.sh) and the guard in a
# Windows VM (Parallels; nettest.exe loads the release odinguard.dll, logs in and types commands).
#   tests/e2e/run.sh <xathena build dir> <release dir> [other release dir for the profile-tag test]
# Every check prints PASS/FAIL; the exit code is the number of failures.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
X=$1; R=$2; OTHER=${3:-}
VM=${VM:-"Tiny 11 Pro"}
HOST_IP=${HOST_IP:-10.211.55.2}
E2E="$ROOT/client/build/e2e"
WIN_E2E=${WIN_E2E:-'Z:\Dev\OdinGuard\client\build\e2e'}
FAIL=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAIL=$((FAIL + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }
sql() { docker exec ogt-db mariadb -N -uragnarok -pragnarok ragnarok -e "$1" 2>/dev/null; }
maplog() { docker exec ogt-srv sh -c 'cat /tmp/map.log' 2>/dev/null; }
vm() { for i in 1 2 3 4; do o=$(prlctl exec "$VM" --current-user cmd /c "$1" 2>&1); case "$o" in *PrlJob_Get*) sleep 2;; *) printf '%s\n' "$o"; return;; esac; done; printf '%s\n' "$o"; }
# nettest <user> <char> "<chat commands>" <hold seconds> [env assignments "A=1 & set B=2"]
nettest() { vm "cd /d $WIN_E2E & set OG_CHAT=$3 & set OG_HOLD=$4 ${5:+& set $5 }& nettest.exe $HOST_IP 6900 $1 ogtest 6121 5121 $2"; }
restart_map() {
	docker exec ogt-srv sh -c "true"
	pid=$(docker exec ogt-srv sh -c 'pidof map-server' | tr -d '\r')
	docker exec ogt-srv kill "$pid"; sleep 2
	docker exec -d ogt-srv sh -c './map-server > /tmp/map.log 2>&1'
	for i in $(seq 1 60); do sleep 3; docker exec ogt-srv sh -c 'grep -a -q "Map Server is now online" /tmp/map.log' 2>/dev/null && break; done
}

# the server picks the challenged protected file at random: up to four sessions until a file challenge failed
file_challenge_fails() {
	for i in 1 2 3 4; do
		nettest ogtest2 OgPlayer '!ping' 45 >/dev/null
		[ "$(sql "SELECT COUNT(*) FROM og_event WHERE type=\"challenge:file\"")" -ge 1 ] && return 0
	done
	return 1
}

stage_client() {
	rm -rf "$E2E"; mkdir -p "$E2E"
	cp "$ROOT/client/build/nettest.exe" "$1/client/odinguard.dll" "$1/client/odin.grf" "$E2E/"
	[ -f "$1/client/odin-dark.grf" ] && cp "$1/client/odin-dark.grf" "$E2E/"
	return 0
}

cat > /tmp/og_e2e.conf <<'EOF'
og_ch_nonce: yes
og_ch_nonce_interval: 20
og_ch_code: yes
og_ch_code_interval: 20
og_ch_time: yes
og_ch_time_interval: 20
og_ch_file: yes
og_ch_file_interval: 20
og_ch_honeypot: yes
og_behaviour: yes
og_bh_nodelay: 20
og_rate_limit: yes
og_rate_pps: 100
og_ban_score: 50
EOF
"$ROOT/tests/e2e/server_env.sh" "$X" "$R" /tmp/og_e2e.conf >/tmp/og_e2e_env.log 2>&1 || { echo "server start failed"; exit 99; }
stage_client "$R"

echo "== 1 handshake, commands, challenges, self integrity (GM, 60 s)"
out=$(nettest Mave OgTester '!ping;+aura;+lgp;+square 2;+aoes;+lgp off;+showauras;+showauras;+oginfo;+ogflags' 60)
printf '%s\n' "$out" | sed 's/^/   /' | head -40
check "login and map enter with the profile guard" 'printf '%s\n' "$out" | grep -q "MAP: .* bytes after enter"'
check "!ping answered by the guard" 'printf '%s\n' "$out" | grep -q "Ping: [0-9]* ms"'
check "@aura lists the catalog with unlock conditions" 'printf '%s\n' "$out" | grep -q "quest:aura_t1"'
check "@square 2 selects colour set 2" 'printf '%s\n' "$out" | grep -q "colour set2"'
check "@aoes limits LGP to SG/LoV/MS" 'printf '%s\n' "$out" | grep -q "Storm Gust, Lord of Vermilion and Meteor Storm"'
check "@oginfo shows protocol v2 and signatures" 'printf '%s\n' "$out" | grep -q "protocol v2.*signatures v1"'
check "guard log: handshake and signatures" 'vm "type $WIN_E2E\\odinguard.log" | grep -q "signatures: [0-9]* received"'
check "no guard-code / exe-code / challenge failure for the honest client" '[ "$(sql "SELECT COUNT(*) FROM og_event WHERE type LIKE \"challenge:%\" OR type IN (\"guard-code\",\"exe-code\")")" = 0 ]'
check "challenges were sent (map log)" 'maplog | grep -q "enabled (required: no, log only: yes, .*challenges nonce code time file"'

echo "== 2 aura unlocks (player + GM)"
out=$(nettest ogtest2 OgPlayer '+aura green' 5)
check "player cannot wear a locked aura" 'printf '%s\n' "$out" | grep -q "You have not unlocked green yet"'
( nettest ogtest2 OgPlayer '+aura' 25 > /tmp/og_p.out ) &
sleep 12
out=$(nettest Mave OgTester '+auraunlock green, OgPlayer;+auraunlock green, OgPlayer;+auraunlock nosuch, OgPlayer' 3)
wait
check "GM unlocks an aura" 'printf '%s\n' "$out" | grep -q "Aura unlocked."'
check "second unlock is reported as duplicate" 'printf '%s\n' "$out" | grep -q "already has this aura"'
check "unknown aura is refused" 'printf '%s\n' "$out" | grep -q "Unknown aura."'
out=$(nettest ogtest2 OgPlayer '+aura green;+aura' 4)
check "after relog the unlocked aura can be worn" 'printf '%s\n' "$out" | grep -q "Aura set."'
check "unlock stored once (account scope)" '[ "$(sql "SELECT COUNT(*) FROM og_aura_unlock WHERE account_id=2000001 AND aura=\"green\"")" = 1 ]'
out=$(nettest ogtest2 OgPlayer '+auraunlock yellow, OgPlayer' 3)
check "players cannot use @auraunlock" '! printf '%s\n' "$out" | grep -q "Aura unlocked."'

echo "== 3 server-side detections (honeypot, no-delay, rate limit) and the ban wave"
nettest ogtest3 OgThird '!ping' 4 'OG_ATTACK=1' >/dev/null
check "honeypot hit is flagged (flagban)" '[ "$(sql "SELECT COUNT(*) FROM og_flag WHERE account_id=2000002 AND rule=\"challenge:honeypot\" AND ban=1")" = 1 ]'
nettest ogtest2 OgPlayer '!ping' 4 'OG_SKILLSPAM=60' >/dev/null
check "skill requests inside the delay are flagged (bh:nodelay)" '[ "$(sql "SELECT COUNT(*) FROM og_flag WHERE account_id=2000001 AND rule=\"bh:nodelay\"")" -ge 1 ]'
sleep 6
out=$(nettest ogtest3 OgThird '!ping' 3 'OG_FLOOD=900'); printf '%s\n' "$out" | sed 's/^/   /' | tail -3
check "packet flood is reported (rate-limit, warn mode = log)" '[ "$(sql "SELECT COUNT(*) FROM og_event WHERE account_id=2000002 AND type=\"rate-limit\"")" -ge 1 ]'
out=$(nettest Mave OgTester '+ogbanwave dry;+ogflags' 3)
check "@ogbanwave dry lists the honeypot account" 'printf '%s\n' "$out" | grep -q "account 2000002"'
check "warn mode never bans" '[ "$(sql "SELECT COUNT(*) FROM og_ban")" = 0 ]'
printf 'og_log_only: no\n' >> "$X/conf/import/odinguard_conf.txt"
restart_map
out=$(nettest Mave OgTester '+ogbanwave' 3)
check "ban wave bans the flagged machine" '[ "$(sql "SELECT COUNT(*) FROM og_ban WHERE reason LIKE \"ban wave%\"")" -ge 1 ]'
check "flags are closed after the ban" '[ "$(sql "SELECT COUNT(*) FROM og_flag WHERE account_id=2000002 AND state=\"banned\"")" -ge 1 ]'
out=$(vm "cd /d $WIN_E2E & nettest.exe $HOST_IP 6900 ogtest3 ogtest")
check "banned machine cannot log in (code 4)" 'printf '%s\n' "$out" | grep -q "refused, code 4"'
sql "DELETE FROM og_ban; DELETE FROM og_flag"

echo "== 4 tampering: signature package, build secret, guard file"
cp "$X/conf/odinguard/odinguard.sigpack" /tmp/og_sigpack.bak
printf 'X' | dd of="$X/conf/odinguard/odinguard.sigpack" bs=1 seek=40 conv=notrunc 2>/dev/null
restart_map
check "tampered signature package is refused at start" 'maplog | grep -q "signature package signature INVALID"'
cp /tmp/og_sigpack.bak "$X/conf/odinguard/odinguard.sigpack"
printf 'og_build_secret: %064d\n' 0 >> "$X/conf/import/odinguard_conf.txt"
restart_map
nettest ogtest2 OgPlayer '!ping' 45 >/dev/null
check "wrong build secret: nonce/time challenges fail" '[ "$(sql "SELECT COUNT(*) FROM og_event WHERE type IN (\"challenge:nonce\",\"challenge:time\")")" -ge 1 ]'
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
printf 'X' | dd of="$X/conf/odinguard/odin.grf" bs=1 seek=100 conv=notrunc 2>/dev/null
restart_map
sql "DELETE FROM og_event"
check "modified odin.grf on one side: file challenge fails" 'file_challenge_fails'
cp "$R/server/conf/odinguard/odin.grf" "$X/conf/odinguard/odin.grf"
restart_map

echo "== 4b competitive pack (optional protected file)"
sql "DELETE FROM og_event"
rm -f "$E2E/odin-dark.grf"
nettest ogtest2 OgPlayer '!ping' 45 >/dev/null
check "optional pack missing on the client: no file challenge failure" '[ "$(sql "SELECT COUNT(*) FROM og_event WHERE type=\"challenge:file\"")" = 0 ]'
cp "$R/client/odin-dark.grf" "$E2E/odin-dark.grf"
printf 'X' | dd of="$E2E/odin-dark.grf" bs=1 seek=60 conv=notrunc 2>/dev/null
check "manipulated competitive pack on the client: file challenge fails" 'file_challenge_fails'
cp "$R/client/odin-dark.grf" "$E2E/odin-dark.grf"

if [ -n "$OTHER" ]; then
	echo "== 5 a client built for another profile"
	stage_client "$OTHER"
	# the refused guard shows a message box: run nettest detached, read the log, close it
	out=$(vm "cd /d $WIN_E2E & del odinguard.log 2>nul & start \"\" /b nettest.exe $HOST_IP 6900 ogtest2 ogtest & ping -n 9 127.0.0.1 >nul & type odinguard.log & taskkill /f /im nettest.exe")
	printf '%s\n' "$out" | sed 's/^/   /' | tail -5
	check "foreign profile is refused with a message (status 3)" 'printf '%s\n' "$out" | grep -q "refused (3)"'
	stage_client "$R"
fi

echo "== $FAIL failure(s)"
exit $FAIL
