# OdinGuard

Own client/server protection for the BurningRO Ragnarok Online servers (successor of the
abandoned Gepard Shield idea). Client: x86 Windows DLL. Server: module for xAthena
(rAthena fork). **Local repository only, no remote.**

* `docs/01-muhro-reference-analysis.md` – how a current guard is bound to the client
* `docs/02-architecture.md` – architecture, feature mapping, wire design, risks
* `docs/03-integration.md` – step-by-step client/server integration and phases
* `docs/04-reverse-engineering.md` – Ghidra/REA findings in alpha.exe (send/recv, main loop, chat)
* `docs/05-tests.md` – test setup and results
* `proto/og_proto.h` – wire protocol shared with the server (copy in xAthena `src/common/og_proto.h`)
* `client/` – `odinguard.dll` (guard), `ijl15.dll` (loader proxy), `tests/selftest.c`, `tests/nettest.c`
* `server/sql/odinguard.sql` – reference copy of the server tables (the servers create them at start)
* `tools/og_keygen.c` – server key pair (`keys/server.seed` secret, `client/src/og_pubkey.h` public)
* `tools/make_manifest.py` – client manifest (file hashes) generator
* `tools/ghidra/` – headless Ghidra scripts
* `third_party/` – Monocypher (CC0/BSD-2), MinHook (BSD-2)

The server side lives in xAthena: `src/common/odinguard.*` (socket layer), `src/login/og_login.*`,
`src/map/og_map.*`, `conf/odinguard_athena.conf`.

## Build (macOS, cross-compile)

```
brew install mingw-w64
make -C client            # -> client/build/{odinguard.dll,ijl15.dll,selftest.exe,nettest.exe}
                          # first run creates keys/server.seed (secret) and client/src/og_pubkey.h
```

`selftest.exe` (run in the Windows VM from the build dir) prints the report the future
server hello will carry: exe code hash, hardware components, unique id, VM flag, file
hashes and loaded modules; `selftest.exe crash` also writes a crash report.

## Config (`odinguard.ini` next to the exe, optional)

```
[OdinGuard]
Ports=6900,6121,5121,6901,6122,5122   ; protected server ports (default)
MaxWindows=0                          ; fallback when the server policy sends 0
```

The protection switches (VM block, window limit, input filter, resource rules, tool signatures,
heartbeat) come from the server policy after the handshake. The hardware salt is compiled in.

## Status (v0.2, 2026-11-23)

Done and tested end to end (see `docs/05-tests.md`): handshake (X25519 + pinned EdDSA server key),
XChaCha20-Poly1305 records for the whole game stream, hardware id with 13 components (incl. disk
serial, MAC, SMBIOS UUID) and exact/fuzzy bans, server policy, heartbeat with exe code hash, tool
signatures (process/window/module), injected-module report, debugger flag, hook integrity flag, VM
check (CPUID + firmware strings), window limit, resource rules, crash log with last packets, `!ping`,
`!vsync [on|off]`, `!crash`, input filter (built, not tested), real client via the loader chain.

Open: RCX and Warsaw tests on real machines, LGP / auras / colour nicks / colour items (need render
hooks in alpha.exe, see `docs/04-reverse-engineering.md`), exe import patch as hardening, signed
releases, website list of the commands.
