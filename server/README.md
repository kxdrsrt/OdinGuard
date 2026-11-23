# OdinGuard – server side (xAthena / rAthena)

This folder is the release copy of the server module. The working copy lives in xAthena; refresh it with
`tools/sync_server.sh` (also writes `HOOKS.txt`). `tools/make_release.sh` packs it with the client.

## Files

| File | Goes to | Purpose |
|---|---|---|
| `src/common/odinguard.cpp/.hpp` | `src/common/` | socket layer: handshake (X25519 + pinned Ed25519 key), XChaCha20-Poly1305 records, config, policy, alive check, SQL tables |
| `src/common/og_crypto.cpp` | `src/common/` | compiles `3rdparty/monocypher/monocypher.c` into common (copy Monocypher 4 there) |
| `src/common/og_proto.h` | `src/common/` | wire protocol, identical to the client's `proto/og_proto.h` |
| `src/login/og_login.cpp/.hpp` | `src/login/` | login check: client report, machine bans (exact + fuzzy), `og_client`/`og_component` |
| `src/map/og_map.cpp/.hpp` | `src/map/` | machine limit, `!ping`/`!vsync`/`!crash`, bans, LGP, auras, `@showauras`, name and item colours |
| `conf/odinguard_athena.conf` | `conf/` | all switches; local values and the key in `conf/import/odinguard_conf.txt` |
| `sql/odinguard.sql` | – | reference DDL; the servers create the tables themselves at start |
| `check_auras.py` | – | which hat effects (auras) a client can draw: `check_auras.py <client folder> --conf` |

## Hooks (exact lines: `HOOKS.txt`)

Build: `odinguard.o og_crypto.o` in `src/common/Makefile.in` (`COMMON_OBJ`) and both files in
`src/common/CMakeLists.txt`. Run `./configure` again after adding them.

| Where | What |
|---|---|
| `socket.hpp` `struct socket_data` | `void* og;` connection state |
| `socket.cpp` `recv_to_fifo` / `send_from_fifo` | `og_handles_recv/og_recv`, `og_handles_send/og_send`: decrypts and encrypts OdinGuard connections, plain ones pass through |
| `socket.cpp` `session_has_output` | `og_has_pending`: encrypted records still waiting to be sent count as output |
| `socket.cpp` `make_connection` | `og_mark_plain`: inter-server links are never OdinGuard connections |
| `socket.cpp` `delete_session` / `socket_init` / `socket_final` | `og_session_free`, `og_init`, `og_final` |
| `login.cpp` `LoginServer::initialize` / `finalize` | `og_login_init`, `og_login_final` |
| `loginclif.cpp` `logclif_auth_ok` | `og_login_check`: refuses banned machines (code 4), clients without OdinGuard when `og_required`, too old versions |
| `char_clif.cpp` `chclif_parse_reqtoconnect` | refuses clients without OdinGuard when `og_required` |
| `clif.cpp` `clif_parse_WantToConnection` | same for the map-server |
| `clif.cpp` `clif_parse_LoadEndAck` | `og_map_on_load` before `clif_spawn`: colour lists, aura from the char reg |
| `clif.cpp` `clif_parse_GlobalMessage` | `og_map_chat_command`: `!ping`, `!vsync`, `!crash` |
| `clif.cpp` `clif_getareachar_skillunit` | LGP: per viewer a flat ground unit instead of `UNT_DUMMYSKILL` |
| `clif.cpp` `clif_skill_poseffect` | LGP: viewers with `@lgp` do not get the heavy ground animation |
| `clif.cpp` `clif_hat_effects` / `clif_hat_effect_single` | `@showauras`: hat effects of others filtered per viewer |
| `map.cpp` `map_quit` / `MapServer::initialize` | `og_map_on_quit` (colour broadcast), `og_map_init` |
| `pc.cpp` `pc_authok` | `og_map_machine_limit` (`og_max_per_machine`) |
| `atcommand.cpp` | `@ogban`, `@ogunban`, `@oginfo`, `@aura`, `@namecolor` (GM); `@lgp`, `@showauras` (players) + `ACMD_DEF`s |
| `conf/groups.yml` | `showauras: true`, `lgp: true` in the player group's commands |

## Key

`make -C client keys` (client repo) creates `keys/server.seed` (secret) and the public key header compiled
into `odinguard.dll`. Give the seed to the servers as `og_key: <hex>` in `conf/import/odinguard_conf.txt` or as
`ODINGUARD_KEY` in the environment (Docker/Easypanel). Never commit it, never ship it with the client.

## Environment (Docker, `deploy/entrypoint.sh`)

`ODINGUARD_KEY`, `ODINGUARD_ENABLE`, `ODINGUARD_REQUIRED`, `ODINGUARD_LOG_ONLY` (Easypanel: `updateEnv`
needs `createDotEnv: true`).

## Rollout

1. `og_enable: yes`, `og_required: no`, `og_log_only: yes` – clients with and without OdinGuard work,
   detections only go to `og_event`.
2. Ship the client package with the patcher, read `og_event`, collect the `exe_code` of the real clients
   (`og_allowed_exe`).
3. `og_log_only: no` – detections act (log / kick / ban per rule).
4. `og_required: yes` – clients without OdinGuard can no longer log in.

## Commands

| Command | Who | Purpose |
|---|---|---|
| `@ogban <char>[, reason]` | GM | bans the machine of an online character (all its accounts), kicks it |
| `@ogunban <account id>` | GM | lifts the machine bans of all machines the account used |
| `@oginfo [char]` | GM | OdinGuard version, build, machine and exe hash of a character |
| `@aura <name\|off>[, char]` | GM | permanent aura from the `og_aura` list |
| `@namecolor <RRGGBB\|off>[, char]` | GM | permanent name colour (OdinGuard clients) |
| `@lgp` | player | lite ground skills on/off |
| `@showauras` | player | auras of other players on/off |
| `!ping`, `!vsync [on\|off]`, `!crash` | player | client commands (chat) |
