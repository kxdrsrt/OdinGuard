# 03 – Einbindung beim Betreiber

Grundsatz: Es gibt kein Spieler-Setup und kein Spieler-`uninstall`. OdinGuard kommt nur über das Release des
Betreibers (Studio/`og build`) und dessen Patcher zum Spieler.

Für **eigene Master- und Test-Clients** gibt es `tools/client/og_client.py` (`status`, `install`, `uninstall`, jeweils mit
`--dry-run`): baut den Loader ein (`ijl15.dll` des Clients bleibt als `ijl15r.dll` in der Kette), kopiert
`odinguard.dll`/`odin.grf` (`--pack`: auch `odin-dark.grf`), setzt sie in `DATA.INI` nach vorn und sichert das Original.
`uninstall` gibt den Ordner zurück: Original-`ijl15.dll` zurück, Guard-Dateien, `odinguard.ini`, `odinguard.log`,
`odinguard-crash\` weg, `DATA.INI` aus der Sicherung. Getestet mit `tests/client/test_og_client.py` und an einer Kopie
eines echten Clients (alle 5545 Nicht-GRF-Dateien nach Ein- und Ausbau identisch). Details: `docs/INTEGRATION.md` §7.

## 1. Profil anlegen und bauen

1. `og new <name>.profile.toml --id <id> --operator "<Servername>"` – legt Profil, Server-Schlüssel, Signier-
   Schlüssel und Build-Secret an (Dateien bleiben lokal, nie committen, sichern).
2. Module, Parameter, Commands, Auren und Signaturquellen im Studio oder direkt im TOML wählen; `og validate`.
3. `og build <profil>` → Release-Ordner (Inhalt siehe `docs/02-architecture.md` §8); `og verify-release <ordner>`.
4. Bestehende Installation weiterführen: `hwid_salt` und `server_seed` **nie ändern** (sonst verlieren alle
   Rechner-Sperren ihre Wirkung bzw. ausgelieferte Clients passen nicht mehr zum Server).

## 2. Client

1. Der vorhandene `ijl15.dll` des Clients (z. B. ROExt) wird zu `ijl15r.dll`; der OdinGuard-Loader wird
   `ijl15.dll`. Ladekette: `<exe> → ijl15.dll (Loader) → ijl15r.dll (ROExt) → ijl15x.dll`, zusätzlich lädt der
   Loader `odinguard.dll`.
2. `odin.grf` (und optional `odin-dark.grf`) in `DATA.INI` **vor** den übrigen GRFs eintragen:
   `0=odin.grf`, `1=odin-dark.grf`, danach die bisherigen Einträge.
3. Alle Dateien aus `client/` plus `ijl15r.dll` in die Pflichtliste des Patchers aufnehmen
   (`patcher/filelist.txt` enthält Namen und SHA-256).
4. `odinguard.ini` ist optional (Splash-Dauer, Debug-Log).

## 3. Server (rAthena/xAthena)

1. `server/src/*` in den Emulator kopieren, Hook-Zeilen laut `server/HOOKS.txt` (Repository) setzen, bauen
   (`og_wire.c` muss in `COMMON_H` stehen, sonst baut make das Objekt bei Änderungen nicht neu).
2. `server/conf/odinguard_athena.conf`, `server/conf/import/odinguard_conf.txt` und `server/conf/odinguard/`
   (Signaturpaket, Referenz-Images von Guard und GRFs) nach `conf/` kopieren.
3. Die drei Werte aus `secrets/odinguard.env` als Umgebungsvariablen setzen (`ODINGUARD_KEY`,
   `ODINGUARD_PROTO_SALT`, `ODINGUARD_BUILD_SECRET`) oder in `conf/import/odinguard_conf.txt` eintragen – nie in git.
4. Tabellen (`og_client`, `og_component`, `og_ban`, `og_event`, `og_flag`, `og_aura_unlock`) entstehen beim ersten
   Start (`CREATE TABLE IF NOT EXISTS`, `server/sql/odinguard.sql` zum Nachlesen).
5. Aura-Quests: `npc/added/odinguard_auras.txt` in `npc/scripts_added.conf` eintragen, Quest-IDs in
   `db/import/quest_db.yml` (Questlog-Texte brauchen Einträge in der Client-Lub).

## 4. Rollout-Stufen (im Profil, `[rollout] stage`)

| Stufe | Server-Schalter | Wirkung | Weiter, wenn |
|---|---|---|---|
| 1 Warnmodus | `og_required: no`, `og_log_only: yes` | alles wird nur protokolliert; Flags werden gesammelt, Ban-Welle läuft trocken | `og rollout` zeigt keine ungeprüften Regeln über der Fehlalarm-Schwelle |
| 2 Kick | `og_log_only: no` | Aktionen greifen (Kick, Flag, Ban-Welle), Spieler ohne Guard spielen weiter | Fehlalarm-Quote bleibt niedrig |
| 3 Patcher | wie 2 | Guard wird an alle Spieler ausgeliefert | Abdeckung (Accounts mit Guard) hoch genug |
| 4 Pflicht | `og_required: yes` | ohne gültigen Guard kein Login | – |

`og rollout <profil> --events og_event.tsv --clients og_client.tsv [--active N] [--advance]` (oder Tab „Rollout“
im Studio) prüft das Gate; nach `--advance` Release neu bauen und die Server-Konfig ausrollen.

## 5. Updates

- Signaturpaket: Quellen ändern, `[signatures] version` erhöhen, neu bauen, nur `conf/odinguard/odinguard.sigpack`
  tauschen und den Map-Server neu starten; der Server lehnt ältere Versionen und ungültige Signaturen ab.
- Guard/Core: neues Release bauen, Server und Patcher gemeinsam aktualisieren (Protokoll-Inkompatibilität meldet das
  Studio; v1-Clients sind mit v2-Servern nicht kompatibel).
- Studio: Tab „Updates“ (Kanal stable/beta), siehe `docs/02-architecture.md` §10.
