# 02 – Architektur von OdinGuard Anti Cheat 1.0

Kennzeichnung: **[gemessen]** = selbst gebaut/getestet, **[gelesen]** = aus Code/Doku belegt,
**[vermutet]** = plausibel, nicht geprüft, **[Vorschlag]** = Empfehlung.

## 1. Grundsätze

1. **Der Server entscheidet.** Ein Client kann nicht beweisen, dass er ehrlich ist. Echte Sicherheit kommt nur aus
   serverseitigen Prüfungen (Ratenlimits, Delay, Plausibilität, Handshake-Pflicht, Bans). Alles im Client ist eine
   **Hürde**: es macht Manipulation teurer und erkennbar und liefert Belege (Abschnitt 9).
2. **Offener Code, geheime Schlüssel.** Sicherheit beruht auf Schlüsseln pro Server (Server-Schlüssel, Build-Secret,
   Signier-Schlüssel) und auf Build-Variation pro Betreiber, nicht auf geheimem Code.
3. **Ein Profil, ein Release.** `odinguard.profile.toml` ist die einzige Quelle. Studio (GUI) und `og` (CLI) bauen
   daraus dasselbe signierte Release. Geheimnisse stehen nie im Profil, nur ihre Dateipfade.
4. **Modular zur Build-Zeit.** Nicht gewählte Module sind nicht im Binary (Beleg: Linker-Map, Abschnitt 2.3).
5. **Flaggen, dann verzögert bannen.** Detections erzeugen Flags mit Punkten; Bans kommen gesammelt in Ban-Wellen,
   damit Cheater die auslösende Prüfung nicht zuordnen können.
6. **Keine Fehlalarme als Ziel.** Jede Regel hat Schwere, Konfidenz und Aktion (log, flag, flagban, kick, ban);
   Allowlist für legitime Add-ons; Rollout beginnt im Warnmodus.

## 2. Modulsystem

### 2.1 Registry (`modules/modules.toml`) [gelesen]

Jedes Modul hat `id`, `version`, `kind` (core, protection, detection, challenge, gameplay, client-feature), `side`
(client, server, both), `requires` (Abhängigkeiten), `client_src` (C-Quellen im Client), `define`
(`OG_MOD_*` im generierten `og_modules.h`), `server_key` (Schalter in `odinguard_athena.conf`), `assets`
(Gruppen für `odin.grf`) und `param` (Name, Typ, Default, Min/Max, Konfig-Schlüssel, Hilfetext).

| Modul | Art | Seite | braucht | Client-Quellen | Server-Schalter | Parameter |
|---|---|---|---|---|---|---|
| core | core | beide | – | og_main, og_log, og_net, og_sha256, og_challenge, og_wire | og_enable | required, log_only, min_version |
| rate-limit | protection | Server | – | – | og_rate_limit | packets_per_second, action |
| hwid | protection | beide | core | og_hwid | og_hwid | ban_match |
| multi-client | protection | beide | hwid | og_env | og_multi_client | max_per_machine, max_windows, block_vm, vm_action |
| heartbeat | protection | beide | core | og_guard | og_heartbeat | interval, timeout |
| integrity | protection | beide | heartbeat | og_integrity | og_integrity | action, tamper_action |
| cheat-signatures | detection | beide | heartbeat | og_detect | og_signatures | package, inject_action |
| grf-rules | protection | beide | heartbeat | og_assets | og_grf_rules | strict, action (+ GRF-Manifest aus `[grf]`) |
| input-filter | protection | beide | heartbeat | og_input | og_input_filter | enabled |
| crash-report | client-feature | Client | core | og_crash | – | – |
| client-commands | client-feature | beide | core | og_cmds | og_client_commands | – |
| colors | gameplay | beide | core | og_text, og_ui | og_colors | – |
| branding | client-feature | Client | core | og_splash | – | – |
| lgp | gameplay | beide | core | og_lgp | og_lgp_enable | – |
| ban-system | protection | Server | core | – | og_ban_system | wave_hour, ban_score |
| behaviour | detection | Server | ban-system | – | og_behaviour | nodelay_per_minute, autopot_ms, macro_jitter_ms |
| ch-nonce | challenge | beide | core | (core) | og_ch_nonce | interval, action |
| ch-code | challenge | beide | integrity | (core) | og_ch_code | interval, action |
| ch-time | challenge | beide | core | (core) | og_ch_time | interval, max_drift_percent, action |
| ch-file | challenge | beide | core | (core) | og_ch_file | interval, action |
| ch-captcha | challenge | Server | behaviour | – | og_ch_captcha | action |
| ch-honeypot | challenge | Server | ban-system | – | og_ch_honeypot | action |
| competitive-pack | gameplay | beide | ch-file | – | og_competitive_pack | – |

### 2.2 Build-Zeit und Laufzeit

- **Build-Zeit (Client):** Das Studio schreibt `og_modules.h` (`#define OG_MOD_* 1` nur für gewählte Module),
  `og_modules.mk` (Liste der C-Quellen) und `og_profile.h` (Betreiber, Ports, Salts, Server-Public-Key,
  geschützte Dateien – Geheimnisse XOR-maskiert mit `BLAKE2b("OdinGuard/mask" ‖ seed ‖ id)`). Nicht gewählte
  Module sind in `og.h` leere `static inline`-Stubs. Die Link-Reihenfolge wird mit dem Seed gemischt.
- **Laufzeit (Server):** `odinguard_athena.conf` + `conf/import/odinguard_conf.txt` (vom Studio generiert) schalten
  Module und Parameter; der Server schickt die Policy (OG_S_POLICY) und das Signaturpaket (OG_S_SIGS) nach dem
  Handshake. Spätere Zeilen überschreiben frühere (`og_switch_set`).
- **Abhängigkeiten:** Das Studio schließt die Hülle (`requires`) und lehnt Profile mit fehlenden Abhängigkeiten,
  Parametern außerhalb Min/Max oder unbekannten Modulen ab (`og validate`, Unit-Test `validation_catches_errors`).

### 2.3 Beleg „nicht gewählter Code steckt nicht im Binary“ [gemessen]

`client/Makefile` erzeugt ein Objekt pro Quelle unter `OUT/obj/` und eine Linker-Map. Die Pipeline liest die
Map (`linked_objects`) und bricht ab, wenn ein nicht gewähltes Modulobjekt gelinkt wurde (`check_linked`).
Messung 2026-11-23: Minimal-Profil (core + rate-limit) linkt 6 Kern-Objekte, DLL 271 360 Byte, keine Strings
anderer Module; Default-Profil 1 026 560 Byte.

## 3. Profil (`odinguard.profile.toml`, Schema 3)

Abschnitte: `modules`, `[general]` (id, operator, client_build, ports, splash_ms, variation_seed, hwid_salt),
`[keys]` (Pfade: server_seed, signing_seed, build_secret), `[params.<modul>]`, `[commands.<id>]` (enabled, name,
group, rate_ms), `[signatures]` (sources, allowlist, max_action, version), `[assets]` (competitive_pack,
odin_grf-Gruppen, competitive_source + competitive = Pack-Kategorien), `[grf]` (GRF-Manifest: `file`, `mod`,
`overlays`, Aktionen), `[signatures.groups]` (pro Tool-Gruppe `off` oder Aktion), `signatures.disabled`, `output`,
`[rollout]` (stage 1–4, reviewed_rules).
Migration: `profile::migrate` hebt Schema 1 → 2 → 3 an (3: Auren raus, sie sind jetzt ein eigenes rAthena-Feature;
Test `schema1_profile_migrates_without_data_loss`),
`og migrate` legt eine `.bak`-Kopie an.

Ableitungen (Studio, `pipeline::Derived`):
- `proto_salt = BLAKE2b-128("OdinGuard/proto-salt" ‖ variation_seed ‖ server_pub)`
- `profile_tag = BLAKE2b-128("OdinGuard/tag" ‖ proto_salt)` – ein Client eines fremden Profils wird mit Status 3
  abgewiesen [gemessen, E2E].
- Secrets für den Server landen in `secrets/odinguard.env` (`ODINGUARD_KEY`, `ODINGUARD_PROTO_SALT`,
  `ODINGUARD_BUILD_SECRET`).

## 4. Protokoll v2 (`proto/og_proto.h`, `og_wire.c`)

- Handshake: Client-Hello (X25519 ephemer, Nonce, Version, Build, Hardware-Komponenten, `profile_tag`) → Server-
  Antwort mit Ed25519-Signatur über das Transkript; der Client prüft gegen den **eingebauten** Server-Key.
- KDF: `BLAKE2b-512("OdinGuard/v2" ‖ proto_salt ‖ shared ‖ pubs ‖ nonces)` → je ein Schlüssel pro Richtung.
- Records: XChaCha20-Poly1305 über den **gesamten** Spielstrom, Zähler-Nonce (Replay/Reorder = Abbruch),
  Schlüsselrotation alle 4096 Records (`key = BLAKE2b-256(key ‖ "OdinGuard/rekey")`).
- Downgrade-Schutz: Client verweigert Server < v2, Server erzwingt `og_min_version ≥ 2`.
- Steuer-Nachrichten: C→S `ALIVE`, `DETECT`, `PONG`, `REPORT`, `RESPONSE`; S→C `POLICY`, `PING`, `COMMAND`,
  `COLORS`, `CHALLENGE`, `SIGS`.
- Parser sind reine Funktionen (`og_wire.c`, im Client und Server byte-identisch) und werden gefuzzt
  (`make -C client fuzz`: 2 000 000 Iterationen ASan/UBSan ohne Befund [gemessen]).
- Vergleiche von MACs/Digests in konstanter Zeit (`crypto_verify16/32`, Monocypher).

## 5. Challenge-Framework

Der Server schickt `OG_S_CHALLENGE {id, type, nonce[16], target, a, b}` in zufälligen Abständen
(`interval` ± Zufallsfenster); der Client antwortet mit `OG_C_RESPONSE`. Jede Challenge-ID gilt einmal
(Antwort auf unbekannte/beantwortete ID = `challenge:replay`), Timeout = Fehlschlag.

| Challenge | Prüft | Server kennt | Schutzwirkung |
|---|---|---|---|
| ch-nonce | `BLAKE2b(key = build_secret, "OG/nonce" ‖ nonce)` | Build-Secret | Hürde: beweist den Betreiber-Build, solange das Secret nicht aus dem RAM gezogen wird |
| ch-code | Hash eines zufälligen Bereichs des kanonischen Code-Images von Exe oder Guard | Referenz-Images (`conf/odinguard/`) | Hürde: Patches im Code fallen auf, auch wenn die Heartbeat-Hashes gefälscht werden |
| ch-time | Client-Uhr gegen Server-Uhr (Tick + Performance-Counter) | eigene Uhr | Hürde gegen Speedhacks; Server-Delay-Prüfung ist die echte Sicherheit |
| ch-file | Hash eines zufälligen Bereichs von `odin.grf` / Competitive-Pack | offizielle Dateien | Hürde, Pflichtdatei fehlt = Fehlschlag, optionale fehlt = ok |
| ch-captcha | Bot-Captcha nach Verhaltens-Flags, nie auf WoE/BG-Maps | Antwort | echte Sicherheit gegen unbeaufsichtigte Bots |
| ch-honeypot | unsichtbare Lockvogel-Einheit, die nur Bots angreifen | Einheiten-ID | echte Sicherheit (serverseitig), Treffer = flagban |

Kanonisches Image: nicht beschreibbare, nicht verwerfbare Sektionen; HIGHLOW-Relocations und die IAT
(Datenverzeichnis 12) genullt, damit ASLR und Loader-Schreibzugriffe den Hash nicht ändern [gemessen].
Fehlschläge gehen über `og_report` → Flag mit Punkten → Aktion laut Profil; Telemetrie nur Diagnose
(Regel, Zeit, Account, gehashte HWID).

## 6. Detection-Modul „cheat-signatures“

- **Quellen:** `signatures/signatures.toml` (300 Signaturen in 21 Tool-Gruppen, davon 164 vom Client prüfbar;
  statische Analyse der Proben plus öffentlich bekannte Tools, `analysis/cheat-signatures/`) und
  `signatures/allowlist.toml` (ROExt-Standard, dxwrapper, Skins, Overlays, Browser, Hersteller-Software).
- **Konfiguration:** Gruppe = ID-Präfix (`OG-RCX-001` → RCX). Profil `[signatures.groups]`: `off` oder eine Aktion
  für die ganze Gruppe; `signatures.disabled` für einzelne IDs; `og sig-groups` / Studio-Tab „Signaturen“.
- **Paket:** Das Studio kompiliert daraus `odinguard.sigpack` – `"OGSP"`, Version, Einträge
  `{id, kind, action, score, name, pattern}`, EdDSA-Signatur (Signier-Schlüssel des Profils). Der Server lädt es
  nur mit gültiger Signatur und Mindestversion und schickt die client-prüfbaren Arten an den Client.
- **Arten:** process, window_title, window_class, module, file_name, file_sha256, driver, grf_sha256, mutex
  (benannte Mutexe/Events/Sections), registry (Schlüssel vorhanden, HKCU/HKLM, 32/64-Bit-Sicht) – vom Client
  geprüft; grf_entry, import_pattern, string_pattern nur Analysedaten; allow_sha256, allow_path (Allowlist).
- **Punkte:** Schwere (low 10, medium 30, high 60, critical 100) × Konfidenz (high 1, medium 0,6, low 0,3);
  `max_action` des Profils kappt Aktionen (log < flag < flagban < kick < ban).
- **Verhalten (Server, echte Sicherheit):** `behaviour` – Skill-Anfragen innerhalb des Delays (nodelay),
  Autopot-Reaktionszeit, Makro-Timing; `rate-limit` – Pakete pro Sekunde (WPE/RPE-Sendeschleifen).
- **Fehlalarm-Schutz:** zwei Scans zur Bestätigung (Integrität), Allowlist vor jeder Meldung, Toleranzpfade
  (Warsaw, Treiber-Overlays, Parallels), jede Meldung einmal pro Verbindung [gemessen, `dettest`].

## 6a. GRF-Prüfung (Modul grf-rules, wie Gepard, aber patcher-tauglich)

- Der Server schickt mit dem Signaturpaket das GRF-Manifest des Betreibers (Arten `GRF_LIST`, `GRF_MOD`, `OVERLAY`).
- Der Guard prüft jede GRF aus `DATA.INI`: Name + Größe + **Tail-Hash** (SHA-256 über den 46-Byte-Header und die
  Dateitabelle bis Dateiende; jedes Umpacken oder Ändern eines Eintrags ändert die Tabelle) bei jedem Scan, gecacht
  nach Größe/Änderungszeit; der **Voll-Hash** der Datei läuft einmal im Hintergrund-Thread mit niedriger Priorität.
- Erlaubte Mods (`grf.mod`) werden über den Tail-Hash unabhängig vom Namen erkannt (Skins, darkworld-Varianten).
- Meldungen `grf:modified` (Tabelle oder Inhalt), `grf:unknown`, `grf:missing` (nur Pflicht-GRFs) mit eigener Aktion.
- Patcher: mehrere Versionen pro Name erlaubt (`og grf-scan --keep-previous`): erst Server-Konfig mit alter + neuer
  Version ausrollen, dann patchen, später alte Version entfernen. `odin.grf`/`odin-dark.grf` trägt jeder Build ein.
- Lose Dateien (`grf_strict`): außerhalb der Overlay-Ordner (eingebaut + `grf.overlays`) gemeldet.
- Grenzen: Wer den Guard selbst manipuliert, kann falsche Antworten liefern; serverseitig gegengeprüft wird
  stichprobenartig mit `ch-file` (zufällige Bereiche der offiziellen Dateien).

## 7. Command-Registry (`modules/commands.toml`)

13 Commands (3 Client `!…`, 10 Server `@…`) mit `id`, `kind`, `name` (umbenennbar), `module`, `group`
(Mindest-Gruppe), `rate_ms`, `params`, `help_de/en`. Das Studio erzeugt daraus:
- die Server-Konfig (`og_cmd_<id>`, `og_cmdgroup_<id>`, `og_cmdrate_<id>`, `og_cmdname_<id>`); deaktivierte
  Commands werden nicht registriert. Auch die `!`-Commands prüft der Map-Server (Registry in `og_map.cpp`:
  Name, Gruppe, Ratenlimit) und schickt dann `OG_S_COMMAND`/`OG_S_PING` an den Guard – der Client braucht
  keine eigene Tabelle, Umbenennen wirkt ohne Client-Update;
- die Doku (`og docs <profil> --lang de|en`, Release `docs/commands-*.md`; Abschnitt in `16-commands-referenz.md`).
Commands eines nicht gewählten Moduls sind automatisch aus.

## 8. OdinGuard Studio (`studio/`)

- `crates/og-core`: registry, profile (Schema + Migration), keys, crypto (Monocypher-FFI), sigpack, grf, generate,
  pipeline (Build), manifest (signiertes Hash-Manifest), update (Updater), rollout (Stufen-Gate).
- `crates/og-cli` (`og`): new, validate, registry, show, migrate, preview, build, verify-release, sigpack,
  sigpack-verify, sig-groups, grf-scan, grf-allow, grf-hash, keys, docs, rollout, update, update-manifest,
  update-verify.
- `apps/studio` (Tauri 2): Tabs Profil, Module & Challenges, Commands, Signaturen, GRF & Mods, Competitive-Pack,
  Build, Rollout, Updates; ruft nur
  og-core auf.
- **Release pro Profil** (`og build`): `client/` (odinguard.dll, ijl15.dll, odin.grf, [odin-dark.grf],
  odinguard.ini, README.txt, Lizenzen), `server/` (src, conf inkl. sigpack und Referenz-Images, sql),
  `patcher/filelist.txt`, `docs/` (Handbuch, Command-Referenz, Build-Report), `secrets/odinguard.env`,
  `manifest.json` + `manifest.sig` + `signing.pub`. `og verify-release` prüft Signatur und jede Datei.
- Reproduzierbar: gleiche Eingaben → gleiche Hashes (zweimal gebaut, identisch [gemessen]); Cross-Compile mit
  mingw-w64 auf macOS. Liegt das Ausgabeverzeichnis auf einem Pfad mit Leerzeichen, baut die Pipeline den
  Client in einem festen Arbeitsordner unter `$TMPDIR/odinguard-build/<id>` (GNU make kann keine Leerzeichen).

## 9. odin.grf

Nur Assets (LGP-Kacheln `odinlgp_<farbe>.tga` und `hanmoon1/2.tga`, später UI), gebaut mit `rgrf-core` (als Kopie in `studio/vendor/rgrf-core`, Stand in `VENDORED.md`, erneuern mit `studio/tools/vendor-rgrf-core.sh`; Studio und `og` brauchen kein RustyGRF) aus den Asset-Gruppen der
gewählten Module und danach mit `rgrf_core::verify` geprüft. DLLs und Server-Code liegen nie in der GRF.
`odin.grf` ist Pflicht-Datei der `ch-file`-Challenge, das Competitive-Pack (`odin-dark.grf`) optional.
Ladereihenfolge (`DATA.INI`): `0=odin.grf`, `1=odin-dark.grf` (falls gewählt), danach die GRFs des Clients.
Competitive-Pack: aus wählbaren Kategorien (`assets/competitive/recipe.toml`, Staging-Ordner pro Kategorie) oder
einer fertigen GRF; alle Spieler bekommen dasselbe Pack.

## 10. Updater und Rollout

- **Studio-Selbstupdate:** Kanäle `stable`/`beta`, Endpunkt `<basis>/<kanal>.json` + `.sig`; Standard: GitHub
  Releases des OdinGuard-Repos (stable = neuestes Release, beta = Release mit Tag `beta`), per
  `OG_STUDIO_UPDATE_ENDPOINT` überschreibbar. Schritt 1 (og-core):
  Manifest-Signatur (minisign), Downgrade-Sperre gegen die höchste je akzeptierte Version, Pinning des
  Manifest-Hashes. Schritt 2 (Tauri-Updater-Plugin, gleicher Endpunkt): Artefakt-Signatur und Installation.
  Nur https (http nur Loopback in Tests). Privater Schlüssel `keys/studio-updater.key` bleibt lokal.
- **Core-/Signatur-Updates:** `og update --key core|sigpack …` lädt, prüft und legt Versionen unter
  `versions/<key>/<version>/` ab; `--rollback` schaltet auf die vorige lokal gehaltene Version.
- **Release-Pipeline:** `tools/release_studio.sh <stable|beta> <https-basis>` baut, signiert Artefakt und
  Manifest, prüft offline (`og update-verify`) und veröffentlicht **nichts**.
- **Rollout-Stufen pro Profil:** 1 Warnmodus (`log_only`) → 2 Kick → 3 Patcher-Auslieferung → 4 Pflicht
  (`required`). `og rollout` liest Exporte von `og_event`/`og_client` und sperrt Stufe 4, solange Regeln mit
  Fehlalarm-Verdacht (Quote über `fp_percent`) ungeprüft sind oder zu wenige aktive Accounts den Guard nutzen.

## 11. Härtung: Hürde oder echte Sicherheit

| Maßnahme | Wo | Wirkung | Stand |
|---|---|---|---|
| AEAD über den ganzen Strom, Sitzungsschlüssel, Replay-Schutz, Rekey | beide | echte Sicherheit gegen Mitlesen/Einschleusen im Netz; Hürde gegen Tools im Prozess | gemessen (E2E) |
| Gepinnter Server-Key, signierte Policy/Sigpack, Downgrade-Schutz | beide | echte Sicherheit gegen MITM und gefälschte Server-Daten | gemessen |
| Profil-Tag / Protokoll-Salt pro Betreiber | beide | Hürde: Clients/Bypässe für einen Server laufen nicht auf einem anderen | gemessen |
| Parser-Fuzzing | beide | echte Sicherheit gegen Speicherfehler in den Parsern | gemessen |
| ASLR, DEP, Stack-Protector, keine Timestamps | Client | Hürde, erschwert Exploits | gelesen (Makefile) |
| CFG (Control Flow Guard) | Client | Hürde gegen Code-Umlenkung; mit llvm-mingw (`TOOLCHAIN=llvm`, `OG_TOOLCHAIN=llvm og build`), mingw-gcc kann es nicht | gemessen: GuardFlags gesetzt (REA), Laufzeit siehe 05-tests |
| String-Maskierung, Build-Variation (Link-Reihenfolge, Konstanten) | Client | Hürde gegen generische Patches | gemessen (Hashes je Profil verschieden) |
| Anti-Debug, Hardware-Breakpoints, API-Hook-Check, Selbst-Integrität | Client | Hürde, zweistufig bestätigt | gemessen (keine Fehlalarme im Testclient) |
| Code-Signing (Authenticode) | Client | senkt AV-Fehlalarme | Hook vorgesehen, kein Zertifikat |
| Challenges nonce/code/time/file | beide | Hürde mit serverseitiger Prüfung | gemessen (E2E) |
| Rate-Limit, Delay/Nodelay, Honeypot, Captcha, Multi-Client, Handshake-Pflicht | Server | **echte Sicherheit** | gemessen (E2E, Captcha nur Server-Logik) |
| Flags, Ban-Wellen, Audit-Log (`og_event`, `og_flag`) | Server | echte Sicherheit + Nachvollziehbarkeit | gemessen |
| GRF-Allowlist (offizielle Hashes, ch-file) | beide | Hürde; serverseitig geprüft | gemessen |

Ehrlich: Wer den Rechner kontrolliert (Kernel-Treiber, VM-Introspektion, eigener Loader ohne Guard + gestohlenes
Build-Secret), kann jede Client-Prüfung umgehen. Ziel ist, öffentliche Tools und Bots zu stoppen und Handarbeit
teuer und nachweisbar zu machen; serverseitige Regeln greifen unabhängig vom Client.

## 12. ADR: Sprachen

| Teil | Sprache | Begründung |
|---|---|---|
| Client-DLL, Loader | C11 (mingw-w64, 32 Bit) | klein (271 KB minimal), AV-freundlich, kein Runtime-Ballast, Hooks/Win32-FFI ohnehin `unsafe`; getestet seit v0.1 |
| Server-Modul | C++ (xAthena) | nativ im Emulator, gleiche Build-Kette |
| Gemeinsame Parser | C (`og_wire.c`), in beiden byte-identisch | eine Implementierung, gefuzzt |
| Studio, CLI, Signatur-/Manifest-/Update-Werkzeuge | Rust (Tauri 2) | Speichersicherheit beim Parsen fremder Daten, Cross-Platform-GUI, RustyGRF-Crates direkt nutzbar |

Keine Abweichung von der Standardannahme in `<architecture>` Punkt 6.
