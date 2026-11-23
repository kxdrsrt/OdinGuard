# OdinGuard

Own client/server protection for the BurningRO Ragnarok Online servers (successor of the
abandoned Gepard Shield idea). Client: x86 Windows DLL. Server: module for xAthena
(rAthena fork). **Local repository only, no remote.**

* `docs/01-muhro-reference-analysis.md` – how a current guard is bound to the client
* `docs/02-architecture.md` – architecture, feature mapping, wire design, risks
* `docs/03-integration.md` – step-by-step client/server integration and phases
* `docs/04-reverse-engineering.md` – Ghidra/REA findings in alpha.exe (send/recv, main loop, chat)
* `docs/05-tests.md` – test setup and results
* `docs/06-gepard-comparison.md` – feature matrix vs Gepard Shield 3.0, language (C vs Rust)
* `proto/og_proto.h` – wire protocol shared with the server (copy in xAthena `src/common/og_proto.h`)
* `client/` – `odinguard.dll` (guard), `ijl15.dll` (loader proxy), `res/` (icon, splash, version),
  `package/` (install/uninstall, ini, README for players), `tests/` (selftest, nettest, inject)
* `server/` – release copy of the xAthena module (`src/`, `conf/`, `sql/`, `README.md` setup and hooks,
  `HOOKS.txt`); refreshed by `tools/sync_server.sh`
* `assets/` – emblem, logo, icon, splash and fonts (Cinzel Decorative, Philosopher; OFL)
* `tools/og_keygen.c` – server key pair (`keys/server.seed` secret, `client/src/og_pubkey.h` public)
* `tools/make_manifest.py` – client manifest (file hashes) generator
* `tools/make_release.sh` – builds `dist/OdinGuard-<ver>-client.zip` / `-server.zip` + SHA256SUMS
* `tools/sync_server.sh` – copies the server module from xAthena into `server/`
* `tools/check_auras.py` – which auras (hat effects) a client can draw without missing files
* `tools/make_branding.py` – renders logo, icon and splash from `assets/src`
* `tools/ghidra/` – headless Ghidra scripts
* `third_party/` – Monocypher (CC0/BSD-2), MinHook (BSD-2)

The server side lives in xAthena: `src/common/odinguard.*` (socket layer), `src/login/og_login.*`,
`src/map/og_map.*`, `conf/odinguard_athena.conf`.

## Build (macOS, cross-compile)

```
brew install mingw-w64
make -C client            # -> client/build/{odinguard.dll,ijl15.dll,selftest.exe,nettest.exe,inject.exe}
                          # first run creates keys/server.seed (secret) and client/src/og_pubkey.h
sh tools/make_release.sh  # release zips in dist/ (stripped, reproducible DLLs)
```

The DLL needs the Universal C Runtime (part of Windows 10/11; Windows 7/8.1 need KB2999226).

`selftest.exe` (run in the Windows VM from the build dir) prints the report the future
server hello will carry: exe code hash, hardware components, unique id, VM flag, file
hashes and loaded modules; `selftest.exe crash` also writes a crash report.

## Config (`odinguard.ini` next to the exe, optional)

```
[OdinGuard]
Ports=6900,6121,5121,6901,6122,5122   ; protected server ports (default)
SplashMs=1800                         ; splash screen at start, 0 = off
MaxWindows=0                          ; fallback when the server policy sends 0
TextLog=0                             ; debug: log every drawn text
```

The protection switches (VM block, window limit, input filter, resource rules, tool signatures,
heartbeat) come from the server policy after the handshake. The hardware salt is compiled in.

## Status (v0.3.0, 2026-11-23)

Done and tested end to end (see `docs/05-tests.md`, comparison in `docs/06-gepard-comparison.md`):
handshake (X25519 + pinned Ed25519 server key), XChaCha20-Poly1305 records for the whole game stream,
hardware id with 13 components and exact/fuzzy bans (`@ogban`/`@ogunban` in game), server policy,
heartbeat with exe code hash and alive timeout, tool signatures, injected-module report, machine limit,
VM check, resource rules, crash log with last packets, `!ping`, `!vsync`, `!crash`, colour nicks and
colour items (exe text hooks), auras (`@aura`, `@showauras`), lite ground skills (`@lgp`, server side,
no client files), splash screen and branding, release packages.

Open: input filter, RCX and Warsaw on real machines; exe import patch; signed releases; website list of
the commands.
