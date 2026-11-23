# 02 – OdinGuard architecture

## 1. Principles

1. **Server authority.** A client-side check can always be bypassed by someone who
   controls the machine. The DLL therefore only *collects and reports* signed/MAC'd
   evidence; the server decides (kick, ban, flag). Client-side checks only raise the
   cost of cheating and produce evidence.
2. **Small stable surface** between exe, loader and guard (versioned C ABI).
3. **No secrets in the repo.** Keys are generated per deployment into `keys/` (git-ignored).
4. **Own code.** Written from the public requirement list, not derived from any other
   guard's code, keys or wire format.
5. **Degrade safely.** If the guard cannot start, the client does not start; if the
   server module is absent, the client still plays (config `og_required`).

## 2. Components

```
┌───────────────────────── client (x86, Windows) ─────────────────────────┐
│ alpha.exe ── loads ──► ijl15.dll (OdinGuard loader/proxy)               │
│                           ├─► ijl15x.dll   (original, forwarded)        │
│                           └─► odinguard.dll (guard)                     │
│   odinguard.dll modules:                                                │
│     core      init, config, logging, versioned ABI                      │
│     integrity exe .text hash, file hashes, module allowlist             │
│     inject    LdrDllNotification + periodic module scan                 │
│     hwid      component fingerprint (several independent sources)       │
│     env       VM check, windows-per-machine limit, tool/process scan    │
│     net       hello + session key + framed channel (O2)                 │
│     input     synthetic input filter, no-delay checks (optional, O4)   │
│     crash     SEH filter, crash log, last packets, GRFs                 │
│     cmds      !vsync !crash !ping                                       │
│     assets    GRF override policy, LGP, auras, colour nick/item (O4)    │
└────────────────────────────────┬────────────────────────────────────────┘
                                 │ game TCP connection (char/map), one reserved packet id
┌────────────────────────────────▼────────────────────────────────────────┐
│ xAthena server module  (src/map + src/login, namespace odinguard)       │
│   og_init     CREATE TABLE IF NOT EXISTS + conf/import/odinguard.conf   │
│   og_session  X25519 handshake, HKDF, ChaCha20-Poly1305 frames          │
│   og_policy   allowed manifests, min client version, VM / multi-window  │
│   og_ban      ban by unique id (+ fuzzy component match), by account    │
│   og_report   crash reports, detections, audit log                      │
│   @og commands: @ogban @ogunban @ogwho @oginfo                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## 3. Feature mapping (your list → module → phase)

Phases are O0–O6 from `docs/estland-rebuild/15-odinguard.md`; this table is the detail.

| # | Requirement | Module | Mechanism | Phase |
|---|---|---|---|---|
| A1 | Runtime integrity of exe code section | integrity | Hash non-writable sections of the main module in memory at start and every N s; value reported, server compares with allowed manifest. Packed exe: hash is taken after unpack and whitelisted per build. | O1 |
| A1 | File integrity exe + all DLLs | integrity | SHA-256 of exe and `*.dll` in client dir via `BCrypt`; server compares with `manifest.json` (built by `tools/make_manifest.py`). | O1 |
| A1 | Unauthorized DLL injection | inject | `LdrRegisterDllNotification` + module list scan; every module must be in the manifest or be a system module in `System32/SysWOW64` with a valid signature (`WinVerifyTrust`). Report unknown module to server. | O1 |
| A2 | Dynamic key packet encryption | net | Per-connection X25519 ephemeral key exchange, server key pinned in the DLL, HKDF to session keys, rotation every N packets. The OG channel is AEAD (ChaCha20-Poly1305). For the game stream itself we feed the per-session values into rAthena's `packet_obfuscation`; this needs a client hook point found in O2 (see Risks). | O2 |
| A2 | Block WPE/RPE/OpenKore | net | Injected or replayed packets fail because: (a) OG frames are MAC'd with a counter, (b) the server requires periodic signed "alive" frames tied to the session, (c) packet-ID obfuscation keyed per session. Sniffing is defeated by (c). | O2 |
| A2 | Malformed chat packets | net + server | Server: strict length/charset validation of chat packets (0x00F3/0x08B8-type, whisper, guild) before processing. Client: drop inbound chat packets whose length field disagrees with the buffer before the exe parses them (hook in O2). | O1 (server) / O2 |
| A3 | Hardware Unique ID, server ban | hwid + og_ban | Not MAC only. Independent components, each hashed with a per-server salt: MachineGuid, SMBIOS UUID/serial, volume serial of the system drive, CPU brand/ID, disk serial, first physical MAC. Server stores the components and matches **fuzzily** (≥ k of n). Unique ID = SHA-256 of the sorted component hashes. | O1 |
| A3 | Max windows per machine | env | Client: named kernel semaphore with N slots (`Global\…`). Server: counts sessions per unique id. (Server-side `max_clients_per_ip` is already live.) | O1 |
| A3 | Optional VM block | env | CPUID hypervisor bit + vendor string + known VM devices; policy flag from server (`og_block_vm`). | O1 |
| A3 | Detect known tools (PotND, meth4u, xRag, xLike, RoTools) | env | Process/window class/module signature list delivered by the server (`og_signatures`), not hardcoded; updatable without a client release. | O3 |
| B1 | 2018 client, ≥ 2010-07-30aRagexeRE | core | Load via proxy (no exe patch); versioned structures; per-build offset table in `builds/<exe-hash>.json`. | O1 |
| B1 | RCX final compatibility | core | RCX loads via its own dinput8/ijl proxy. OdinGuard chains **after** it and tolerates RCX modules (allowlisted by manifest); test matrix in `tests/`. | O3 |
| B1 | Warsaw (Brazil) crash fix | core | Warsaw (`gbpsv/gbp`, `Gbp*` DLLs from Diebold) hooks into every process. Fix: do not hook APIs it detours (use inline-hook checks + disable own hooks if the target prologue already starts with a jump into a foreign module), and make module scan tolerate it. Needs a Warsaw machine to verify. | O3 |
| B2 | Crash log | crash | `SetUnhandledExceptionFilter` + vectored handler: registers, stack walk, stack dump, module list, GRF list (read from `DATA.INI`/GRF.INI), missing-resource log (hook later), last 32 sent/received packet headers (ring buffer, filled by net hooks). Upload to the server in O2. | O1 skeleton / O2 |
| B2 | `!vsync` | cmds | Intercept local `/`-chat-like input (client chat send hook), toggle DDraw/D3D present interval or the exe's FPS limiter. | O3 |
| B2 | `!crash` | cmds | Writes a crash log on demand without terminating (`MiniDump`-like). | O1 |
| B2 | `!ping` | cmds | Round-trip over the OG channel (ping/pong frame with timestamp), shown in chat. | O2 |
| B3 | Auto SQL + config | og_init | `CREATE TABLE IF NOT EXISTS` at map-server start, default `conf/import/odinguard.conf` written if missing. | O1 |
| C1 | Block synthetic mouse/keyboard | input | `SetWindowsHookEx(WH_MOUSE_LL/WH_KEYBOARD_LL)` and check `LLMHF_INJECTED/LLKHF_INJECTED`; `GetMessageExtraInfo` check on `WM_*` input. Optional flag. | O4 |
| C1 | Block nodelay / animation-delay | input | Server side: enforce `min_skill_delay_limit` (done: 200 ms) and validate action intervals; client side: detect patched delay values in the exe's `.text` (integrity covers it). | O4 |
| C2 | GRF override protection + LGP, auras, colour nick/item | assets | Hash and order-check configured GRFs; allow an explicit overlay path for the LGP/aura/colour features; override of protected files elsewhere is rejected and reported. | O4 |

## 4. Wire design (OG channel)

* Transport: the normal game TCP connection. One reserved packet id pair
  (`CZ_OG_FRAME` / `ZC_OG_FRAME`, variable length) — id chosen from the free range
  of the `PACKETVER 20180620` table and kept in `server/conf/odinguard.conf`.
* Handshake (after login-server accept, before char select):
  1. Client → server `HELLO{ver, build_id, nonce_c, eph_pub_c, hwid components, manifest_digest, flags}`
  2. Server → client `CHALLENGE{nonce_s, eph_pub_s, signature(server_static_key, transcript)}`
     — the client verifies against the **pinned** server public key.
  3. Both derive `k_c2s`, `k_s2c` = HKDF(X25519(eph), nonces); AEAD frames with a 64-bit
     counter (replay-proof).
  4. Server → client `POLICY{block_vm, max_windows, scan_interval, signature_list_version}`.
* After that: `ALIVE{counter, integrity_digest}` every `scan_interval`, `DETECT{type, data}`,
  `CRASH{…}`, `PING/PONG`.
* Server side decision: unknown/old manifest, missing alive for > 2 intervals, detections,
  or banned unique id → disconnect with a message; ban is applied at login (account
  and unique id).

## 5. Server data model (`server/sql/odinguard.sql`)

`og_client` (unique id ↔ account/last seen), `og_component` (component hashes per unique id),
`og_ban`, `og_manifest` (allowed exe/DLL hashes per build), `og_signature` (tool detection
list), `og_event` (detections/crashes), `og_session`.

## 6. Risks and honest limits

* **Packet hook point:** the game's send/recv functions inside the packed/patched
  `alpha.exe` must be located once (reverse-engineering of *our own* client build, with
  REA/Ghidra on the VM). Until then O2 runs on `ws2_32` `send`/`recv` import hooks, which
  is sufficient for the OG channel but not for hooking inside the packet parser.
* **Any client guard can be defeated by a determined attacker** (kernel drivers, emulated
  environments). The aim is: automated, public bots (WPE/RPE/OpenKore/macros) stop
  working and manual cheating becomes expensive and traceable.
* **Packed exe:** `.text` integrity must be verified after the packer has run; the
  build-specific whitelist handles this, but every exe change needs a new manifest.
* Anti-cheat code can trigger antivirus false positives: sign releases (Authenticode) when
  we get a certificate.
* Privacy: hardware components are stored hashed + salted; mention it in the rules/privacy
  text on the website.
