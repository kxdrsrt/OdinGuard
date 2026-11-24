# 05 – Tests und Testmatrix (OdinGuard Anti Cheat 1.0)

Stand 2026-11-24. Ergebnis-Spalte: **grün** = automatisch getestet und bestanden (mit Beleg), **v0.3** = in v0.3
getestet, Code unverändert übernommen, **Abnahme** = nur im echten Client/auf echter Hardware prüfbar (Abschnitt 6),
**offen** = noch kein Test. Ein Command, eine Challenge oder Detection gilt erst als fertig, wenn ihr Test grün ist
und die Doku generiert wurde (`og docs`).

Umgebung: macOS (Cross-Compile mingw-w64), Docker (MariaDB + xAthena login/char/map), Windows-VM „Tiny 11 Pro“
(ARM64, x86-Emulation, Parallels), Host 10.211.55.2.

## 1. Automatische Suiten

| Suite | Befehl | Umfang | Ergebnis |
|---|---|---|---|
| Rust Unit-Tests | `cd studio && cargo test --workspace` | 22 Tests (Liste unten) | grün, 22/22 (auch mit `rgrf-core` aus GitHub in frischem Target) |
| Rust Lints | `cargo clippy --workspace --all-targets` (+ `apps/studio/src-tauri`) | alle Lints deny | grün, 0 Warnungen |
| Parser-Fuzzing | `make -C client fuzz` | Handshake, Records, Policy, Challenge, Sigs, Colors; ASan + UBSan | grün, 2 000 000 Iterationen |
| Detection-Test | `dettest.exe <sigpack> positive <leerer Ordner>` (VM) | Prozess, Modul, Fensterklasse, Fenstertitel, Dateiname, Datei-Hash, GRF-Hash, Mutex, Registry (vorhanden/fehlend), Treiber (aktiver Systemtreiber), Einmal-Meldung, Allowlist | grün, 15/15 (Paket mit 300 Signaturen) |
| Fehlalarm-Test | `dettest.exe <sigpack> clean <client-dev/ragnarok>` (VM) | 17 Client-DLLs gemappt (ROExt `ijl15r.dll`, dxwrapper, ddraw, …) + Ordner-, Fenster-, Prozess-, Treiber-, Mutex-, Registry-Scan | grün, 0 Signaturtreffer (300er-Paket) |
| E2E | `sh tests/e2e/run.sh <xathena> dist/e2e dist/minimal` | 42 Prüfungen (Abschnitt 3) | grün, 42/42 (Lauf 2026-11-24) |
| Reproduzierbarkeit | `og build` zweimal je Profil | DLL-, GRF-, Manifest-Hashes | grün (default, minimal, Betreiber-Profil) |
| Release-Signatur | `og verify-release <ordner>` | Manifest-Signatur + jede Datei | grün (default, minimal, e2e, cptest, Betreiber) |
| Studio-Release | `tools/release_studio.sh beta https://…` + `og update-verify` | Build, Signatur Artefakt + Manifest | grün; Manipulation von Manifest bzw. Artefakt abgelehnt |
| GUI-Smoke | Tauri-App startet; UI im Browser-Mock mit echten Daten (Betreiber-Profil): Profil, Module, Commands, Signaturen (22 Gruppen), GRF & Mods, Competitive-Pack (12 Ordner) | sichtbar und bedienbar | grün |
| CFG-Build | `make TOOLCHAIN=llvm` / `OG_TOOLCHAIN=llvm og build` | GuardFlags (REA), Laufzeit in der VM: Handshake, `!ping`, `!crash`, Selbstintegrität gegen die llvm-Referenz | grün: `guard_cf` + `cfg_instrumented` gesetzt, keine Ereignisse |
| rAthena-Upstream | `server/patches/rathena-4cd08b7.patch` | `git apply --check`, Make- und CMake-Build, Start aller Server mit `og_enable`, Tabellen angelegt | grün (Docker) |

Rust-Tests: `registry::repository_registry_is_consistent`, `registry::closure_pulls_in_dependencies`,
`profile::new_profile_validates_and_round_trips`, `profile::validation_catches_errors`,
`profile::schema1_profile_migrates_without_data_loss`, `generate::minimal_profile_selects_only_core_sources`,
`generate::secrets_are_masked_in_the_header`, `generate::server_conf_follows_rollout_stage_and_commands`,
`manifest::signed_release_detects_changes`, `update::update_n_to_n1_tamper_downgrade_and_rollback`,
`update::only_https_outside_tests`, `update::release_folder_roundtrip_and_tamper`, `update::versions`,
`rollout::gate_blocks_on_false_alarm_candidates_and_coverage`, `crypto::sign_verify_and_tamper`,
`grf::tail_hash_covers_header_and_table_only`, `grf::scan_and_merge_keep_rollout_versions`,
`pack::repository_recipe_orders_flat_floor_after_flat_maps`, `pipeline::tests::linker_maps_of_gnu_ld_and_lld`,
`sigpack::build_verify_cap_and_skip`, `sigpack::tampered_or_foreign_package_is_rejected`,
`sigpack::repository_signatures_compile`.

## 2. Build und Regression

| Prüfung | Soll | Ergebnis |
|---|---|---|
| Minimal-Profil enthält nur Kern-Code | Linker-Map: nur og_main/og_log/og_net/og_sha256/og_challenge/og_wire; keine Strings anderer Module | grün (271 360 Byte; Pipeline bricht sonst ab) |
| Default-Profil = Verhalten v0.3 | gleiche Module/Defaults wie v0.3; neue Module opt-in; E2E Abschnitt 1 grün | grün (Ausnahme: LGP-Standard-Units 0xc8/0xc9 statt 0x9b, siehe Abnahme A6) |
| Server kompiliert | xAthena `make server` (Docker, `-j3`) | grün |
| Kein alter Servername im Repo | `grep -ri` nach dem bisherigen Betreibernamen: nur die Herkunftsdoku `07-darkworld-lgp.md` (Ordnername einer Quelle) | grün |

## 3. E2E-Prüfungen (Server in Docker, Guard in der VM über `nettest.exe`)

| # | Prüfung | Ergebnis |
|---|---|---|
| 1 | Login und Map-Eintritt mit dem Profil-Guard | grün |
| 2 | `!ping` vom Guard beantwortet | grün |
| 3 | `@aura` listet Katalog mit Freischalt-Bedingungen (Aura-Feature von xAthena, nicht mehr OdinGuard) | grün |
| 4 | `@square 2` wählt Farbset 2 | grün |
| 5 | `@aoes` begrenzt LGP auf SG/LoV/MS | grün |
| 6 | `@oginfo` zeigt Protokoll v2 und Signaturversion | grün |
| 7 | Guard-Log: Handshake und Signaturen empfangen | grün |
| 8 | ehrlicher Client: kein guard-code/exe-code/challenge-Fehlschlag | grün |
| 9 | Challenges werden gesendet (Map-Log) | grün |
| 10–16 | Auren: gesperrt → Fehlermeldung; GM-Freischaltung; doppelt → Hinweis; unbekannt → abgelehnt; nach Relog tragbar; einmal gespeichert (Account-Scope); Spieler dürfen `@auraunlock` nicht | grün |
| 17 | Honeypot-Treffer → Flag (flagban) | grün |
| 18 | Skill-Anfragen innerhalb des Delays → Flag `bh:nodelay` | grün |
| 19 | Paket-Flut → `rate-limit` (Warnmodus = Log) | grün |
| 20 | `@ogbanwave dry` listet den Honeypot-Account | grün |
| 21 | Warnmodus bannt nie | grün |
| 22–24 | Ban-Welle bannt den geflaggten Rechner, Flags geschlossen, gebannter Rechner → Login-Code 4 | grün |
| 25 | manipuliertes Signaturpaket wird beim Start abgelehnt | grün |
| 26 | falsches Build-Secret → nonce/time-Challenges schlagen fehl | grün |
| 27 | veränderte `odin.grf` → file-Challenge schlägt fehl | grün |
| 28 | optionales Competitive-Pack fehlt beim Client → kein Fehlschlag | grün |
| 29 | manipuliertes Competitive-Pack → file-Challenge schlägt fehl (GRF-Allowlist) | grün |
| 30 | Client eines fremden Profils → abgewiesen mit Meldung (Status 3) | grün |
| 31 | `@whosell` ohne Parameter antwortet im Chat (Marktübersicht) | grün |
| 32 | ehrlicher Client: keine GRF-Meldung | grün |
| 33 | GRF weder gelistet noch erlaubter Mod → `grf:unknown` | grün |
| 34 | erlaubter Mod (anderer Name, gleicher Tabellen-Hash) wird akzeptiert | grün |
| 35 | geänderte Dateitabelle einer gelisteten GRF → `grf:modified` | grün |
| 36 | gleiche Tabelle, geänderter Inhalt → Hintergrund-Hash `grf:modified (content)` (auch auf der Map-Verbindung) | grün |
| 37 | Pflicht-GRF fehlt in DATA.INI → `grf:missing` | grün |
| 38 | Speedhack-Stub (GetTickCount/QPC ×3): API-Hook gemeldet (`api-hook`) | grün |
| 39 | Speedhack-Stub: Zeit-Challenge schlägt fehl (`client clock +200%` nach > 120 s) | grün |
| 40 | Guard-Code im Speicher gepatcht → `guard-code` | grün |
| 41 | unveränderte Exe passt zum erlaubten Exe-Hash; Exe-Code im Speicher gepatcht → `exe-code` | grün |

Replay-Schutz: Records mit falschem Zähler und Antworten auf unbekannte Challenge-IDs werden verworfen
(`challenge:replay`); abgedeckt durch Fuzzing (Parser) und Server-Logik, ein eigener E2E-Fall mit
wiedergespieltem Record fehlt noch (**offen**, siehe 7).

## 4. Commands

Fehlerfälle: R = Rechte (Gruppe), P = Parameter, L = Ratenlimit, M = Modul abgewählt (Command nicht registriert),
V = Verbindungsabbruch (Command ohne Wirkung, kein Absturz).

| Command | Soll | R | P | L | M | Ergebnis |
|---|---|---|---|---|---|---|
| `!ping` | Latenz im Chat | – | – | Konfig | Unit (Konfig) | grün (E2E 2) |
| `!vsync [on\|off]` | Bildratenlimit aus/an | – | v0.3 | Konfig | Unit | v0.3 + Abnahme A3 |
| `!crash` | Crash-Bericht ohne Absturz | – | – | Konfig | Unit | v0.3 + Abnahme A3 |
| `@lgp [on\|off]` | LGP an/aus, gespeichert | – | E2E | Konfig | Unit | grün (E2E 1) + Abnahme A6 |
| `@square [on\|off\|1-14]` | Farbset | – | E2E | Konfig | Unit | grün (E2E 4) + Abnahme A6 |
| `@circle [on\|off]` | runde Kacheln | – | – | Konfig | Unit | Abnahme A6 |
| `@aoes [on\|off]` | nur SG/LoV/MS | – | E2E | Konfig | Unit | grün (E2E 5) |
| `@showauras` | Auren anderer aus/an | – | – | Konfig | Unit | v0.3 (E2E 1 ruft ihn auf, ohne eigene Prüfung) |
| `@aura [name\|off]` | nur freigeschaltete | E2E | E2E | Konfig | Unit | grün (E2E 3, 10–15) |
| `@auraunlock <aura>, <char>` | GM-Freischaltung | E2E | E2E | – | Unit | grün (E2E 11–16) |
| `@namecolor` | Namensfarbe | v0.3 | v0.3 | – | Unit | v0.3 |
| `@ogban` / `@ogunban` | Rechner-Sperre setzen/aufheben | v0.3 | v0.3 | – | Unit | v0.3 |
| `@oginfo` | Version/Build/Rechner/Exe-Hash | – | – | – | Unit | grün (E2E 6) |
| `@ogflags` | offene Flags | – | – | – | Unit | E2E 1 ruft ihn auf, ohne eigene Prüfung (**offen**) |
| `@ogbanwave [dry]` | Ban-Welle | Gruppe 99 | E2E | – | Unit | grün (E2E 20–24) |

„Unit“ in Spalte M: `generate::server_conf_follows_rollout_stage_and_commands` (abgewählte Commands → `og_cmd_<id>: no`).
Ratenlimit pro Command (`og_cmdrate_*`) ist im Server implementiert, aber nicht eigens E2E-getestet (**offen**).

## 5. Challenges und Signaturklassen

| Element | Soll | Fehlerfälle | Ergebnis |
|---|---|---|---|
| ch-nonce | gültiger Digest mit Build-Secret | falsches Secret → Fehlschlag; Replay → `challenge:replay`; Timeout | grün (E2E 8, 26) |
| ch-code | Bereich des kanonischen Images stimmt | gepatchter Code → Fehlschlag | grün (ehrlicher Client E2E 8; Patch-Fall über Heartbeat-Hash E2E 40/41) |
| ch-time | Uhr-Drift ≤ `max_drift_percent` | Speedhack | grün (E2E 8, 26, 39) |
| ch-file | Bereich von `odin.grf`/Pack stimmt | Pflichtdatei fehlt/anders → Fehlschlag; optional fehlt → ok | grün (E2E 27–29) |
| ch-honeypot | nur Bots treffen den Lockvogel | Treffer → flagban | grün (E2E 17) |
| ch-captcha | Captcha nach Verhaltens-Flags, nie WoE/BG | falsche Antwort/Timeout | Server-Logik gebaut, Spiel-UI: Abnahme A9 |
| behaviour nodelay | Skill innerhalb Delay → Flag | – | grün (E2E 18) |
| rate-limit | > pps → Aktion | Warnmodus = Log | grün (E2E 19) |
| Signaturen process/module/window_class/window_title/file_name | Treffer gemeldet, einmal pro Verbindung | Allowlist unterdrückt | grün (`dettest`) |
| file_sha256 / grf_sha256 | Hash-Treffer | Allowlist-Hash unterdrückt | grün (`dettest`, synthetische Fixtures) |
| driver | Treiber/Dienst-Name | – | grün (`dettest`: aktiver Systemtreiber als synthetische Signatur) |
| mutex / registry | benannte Objekte, Registry-Schlüssel | fehlender Schlüssel → kein Treffer | grün (`dettest`) |
| GRF-Manifest | Tabellen-Hash, Voll-Hash, Mods, Pflicht | Patch-Rollout mit mehreren Versionen | grün (E2E 32–37, Unit `grf::*`) |
| grf_entry / import_pattern / string_pattern | nur Analyse, nicht client-prüfbar | – | dokumentiert (Paket überspringt sie, `sigpack::build_verify_cap_and_skip`) |
| Fehlalarme legitimer Client | ROExt, dxwrapper, Skins ohne Treffer | – | grün (`dettest clean`) |
| Signaturpaket | nur mit gültiger Signatur + Mindestversion | manipuliert/fremd → abgelehnt | grün (Unit + E2E 25) |

## 6. Live-Abnahme-Checkliste (echter Client, nach G1)

| # | Prüfung | Erwartung |
|---|---|---|
| A1 | Dev-Client mit 1.0-Release (Betreiber-Profil) starten | Splash mit dem Betreibernamen aus dem Profil, Login HR und M-LR, kein Hänger, keine AV-Meldung |
| A2 | Warnmodus: `og_event` nach 1 Spielstunde | keine Treffer für ROExt/dxwrapper/Skins; nur erwartete Einträge |
| A3 | `!ping`, `!vsync off/on`, `!crash` | Antworten im Chat, Crash-Ordner geschrieben |
| A4 | Farb-Nick (GM gold), Farb-Item | wie v0.3 |
| A5 | (Aura-Feature, nicht OdinGuard) Aura Keeper (Prontera 160,192): Quest abschließen | Questlog-Eintrag sichtbar (braucht Lub-Text), Aura frei, `@aura <name>` zeigt sie anderen; `@showauras` blendet aus |
| A6 | `@lgp`, `@square 2`, `@circle`, `@aoes` mit Storm Gust, LoV, Meteor Storm | flache blaue/orange Zellen pro Zelle, Charaktere sichtbar; Vergleich mit darkworld-Boden (Kontrast) |
| A7 | Competitive-Pack (Standard: Flachmaps WoE/BG/PvP, dunkle Böden, Bäume, Pneuma) | Ladereihenfolge `0=odin.grf`, `1=odin-dark.grf`; flache WoE-/BG-Maps mit dunklen Kacheln, Pneuma-Countdown; FPS/Ladezeit notieren; keine `grf:*`-Meldung |
| A7b | GRF-Prüfung mit echtem Client | Dev-Client ohne Änderung: keine Meldung; darkworld `dark.grf` zusätzlich: keine Meldung (erlaubter Mod); geänderte data.grf: `grf:modified` |
| A8 | Challenges einschalten (Stufe 1, je eins): nonce, code, time, file | keine Fehlschläge für ehrliche Spieler über 24 h |
| A9 | Verhalten + Captcha auf einem Testaccount mit Makro-Muster | Captcha erscheint, nicht auf WoE/BG-Maps |
| A10 | Input-Filter, RCX, Warsaw, VM-Block auf VMware/VirtualBox/Hyper-V | echte Rechner: Verhalten wie konfiguriert, keine Abstürze |
| A11 | Rollout-Gate: `og rollout` mit Export aus Stufe 1 | Fehlalarm-Kandidaten geprüft, dann Stufe 2 |
| A12 | Studio auf Windows/Linux bauen (`release_studio.sh`) | signiertes Artefakt, `og update-verify` ok |

## 7. Offene Testlücken (ehrlich)

- Wiedergespielter Record als eigener E2E-Fall; Ratenlimit pro Command.
- Echte Cheat-Programme wurden nicht ausgeführt (nur statische Analyse und harmlose Stubs); Treffer echter Tools
  bestätigt erst die Abnahme auf einem Testrechner.
- Input-Filter: Parallels verwirft synthetische Eingaben schon vor dem Gast (v0.3-Befund), daher nur auf echter
  Hardware.
- Questlog-Texte, Captcha-Fenster, LGP-Look: nur im echten Client.
- CFG nur mit llvm-mingw (`TOOLCHAIN=llvm`); der Standard-Build (mingw-gcc) hat es nicht. Visual-Studio-Builds des
  Server-Patches sind ungetestet.

## 8. Historie

Die Ergebnisse von v0.1–v0.3 (Handshake, Hardware-Bans, Rechner-Limit, Alive-Timeout, Farb-Nicks, Auren-IDs,
Release-Paket) gelten weiter für den unveränderten Code; Details im Git-Verlauf dieser Datei (Commit `4ad6498`).
