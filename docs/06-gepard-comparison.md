# 06 – OdinGuard compared with Gepard Shield 3.0

> **Stand 1.0.0 (2026-11-23):** Die Matrix unten stammt aus v0.3 und gilt weiter. Neu in 1.0 (Details
> `02-architecture.md`, Tests `05-tests.md`): Protokoll v2 mit Schlüsselrotation und Profil-Tag pro Betreiber,
> Challenges (nonce, code, time, file, honeypot, captcha), signiertes Signaturpaket mit 170 Signaturen und
> Allowlist, serverseitige Verhaltensregeln (nodelay, Rate-Limit), Flags und Ban-Wellen, Aura-Freischaltung per
> Quest, Studio + CLI mit Modulauswahl zur Build-Zeit, signierte Releases und Updater. Gepard bietet davon
> weder Modulauswahl durch den Betreiber noch offene, signierte Signaturpakete [gelesen: öffentliche Featureliste].
> Datei-Integrität läuft jetzt über `ch-file`/`og verify-release` statt `tools/make_manifest.py` (entfernt).

State: OdinGuard v0.3.0 (2026-11-23). "Tested" means tested end to end with our 2018-06-20 client and
xAthena (`05-tests.md`). Gepard facts are from its public feature list; its code was not used.

## Feature matrix

| Gepard 3.0 feature | OdinGuard | Status |
|---|---|---|
| Exe code integrity in memory | SHA-256 of the unpacked code section, sent in every heartbeat; server compares with `og_allowed_exe` | tested; **better**: checked continuously, not only at start |
| File integrity exe + DLLs | file hashes in the report, `tools/make_manifest.py` | built; server list optional |
| DLL injection protection | module scan + report of unknown modules, action per server rule | tested (report) |
| Dynamic packet encryption | X25519 per connection, server key pinned (Ed25519), XChaCha20-Poly1305 for the **whole** game stream | tested; **better**: authenticated encryption, replayed or changed bytes are rejected |
| Packet editors / sniffers / bots (WPE, RPE, OpenKore) | encrypted stream + signed heartbeat; tools from the signature list kick or ban | tested (OpenKore signature, plain clients refused when `og_required`) |
| Malformed chat packets | server strips colour codes and checks commands; xAthena validates lengths | server side |
| Unique ID, ban by ID | 13 hardware components (MachineGuid, SMBIOS, disks, volume, CPU, MAC, ...), salted; exact **and fuzzy** ban (k of n) | tested; **better**: a changed MAC or disk does not lift the ban |
| Max windows per machine | client semaphore + server limit per machine (`og_max_per_machine`) | tested; **better**: also enforced by the server |
| Block VMs | CPUID + firmware strings, policy from the server | built; Parallels detected, other hypervisors untested |
| Known cheat tools (PotND, meth4u, xRag, xLike, RoTools, ...) | process / window / module signatures **delivered by the server** | tested; **better**: new tools without a client update |
| 2018 client, 2010+ clients | loader proxy without exe patch, exe hooks with byte signatures (inactive on other builds) | tested with 2018-06-20 |
| RCX final | loads after RCX through the same proxy chain, RCX modules tolerated | **untested** (needs a machine with RCX) |
| Warsaw (Brazil) | hooks only ws2_32 / gdi32 / exe code, tolerates foreign modules | **untested** (needs a Warsaw machine) |
| Crash log (stack, GRFs, resources, last packets) | registers, stack walk, stack dump, modules, GRF list, last 32 packets | tested |
| `!vsync`, `!crash`, `!ping` | same commands | tested |
| Automatic SQL tables + config | `CREATE TABLE IF NOT EXISTS`, config with defaults | tested |
| Synthetic input block | low-level hooks reject injected mouse/keyboard events | built, **untested** (Parallels drops synthetic input) |
| Nodelay / animation delay | server-side `min_skill_delay_limit` + exe code hash | tested (hash) |
| GRF override protection | resource rules (loose files, GRF order), action per server rule | tested (report) |
| LGP | server-side: flat ground units per viewer (`@lgp`) | tested; **better**: no client files, no BMP errors, works for every client with OdinGuard |
| Custom auras | hat effects, allow list `og_aura`, `@aura` | tested; resource check tool |
| Colour nicks | server colour list, drawn by the exe text hook | tested |
| Colour items | server colour list per item id | tested |

## Only in OdinGuard

- `@showauras` (hide auras of others per player) and `@lgp` as player commands.
- Alive timeout: a client that stops sending heartbeats is logged or kicked.
- Machine bans with fuzzy matching and `@oginfo` (version, build, machine, exe hash per character).
- Everything configured on the server (signatures, policy, colours, auras, LGP): no client update needed.
- Own key per deployment, open protocol in `proto/og_proto.h`, reproducible builds (same input, same
  DLL hash).
- `tools/check_auras.py`: checks which auras a client can draw before they are allowed.

## Still open

- Tests on real machines: input filter, RCX, Warsaw, VM block on VMware/VirtualBox/Hyper-V.
- Exe import patch (guard loaded by the exe itself instead of the `ijl15.dll` proxy) and Authenticode
  signing of the DLLs (reduces antivirus false positives).
- Live rollout steps: `og_log_only: no`, then `og_required: yes` (see `server/README.md`).
- Website list of the player commands.

## Language: C, and why not Rust (yet)

The client is C11 (mingw-w64, 32-bit), the server module C++ inside xAthena. Crypto: Monocypher, hooks:
MinHook.

Rust would work (`i686-pc-windows-gnu` or `-msvc`): memory safety for the parsers (handshake, records,
colour lists), `cargo` for dependencies. But:

- **Speed is not the reason.** The guard does little per frame: a hash table lookup per drawn text and an
  AEAD record per network packet. Both cost microseconds in C; Rust would be the same speed.
- **The risky part stays `unsafe`.** Hooks on exe addresses, `thiscall` trampolines, Win32 callbacks and
  raw packets are FFI either way.
- **Size and build.** A 32-bit Rust DLL with std is larger, and the toolchain for the 2018 client
  (i686, no UCRT surprises) needs extra care.
- **Working, tested code.** v0.3 is tested end to end; a rewrite would mean the same tests again.

Recommendation: stay in C for the client DLL and keep the parsers small (fuzz test: done in 1.0, `make -C client fuzz`).
Decision record for 1.0: `02-architecture.md` §12 (client C, server C++, Studio and tools Rust). Rust is a
good choice for new standalone tools (patcher, manifest/asset tools; RustyGRF already is Rust).
