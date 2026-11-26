#!/bin/sh
# Local OdinGuard test servers: MariaDB + login/char/map of a built xAthena tree in Docker, configured with a Studio
# release (server package + secrets). Reachable from the Windows test VM as $HOST_IP.
#   tests/e2e/server_env.sh <xathena build dir> <release dir> [extra conf lines file]
# Accounts: Mave/ogtest (GM 99, char OgTester), ogtest2 (OgPlayer), ogtest3 (OgThird).
set -e
X=$(cd "$1" && pwd)
R=$(cd "$2" && pwd)
EXTRA=${3:-}
HOST_IP=${HOST_IP:-10.211.55.2}
docker rm -f ogt-db ogt-srv >/dev/null 2>&1 || true
. "$R/secrets/odinguard.env"
mkdir -p "$X/conf/import" "$X/conf/odinguard"
cp "$R/server/conf/odinguard_athena.conf" "$X/conf/odinguard_athena.conf"
cp -R "$R/server/conf/odinguard/"* "$X/conf/odinguard/"
{
	cat "$R/server/conf/import/odinguard_conf.txt"
	printf 'og_key: %s\nog_proto_salt: %s\nog_build_secret: %s\nog_alive_interval: 5\nog_alive_timeout: 20\n' "$ODINGUARD_KEY" "$ODINGUARD_PROTO_SALT" "$ODINGUARD_BUILD_SECRET"
	[ -n "$EXTRA" ] && cat "$EXTRA"
} > "$X/conf/import/odinguard_conf.txt"
cat > "$X/conf/import/inter_conf.txt" <<'X'
login_server_ip: 127.0.0.1
login_server_id: ragnarok
login_server_pw: ragnarok
login_server_db: ragnarok
char_server_ip: 127.0.0.1
char_server_id: ragnarok
char_server_pw: ragnarok
char_server_db: ragnarok
map_server_ip: 127.0.0.1
map_server_id: ragnarok
map_server_pw: ragnarok
map_server_db: ragnarok
ipban_db_ip: 127.0.0.1
ipban_db_id: ragnarok
ipban_db_pw: ragnarok
ipban_db_db: ragnarok
log_db_ip: 127.0.0.1
log_db_id: ragnarok
log_db_pw: ragnarok
log_db_db: ragnarok
X
printf 'login_port: 6900\n' > "$X/conf/import/login_conf.txt"
printf 'login_port: 6900\nchar_port: 6121\nchar_ip: %s\nlogin_ip: 127.0.0.1\nserver_name: OGTest\npincode_enabled: no\n' "$HOST_IP" > "$X/conf/import/char_conf.txt"
printf 'char_port: 6121\nmap_port: 5121\nchar_ip: 127.0.0.1\nmap_ip: %s\n' "$HOST_IP" > "$X/conf/import/map_conf.txt"
grep -q "^atcommand_symbol: +" "$X/conf/import/battle_conf.txt" 2>/dev/null || printf "\natcommand_symbol: +\n" >> "$X/conf/import/battle_conf.txt"
docker run -d --name ogt-db -e MARIADB_ROOT_PASSWORD=root -e MARIADB_DATABASE=ragnarok -e MARIADB_USER=ragnarok -e MARIADB_PASSWORD=ragnarok \
  -v "$X/sql-files/main.sql:/docker-entrypoint-initdb.d/1.sql" -v "$X/sql-files/logs.sql:/docker-entrypoint-initdb.d/2.sql" \
  -p 6900:6900 -p 6121:6121 -p 5121:5121 mariadb:11 >/dev/null
# the image imports the SQL with a temporary server and restarts: wait for the second "ready for connections"
for i in $(seq 1 90); do sleep 2; [ "$(docker logs ogt-db 2>&1 | grep -c 'ready for connections')" -ge 2 ] && break; done
docker exec -i ogt-db mariadb -uragnarok -pragnarok ragnarok <<SQL

INSERT INTO login (account_id,userid,user_pass,sex,email,group_id) VALUES (2000000,'Mave','ogtest','M','a@a.com',99),(2000001,'ogtest2','ogtest','M','b@a.com',0),(2000002,'ogtest3','ogtest','F','c@a.com',0);
INSERT INTO \`char\` (char_id,account_id,char_num,name,class,base_level,job_level,last_map,last_x,last_y,save_map,save_x,save_y,hp,max_hp,sp,max_sp,skill_point) VALUES
 (150000,2000000,0,'OgTester',0,99,10,'prontera',156,180,'prontera',156,180,400,400,100,100,0),
 (150001,2000001,0,'OgPlayer',0,10,10,'prontera',158,180,'prontera',158,180,100,100,10,10,0),
 (150002,2000002,0,'OgThird',0,10,10,'prontera',154,180,'prontera',154,180,100,100,10,10,0);
INSERT INTO skill (char_id,id,lv,flag) VALUES (150001,142,1,0),(150000,142,1,0);
SQL
docker run -d --name ogt-srv --network container:ogt-db -v "$X:/rathena" -w /rathena alpine:3.23 sh -c 'apk add -q libstdc++ zlib mariadb-connector-c >/dev/null; ./login-server > /tmp/login.log 2>&1 & sleep 3; ./char-server > /tmp/char.log 2>&1 & sleep 3; ./map-server > /tmp/map.log 2>&1 & wait' >/dev/null
for i in $(seq 1 90); do sleep 5; docker exec ogt-srv sh -c 'grep -a -q "Map Server is now online" /tmp/map.log' 2>/dev/null && break; done
docker exec ogt-srv sh -c 'grep -a "OdinGuard" /tmp/map.log | head -8'
echo "test servers ready"
