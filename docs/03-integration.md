# 03 – Client and server integration, step by step

## A. Client (BurningRO 2018-06-20 client)

Goal: `alpha.exe` starts `odinguard.dll` before any game logic, without a patched exe
at first.

1. **Build** the guard: `make -C client` (32-bit, mingw-w64 or MSVC x86). Output
   `client/build/odinguard.dll`.
2. **Loader chain (no exe patch).** `alpha.exe` loads `ijl15.dll` by name. Today that is
   the ROExt proxy. Rename it to `ijl15r.dll` (it still forwards to `ijl15x.dll`) and put
   OdinGuard's `ijl15.dll` (`client/loader/`) in its place:
   `alpha.exe → ijl15.dll (loader) ⇒ forwards 6 exports → ijl15r.dll (ROExt) → ijl15x.dll`,
   and the loader additionally maps `odinguard.dll` from the same folder. Tested: loader
   and guard build and the guard runs on x86 Windows (see `client/tests`).
3. **Manifest.** `python tools/make_manifest.py <client-dir> --build 2018-06-20-a`
   writes `manifest.json` (SHA-256 of `alpha.exe`, all DLLs, GRFs hash tree optional).
   Import it on the server (`@ogmanifest` or `server/sql/odinguard.sql` insert).
4. **Pin the server key (O2).** An Ed25519 key pair is generated per deployment
   (`keys/`, git-ignored). The public key is compiled into `odinguard.dll`, the private key
   stays on the server (`conf/import/odinguard.conf`, never in git).
5. **Ship** `odinguard.dll` + `ijl15.dll` (loader) + `ijl15x.dll` + updated `DATA.INI`
   through the patcher; add the files to the patcher's required list.
6. **Stage 2 hardening (O3):** patch the exe import table to include `odinguard.dll`
   (`tools/patch_import.py`, with backup). After that, deleting the proxy no longer
   removes the guard. Re-run `make_manifest.py` because the exe hash changes.
7. **Test matrix:** clean Windows 10/11, Wine/Proton, VM (blocked when `og_block_vm`),
   RCX, Warsaw machine, antivirus on.

## B. Server (xAthena)

Files, hooks, key, environment, rollout and commands: `server/README.md` (exact hook lines in
`server/HOOKS.txt`). Tables are created at start; the config has working defaults.

## B2. Release package

`sh tools/make_release.sh` builds `dist/OdinGuard-<ver>-client.zip` (loader, guard, ini, install and
uninstall, README, licences) and `dist/OdinGuard-<ver>-server.zip` (module, config, SQL, setup guide,
aura check), plus SHA-256 sums. The DLLs are stripped and reproducible. Players install with
`install.bat <client folder>`; for the patcher, ship `ijl15.dll`, `ijl15r.dll` (ROExt), `odinguard.dll`
and optionally `odinguard.ini`.

## C. Order of work (maps to the roadmap)

| Phase | Deliverable | Status |
|---|---|---|
| O0 | Repo, reference analysis, architecture, build toolchain | **done** |
| O1 | Guard: integrity, hwid, VM, window limit, crash log, manifest tool, SQL | **done** |
| O2 | Handshake + encrypted records + `!ping`, ws2_32 hooks, server module (login/char/map), bans | **done, tested end to end** |
| O3 | Tool signatures, injected modules, `!vsync`, `!crash`, Warsaw/RCX tolerance | **done** (RCX/Warsaw untested) |
| O4 | Input filter, resource rules | **done** (input filter untested) |
| O4 | LGP, auras, colour nick/item, `@showauras`, splash/branding | **done, tested** (v0.3) |
| O5 | Release packages (client + server), reproducible builds | **done** |
| O6 | Exe import patch, signed releases, RCX/Warsaw/input tests on real machines | open |

## D. Live rollout (2026-11-23)

- Easypanel (both services): `ODINGUARD_KEY`, `ODINGUARD_ENABLE=yes`, `ODINGUARD_REQUIRED=no`,
  `ODINGUARD_LOG_ONLY=yes`. Clients without OdinGuard keep working; clients with it talk encrypted.
- Dev client `client-dev/ragnarok` has the loader installed; package in `client-dev/odinguard/`
  (`install.bat` / `uninstall.bat`).
- Next steps: test with the dev client, read `og_event`, then `ODINGUARD_LOG_ONLY=no`, ship the DLLs
  with the patcher, finally `ODINGUARD_REQUIRED=yes`.
