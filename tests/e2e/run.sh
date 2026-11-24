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
nettest_once() { vm "cd /d $WIN_E2E & set OG_CHAT=$3 & set OG_HOLD=$4 ${5:+& set $5 }& nettest.exe $HOST_IP 6900 $1 ogtest 6121 5121 $2"; }
# an immediate relogin of the same account can be refused with 0x0081 (still online): wait and retry
nettest() {
	for try in 1 2 3; do
		o=$(nettest_once "$@")
		case "$o" in *"unexpected packet 0x0081"*|*"refused, code 8"*|*"no map server answer"*|*"CHAR: unexpected answer"*) sleep 8 ;; *) break ;; esac
	done
	printf '%s\n' "$o"
}
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
	data_ini "0=odin.grf" "1=odin-dark.grf"
	return 0
}

# DATA.INI of the test client folder
data_ini() { { printf '[Data]\r\n'; for l in "$@"; do printf '%s\r\n' "$l"; done; } > "$E2E/DATA.INI"; }
events() { sql "SELECT COUNT(*) FROM og_event WHERE $1"; }
# up to three sessions until an event matching $1 exists (session timing in the emulated VM varies)
event_after_session() {
	cond=$1; shift
	for i in 1 2 3; do
		nettest "$@" >/dev/null
		[ "$(events "$cond")" -ge 1 ] && return 0
	done
	return 1
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
out=$(nettest Mave OgTester '!ping;+aura;+lgp;+square 2;+aoes;+lgpsound off;+lgp off;+showauras;+showauras;+oginfo;+ogflags;+whosell' 60)
printf '%s\n' "$out" | sed 's/^/   /' | head -40
check "login and map enter with the profile guard" 'printf '%s\n' "$out" | grep -q "MAP: .* bytes after enter"'
check "!ping answered by the guard" 'printf '%s\n' "$out" | grep -q "Ping: [0-9]* ms"'
check "@aura lists the catalog with unlock conditions" 'printf '%s\n' "$out" | grep -q "quest:aura_t1"'
check "@square 2 selects colour set 2" 'printf '%s\n' "$out" | grep -q "colour set 2"'
check "@lgpsound off is acknowledged" 'printf '%s\n' "$out" | grep -q "sounds off"'
check "@aoes limits LGP to SG/LoV/MS" 'printf '%s\n' "$out" | grep -q "Storm Gust, Lord of Vermilion and Meteor Storm"'
check "@whosell without parameter answers in the chat (market overview)" 'printf '%s\n' "$out" | grep -q "No vending shop is open right now\|Market (selling)"'
check "@oginfo shows protocol v2 and signatures" 'printf '%s\n' "$out" | grep -q "protocol v2.*signatures v1"'
check "guard log: handshake and signatures" 'vm "type $WIN_E2E\\odinguard.log" | grep -q "signatures: [0-9]* received"'
check "no guard-code / exe-code / challenge failure for the honest client" '[ "$(sql "SELECT COUNT(*) FROM og_event WHERE type LIKE \"challenge:%\" OR type IN (\"guard-code\",\"exe-code\")")" = 0 ]'
check "honest client: no GRF manifest report" '[ "$(events "type LIKE \"grf:%\"")" = 0 ]'
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
check "unlock stored once (account scope)" '[ "$(sql "SELECT COUNT(*) FROM aura_unlock WHERE account_id=2000001 AND aura=\"green\"")" = 1 ]'
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
# a byte every 4000 bytes: every challenge window (at least 4096 bytes) of the 1.2 MB file contains one
python3 - "$X/conf/odinguard/odin.grf" <<'PY'
import sys
with open(sys.argv[1], "r+b") as f:
    f.seek(0, 2)
    n = f.tell()
    for off in range(100, n, 4000):
        f.seek(off)
        f.write(b"X")
PY
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
# detected by the GRF manifest (immediately, kick) or by the file challenge (random file, every 20 s)
check "manipulated competitive pack on the client is detected (GRF check or file challenge)" 'event_after_session "type=\"challenge:file\" OR (type=\"grf:modified\" AND detail LIKE \"odin-dark.grf%\")" ogtest2 OgPlayer "!ping" 45'
cp "$R/client/odin-dark.grf" "$E2E/odin-dark.grf"

echo "== 6 GRF manifest (Gepard-like start-up check, patcher friendly)"
sql "DELETE FROM og_event"
cp "$E2E/odin-dark.grf" "$E2E/extra.grf"
data_ini "0=odin.grf" "1=odin-dark.grf" "2=extra.grf"
nettest ogtest2 OgPlayer '!ping' 25 >/dev/null
check "GRF that is neither listed nor an allowed mod is reported (grf:unknown)" '[ "$(events "type=\"grf:unknown\" AND detail=\"extra.grf\"")" -ge 1 ]'
set -- $("$ROOT/studio/target/debug/og" grf-hash "$E2E/extra.grf")
printf 'og_grf_mod: %s,e2e test mod\n' "$2" >> "$X/conf/import/odinguard_conf.txt"
restart_map
sql "DELETE FROM og_event"
nettest ogtest2 OgPlayer '!ping' 25 >/dev/null
check "allowed mod (any name, same tail hash) is accepted" '[ "$(events "type LIKE \"grf:%\"")" = 0 ]'
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
restart_map
data_ini "0=odin.grf" "1=odin-dark.grf"
rm -f "$E2E/extra.grf"
sql "DELETE FROM og_event"
size=$(wc -c < "$E2E/odin.grf")
printf 'X' | dd of="$E2E/odin.grf" bs=1 seek=$((size - 2)) conv=notrunc 2>/dev/null
nettest ogtest2 OgPlayer '!ping' 25 >/dev/null
check "changed file table of a listed GRF (grf:modified)" '[ "$(events "type=\"grf:modified\" AND detail=\"odin.grf\"")" -ge 1 ]'
cp "$R/client/odin.grf" "$E2E/odin.grf"
sql "DELETE FROM og_event"
printf 'X' | dd of="$E2E/odin.grf" bs=1 seek=60 conv=notrunc 2>/dev/null
check "same table, changed content: background full hash (grf:modified content)" 'event_after_session "type=\"grf:modified\" AND detail LIKE \"odin.grf (content)%\"" ogtest2 OgPlayer "!ping" 30'
cp "$R/client/odin.grf" "$E2E/odin.grf"
sql "DELETE FROM og_event"
data_ini "0=odin-dark.grf"
nettest ogtest2 OgPlayer '!ping' 25 >/dev/null
check "required GRF missing from DATA.INI (grf:missing)" '[ "$(events "type=\"grf:missing\" AND detail=\"odin.grf\"")" -ge 1 ]'
data_ini "0=odin.grf" "1=odin-dark.grf"

echo "== 7 tamper stubs: speed hack, code patches (test process only)"
sql "DELETE FROM og_event"
nettest ogtest2 OgPlayer '!ping' 160 "OG_SPEEDTEST=3" | grep -E "speed test" | sed 's/^/   /'
check "speed hack: patched time API reported (api-hook)" '[ "$(events "type=\"api-hook\"")" -ge 1 ]'
check "speed hack: time challenge fails" '[ "$(events "type=\"challenge:time\"")" -ge 1 ]'
sql "DELETE FROM og_event"
nettest ogtest2 OgPlayer '!ping' 30 "OG_CODEPATCH=guard" | grep -E "code patch" | sed 's/^/   /'
check "guard code patched in memory (guard-code)" '[ "$(events "type=\"guard-code\"")" -ge 1 ]'
exe=$(sql "SELECT exe_code FROM og_client WHERE account_id=2000001 AND exe_code<>'' LIMIT 1")
printf 'og_allowed_exe: %s\n' "$exe" >> "$X/conf/import/odinguard_conf.txt"
restart_map
sql "DELETE FROM og_event"
nettest ogtest2 OgPlayer '!ping' 20 >/dev/null
check "unchanged exe matches the allowed exe hash" '[ "$(events "type=\"exe-code\"")" = 0 ]'
nettest ogtest2 OgPlayer '!ping' 30 "OG_CODEPATCH=exe" | grep -E "code patch" | sed 's/^/   /'
check "exe code patched in memory (exe-code)" '[ "$(events "type=\"exe-code\"")" -ge 1 ]'
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
restart_map

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
