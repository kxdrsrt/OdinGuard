# 01 – Reference analysis: how a modern RO client guard is bound (MuhRO)

Scope: structure and binding only. No keys, protocol constants or detection
lists were extracted, and none will be copied. Analysis date 2026-11-23, files
copied read-only from the VM (`C:\muh\MuhRO`) to the git-ignored `analysis/_bin/`.

Note: MuhRO does **not** ship a `gepard.dll`. It uses its own guard (`MuhExt.dll`,
internal name "MuhGuard"). It is still a good example of a current 2018+ design
and shows the same binding pattern as Gepard.

## 1. Files and roles

| File | Size | Role |
|---|---|---|
| `Muh.exe` | 49.8 MB | Game client. Themida-protected (`.themida`, `.boot`, `.scrt`), signed (Authenticode), has TLS callbacks. |
| `MuhExt.dll` | 4 MB | The guard and extensions. **Statically imported by the exe** (string `MuhExt.dll` is in the exe's import table), so the loader maps it before game code runs. |
| `ijl15.dll` (9.7 KB) | – | Small proxy of the Intel JPEG lib. Reads `dinput.ini` (ROExt settings) and forwards to `ijl15x.dll` (the real one). Classic extension hook, unrelated to the guard. |
| `v3hunt.dll`, `mguard1.dat`, `mguard2.dat`, `muhguard-reject-paths.v1` | – | Data and AV helper. The reject-path list is a server-supplied deny list for resource paths. |
| `muhvault-current.mvm` + `.sig` + `muhvault-signing.public` | 338 MB | Signed asset container. Public key shipped, signature checked at start. |
| `plugin.ini` | 5 KB | LGP (Lite Graphics Plugin): per-skill ground-cell images and fade times. |
| `GRF.INI` | – | Plain GRF load order: `custom.grf`, `addons.grf`, `muh-generated.grf`, `muh.grf`. |
| `Muh_Patcher.exe` | 18 MB | Patcher / launcher. |
| `ddraw.dll` (8.9 MB), `dxvk.conf`, `dgVoodoo.conf` | – | Graphics wrapper. |

## 2. Binding mechanism

```
Muh_Patcher.exe  --(starts)-->  Muh.exe
                                  │ Windows loader resolves imports
                                  ▼
   basesdk.dll, iapsdk.dll, binkw32.dll, granny2.dll, …, MuhExt.dll   (static imports)
                                  │ DLL init (DllMain, TLS callbacks)
                                  ▼
   MuhExt.dll exports a versioned C ABI (35 symbols, suffix V1/V2/V3), e.g.
     MuhGuardImportBind_<hash>        <- bind function; hash differs per build
     MuhGuardRouteNativeInboundV1     <- inbound packet routing
     MuhVaultAcquireClientBridgeV3
```

Key facts:

1. **Binding is an import-table patch of the exe**, not injection. `MuhExt.dll`
   is a normal static dependency, so the loader maps it before any game code.
   No `CreateRemoteThread`/`LoadLibrary` injection, which is robust against
   launchers and compatibility layers.
2. **Two-way coupling.** The exe imports DLL functions and the DLL hooks the exe
   (Detours sections `.detourc/.detourd`). The per-build hash in the bind export
   name means a swapped DLL does not fit another build.
3. **Versioned ABI** (`…V1`, `…V3`): the exe uses a small stable surface, so the guard
   can be updated independently.
4. **Network protocol** has its own namespaces inside the DLL (hello with build +
   device fingerprint, framed and keyed messages in the normal game connection,
   crash reports with a schema version).
5. HTTP (`winhttp`) for updates/reports; signature checks via `wintrust`/`crypt32`/`bcrypt`.

## 3. Reference client (2018-06-20 Sakray)

* `alpha.exe`: 32-bit, packed/patched, no guard imports.
* `client-dev/ragnarok/ijl15.dll` is already an **ROExt proxy** (from muhRO, config
  `dinput.ini`), original renamed to `ijl15x.dll`. The exe loads it by name, so we
  have a guaranteed load point **without touching the exe**.
* GRFs are read through `DATA.INI` (`cdata.grf` first).

## 4. Consequences for OdinGuard

* Load point 1 (now): own proxy DLL named `ijl15.dll` that chains to `ijl15x.dll`
  and loads `odinguard.dll` (reversible, no exe patch).
* Load point 2 (hardening): import-table patch of the exe so the guard is mapped
  before any game code and cannot be removed by deleting the proxy.
* Keep a small versioned export ABI between loader and guard.
* Per-build bind name/secret.
* Server authority: the DLL collects and reports, the server decides.
