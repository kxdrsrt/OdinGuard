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
restart_login() {
	pid=$(docker exec ogt-srv sh -c 'pidof login-server' | tr -d '\r')
	docker exec ogt-srv kill "$pid"; sleep 2
	docker exec -d ogt-srv sh -c './login-server > /tmp/login.log 2>&1'
	for i in $(seq 1 30); do sleep 2; docker exec ogt-srv sh -c 'grep -a -q "Server is listening on the port" /tmp/login.log' 2>/dev/null && break; done
	sleep 12 # the char-server reconnects to the login-server
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
	lang_ini en
	return 0
}
# language of the test guard (the VM's Windows is German; the checks expect English unless a section says otherwise)
lang_ini() { printf '[OdinGuard]\r\nLanguage=%s\r\n' "$1" > "$E2E/odinguard.ini"; }

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
# the daily ban wave must not run during the suite (it bans the machines flagged by the tamper tests); @ogbanwave is used
og_banwave_hour: -1
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

echo "== 1b !dark switches the competitive pack in DATA.INI"
out=$(nettest Mave OgTester '!dark off;!dark on;!dark' 12)
check "!dark off removes the pack from DATA.INI (restart needed)" 'printf "%s\n" "$out" | grep -q "Competitive pack OFF"'
check "!dark on puts it back" 'printf "%s\n" "$out" | grep -q "Competitive pack ON. Restart"'
check "DATA.INI lists odin-dark.grf again after on" 'vm "type $WIN_E2E\\DATA.INI" | grep -qi "odin-dark.grf"'

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
check "honeypot hit is flagged in warn mode, without the ban mark" '[ "$(sql "SELECT COUNT(*) FROM og_flag WHERE account_id=2000002 AND rule=\"challenge:honeypot\" AND ban=0")" = 1 ]'
nettest ogtest2 OgPlayer '!ping' 4 'OG_SKILLSPAM=60' >/dev/null
check "skill requests inside the delay are flagged (bh:nodelay)" '[ "$(sql "SELECT COUNT(*) FROM og_flag WHERE account_id=2000001 AND rule=\"bh:nodelay\"")" -ge 1 ]'
sleep 6
out=$(nettest ogtest3 OgThird '!ping' 3 'OG_FLOOD=900'); printf '%s\n' "$out" | sed 's/^/   /' | tail -3
check "packet flood is reported (rate-limit, warn mode = log)" '[ "$(sql "SELECT COUNT(*) FROM og_event WHERE account_id=2000002 AND type=\"rate-limit\"")" -ge 1 ]'
out=$(nettest Mave OgTester '+ogbanwave dry' 3)
check "warn-mode flags do not count for the ban wave" 'printf '%s\n' "$out" | grep -q "0 account(s) would be banned"'
check "warn mode never bans" '[ "$(sql "SELECT COUNT(*) FROM og_ban")" = 0 ]'
out=$(nettest Mave OgTester '+ogflagclear 2000002;+ogflagclear 2000001 bh:nodelay;+ogflagclear' 3)
check "@ogflagclear clears the open flags of a character" 'printf '%s\n' "$out" | grep -q "open flag(s) of account 2000002 cleared"'
check "@ogflagclear with a rule clears only that rule" '[ "$(sql "SELECT COUNT(*) FROM og_flag WHERE account_id=2000001 AND rule=\"bh:nodelay\" AND state=\"open\"")" = 0 ]'
check "@ogflagclear without a name shows the usage" 'printf '%s\n' "$out" | grep -q "Usage: @ogflagclear"'
printf 'og_log_only: no\n' >> "$X/conf/import/odinguard_conf.txt"
restart_map
nettest ogtest3 OgThird '!ping' 4 'OG_ATTACK=1' >/dev/null
check "honeypot hit outside warn mode carries the ban mark (flagban)" '[ "$(sql "SELECT COUNT(*) FROM og_flag WHERE account_id=2000002 AND rule=\"challenge:honeypot\" AND ban=1 AND state=\"open\"")" = 1 ]'
out=$(nettest Mave OgTester '+ogbanwave dry' 3)
check "@ogbanwave dry lists the honeypot account" 'printf '%s\n' "$out" | grep -q "account 2000002"'
out=$(nettest Mave OgTester '+ogbanwave' 3)
check "ban wave bans the flagged machine" '[ "$(sql "SELECT COUNT(*) FROM og_ban WHERE reason LIKE \"ban wave%\"")" -ge 1 ]'
check "flags are closed after the ban" '[ "$(sql "SELECT COUNT(*) FROM og_flag WHERE account_id=2000002 AND state=\"banned\"")" -ge 1 ]'
out=$(vm "cd /d $WIN_E2E & nettest.exe $HOST_IP 6900 ogtest3 ogtest")
check "banned machine cannot log in (code 4)" 'printf '%s\n' "$out" | grep -q "refused, code 4"'
sql "DELETE FROM og_ban; DELETE FROM og_flag"

echo "== 3b replayed record (captured encrypted bytes sent again)"
sql "DELETE FROM og_event"
python3 "$ROOT/tests/e2e/replay_proxy.py" 5199 127.0.0.1 5121 8 > /tmp/og_replay.log 2>&1 &
RP=$!
sleep 1
out=$(vm "cd /d $WIN_E2E & set OG_CHAT=!ping & set OG_HOLD=20 & nettest.exe $HOST_IP 6900 ogtest2 ogtest 6121 5199 OgPlayer")
kill $RP 2>/dev/null; wait $RP 2>/dev/null
check "proxy replayed a client record" 'grep -q "replayed [0-9]* bytes" /tmp/og_replay.log'
check "replayed record is detected (record-mac)" '[ "$(events "type=\"record-mac\" AND account_id=2000001")" -ge 1 ]'
check "replayed record kicks outside warn mode" '[ "$(events "type=\"record-mac\" AND account_id=2000001 AND action=\"kick\"")" -ge 1 ]'

echo "== 3c command registry: rate limit, switch, group (players only)"
printf 'og_cmdrate_ping: 10000\nog_cmd_vsync: no\nog_cmdgroup_crash: 10\n' >> "$X/conf/import/odinguard_conf.txt"
restart_map
out=$(nettest ogtest2 OgPlayer '!ping;!ping;!vsync off;!crash' 4)
check "first !ping is answered" 'printf "%s\n" "$out" | grep -q "Ping: [0-9]* ms"'
check "second !ping inside og_cmdrate_ping is refused" 'printf "%s\n" "$out" | grep -q "Please wait a moment before using this command again"'
check "og_cmd_vsync: no disables !vsync" 'printf "%s\n" "$out" | grep -q "This command is disabled on this server"'
check "og_cmdgroup_crash: 10 refuses players" 'printf "%s\n" "$out" | grep -q "You are not allowed to use this command"'
out=$(nettest Mave OgTester '!ping;!ping' 4)
check "GMs are not rate limited" '[ "$(printf "%s\n" "$out" | grep -c "Ping: [0-9]* ms")" = 2 ]'
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"; sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"; sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
restart_map

echo "== 3d account lock (og_account_lock, @oglock)"
sql "DELETE FROM og_event; DELETE FROM og_account_lock"
( nettest ogtest2 OgPlayer '!ping' 20 >/dev/null ) &
sleep 12
out=$(nettest Mave OgTester '+oglock OgPlayer, add;+oglock OgPlayer' 3)
wait
check "@oglock add locks the account to the machine it plays on" '[ "$(sql "SELECT COUNT(*) FROM og_account_lock WHERE account_id=2000001")" = 1 ]'
check "@oglock lists the machine" 'printf "%s\n" "$out" | grep -q "Account lock of OgPlayer: 1 machine"'
out=$(nettest ogtest2 OgPlayer '!ping' 3)
check "locked account logs in from its machine" 'printf "%s\n" "$out" | grep -q "Ping: [0-9]* ms"'
sql "UPDATE og_account_lock SET unique_id=REPEAT('0',64) WHERE account_id=2000001"
out=$(vm "cd /d $WIN_E2E & nettest.exe $HOST_IP 6900 ogtest2 ogtest")
check "locked account is refused on another machine (code 3)" 'printf "%s\n" "$out" | grep -q "refused, code 3"'
check "the refusal is logged (account-lock)" '[ "$(events "type=\"account-lock\" AND account_id=2000001")" -ge 1 ]'
sql "DELETE FROM og_account_lock"
out=$(nettest ogtest2 OgPlayer '!ping' 3)
check "without lock rows the account logs in again" 'printf "%s\n" "$out" | grep -q "Ping: [0-9]* ms"'

echo "== 3e @pf packet filter (GvG/BG maps) and @spb"
# castles have nosave (a logout there starts the next login at the save point), so both are on the castle in the same
# session: the player waits (OG_DELAY), the GM goes there and recalls them, nettest confirms the map change, then the
# player talks. An unowned castle has monsters (a Raydric Archer killed the GM after the 10 s castle invincibility and
# the chat was then counted in Prontera), so the GM clears the castle first
pf_round() { # $1 = @pf setting of the watcher; prints the number of chat packets of OgPlayer the watcher received
	( nettest ogtest2 OgPlayer 'hello from the castle;hello again' 3 'OG_DELAY=40' >/dev/null ) &
	sleep 9
	nettest Mave OgTester "+mapmove prtg_cas01 111 184;+killmonster2;+heal;+recall OgPlayer;+pf $1" 40 'OG_WATCH=008d' > /tmp/og_pf.out
	wait
	sed -n 's/^WATCH 0 \([0-9]*\).*/\1/p' /tmp/og_pf.out | tail -1
}
n_on=$(pf_round c); n_off=$(pf_round off)
echo "   chat packets with @pf c: $n_on, with @pf off: $n_off"
check "@pf c: no public chat of other players on a castle map" '[ "${n_on:-x}" = 0 ]'
check "@pf off: the chat arrives again" '[ "${n_off:-0}" -ge 2 ]'
check "@pf prints the filter state" 'grep -q "Packet filter (WoE/BG maps): chat OFF | items OFF | emotions OFF" /tmp/og_pf.out'
out=$(nettest Mave OgTester '+spb;+party SpbTest;+useskill 34 10 OgTester;+spb' 4)
check "@spb outside a party says so" 'printf "%s\n" "$out" | grep -q "You are not in a party"'
check "@spb lists the party member with Blessing and seconds left" 'printf "%s\n" "$out" | grep -q "OgTester: Bless [0-9]*s"'

echo "== 3f crash report upload (og_crash_upload_url, consent in odinguard.ini)"
rm -f /tmp/og_crash_rx.log
python3 "$ROOT/tests/e2e/crash_receiver.py" 8899 /tmp/og_crash_rx.log > /dev/null 2>&1 &
CR=$!
printf 'og_crash_upload_url: http://%s:8899/api/v1/integrations/odinguard/crash\n' "$HOST_IP" >> "$X/conf/import/odinguard_conf.txt"
restart_map
rm -rf "$E2E/odinguard-crash"; mkdir -p "$E2E/odinguard-crash"
printf 'OdinGuard test crash report\r\nreason: e2e\r\n' > "$E2E/odinguard-crash/crash-20260101-000000.txt"
printf '[OdinGuard]\r\nLanguage=en\r\n[crash]\r\nupload=yes\r\n' > "$E2E/odinguard.ini"
nettest ogtest2 OgPlayer '!ping' 12 >/dev/null
check "agreed report is uploaded to the panel URL" 'grep -q "file=crash-20260101-000000.txt bytes=4[0-9] first=b.OdinGuard test crash" /tmp/og_crash_rx.log'
tok=$(sed -n 's/.*token=\([0-9a-f]*\) .*/\1/p' /tmp/og_crash_rx.log | head -1)
check "the upload carries the session token stored for the account" '[ -n "$tok" ] && [ "$(sql "SELECT COUNT(*) FROM og_crash_token WHERE token=\"$tok\" AND account_id=2000001")" = 1 ]'
check "sent reports are listed in sent.txt" 'grep -q "crash-20260101-000000.txt" "$E2E/odinguard-crash/sent.txt"'
nettest ogtest2 OgPlayer '!ping' 10 >/dev/null
check "a sent report is not uploaded twice" '[ "$(grep -c "crash-20260101-000000.txt" /tmp/og_crash_rx.log)" = 1 ]'
printf 'second\r\n' > "$E2E/odinguard-crash/crash-20260102-000000.txt"
printf '[OdinGuard]\r\nLanguage=en\r\n[crash]\r\nupload=no\r\n' > "$E2E/odinguard.ini"
nettest ogtest2 OgPlayer '!ping' 10 >/dev/null
check "declined: nothing is uploaded" '! grep -q "crash-20260102-000000.txt" /tmp/og_crash_rx.log'
kill $CR 2>/dev/null
rm -rf "$E2E/odinguard-crash"; lang_ini en
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
restart_map

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

echo "== 4c competitive pack required on battleground maps (og_dark_enforce)"
printf 'og_dark_enforce: bg\nog_dark_gm_level: 100\n' >> "$X/conf/import/odinguard_conf.txt"
restart_map
sql "DELETE FROM og_event"
out=$(nettest Mave OgTester '+mapmove bat_b01 100 100;+oginfo' 20)
check "client with the pack may enter a battleground map" '! printf "%s\n" "$out" | grep -q "needs the competitive pack"'
rm -f "$E2E/odin-dark.grf"
out=$(nettest Mave OgTester '+mapmove bat_b01 100 100;+oginfo' 20)
check "client without the pack is sent back from a battleground map" 'printf "%s\n" "$out" | grep -q "needs the competitive pack"'
check "the refusal is logged (darkpack:required)" '[ "$(sql "SELECT COUNT(*) FROM og_event WHERE type=\"darkpack:required\"")" -ge 1 ]'
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"; sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
cp "$R/client/odin-dark.grf" "$E2E/odin-dark.grf"
restart_map

if [ -n "$OTHER" ]; then
	echo "== 4d guard rollout: an older guard stays accepted (og_guard_previous_file)"
	# the server's current guard becomes another build (the minimal release); the client keeps the e2e guard, which
	# the server only knows as a previous release
	cp "$X/conf/odinguard/odinguard.dll" /tmp/og_guard_e2e.dll
	mkdir -p "$X/conf/odinguard/previous"
	cp /tmp/og_guard_e2e.dll "$X/conf/odinguard/previous/odinguard-old.dll"
	cp "$OTHER/client/odinguard.dll" "$X/conf/odinguard/odinguard.dll"
	printf 'og_guard_previous_file: conf/odinguard/previous/odinguard-old.dll\n' >> "$X/conf/import/odinguard_conf.txt"
	restart_map
	sql "DELETE FROM og_event"
	nettest ogtest2 OgPlayer '!ping' 45 >/dev/null
	check "previous guard is loaded at start" 'maplog | grep -q "previous guard conf/odinguard/previous/odinguard-old.dll accepted"'
	check "client with the previous guard: no guard-code, no code challenge failure" '[ "$(events "type=\"guard-code\" OR type=\"challenge:code\"")" = 0 ]'
	sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
	restart_map
	sql "DELETE FROM og_event"
	nettest ogtest2 OgPlayer '!ping' 20 >/dev/null
	check "without the previous entry the old guard is reported (guard-code)" '[ "$(events "type=\"guard-code\"")" -ge 1 ]'
	cp /tmp/og_guard_e2e.dll "$X/conf/odinguard/odinguard.dll"
	# odin.grf: the server's official copy changes (a byte every 4000 bytes, so every challenge range differs), the
	# client keeps the old one, which the server knows as a previous version
	cp "$X/conf/odinguard/odin.grf" "$X/conf/odinguard/previous/odin-old.grf"
	python3 - "$X/conf/odinguard/odin.grf" <<'PY'
import sys
with open(sys.argv[1], "r+b") as f:
    f.seek(0, 2)
    for off in range(100, f.tell(), 4000):
        f.seek(off)
        f.write(b"Y")
PY
	printf 'og_protected_previous: 0,conf/odinguard/previous/odin-old.grf\n' >> "$X/conf/import/odinguard_conf.txt"
	restart_map
	sql "DELETE FROM og_event"
	nettest ogtest2 OgPlayer '!ping' 45 >/dev/null
	check "previous odin.grf is loaded at start" 'maplog | grep -q "protected file 0 also accepts conf/odinguard/previous/odin-old.grf"'
	check "client with the previous odin.grf: no file challenge failure" '[ "$(events "type=\"challenge:file\"")" = 0 ]'
	sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
	restart_map
	sql "DELETE FROM og_event"
	check "without the previous entry the old odin.grf fails the file challenge" 'file_challenge_fails'
	cp "$R/server/conf/odinguard/odin.grf" "$X/conf/odinguard/odin.grf"
	rm -rf "$X/conf/odinguard/previous"
	restart_map
fi

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
# two accepted versions with the same size and file table (only file contents differ): the client's version is not
# the last one listed, it must still be accepted (the 1.0.0 guard compared with the last matching entry only)
set -- $("$ROOT/studio/target/debug/og" grf-hash "$E2E/odin.grf")
printf 'og_grf_file: odin.grf,%s,%s,%064d,yes\n' "$1" "$2" 7 >> "$X/conf/import/odinguard_conf.txt"
restart_map
sql "DELETE FROM og_event"
nettest ogtest2 OgPlayer '!ping' 30 >/dev/null
check "two versions with the same file table: the client's (not the last listed) is accepted" '[ "$(events "type LIKE \"grf:%\"")" = 0 ]'
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
restart_map
sql "DELETE FROM og_event"
data_ini "0=odin-dark.grf"
nettest ogtest2 OgPlayer '!ping' 25 >/dev/null
check "required GRF missing from DATA.INI (grf:missing)" '[ "$(events "type=\"grf:missing\" AND detail=\"odin.grf\"")" -ge 1 ]'
data_ini "0=odin.grf" "1=odin-dark.grf"

echo "== 7 tamper stubs: speed hack, code patches (test process only)"
sql "DELETE FROM og_event"
nettest ogtest2 OgPlayer '!ping' 160 "OG_SPEEDTEST=3" | grep -E "speed test" | sed 's/^/   /'
check "speed hack: patched time API reported (api-hook)" '[ "$(events "type=\"api-hook\"")" -ge 1 ]'
# the time challenge is issued at a random moment; one more session if the first did not meet it
[ "$(events "type=\"challenge:time\"")" -ge 1 ] || nettest ogtest2 OgPlayer '!ping' 160 "OG_SPEEDTEST=3" >/dev/null
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
	out=$(vm "cd /d $WIN_E2E & del odinguard.log 2>nul & start \"\" /b nettest.exe $HOST_IP 6900 ogtest2 ogtest & ping -n 14 127.0.0.1 >nul & type odinguard.log & taskkill /f /im nettest.exe")
	printf '%s\n' "$out" | sed 's/^/   /' | tail -5
	check "foreign profile is refused with a message (status 3)" 'printf '%s\n' "$out" | grep -q "refused (3)"'
	stage_client "$R"
fi

echo "== 5b an older guard release is refused with a message (og_min_release)"
sql "DELETE FROM og_event"
echo "og_min_release: 9.9.9" >> "$X/conf/import/odinguard_conf.txt"
restart_login
out=$(vm "cd /d $WIN_E2E & del odinguard.log 2>nul & start \"\" /b nettest.exe $HOST_IP 6900 ogtest2 ogtest & ping -n 14 127.0.0.1 >nul & type odinguard.log & taskkill /f /im nettest.exe")
printf '%s\n' "$out" | grep "refused" | sed 's/^/   /'
check "older release is refused at the handshake (status 1)" 'printf "%s\n" "$out" | grep -q "refused (1)"'
check "the message names the player's and the needed release" 'printf "%s\n" "$out" | grep -q "outdated (OdinGuard $(sed -n "s/.*OG_VERSION_STR \"\(.*\)\"/\1/p" "$ROOT/client/src/og_version.h")). This server needs OdinGuard 9.9.9 or newer"'
check "the login server logs the refusal" 'docker exec ogt-srv sh -c "grep -a -q \"handshake refused (Your client is outdated\" /tmp/login.log"'
check "an outdated client is not logged as a cheat" '[ "$(events "1=1")" = 0 ]'
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
restart_login
out=$(nettest ogtest2 OgPlayer '!ping' 3)
check "with the current release the login works again" 'printf "%s\n" "$out" | grep -q "Ping: [0-9]* ms"'
check "the server reports min release at start" 'maplog | grep -q "min release: $(sed -n "s/.*OG_VERSION_STR \"\(.*\)\"/\1/p" "$ROOT/client/src/og_version.h")"'

echo "== 5c the player's language (og-lang, 1.0.5): German, a language added in the conf, English for the rest"
VER=$(sed -n "s/.*OG_VERSION_STR \"\(.*\)\"/\1/p" "$ROOT/client/src/og_version.h")
lang_ini de
echo "og_min_release: 9.9.9" >> "$X/conf/import/odinguard_conf.txt"
restart_login
out=$(vm "cd /d $WIN_E2E & del odinguard.log 2>nul & start \"\" /b nettest.exe $HOST_IP 6900 ogtest2 ogtest & ping -n 14 127.0.0.1 >nul & type odinguard.log & taskkill /f /im nettest.exe")
check "a German guard gets the refusal in German" 'printf "%s\n" "$out" | grep -q "Dein Client ist veraltet (OdinGuard $VER). Dieser Server braucht OdinGuard 9.9.9 oder neuer"'
check "the login server logs the German refusal" 'docker exec ogt-srv sh -c "grep -a -q \"handshake refused (Dein Client ist veraltet\" /tmp/login.log"'
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
restart_login
printf 'og_cmdrate_ping: 10000\nog_cmdrate_lang: 0\nog_text_fr_cmd_wait: Patientez un instant.\n' >> "$X/conf/import/odinguard_conf.txt"
restart_map
out=$(nettest ogtest2 OgPlayer '!ping;!ping;!dark' 4)
check "German: the rate limit answers in German" 'printf "%s\n" "$out" | grep -q "Bitte warte einen Moment, bevor du den Befehl erneut benutzt"'
check "German: the guard's own reply (!dark) is German" 'printf "%s\n" "$out" | LC_ALL=C grep -q "Competitive-Pack ist beim n.chsten Start"'
check "German: umlauts reach the client as Windows-1252" 'printf "%s\n" "$out" | LC_ALL=C grep -q "n$(printf "\344")chsten"'
lang_ini en
out=$(nettest ogtest2 OgPlayer '!lang;!lang de ;!lang en;!lang xx ' 6)
check "!lang shows the current language" 'printf "%s\n" "$out" | grep -q "Language: en (set: en)"'
check "!lang de answers in German (a trailing space is fine)" 'printf "%s\n" "$out" | grep -q "Sprache auf de gestellt"'
check "!lang en switches back" 'printf "%s\n" "$out" | grep -q "Language set to en"'
check "an unknown language is refused" 'printf "%s\n" "$out" | grep -q "Unknown language xx"'
check "!lang is saved in odinguard.ini" 'vm "type $WIN_E2E\\odinguard.ini" | grep -qi "Language=en"'
out=$(nettest Mave OgTester '!lang de;+pf;!lang en;+pf' 8)
check "after !lang de the server's own messages are German (no restart)" 'printf "%s\n" "$out" | grep -q "Paketfilter (WoE/BG-Karten)"'
check "after !lang en they are English again" 'printf "%s\n" "$out" | grep -q "Packet filter (WoE/BG maps)"'
lang_ini fr
out=$(nettest ogtest2 OgPlayer '!ping;!ping;!dark' 4)
check "a language from the conf (og_text_fr_cmd_wait)" 'printf "%s\n" "$out" | grep -q "Patientez un instant."'
check "texts the conf does not translate stay English" 'printf "%s\n" "$out" | grep -q "Competitive pack is"'
sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"; sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"; sed -i '' '$d' "$X/conf/import/odinguard_conf.txt"
restart_map
lang_ini en

echo "== $FAIL failure(s)"
exit $FAIL
