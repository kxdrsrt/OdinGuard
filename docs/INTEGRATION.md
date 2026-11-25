# Integrating OdinGuard Anti Cheat into your server and client

This guide takes a server owner from zero to a protected rAthena server: build a release from a profile, patch
the server, ship the client files, configure the GRF check for your patcher, and roll out in stages without
locking out honest players.

German notes: [03-integration.md](03-integration.md). Architecture: [02-architecture.md](02-architecture.md).

## 0. What you need

| | |
|---|---|
| Server | rAthena (or a fork) with MariaDB/MySQL, C++17 toolchain. Tested: PACKETVER 20180620 **pre-renewal**. The lite ground skills (LGP) and the competitive-pack map lists are pre-renewal only; all other features are not tied to the renewal switch but untested there. |
| Client | Windows client 2010-07-30 or newer that loads `ijl15.dll` (all Ragexe/RagexeRE builds do). Tested: 2018-06-20. |
| Build machine | macOS or Linux with `mingw-w64` (`brew install mingw-w64` / `apt install mingw-w64`), Rust (rustup), optional Node.js for the Studio GUI. Windows works through WSL. |
| Patcher | any patcher that can ship files and DATA.INI changes (Thor, NeonCube, rpatchur, ...). |

## 1. Build the Studio and create a profile

```sh
git clone https://github.com/kxdrsrt/OdinGuard && cd OdinGuard
cd studio && cargo build --release && cd ..
export PATH="$PWD/studio/target/release:$PATH"      # the `og` command

og new myserver.profile.toml --id myserver --operator "My Ragnarok"
```

`og new` writes the profile and three secret files next to it (`keys/server.seed`, `keys/signing.seed`,
`keys/build.secret`). **Back them up and never commit or ship them.** If you lose `server.seed`, every client you
shipped must be replaced; if you change `general.hwid_salt`, every machine ban becomes useless.

The GUI does the same with forms: `cd studio/apps/studio && npm install && npx tauri dev`.

## 2. Choose modules, commands and signatures

Open the profile in the Studio (tab *Module & Challenges*) or edit the TOML:

- `modules` – what is built into the DLL and enabled on the server. Unselected modules are not compiled into the
  client (the build checks the linker map). Start with the default set; challenges, `rate-limit`, `behaviour`,
  `ch-captcha`, `ch-honeypot` are opt-in.
- `[params]` – module parameters (`"heartbeat.interval" = 30`, `"multi-client.max_per_machine" = 3`, ...). The
  Studio shows ranges and refuses invalid values (`og validate`).
- `[commands.<id>]` – switch commands off, rename them, change the minimum group or the rate limit.
- `[signatures]` – signature sources and allowlists (`signatures/*.toml`), `max_action` (caps every signature
  action, e.g. `flagban` during the rollout) and per tool group: `[signatures.groups] RCX = "kick"`,
  `GRF = "off"`, `DEBUG = "log"`. `og sig-groups myserver.profile.toml` lists the groups.

Add your own signatures in a separate TOML file (same format as `signatures/signatures.toml`) and list it in
`signatures.sources`. Legitimate tools your players use go to an allowlist file (`[[allow]]`, kinds `file_sha256`,
`grf_sha256`, `path`).

## 3. GRF check (Gepard-style, patcher friendly)

The guard compares every GRF in the client's `DATA.INI` with your list: name, size and a *tail hash* (header +
file table, checked at every scan in milliseconds), and in a background thread the full-file SHA-256.

```sh
og grf-scan myserver.profile.toml --client /path/to/your/clean/client --full
og grf-allow myserver.profile.toml /path/to/dark.grf --note "darkworld"     # optional mods, any name
```

- `grf.file` – accepted versions of your GRFs. Several versions with the same name may be listed.
- `grf.mod` – GRFs players may add (skins, translations, darkworld variants), matched by content, not by name.
- `grf.overlays` – folders where loose files are allowed (e.g. `data\texture\skin\`).
- Actions: `modified_action` (default `kick`), `unknown_action` (`log`), `missing_action` (`log`).
- `odin.grf` and the competitive pack are added by every build automatically.

**Patching workflow** (no false alarms while players update):

1. Build your patch and apply it to a clean client copy.
2. `og grf-scan myserver.profile.toml --client <patched copy> --full --keep-previous` (old and new versions are both
   accepted now).
3. `og build`, deploy the new server config (only `conf/import/odinguard_conf.txt` changes), restart the map-server.
4. Publish the patch through your patcher.
5. After a few days run `og grf-scan` without `--keep-previous` to drop the old versions.

## 4. Optional: competitive pack

An optional GRF (`odin-dark.grf`) that every player gets from you, so everyone plays with the same visuals (dark
floors, flat maps, simplified effects, ... – WoE RO / darkworld style). Pick categories in the Studio tab
*Competitive-Pack* (recipe `assets/competitive/recipe.toml`, staging folders built by
`tools/pack/build_categories.sh`) or point `assets.competitive_pack` to a ready GRF. Its hash enters the GRF
check, so modified or foreign variants are reported.

Per game mode and optionally **required**: pick the map variants `woe`, `bg`, `pvp` one by one
(`tools/pack/split_by_mode.py`), and set `assets.competitive_enforce = ["bg", ...]` so players without the pack are
sent back from those maps (empty = player choice). Check that the pack's maps match the server's map cache with
`tools/pack/verify_mapcache.py`. Everything is in [09-competitive-pack.md](09-competitive-pack.md); the lite ground
skills (`@lgp`, `@square`, `@circle`, `@lgpsound`) are in [08-lgp-guide.md](08-lgp-guide.md).

## 5. Build and verify the release

```sh
og build myserver.profile.toml          # -> output folder of the profile
og verify-release dist/myserver         # signature of manifest.json and every file hash
```

| Folder | Content | Goes to |
|---|---|---|
| `client/` | `odinguard.dll`, `ijl15.dll` (loader), `odin.grf`, `[odin-dark.grf]`, `odinguard.ini`, `README.txt`, licences | your patcher |
| `server/` | module sources, `conf/odinguard_athena.conf`, `conf/import/odinguard_conf.txt`, `conf/odinguard/` (signature package, reference DLL and GRFs), SQL | your rAthena |
| `secrets/odinguard.env` | `ODINGUARD_KEY`, `ODINGUARD_PROTO_SALT`, `ODINGUARD_BUILD_SECRET` | server environment only |
| `patcher/filelist.txt` | file, SHA-256, size | your patcher's file list |
| `docs/` | operator handbook, command reference (de/en), build report | you |

Builds are reproducible: the same profile and sources give byte-identical files. For Control Flow Guard build the
client with llvm-mingw: `OG_TOOLCHAIN=llvm og build ...` (see `client/Makefile`, `LLVM_MINGW`).

## 6. Server (rAthena)

**Quick path (upstream rAthena):** apply `server/patches/rathena-4cd08b7.patch` in your rAthena checkout
(`git apply --check …` then `git apply …`). It contains steps 1–4 below (module files, Monocypher, all hooks, Makefile,
CMake and Visual Studio entries, `conf/odinguard_athena.conf`). Run `./configure` again, rebuild and continue with
step 5. Base commit, conflict resolution on newer rAthena and PACKETVER notes: [`server/patches/README.md`](../server/patches/README.md).

**Manual steps** (forks, or when the patch does not apply):

1. Copy `server/src/common/*`, `server/src/login/*`, `server/src/map/*` into the same folders of rAthena.
   `og_crypto.cpp` compiles Monocypher: copy `third_party/monocypher.c/.h` to `3rdparty/monocypher/`.
2. Add the hook lines listed in [`server/HOOKS.txt`](../server/HOOKS.txt) (socket layer, login, char and map
   server, atcommands, `conf/groups.yml`). [`server/README.md`](../server/README.md) explains each hook.
3. Add `odinguard.o og_crypto.o og_wire.o` to `src/common/Makefile.in` (and the CMake list), add `og_wire.c`,
   `og_wire.h`, `og_proto.h` to the common header dependencies, run `./configure` again and rebuild.
4. Copy `server/conf/*` into `conf/` (keeps your edits out of `odinguard_athena.conf`: the generated
   `conf/import/odinguard_conf.txt` overrides it).
5. Put the three values of `secrets/odinguard.env` into the server environment (Docker/Easypanel/systemd), or as
   `og_key`, `og_proto_salt`, `og_build_secret` lines into `conf/import/odinguard_conf.txt` (keep that file out of
   git then).
6. Start the servers. Tables (`og_client`, `og_component`, `og_ban`, `og_event`, `og_flag`) are created
   automatically; the map-server log shows `OdinGuard Anti Cheat: enabled (...)`.

## 7. Client

1. In the client folder rename the existing `ijl15.dll` (e.g. ROExt's proxy) to `ijl15r.dll`. A plain client has
   Intel's `ijl15.dll`: rename that to `ijl15r.dll` too.
2. Ship `client/ijl15.dll` and `client/odinguard.dll` next to the exe. Load chain:
   `exe → ijl15.dll (OdinGuard loader) → ijl15r.dll (ROExt or Intel) [→ ijl15x.dll]`; the loader also starts the
   guard.
3. Put `odin.grf` (and `odin-dark.grf`) first in `DATA.INI`:
   ```ini
   [Data]
   0=odin.grf
   1=odin-dark.grf
   2=rdata.grf
   3=data.grf
   ```
4. Add every file of `client/` plus `ijl15r.dll` and `DATA.INI` to your patcher's required list.
5. Players never install or uninstall anything themselves; there is no player uninstaller.

### Install into / remove from a client folder (operator tool)

For your own master and test clients, `tools/client/og_client.py` does steps 1-3 and the way back, with no dependencies
besides Python 3:

```sh
python3 tools/client/og_client.py status    ~/clients/full
python3 tools/client/og_client.py install   ~/clients/full --release myserver/dist/client [--pack [--pack-on]] [--dry-run]
python3 tools/client/og_client.py uninstall ~/clients/full [--keep-settings] [--dry-run]
```

* **Install** keeps the client's own `ijl15.dll` as `ijl15r.dll` (Intel's or ROExt's), copies the loader, `odinguard.dll`,
  `odin.grf` (and `odin-dark.grf` with `--pack`), puts `odin.grf` first in `DATA.INI` (numbered from 0, like the client's
  own `!dark`) and saves the original `DATA.INI` as `DATA.INI.pre-odinguard`. Running it again only updates the files.
  Files are written through a temporary file, so hard-linked client copies are never changed in place.
  The competitive pack is copied **switched off**: players turn it on with `!dark on` (and off with `!dark off`), each
  followed by a restart. `--pack-on` lists it in `DATA.INI` right away; a player's earlier choice survives an update.
* **Uninstall** puts `ijl15r.dll` back as `ijl15.dll`, removes the guard files, `odinguard.ini` (unless
  `--keep-settings`), `odinguard.log` and `odinguard-crash\`, and restores `DATA.INI`: the saved original when the other
  entries are unchanged, otherwise the same list without the OdinGuard entries. It also works on a client that was set up
  by hand (no manifest needed). If `ijl15r.dll` is missing it stops and says what to do; it deletes nothing then.
* It refuses an unclear state (an `ijl15r.dll` next to an `ijl15.dll` that is not the OdinGuard loader).
* Tested: `python3 tests/client/test_og_client.py` (13 cases, uninstall gives back the exact original folder), and on a
  copy of a real 5,545-file client (every non-GRF file identical after install + uninstall) with the game started
  through the loader chain (plain Intel `ijl15.dll`, and the ROExt/dxwrapper client).

On players' machines the patcher does this (the OpenThor patch list's `remove` and `files`); there is still no player
uninstaller. If you let OpenThor Patcher manage `DATA.INI` (`data_ini` in
its patch list), do not run this tool on the same client.

## 8. Roll out in stages

| Stage | Server | Effect | Move on when |
|---|---|---|---|
| 1 warn | `og_required: no`, `og_log_only: yes` | everything is only logged; flags collect, the ban wave runs dry | `og rollout` shows no unreviewed rule above the false-alarm threshold |
| 2 kick | `og_log_only: no` | actions apply to clients with the guard; players without it still play | false alarms stay low |
| 3 patcher | as 2 | the guard reaches every player | most active accounts log in with the guard |
| 4 required | `og_required: yes` | no login without a valid guard of your build | – |

The stage is a profile setting (`[rollout] stage`). Export `og_event` and `og_client` (`mysql -B -e "SELECT ..."`)
and run `og rollout myserver.profile.toml --events og_event.tsv --clients og_client.tsv --advance`, then rebuild
and deploy the server config. The Studio tab *Rollout* does the same.

## 9. Day-to-day

- In game: `@oginfo <name>` (guard version, machine, exe hash), `@ogflags [name]` (open flags), `@ogbanwave dry`
  (who the next ban wave would ban), `@ogban <name>, <reason>`, `@ogunban <account id>`.
- New signatures: edit your signature TOML, raise `signatures.version`, rebuild, replace
  `conf/odinguard/odinguard.sigpack`, restart the map-server. No client update needed.
- New OdinGuard version: rebuild with the same profile and keys, deploy server and client together (the Studio
  warns about protocol changes).

## 9a. Web panel (MidgardCP, optional)

The tables `og_client`, `og_component`, `og_ban`, `og_event`, `og_flag` and `og_command` are the interface for web panels; the
reference copy is `server/sql/odinguard.sql`. A panel such as MidgardCP (planned, `kxdrsrt/MidgardCP`) reads them with a
read-only database user and writes only:

- `INSERT` / `DELETE` on `og_ban` (machine ban / unban; the login-server checks `og_ban` at every login, no restart),
- `UPDATE og_flag SET state = 'cleared'` (false positive),
- `INSERT` into `og_command` (`account_id` or `unique_id`, `command` = `kick` or `message`, `arg`, `issued_by`): the
  map-server that has the player online runs it within `og_command_interval_ms` (default 3 s) and marks it `done`;
  rows nobody takes become `expired` after `og_command_expire_s` (default 600). Only these fixed commands exist, no
  free @commands. Typical use: machine ban in the panel, then `kick` so the ban takes effect immediately.

Keep the column names stable; a change to these tables is a breaking change for panels. Planned on the OdinGuard
side for panels (no HTTP API in the game server): an account HWID lock table, an opt-in crash-dump upload to the panel, and a published GRF manifest (see
MidgardCP `docs/07-ecosystem.md`).

## 10. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Map log: `signature package signature INVALID` | package built with another signing key, or edited: rebuild and copy `conf/odinguard/` again |
| Client message "client built for another server" | the DLL was built from another profile (status 3) |
| Everyone gets `grf:modified` after a patch | the new GRF versions were not scanned before the patch – section 3 |
| `grf:unknown` for a skin GRF | allow it with `og grf-allow` or set `grf.unknown_action = "log"` |
| Antivirus warns about `odinguard.dll` | sign the DLLs with your Authenticode certificate; submit the files to the vendor |
| Detection of a legitimate tool | add it to an allowlist file, or set its group to `log` in `signatures.groups` |
