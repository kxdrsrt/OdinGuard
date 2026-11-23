# OdinGuard

Own client/server protection for the BurningRO Ragnarok Online servers (successor of the
abandoned Gepard Shield idea). Client: x86 Windows DLL. Server: module for xAthena
(rAthena fork). **Local repository only, no remote.**

* `docs/01-muhro-reference-analysis.md` – how a current guard is bound to the client
* `docs/02-architecture.md` – architecture, feature mapping, wire design, risks
* `docs/03-integration.md` – step-by-step client/server integration and phases
* `client/` – `odinguard.dll` (guard), `ijl15.dll` (loader proxy), `tests/selftest.c`
* `server/sql/odinguard.sql` – server tables
* `tools/make_manifest.py` – client manifest (file hashes) generator

## Build (macOS, cross-compile)

```
brew install mingw-w64
make -C client            # -> client/build/{odinguard.dll,ijl15.dll,selftest.exe}
```

`selftest.exe` (run in the Windows VM from the build dir) prints the report the future
server hello will carry: exe code hash, hardware components, unique id, VM flag, file
hashes and loaded modules; `selftest.exe crash` also writes a crash report.

## Config (`odinguard.ini` next to the exe)

```
[OdinGuard]
MaxWindows=2    ; 0 = unlimited
BlockVM=0
LogOnly=1       ; 1 = only log, never block (default while rolling out)
Salt=...        ; per-server salt for the hardware hashes
```

## Status (v0.1.0, phase O1 skeleton)

Done: SHA-256, exe code-section hash (relocation-neutral), file hashes, module scan,
hardware components + unique id, VM check (CPUID), windows-per-machine limit, crash log
(registers, EBP call stack, stack data, modules, GRF lists, packet ring buffer), exports
`OdinGuard_{Version,Start,Report,CrashNow}`, loader proxy, manifest tool, SQL schema.

Open (see docs): net hello/keys/channel (O2), server module in xAthena, `!ping`/`!vsync`,
disk serial and MAC as extra components, signature list, input filter, asset policy (O3/O4).
Known limit: under Windows-on-ARM x86 emulation the CPUID hypervisor bit is not set, so the
VM check does not fire inside the Parallels test VM.
