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

1. `src/map/odinguard.cpp/.hpp` (new): registered in `Makefile.in` / `CMakeLists.txt`.
2. `conf/import/odinguard.conf` is generated at first start with defaults; the SQL
   tables come from `og_init()` (`CREATE TABLE IF NOT EXISTS`, same DDL as
   `server/sql/odinguard.sql`).
3. Hook points:
   - `clif.cpp`: packet table entry for `CZ_OG_FRAME`, dispatch to `og_session`.
   - `login`: ban check by unique id in `login_auth_ok`; `char`: refuse `chr_select`
     until the handshake is complete when `og_required: true`.
   - `pc_authok`: attaches session info (already has `max_clients_per_ip`).
4. Admin: `@ogban <id|char>`, `@ogunban`, `@ogwho`, plus ban list export for the web panel.
5. Rollout: `og_required: false` + `og_log_only: true` first (collect data), then
   `og_required: true`.

## C. Order of work (maps to the roadmap)

| Phase | Deliverable | Status |
|---|---|---|
| O0 | Repo, reference analysis, architecture, build toolchain | **done** |
| O1 | Guard skeleton: integrity, hwid, VM, window limit, crash log, manifest tool, SQL | **client skeleton done and tested in the VM**; server module open |
| O2 | Handshake + framed channel + `!ping`, ws2_32 hooks, server module | next |
| O3 | Tool signatures, RCX/Warsaw fixes, import patch, `!vsync` | planned |
| O4 | Input filter, no-delay, asset/GRF policy, LGP, auras, colour nick/item | planned |
