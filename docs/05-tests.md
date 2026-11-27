# 05 – Tests und Testmatrix (OdinGuard Anti Cheat 1.0)

Stand 2026-11-26. Ergebnis-Spalte: **grün** = automatisch getestet und bestanden (mit Beleg), **v0.3** = in v0.3
getestet, Code unverändert übernommen, **Abnahme** = nur im echten Client/auf echter Hardware prüfbar (Abschnitt 6),
**offen** = noch kein Test. Ein Command, eine Challenge oder Detection gilt erst als fertig, wenn ihr Test grün ist
und die Doku generiert wurde (`og docs`).

Umgebung: macOS (Cross-Compile mingw-w64), Docker (MariaDB + xAthena login/char/map), Windows-VM „Tiny 11 Pro“
(ARM64, x86-Emulation, Parallels), Host 10.211.55.2.

## 1. Automatische Suiten

| Suite | Befehl | Umfang | Ergebnis |
|---|---|---|---|
| Rust Unit-Tests | `cd studio && cargo test --workspace` | 24 Tests (Liste unten) | grün, 24/24 (`rgrf-core` liegt als Kopie in `studio/vendor/rgrf-core`, kein Netz und kein RustyGRF nötig) |
| Rust Lints | `cargo clippy --workspace --all-targets` (+ `apps/studio/src-tauri`) | alle Lints deny | grün, 0 Warnungen |
| Parser-Fuzzing | `make -C client fuzz` | Handshake, Records, Policy, Challenge, Sigs, Colors; ASan + UBSan | grün, 2 000 000 Iterationen |
| Detection-Test | `dettest.exe <sigpack> positive <leerer Ordner>` (VM) | Prozess, Modul, Fensterklasse, Fenstertitel, Dateiname, Datei-Hash, GRF-Hash, Mutex, Registry (vorhanden/fehlend), Treiber (aktiver Systemtreiber), Einmal-Meldung, Allowlist | grün, 15/15 (Paket mit 300 Signaturen) |
| Fehlalarm-Test | `dettest.exe <sigpack> clean <client-dev/ragnarok>` (VM) | 17 Client-DLLs gemappt (ROExt `ijl15r.dll`, dxwrapper, ddraw, …) + Ordner-, Fenster-, Prozess-, Treiber-, Mutex-, Registry-Scan | grün, 0 Signaturtreffer (300er-Paket) |
| E2E | `sh tests/e2e/run.sh <xathena> dist/e2e dist/minimal` | 97 Prüfungen (Abschnitt 3) | grün, 97/97 (Lauf 2026-11-26, 1.0.5; davon 7 neue in 5c) |
| Reproduzierbarkeit | `og build` zweimal je Profil | DLL-, GRF-, Manifest-Hashes | grün (default, minimal, Betreiber-Profil) |
| Release-Signatur | `og verify-release <ordner>` | Manifest-Signatur + jede Datei | grün (default, minimal, e2e, cptest, Betreiber) |
| Studio-Release | `tools/release_studio.sh beta https://…` + `og update-verify` | Build, Signatur Artefakt + Manifest | grün; Manipulation von Manifest bzw. Artefakt abgelehnt |
| GUI-Smoke | Tauri-App startet; UI im Browser-Mock mit echten Daten (Betreiber-Profil): Profil, Module, Commands, Signaturen (22 Gruppen), GRF & Mods, Competitive-Pack (12 Ordner) | sichtbar und bedienbar | grün |
| CFG-Build | `make TOOLCHAIN=llvm` / `OG_TOOLCHAIN=llvm og build` | GuardFlags (REA), Laufzeit in der VM: Handshake, `!ping`, `!crash`, Selbstintegrität gegen die llvm-Referenz | grün: `guard_cf` + `cfg_instrumented` gesetzt, keine Ereignisse |
| rAthena-Upstream | `server/patches/rathena-4cd08b7.patch` | `git apply --check`, Make- und CMake-Build, Start aller Server mit `og_enable`, Tabellen angelegt | grün (Docker) |
| rAthena-Upstream in der CI | Jobs `server-patch-gcc` (Ubuntu, `./configure --enable-prere`) und `server-patch-msvc` (Windows, MSBuild Debug, `PRERE`, `/warnaserror` wie die Upstream-CI) auf sauberem rAthena `4cd08b7` | Patch anwenden, alle Server bauen | grün (2026-11-26). Der erste MSVC-Lauf fand einen echten Fehler: C4838 (verengende Umwandlung `int32` → `uint32` in der Initialisierungsliste des LGP-Hooks in `clif.cpp`), GCC hatte nur gewarnt; behoben |

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
| Default-Profil = Verhalten v0.3 | gleiche Module/Defaults wie v0.3; neue Module opt-in; E2E Abschnitt 1 grün | grün (Ausnahme: LGP jetzt mit Farbpalette, `07-darkworld-lgp.md` §1.0) |
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
| 17 | Honeypot-Treffer im Warnmodus → Flag ohne Bann-Markierung; außerhalb des Warnmodus → `flagban` | grün |
| 18 | Skill-Anfragen innerhalb des Delays → Flag `bh:nodelay` | grün |
| 19 | Paket-Flut → `rate-limit` (Warnmodus = Log) | grün |
| 20 | Warnmodus-Flags zählen nicht für die Ban-Welle (`0 account(s) would be banned`); `@ogbanwave dry` listet den Honeypot-Account nach dem Umschalten | grün |
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
| 42–44 | `@ogflagclear <account id>` schließt offene Flags, `@ogflagclear <id> <regel>` nur diese Regel, ohne Namen → Usage | grün |
| 45–47 | **wiedergespielter Record:** `tests/e2e/replay_proxy.py` zwischen VM und Map-Server schickt einen mitgeschnittenen verschlüsselten Client-Record noch einmal → `record-mac` (Zähler/Tag passt nicht), außerhalb des Warnmodus `kick` | grün |
| 48–52 | **Ratenlimit pro Command:** `og_cmdrate_ping: 10000` → zweites `!ping` abgewiesen; `og_cmd_vsync: no` → abgeschaltet; `og_cmdgroup_crash: 10` → Spieler abgewiesen; GMs nicht begrenzt | grün |
| 53–58 | **Account-Lock:** `@oglock <char>, add` trägt den Rechner ein; Login vom eigenen Rechner ok; anderer Rechner → Login-Code 3 + Ereignis `account-lock`; ohne Einträge wieder frei | grün |
| 59–61 | **`@pf`** auf `prtg_cas01` (GM holt den Spieler per `@recall`, `nettest` bestätigt den Kartenwechsel): `@pf c` → 0 Chat-Pakete des anderen Spielers, `@pf off` → 2; Statusmeldung | grün |
| 62–63 | **`@spb`:** ohne Party → Hinweis; mit Party und Blessing → `OgTester: Bless <n>s` | grün |
| 64–68 | **Crash-Upload:** Bericht mit Zustimmung (`odinguard.ini`) per POST an die Panel-URL, Token steht in `og_crash_token` für den Account, `sent.txt`, kein zweiter Upload, abgelehnt → nichts gesendet | grün |
| 69–74 | **Rollout eines neuen Release** (`og_guard_previous_file`, `og_protected_previous`): beide werden beim Start geladen; Client mit dem vorigen Guard → kein `guard-code`, kein Code-Challenge-Fehlschlag; mit der vorigen `odin.grf` → keine File-Challenge-Fehlschläge; ohne die Einträge → `guard-code` bzw. File-Challenge schlägt fehl | grün |
| 75 | zwei erlaubte Versionen einer GRF mit gleicher Größe und Dateitabelle (nur Inhalt anders): die Version des Clients steht nicht als letzte in der Liste und wird trotzdem akzeptiert | grün |
| 76–81 | **Mindest-Release** (`og_min_release: 9.9.9`): Guard wird schon beim Handshake abgewiesen (Status 1), Meldung nennt die eigene und die nötige Version, Login-Server protokolliert es, kein `og_event`; mit der normalen Konfig wieder Login; Map-Server meldet `min release` beim Start | grün |

Replay-Schutz: Records mit falschem Zähler werden verworfen (`record-mac`, E2E 45–47), Antworten auf unbekannte
Challenge-IDs ebenfalls (`challenge:replay`, Server-Logik und Fuzzing).

## 4. Commands

Fehlerfälle: R = Rechte (Gruppe), P = Parameter, L = Ratenlimit, M = Modul abgewählt (Command nicht registriert),
V = Verbindungsabbruch (Command ohne Wirkung, kein Absturz).

| Command | Soll | R | P | L | M | Ergebnis |
|---|---|---|---|---|---|---|
| `!ping` | Latenz im Chat | – | – | Konfig | Unit (Konfig) | grün (E2E 2) |
| `!vsync [on\|off]` | Bildratenlimit aus/an | – | v0.3 | Konfig | Unit | v0.3 + Abnahme A3 |
| `!crash` | Crash-Bericht ohne Absturz | – | – | Konfig | Unit | v0.3 + Abnahme A3 |
| `@lgp [on\|off]` | LGP an/aus, gespeichert | – | E2E | Konfig | Unit | grün (E2E 1) + Abnahme A6 |
| `@square [on\|off\|1-18]` | Farbset (2-18 einfarbig) | – | E2E | Konfig | Unit | grün (E2E 4) + Abnahme A6 |
| `@circle [on\|off]` | runde Kacheln | – | – | Konfig | Unit | Abnahme A6 |
| `@aoes [on\|off]` | nur SG/LoV/MS | – | E2E | Konfig | Unit | grün (E2E 5) |
| `@lgpsound [on\|off]` | Skill-Sound der Bodenskills für LGP-Spieler | – | E2E | Konfig | Unit | Meldung geprüft; Hören nicht prüfbar (VM ohne Ton) |
| `@showauras` | Auren anderer aus/an | – | – | Konfig | Unit | v0.3 (E2E 1 ruft ihn auf, ohne eigene Prüfung) |
| `@aura [name\|off]` | nur freigeschaltete | E2E | E2E | Konfig | Unit | grün (E2E 3, 10–15) |
| `@auraunlock <aura>, <char>` | GM-Freischaltung | E2E | E2E | – | Unit | grün (E2E 11–16) |
| `@namecolor` | Namensfarbe | v0.3 | v0.3 | – | Unit | v0.3 |
| `@ogban` / `@ogunban` | Rechner-Sperre setzen/aufheben | v0.3 | v0.3 | – | Unit | v0.3 |
| `@oginfo` | Version/Build/Rechner/Exe-Hash | – | – | – | Unit | grün (E2E 6) |
| `@ogflags` | offene Flags | – | – | – | Unit | E2E 1 ruft ihn auf, ohne eigene Prüfung (**offen**) |
| `@ogflagclear <char\|id> [regel]` | offene Flags schließen | Gruppe 60 | E2E | – | Unit | grün (E2E 42–44) |
| `@oglock <char>[, add\|remove\|clear\|list]` | Account an Rechner binden | Gruppe 60 | E2E | – | Unit | grün (E2E 53–58) |
| `@pf on\|off\|<c i g>` | Paketfilter WoE/BG | – | E2E | Konfig | Unit | grün (E2E 59–61); Item-Nutzung und Emotes nur im Code, nicht eigens gemessen |
| `@spb` | Party-Buffs im Chat | – | E2E | Konfig | Unit | grün (E2E 62–63) |
| `@ogbanwave [dry]` | Ban-Welle | Gruppe 99 | E2E | – | Unit | grün (E2E 20–24) |

„Unit“ in Spalte M: `generate::server_conf_follows_rollout_stage_and_commands` (abgewählte Commands → `og_cmd_<id>: no`).
Ratenlimit pro Command (`og_cmdrate_*`), Abschalten (`og_cmd_*`) und Gruppe (`og_cmdgroup_*`): grün (E2E 48–52).

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
| A6 | `@lgp`, `@square 2..13`, `@circle`, `@aoes` mit Storm Gust, LoV, Meteor Storm und weiteren Bodenskills | flache Zellen in der Farbe des Skills (SG blau, LoV gelb, MS rot, Pneuma grün …), Charaktere sichtbar; Vergleich mit darkworld-Boden (Kontrast). **Vorab in der VM gemessen (2026-11-24):** alle 12 Farben in Quadrat und Dreieck, kompletter Test-NPC-Durchlauf (76 Skills); siehe `analysis/lgp/lgp-audit.md` §9 |
| A7 | Competitive-Pack (Standard: Flachmaps WoE/BG/PvP, dunkle Böden, Bäume, Pneuma) | Ladereihenfolge `0=odin.grf`, `1=odin-dark.grf`; flache WoE-/BG-Maps mit dunklen Kacheln, Pneuma-Countdown; FPS/Ladezeit notieren; keine `grf:*`-Meldung |
| A7b | GRF-Prüfung mit echtem Client | Dev-Client ohne Änderung: keine Meldung; darkworld `dark.grf` zusätzlich: keine Meldung (erlaubter Mod); geänderte data.grf: `grf:modified` |
| A8 | Challenges einschalten (Stufe 1, je eins): nonce, code, time, file | keine Fehlschläge für ehrliche Spieler über 24 h |
| A9 | Verhalten + Captcha auf einem Testaccount mit Makro-Muster | Captcha erscheint, nicht auf WoE/BG-Maps |
| A10 | Input-Filter, RCX, Warsaw, VM-Block auf VMware/VirtualBox/Hyper-V | echte Rechner: Verhalten wie konfiguriert, keine Abstürze |
| A11 | Rollout-Gate: `og rollout` mit Export aus Stufe 1 | Fehlalarm-Kandidaten geprüft, dann Stufe 2 |
| A12 | Studio auf Windows/Linux bauen (`release_studio.sh`) | signiertes Artefakt, `og update-verify` ok |

## 7. Offene Testlücken (ehrlich)

- Echte Cheat-Programme wurden nicht ausgeführt (nur statische Analyse und harmlose Stubs); Treffer echter Tools
  bestätigt erst die Abnahme auf einem Testrechner.
- Input-Filter: Parallels verwirft synthetische Eingaben schon vor dem Gast (v0.3-Befund), daher nur auf echter
  Hardware.
- Questlog-Texte, Captcha-Fenster: nur im echten Client. LGP-Look: in der VM im echten Client gemessen. **Fallback ohne Palette (2026-11-26, gemessen):** Kopie des Clients mit einem geänderten Byte an der Hook-Stelle (`push 3` → `push 2` bei 0x93065b, wie ein anderer Build): Guard-Log `lgp palette: client code not recognised (other build), blue tiles only`, `@oginfo` meldet `flags 1` (keine Paletten-Fähigkeit), der Server schickt die normalen Units, Storm Gust erscheint als blaue Standard-Dreiecke (`docs/images/lgp-fallback-other-build.png`), kein Absturz. Offen: zwei Betrachter gleichzeitig.
- CFG nur mit llvm-mingw (`TOOLCHAIN=llvm`); der Standard-Build (mingw-gcc) hat es nicht. Der Server-Patch baut mit
  Visual Studio (CI); gelaufen ist ein MSVC-Server nicht.

## 8. Historie

**2026-11-27 (1.0.8, Studio für Windows und Linux):** Ein GitHub-Workflow (`studio-release.yml`) baut das signierte Studio
auf macOS, Windows und Linux und veröffentlicht EIN `stable.json` mit allen drei (vorher nur macOS, lokal gebaut). Gemessen:
Aktualisierung mit Signaturprüfung für alle drei Ziele gegen das echte Release, das Windows-Setup in der VM still
installiert und gestartet. Dabei gefunden: ein heruntergeladenes Studio suchte das Repository unter dem Pfad der
Build-Maschine („Registry not found: D:\a\…“). Jetzt fragt es einmal nach dem OdinGuard-Ordner und merkt ihn sich
(`repo_status`, `set_repo`); DOM-Test +4 Prüfungen, Rust-Test `root_tests`.

**2026-11-27 (1.0.7, Update-Hinweis im Studio):** Das Studio fragt beim Start einmal im Hintergrund nach dem signierten
`stable.json` (abschaltbar), zeigt ein Banner mit den Release-Notizen und dem Hinweis, dass Spieler und Server nicht
angefasst werden, und der Tab „Updates“ nennt die sieben Schritte für einen sicheren Roll-out. Neu: Knopf „Keep the built
release“ (Rollout-Tab, wie `og keep-previous`), vorher nur per Kommandozeile. `tools/release_studio.sh` veröffentlicht
auf Wunsch (`OG_PUBLISH_TAG`). Vorher gefunden: `releases/latest/download/stable.json` gab 404, die Update-Prüfung fand für
niemanden etwas. DOM-Test `tests/studio-ui` (14 Prüfungen: Banner, Später, abgeschaltet, kein Netz bleibt still).

**2026-11-27 (1.0.6, `!lang`):** Sprache im Spiel umschalten (`!lang`, `!lang de|en|auto`), sofort für Guard-Texte, Titel
und Server-Meldungen (neuer Steuer-Record `OG_C_LANG`), gespeichert in `odinguard.ini`. E2E 5c um 7 Prüfungen erweitert
(Status, de, en, unbekannte Sprache, Speichern, Server-Meldungen wechseln ohne Neustart). Dabei gefunden: ein Leerzeichen am
Ende der Eingabe (`!lang de `) lehnte der Server ab (jetzt getrimmt); der Test selbst nutzte erst `!pf`/`@pf` statt `+pf`.

**2026-11-26 (1.0.5, Sprachen DE/EN):** Alle Spielertexte auf Englisch und Deutsch (Client: Meldungsfenster, Titel,
`!`-Antworten; Server: Chat-Meldungen, Ablehnung beim Login), Sprache vom Guard im Hello (`og-lang`). E2E 97/97, neu 5c:
deutsche Ablehnung (Client und Login-Log), deutsches Ratenlimit, deutsche `!dark`-Antwort, Umlaut als Windows-1252-Byte
beim Client, Zusatzsprache aus der Konfiguration (`og_text_fr_…`), nicht übersetzte Texte bleiben Englisch. Die
übrigen Prüfungen laufen mit `Language=en` (die VM ist deutsch). Dabei gefunden: ein Testfehler (`grep` mit `.` passt in
UTF-8 nicht auf ein Einzelbyte), behoben. **Gemessen im echten Client (deutsches Windows, lokaler Testserver):** Titel
„OdinGuard Anti Cheat 1.0.5 - wartet auf den Server“, Chat „Competitive-Pack ist beim nächsten Start AUS …“, „Paketfilter
(WoE/BG-Karten): Chat AUS …“, „Du bist in keiner Gruppe.“, „Kreis 1 an …“, Umlaute korrekt (`docs/images/german-chat.png`).

**2026-11-26 (1.0.4, Modul `launcher`, Sichttest in der VM mit OpenThor 0.4.0):** Profil mit `launcher`, BurningRO-Client.
Gemessen: `alpha.exe` direkt gestartet → Meldung „Please start the game with the patcher (openthor.exe) …“ vor dem
Spielfenster, Log `launcher: started without the patcher, refused`, kein `alpha.exe` danach
(`docs/images/launcher-start-message.png`); *Ja* → `openthor.exe` läuft. Play im Patcher → Log `launcher: started by the
patcher`, Spiel läuft, Ticket gelöscht. Drei schnelle Klicks auf Play → ein Spiel (Knopf gesperrt); noch ein Play bei
laufendem Spiel und `max_instances: 1` → Hinweis im Patcher, weiter ein `alpha.exe`. Kein Bestandteil der E2E-Suite
(braucht den echten Client); Studio-Test prüft die Profil-Defines.

Die Ergebnisse von v0.1–v0.3 (Handshake, Hardware-Bans, Rechner-Limit, Alive-Timeout, Farb-Nicks, Auren-IDs,
Release-Paket) gelten weiter für den unveränderten Code; Details im Git-Verlauf dieser Datei (Commit `4ad6498`).

**2026-11-26 (1.0.3, Mindest-Release, Sichttest in der VM):** Standard ist jetzt: nur das gebaute Release darf rein
(`og_min_release`, Studio schreibt es). Gemessen gegen einen Server mit `og_min_release: 1.0.3`: echter Client mit
1.0.2 → Meldungsfenster „Your client is outdated (OdinGuard older than 1.0.3) …“, kein Login, kein Ereignis
(`docs/images/outdated-release-message.png`); echter Client mit 1.0.3 → Login, über 2 min im Spiel, kein Ereignis.
Dabei gefunden: Das Meldungsfenster bekam den OdinGuard-Titel (der Titel-Thread hielt es für das Spielfenster);
ab 1.0.3 werden Dialogfenster übergangen.

**2026-11-26 (Rollout 1.0.0 → 1.0.2, Sichttest in der VM):** Ein neuer Guard ändert den Guard-Code, eine neue
`odin.grf` den Inhalt (Versions-String in `data\odinguard\profile.txt`). Ohne Vorkehrung wären alle Spieler mit 1.0.0
nach dem Server-Update gekickt worden (`guard-code`, File-Challenge, `grf:modified (content)`). Neu:
`og keep-previous`, `og_guard_previous_file`, `og_protected_previous` (E2E 69–74). Dabei gefunden: Der 1.0.1-Guard
verglich den Hintergrund-Hash nur mit dem **letzten** passenden `og_grf_file`-Eintrag; haben zwei Versionen dieselbe
Dateitabelle, wurde die neuere gekickt. Behoben in 1.0.2 (jeder passende Eintrag zählt, E2E 75); der Generator
schreibt die vorige Version nach der aktuellen, damit 1.0.0-Clients (alte Logik) passen. Gemessen gegen den neuen
Server je 2,5 min im Spiel: 1.0.0- und 1.0.2-Client ohne Ereignis, kein Kick. v1.0.1 ging nie an Spieler.

**2026-11-24 (LGP-Palette, Sichttest in der VM):** Beim ersten Login mit dem echten Client meldete der Heartbeat
`api-hook` (`kernel32!GetTickCount -> dxwrapper.dll`): DxWrapper hängt sich in `GetTickCount`, die Prüfung kannte nur
Pfad-Ausnahmen. Behoben: Module mit einem SHA-256 aus der Allowlist (`OG_KIND_ALLOW_SHA256`, z. B. OG-ALLOW-004
DxWrapper) gelten auch dort als bekannt (`og_file_allowed`, Hash je Datei gecacht). Danach keine Meldung mehr.
