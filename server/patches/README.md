# OdinGuard Anti Cheat – patch for upstream rAthena

`rathena-4cd08b7.patch` installs the complete server module into upstream rAthena
([rathena/rathena](https://github.com/rathena/rathena), branch `master`).

| | |
|---|---|
| Base commit | `4cd08b739035721b2cbffa0310dbbf0eac081d9d` (2026-11-23, "Removed duplicate header includes in clif.cpp and npc.cpp (#10186)") |
| Content | 39 files, +8182 / −9 lines: module files, Monocypher, all hooks, build files, config |

## What the patch contains

- Module files (unchanged copies of `server/src` and `server/conf`): `src/common/odinguard.cpp/.hpp`, `og_crypto.cpp`,
  `og_proto.h`, `og_wire.c/.h/.cpp`; `src/login/og_login.cpp/.hpp`; `src/map/og_map.cpp/.hpp`, `og_map_guard.cpp`;
  `conf/odinguard_athena.conf`.
- `3rdparty/monocypher/` (`monocypher.c/.h`, `LICENCE.md`) – upstream rAthena does not ship Monocypher.
- Hooks (see `../HOOKS.txt` and `../README.md`): `socket.cpp/.hpp`, `login.cpp`, `loginclif.cpp`, `char_clif.cpp`,
  `clif.cpp/.hpp`, `skill.hpp` (declaration of `skill_get_unit_layout_type`), `map.cpp`, `pc.cpp`, `atcommand.cpp` (`@ogban @ogunban @oginfo @ogflags @ogflagclear @oglock @ogbanwave @namecolor
  @lgp @square @circle @aoes @pf`/`@packetfilter` `@spb`), `clif.cpp` `clif_send_sub` (`og_map_pf_filtered`),
  `conf/groups.yml` (player group: `lgp`, `square`, `circle`, `aoes`, `pf`, `packetfilter`, `spb`).
- Build files: `src/common/Makefile.in` (`odinguard.o og_crypto.o og_wire.o`; `og_proto.h og_wire.h og_wire.c` as
  header dependencies so `og_wire.o` is rebuilt), `src/common/CMakeLists.txt`, and the Visual Studio projects and
  filters of common, login-server, map-server and map-server-generator (`og_wire.c` is listed as a header because
  `og_wire.cpp` includes it). Login and map `Makefile.in`/`CMakeLists.txt` pick up the new `.cpp` files by wildcard.

Not part of the patch: the unrelated custom changes of the development fork (auras, `@whosell`, battleground
extensions, `max_clients_per_ip`).

## Apply

Plain `git apply` (no `git am`, the file is a diff, not a mail series):

```sh
cd rathena
git checkout 4cd08b739035721b2cbffa0310dbbf0eac081d9d   # or a newer master, see below
git apply --check /path/to/OdinGuard/server/patches/rathena-4cd08b7.patch
git apply /path/to/OdinGuard/server/patches/rathena-4cd08b7.patch
./configure --enable-packetver=20180620     # your PACKETVER
make clean && make server
```

Without git: `patch -p1 < rathena-4cd08b7.patch`.

Then continue with [docs/INTEGRATION.md](../../docs/INTEGRATION.md) section 6, steps 5–6 (secrets, first start). The
servers create `conf/import/odinguard_conf.txt` and the tables `og_client`, `og_component`, `og_ban`, `og_event`,
`og_flag`, `og_command`, `og_account_lock` and `og_crash_token` on the first start.

## Newer rAthena: resolving conflicts

The module files never conflict (they are new). Conflicts can only appear in the hook context:

1. Try a three-way apply first: `git apply --3way rathena-4cd08b7.patch` (needs the base commit in your history,
   i.e. a clone of upstream rather than a tarball). Or apply with rejects: `git apply --reject ...` and fix the
   `*.rej` hunks by hand.
2. For every rejected hunk look up the hook in `../HOOKS.txt` (file:line:content of the reference fork) and the
   function it belongs to in `../README.md` ("Hooks" table). Place the line in the same function at the same logical
   spot; the line numbers in HOOKS.txt are only a guide.
3. `clif_og_honeypot()` (listed in HOOKS.txt) goes into `src/map/clif.cpp` after `clif_sendfakenpc`; its declaration
   in `src/map/clif.hpp` must be outside any `#ifdef` (used by `og_map_guard.cpp`).
4. Typical upstream drift: renamed/reshaped packet structs in `clif.cpp` (`packet_idle_unit`,
   `PACKET_ZC_NOTIFY_GROUNDSKILL`), `map_session_data*` vs `map_session_data&` parameters, and new `ACMD_DEF` list
   order. The hooks only need `sd`, `fd`, `skill_id`, `target_id` and the packet buffer.
5. Rebuild and check the map-server log for `OdinGuard Anti Cheat: enabled`.

## Smoke test (base 4cd08b7)

- 2026-11-26 (account lock, crash upload, `@ogflagclear`, `@oglock`, `@pf`, `@spb`, `og_command`): `make server -j2` in
  `alpine:3.23` built login, char and map server without errors. CI builds the patch on every push: GCC (Ubuntu) and
  Visual Studio (MSBuild, Windows, pre-renewal, warnings as errors like upstream's own CI).

- `make server -j3` in `alpine:3.23` (`./configure --enable-packetver=20180620`): login, char and map server built
  without errors; the only new warning is `-Wshadow` inside `monocypher.c`.
  Rebuilt with `make server -j2` after the module update (`BCryptGenRandom` on Windows): no errors; the Linux
  startup path is unchanged, so the run below was not repeated.
- CMake (`cmake -DPACKETVER=20180620`, targets `login-server char-server map-server`) on the same tree: built.
- Run: MariaDB 11 in its own Docker network, `sql-files/main.sql` + `logs.sql` imported, `conf/import/odinguard_conf.txt`
  with `og_enable: yes`, `og_log_only: yes` and random `og_key`/`og_proto_salt`/`og_build_secret`:
  - login, char and map server log `OdinGuard Anti Cheat: enabled (required: no, log only: yes, 22 signatures,
    challenges)`;
  - char-server connected to the login-server, map-server connected to the char-server, `Map Server is now online`
    (inter-server links stay plain);
  - the tables `og_ban`, `og_client`, `og_component`, `og_event`, `og_flag` were created;
  - a plain client login packet (0x0064, unknown account) was answered with `0x083e` (refused, code 0), i.e. clients
    without OdinGuard still pass when `og_required` is off.
- Not covered here: a real OdinGuard client handshake (use the project's end-to-end test suite for that).

## PACKETVER notes

- Tested with `PACKETVER 20180620`; upstream's default is `20211103`. The module itself has no PACKETVER switches.
- Lite ground skills (`@lgp`, `@square`, `@circle`, `@aoes`) replace units only in the skill unit packet `0x09ca`,
  i.e. `PACKETVER >= 20130731`. The `clif_skill_poseffect` hook hides the heavy animation for those players with every
  PACKETVER, so with older PACKETVER they would see neither animation nor cells: keep `og_lgp_enable` off there.
- The honeypot unit (`clif_og_honeypot`) uses `packet_idle_unit` with the same PACKETVER conditions as
  `clif_sendfakenpc`.

## Known limitations

- Windows: the Visual Studio project entries are included but untested (`odinguard.cpp` uses `BCryptGenRandom` there
  and links `bcrypt.lib` via `#pragma comment`; Linux uses `/dev/urandom`).
