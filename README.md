# OdinGuard Anti Cheat

Open-source, modular anti-cheat for Ragnarok Online servers running **rAthena** (or a fork). Server owners pick
the protection modules, challenges, commands and signatures in **OdinGuard Studio** and get a signed,
reproducible release for client and server – their own keys, their own build.

Deutsch: [README.de.md](README.de.md)

## Why

Ragnarok Online servers fight packet editors, bots, macro and autopot tools, memory readers, no-delay GRFs and
modified clients. Closed anti-cheats are hard to audit, cause start-up hangs and false "illegal files" alarms, and
tie every server to one vendor. OdinGuard is:

- **Server-authoritative.** The server decides. Client checks raise the cost of cheating and produce evidence; the
  real security comes from server-side rules (rate limits, delay checks, honeypots, captchas, mandatory handshake).
- **Open code, secret keys.** Security comes from per-server keys and per-server build variation, not from hiding
  the code.
- **Modular.** Unselected modules are not compiled into the client DLL (checked against the linker map).
- **Careful with players.** Every rule has severity, confidence and an action (log, flag, flag-then-ban, kick,
  ban); bans come in delayed waves; rollout starts in warn mode; legitimate add-ons are allowlisted.

## Features

| Area | |
|---|---|
| Network | X25519 handshake with a pinned server key, XChaCha20-Poly1305 for the whole game stream, replay protection, key rotation, per-server profile tag, downgrade protection, fuzzed parsers |
| Integrity | exe and guard code hashes in every heartbeat, canonical code image, anti-debug, hardware breakpoints, API-hook checks (speed hacks), GRF check with patch-rollout support |
| Detection | signed, updatable signature package (300 signatures: processes, modules, windows, files, drivers, mutexes, registry, GRF hashes), allowlist, per-group actions |
| Server rules | packet rate limit (WPE/RPE loops), no-delay detection, autopot/macro timing, honeypot unit, captcha, multi-client limit, hardware bans (exact and fuzzy), flags and ban waves |
| Challenges | nonce (build secret), random code ranges, clock rate, random file ranges, captcha, honeypot |
| Features | `!ping`, `!vsync`, `!crash` (crash reports with the last packets), colour names and items, lite ground skills (`@lgp`, flat cell tiles), optional competitive pack (same darkworld-style visuals for everyone) |
| Tooling | Studio GUI (Tauri 2) and `og` CLI: profiles, validation, reproducible signed releases, signature packages, GRF scan, rollout gate, signed self-update |

## Quick start

```sh
brew install mingw-w64            # or: apt install mingw-w64
cd studio && cargo build --release && cd ..
studio/target/release/og new myserver.profile.toml --id myserver --operator "My Ragnarok"
studio/target/release/og build myserver.profile.toml
```

Then follow **[docs/INTEGRATION.md](docs/INTEGRATION.md)** (server hooks, client loader, GRF check with your patcher,
staged rollout).

## Repository

| Path | |
|---|---|
| `client/` | guard DLL and loader (C11, 32-bit, mingw-w64 or llvm-mingw with CFG), tests (`selftest`, `nettest`, `dettest`, fuzzer) |
| `server/` | rAthena module and hook list |
| `proto/` | wire protocol shared by client and server |
| `studio/` | Rust workspace: `og-core`, `og` CLI, Tauri app |
| `modules/` | module and command registry (single source for Studio, builds and docs) |
| `signatures/` | cheat signatures and allowlist (static analysis results only, no cheat binaries) |
| `assets/` | own assets for `odin.grf` (LGP tiles) and the competitive-pack recipe |
| `profiles/` | example profiles: `default`, `minimal`, `e2e` |
| `tests/e2e/` | end-to-end suite (rAthena in Docker + Windows VM) |
| `docs/` | integration guide (English), architecture, tests, analyses (German) |

## Status

1.0.0. Tested end to end with a 2018-06-20 client and an rAthena fork (PACKETVER 20180620): see
[docs/05-tests.md](docs/05-tests.md). Not yet tested on real hardware: input filter, RCX, Warsaw (Brazilian
banking module), VM detection on VMware/VirtualBox/Hyper-V.

## Honest limits

Anyone who controls the machine (kernel drivers, VM introspection, a reimplemented client) can defeat any
client-side check. OdinGuard aims to stop public tools and bots and to make manual cheating expensive and
traceable; the server-side rules work regardless of the client. See the "hurdle vs. real security" table in
[docs/02-architecture.md](docs/02-architecture.md).

## Licence

MIT OR Apache-2.0 (your choice): [LICENSE-MIT](LICENSE-MIT), [LICENSE-APACHE](LICENSE-APACHE). Third-party: Monocypher (CC0/BSD-2), MinHook (BSD-2), fonts (OFL).
Security reports: [SECURITY.md](SECURITY.md).
