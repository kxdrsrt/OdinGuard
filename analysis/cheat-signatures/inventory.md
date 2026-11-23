# Cheat-Inventar und Erkennungsmerkmale

Stand: 2026-11-23. Zweck: Erkennungs- und Abwehrsignaturen für OdinGuard (Client 2018-06-20, 32 Bit).
Methode: ausschließlich statische Analyse. Kein Programm wurde ausgeführt (auch nicht in Wine),
keine Binärdatei wurde kopiert oder entpackt auf Platte geschrieben. Archiv-Einträge wurden mit
`bsdtar -xO` gestreamt und nur gehasht, eingebettete Treiber nur im Speicher gehasht.

Kennzeichnung der Aussagen:

- **[gemessen]** – direkt mit Werkzeug ermittelt (SHA-256/MD5, PE-Header, Importe/Exporte, Ressourcen,
  Authenticode, GRF-Inhalte, ACT-Framezahlen, Texturgrößen).
- **[gelesen]** – aus Klartext gelesen (Skripte, ini, readme, Edit-Log, Versionsressource).
- **[vermutet]** – Schlussfolgerung, nicht direkt belegt.

Werkzeuge: `shasum`, `i686-w64-mingw32-objdump -p`, Python `pefile`/eigene String-Extraktion,
REA `inspect_pe_resources`, RustyGRF `rgrf` (`info`, `hash`, `diff`, `act info`, `cat` nach stdout).
Referenzen für „unverändert“: `data kro latest.grf`, `official_data (chris).grf` und die GRFs des
Dev-Clients (`data.grf`, `core.grf`, `cdata.grf`, `cpal.grf`).

Dateien in diesem Ordner:

| Datei | Inhalt |
|---|---|
| `signatures.toml` | 170 Signaturen (Schema 1), Konventionen im Kopf der Datei |
| `allowlist.toml` | 32 Einträge: SHA-256 der legitimen Client-Dateien + generische Regeln |
| `behaviour.md` | Serverseitige Verhaltenssignaturen mit Schwellwerten |
| `hashes.txt` | SHA-256 aller untersuchten Dateien (auch Archiv-Einträge, eingebettete Treiber, Legit-Client) |
| `grf_entries.tsv` | Ergänzung: alle 10 246 abweichenden oder unlesbaren GRF-Einträge (Pfad, MD5, CRC32, Größe) |

Generator (nicht versioniert, `analysis/_work/` ist git-ignoriert): `analysis/_work/cheatsig/gen.py`.

## Übersicht

| Tool | Typ | Angriffsfläche | Greifende OdinGuard-Erkennung | Restlücke → Server |
|---|---|---|---|---|
| Autopot Fresh | Autopot, Memory-Reader (AutoHotkey) | Client-Prozessspeicher (lesen), Fensternachrichten | Datei-Hash, Prozess, AHK-Fensterklasse/-Ressource, String | Umbenannte/neu kompilierte Varianten → Reaktionszeit-Analyse |
| Class_DD | Makro/Input-Treiber (Kernel) | Eingabe auf Treiberebene; x86-DLL zusätzlich Speicher-Lesen/Schreiben | Treiber/Service, Datei-Hash (DLL + Treiber), Exporte, Registry, PDB-String | Eingaben nicht als „injected“ markiert → Intervall-Analyse |
| RoMedic | Autopot, Memory-Reader (.NET) | Client-Prozessspeicher (lesen/schreiben), Fensternachrichten | Datei-Hash, Prozess, Fenstertitel (vermutet) | Neu obfuskierte Builds → Reaktionszeit-Analyse |
| RCX (in WOERO/RCX) | Paket-Mitleser + Overlay | Hook-DLL im Client-Prozess, liest empfangene Pakete | Modul `rcxdraw.dll`, Prozesse, Fensterklasse, Hashes, Versionsstring | Reines Mitlesen erzeugt kein Serververhalten; Info-Minimierung serverseitig |
| RoExt (dinput.dll) | Eingabe-/Fenster-Erweiterung (dinput-Proxy) | Ersetzt DirectInput des Clients | Datei-Hash, Export-/Versions-Muster | – (statisch eindeutig) |
| nodelay.grf | Datei-Modifikation (Animation) | Client-Ressourcen | GRF-Hash, 29 `.act`-Einträge | Aktionsraten gegen Server-Delays |
| Thoth/woe/woebg/desu/dark/dark2/woerol.grf | Datei-Modifikation (Sicht/Effekte) | Client-Ressourcen | GRF-Hash, Eintrags-Hashes, Inhaltsklassen (1×1-Texturen, leere `.str`) | Sichtvorteil nicht serverseitig messbar |
| WOERO (woero.exe, woero.grf, woerop.grf) | Fremd-Client eines anderen Servers | – | nur `log` | – |

## Signaturen nach Art und Konfidenz [gemessen]

| kind | high | medium | low | gesamt |
|---|---|---|---|---|
| grf_entry | 34 | 72 | 0 | 106 |
| file_sha256 | 19 | 0 | 0 | 19 |
| grf_sha256 | 10 | 0 | 0 | 10 |
| string_pattern | 3 | 3 | 1 | 7 |
| file_name | 2 | 2 | 1 | 5 |
| process | 3 | 2 | 0 | 5 |
| module | 2 | 3 | 0 | 5 |
| window_title | 0 | 4 | 0 | 4 |
| window_class | 1 | 1 | 1 | 3 |
| import_pattern | 2 | 0 | 1 | 3 |
| driver | 2 | 0 | 0 | 2 |
| registry | 0 | 1 | 0 | 1 |
| **Summe** | **78** | **88** | **4** | **170** |

Nach Werkzeug: GRF 114, RCX 18, DD 16, Autopot 6, AHK 5, RoMedic 4, RoExt 4, WOERO 3.

## Archive

Die `.zip`/`.rar` neben den Ordnern enthalten **keine zusätzlichen Dateien** [gemessen]: alle
Einträge sind byte-identisch mit den entpackten Kopien. Einzige Abweichung: `WOERO/skin/default/Thumbs.db`
existiert nur auf der Platte, nicht in `WOERO.rar`. `WOERO/dinput.dll` ist identisch mit
`RoExt/dinput.dll` (SHA-256 `3d8d3f03…e878`).

## Autopot Fresh

- **Typ:** Autopot / Memory-Reader auf AutoHotkey-Basis.
- **Wirkweise (hohe Ebene):** liest HP/SP und Statuswerte aus dem Speicher des Client-Prozesses und
  schickt bei Unterschreiten eines Schwellwerts Tastendrücke an das Spielfenster [gelesen, `autopot.ahk`].
- **Merkmale:**
  - `autopot.exe`: PE32+ x64, kompiliertes AutoHotkey **1.1.26.01**, Skript liegt als RCDATA
    **`>AHK WITH ICON<`** (nicht `>AUTOHOTKEY SCRIPT<`); beide Namen stehen als Konstanten im AHK-Laufzeitcode [gemessen].
  - Manifest-Identität `AutoHotkey`, Fensterklasse des versteckten AHK-Hauptfensters `AutoHotkey` [gemessen: String `AutoHotkey` im Image; Klasse gelesen aus AHK-Doku/Verhalten → vermutet für diesen Build].
  - Das Skript ist auf einen Fenstertitel eines anderen Servers („Ragnaclan“) und dessen feste
    Speicheradressen zugeschnitten; in dieser Form auf unserem Client wirkungslos [gelesen] – Varianten sind aber leicht anzupassen [vermutet].
  - Konfigurationsdatei `ragnapot.ini` (Intervall 100 ms) [gelesen].
- **Erkennung:** `OG-APOT-001..006` (Hash, Prozess, ini, Skripttext), generisch `OG-AHK-001..005`
  (Fensterklasse/-titel, RCDATA-Namen). Zusätzlich sinnvoll: offene Prozess-Handles mit Lese-/Schreibrecht
  auf den Client melden (`OG_KIND_INJECT`-ähnlich) [vermutet, nicht Teil der Signaturdatei].
- **Clientseitig nicht erkennbar:** neu kompilierte/umbenannte Builds ohne AHK-Laufzeit; Eingaben über
  Fensternachrichten sind nicht immer vom Spieler unterscheidbar → `behaviour.md` B1 (Reaktionszeit).

## Class_DD (DD / ddxoft)

- **Typ:** Makro/Input-Treiber (Kernel) mit AHK-Wrapper.
- **Wirkweise (hohe Ebene):** eine DLL installiert einen eigenen Kernel-Treiber und erzeugt darüber
  Tastatur-/Mauseingaben auf Treiberebene; die AHK-Skripte nutzen das für Tastenspam und Inventar-Verschieben
  [gelesen, README/Skripte; gemessen, Exporte].
- **Merkmale [gemessen]:**
  - `ddx32.dll` (x86), `ddx64.32.dll` (x86-Host, x64-Treiber), `ddx64.64.dll` (x64): Exporte
    `DD_btn, DD_key, DD_mov, DD_movR, DD_whl, DD_str, DD_todc, DD_chk, DD_SnapPic, DD_PickColor, …`;
    `ddx32.dll` exportiert zusätzlich `DD_ReadMem/DD_WriteMem/DD_ReadProcessMem` (Speicherzugriff).
  - Eingebetteter Treiber als Ressource `RT_HTML/9001` (MZ, Subsystem native, Importe `ntoskrnl.exe`/`HAL.dll`):
    x86 843 240 Byte `4f5e2b54…1dcc`, x64 1 270 648 Byte `6c81e777…68b7` (in beiden 64-Bit-DLLs gleich).
  - Service-/Gerätenamen `dd71800x32`/`dd71800x64`, `\\.\dd71800x32`/`\\.\dd71800x64`,
    Anzeigename `Display_dd71800x64`, Event `ddevent`, Datei `dd71800x64.sys` (Ablage vermutlich `C:\`) [Ablageort vermutet].
  - Registry `\Registry\Machine\Software\DD XOFT\`; x86-Treiber zusätzlich `…\classes\{9B86BE0C-ABE2-4a9e-89D1-BDB0BFB851B6}`.
  - PDB `C:\dd71800x32\output\Drv_dll.pdb`, InternalName `Drv_dll.dll`, Versionsressource mit chinesischen VS-Platzhaltern.
  - Authenticode (DLLs und Treiber): Signer `CN=Wang Zhen, Chongqing, CN`, CA WoSign (inzwischen nicht mehr vertrauenswürdig).
  - Laut README braucht das Laden eine Netzverbindung [gelesen].
- **Ist es ein Kernel-Input-Treiber?** Ja [gemessen: native PE + Service-API-Importe `CreateServiceW/StartServiceW/DeviceIoControl`].
- **Erkennung:** `OG-DD-001..016` – Treiber/Service (`driver`), Hashes von DLL und Treiber,
  Export-Muster, PDB-String, Dateinamen `.sys`, Registry (nur `flag`, kann nach Deinstallation bleiben).
- **Clientseitig nicht erkennbar:** die erzeugten Eingaben tragen kein „injected“-Kennzeichen; der
  OdinGuard-Eingabefilter kann sie nicht vom echten Gerät unterscheiden [vermutet, typisch für
  Treiber-Eingaben]. Umbenannte/neu signierte Treiber nur über Verhalten → `behaviour.md` B2.

## RoMedic

- **Typ:** Autopot / Memory-Reader (.NET).
- **Wirkweise (hohe Ebene):** liest Werte aus dem Client-Prozess und löst über Fensternachrichten
  Item-Benutzung aus [vermutet aus P/Invoke-Satz; Logik ist obfuskiert].
- **Merkmale [gemessen]:** PE32 .NET 2.0 (WPF), obfuskiert mit **SmartAssembly 5.5.0.153**
  (Ressourcen und Benutzer-Strings verschlüsselt), P/Invoke `OpenProcess, ReadProcessMemory,
  WriteProcessMemory, FindWindow, PostMessageA`, `GetProcessesByName`; Versionsressource
  `RoMedic 1.0.0.0`, „Copyright © 2009“, unsigniert, 6 715 392 Byte.
- **Erkennung:** `OG-ROMED-001..004` (Hash, Prozessname, Fenstertitel [vermutet], GUID-String).
- **Clientseitig nicht erkennbar:** neu obfuskierte Builds mit anderem Namen → `behaviour.md` B1;
  Speicherzugriff von außen nur über Handle-Überwachung (Kernel-Komponente fehlt) [vermutet].

## RCX (gefunden in `WOERO/RCX/`)

RCX wurde **nur** in `WOERO/RCX/` gefunden (rekursive Suche, auch in allen Archiven) [gemessen]:
`RCX.exe`, `RCXDraw.exe`, `RCXDrawLauncher.exe`, `RCXDraw.dll`, `RCXJPN.dll`, `RCX.ini` (+ Varianten in
`EnglishSettingFilePack/` und `jRO_CustomFiles/`), `mobinfo.ini`, `mobinfo_preR.ini`, `RagMap.ini`,
`JobClass.ini`, `history.txt`, `skin/mobhp_*.bmp`, leere `SendCommands/*.txt`, `KillCount/*.log`.

- **Typ:** Paket-Mitleser + Overlay („Ragnarok Online Support Soft“, redchat 2004–2010).
- **Wirkweise (hohe Ebene):** eine Hook-DLL wird über einen globalen Fenster-Hook in den Client geladen,
  liest empfangene Pakete mit und zeigt daraus Zusatzinformationen (Monster-HP, Skill-Timer, EXP) an
  [gelesen, `RCX.ini`: `APIHookType`, `PacketLog_WS2.txt`, `[MOBInfo]`, `[SkillTimer]`; gemessen: Exporte].
- **Merkmale [gemessen]:**
  - `RCX.exe` 0.0.1.8 und `RCXDraw.dll`: **MPRESS**-gepackt (Sektionen `.MPRESS1/.MPRESS2`).
  - `RCXDraw.dll` exportiert `HookProc, InstallHook, RemoveHook`.
  - `RCXDraw.exe`/`RCXDrawLauncher.exe` (FileDescription „RCXDraw Launcher“, © 2009 redchat) laden
    `RCXDraw.dll`, rufen `InstallHook` und verwenden den String `RCXDraw` für `RegisterClassExA`,
    `CreateWindowExA`, `CreateMutexA`, `FindWindowA`; außerdem `FindWindowA("Ragnarok")`.
  - `RCXJPN.dll`: reine Ressourcen-DLL (Dialoge „RCX - GPS“, „RO-GPS“, „RCX.Document“).
  - `RCX.ini` enthält Client-Pfade, MD5/CRC32 des Fremd-Clients und Server-Adressen (GPS-Dienst
    `home.planetleaf.com:19802`) [gelesen].
- **Erkennung:** stärkstes Merkmal ist das Modul **`rcxdraw.dll` im Client-Prozess** (`OG-RCX-004`), dazu
  Prozesse, Fensterklasse `RCXDraw`, Hashes, Versionsstring (`OG-RCX-001..018`).
- **Clientseitig nicht erkennbar:** ein Mitleser außerhalb des Prozesses (z. B. Netzwerk-Mitschnitt)
  hinterlässt im Client keine Spur; serverseitig nur durch Informationsminimierung begrenzbar (`behaviour.md` B5).

## RoExt (`RoExt/dinput.dll`) – Vergleich mit dem legitimen ROExt

- **Typ:** Eingabe-/Fenster-Erweiterung als dinput-Proxy (Mausfreiheit, Tasten-Umbelegung, Fenstergröße,
  CPU-Drosselung für Mehrfach-Clients) [gelesen, readme]. Kein Speicher-Cheat, aber eine nicht freigegebene
  DLL im Eingabepfad des Clients.

| Merkmal | Legit `ijl15r.dll` (Dev-Client) | Cheat-Ordner `RoExt/dinput.dll` |
|---|---|---|
| SHA-256 | `4d002638…ea72` | `3d8d3f03…e878` |
| Größe | 9 728 Byte | 10 784 Byte |
| Proxy-Rolle | ijl15-Proxy | dinput-Proxy |
| Exporte | `ijlInit/ijlFree/ijlRead/ijlWrite` → Forwarder `ijl15x.*`, `compress/uncompress` → `cpsx.*` | nur `DirectInputCreateA`, Exportname `ROExt.dll` |
| FileVersion | 2026.01.18.0 | 2010.2.17.0 |
| FileDescription / Copyright | „Ragnarok Extension“ / „Ruri“ | gleich |
| PE-Zeitstempel / ImageBase | 2026-01-18 / 0x10000000 | 2010-02-17 / 0x003F0000 |
| Importe | KERNEL32, USER32, DDRAW, WINMM (u. a. Clipboard-APIs, `timeGetTime`) | KERNEL32, USER32, ntdll, DDRAW (lädt System-`dinput.dll` nach, Strings `GetPhysicalMonitors`, `DdEntry38/56`) |
| gelesene ini-Schlüssel | `MinimizeKey, RemapMouseButtons, RemapAltF4, WindowOnTop, WindowLock, WindowPos*, AutoFreeCPU, DisableVSync, DisableFadeInOut, CodePage` | `MouseFreedom, RemapMouseButtons, RemapAltF4, KeyRemap, AutoFreeCPU, WindowOnTop, WindowLock, WindowPos*, CodePage` |

Alle Werte [gemessen]. Unterschied in Kurzform: andere Proxy-Rolle (dinput statt ijl15), eigene
DirectInput-Emulation, zusätzliche Schlüssel `MouseFreedom` und `KeyRemap`, alter Build. Die legitime
`dinput.ini` des Dev-Clients enthält die Schlüssel `MouseFreedom`/`KeyRemap` ebenfalls (Altlast), daher
sind ini-Schlüssel **kein** Unterscheidungsmerkmal – nur Hash, Dateiname der DLL und Export-Form.

- **Erkennung:** `OG-ROEXT-001` (Hash), `OG-ROEXT-002` (jede ROExt-Version als `dinput.dll` mit Export
  `DirectInputCreateA`), ini-Hashes nur `log`. Allowlist: `OG-ALLOW-001` + `OG-ALLOW-RULE-001`.
- **Wichtig:** `dinput.dll` als Name allein ist nie eine Signatur (DxWrapper-Stub unterstützt ebenfalls `dinput.dll`).

## Cheat-GRFs

Alle Werte [gemessen] mit `rgrf hash` gegen die Referenzen. „verändert“ = Pfad existiert in einer
Referenz mit anderem Inhalt, „neu“ = Pfad in keiner Referenz, „unlesbar“ = Eintrag nicht dekodierbar.

| GRF | SHA-256 (gekürzt) | Einträge | verändert | neu | identisch | unlesbar | Hauptinhalte |
|---|---|---|---|---|---|---|---|
| nodelay.grf | `0dba7a75…8f11` | 63 | 44 | 0 | 12 | 7 | 29 Body-`.act` der 2./Trans-Klassen: je 8–40 Aktionen (u. a. Angriff, Zaubern, Aufheben, Treffer [Zuordnung vermutet]) von 3–9 auf **1 Frame** gekürzt, Intervall unverändert |
| Thoth.grf | `25ca384a…d6d4` | 491 | 370 | 56 | 49 | 16 | 36 WoE-Burgkarten (`.gat/.gnd/.rsw`), Minimaps, MVP-`.act`, Effekt-`.str`, Lied-`.wav`, Marker `Compiled by Thoth aka Luned.txt` |
| woe.grf | `ce6623a1…0af5` | 341 | 242 | 55 | 44 | 0 | Minimaps, Effekt-Texturen (Devotion, Fußspuren), Status-Sprites, Lieder, Ladebilder |
| woebg.grf | `cca1d37f…35dd` | 285 | 215 | 28 | 42 | 0 | wie woe.grf, Marker `credits.txt` |
| dark.grf | `f7baefcd…06ae` | 3316 | 2571 | 688 | 56 | 1 | ~816 Karten verändert/neu, 117 Texturen auf 1×1 px (Bäume/Pflanzen), Raster-Texturen `data\texture\c*.bmp`, `points`, `woe` |
| dark2.grf | `045c71de…a748` | 3689 | 2891 | 734 | 64 | 0 | dark.grf + 127 Effekt-Texturen 1×1 px, 16 leere `.str` |
| desu.grf | `f8e44f94…ba06` | 1686 | 682 | 461 | 60 | 483 | 131 Texturen 1×1 px, Effekt-`.str`, vergrößerte Monster-`.act`, Raster-Texturen, Ordner `data\texture\desu` |
| woerol.grf (WOERO) | `5c59071c…df76` | 739 | 665 | 37 | 37 | 0 | 181 Texturen 1×1 px (137 Effekte, 44 Pflanzen/Bäume), 18 leere `.str`, Wasser-Texturen |

Ergänzend [gemessen]: Die 1×1-Ersatztexturen sind voll transparent (Pixel `0xFF00FF`), leere `.str`
haben 0 Ebenen. Gleiche Inhalte tauchen in mehreren GRFs auf → Inhaltsklassen-Regeln `OG-GRF-BLANK-001..005`
(`*|md5`, beliebiger Pfad). desu.grf hat 284 Einträge mit ungültigem zlib und 198 mit falscher Größe;
nodelay/Thoth enthalten Verzeichnis-Pseudoeinträge (Artefakt des Pack-Werkzeugs).
dark/dark2 werden separat auf Fairness bewertet; hier nur Hash-/Pfad-Signaturen.
`woero.grf`/`woerop.grf` sind Daten des Fremd-Servers (Paletten, NPC-Sprites) und keine Cheats (`log`).

**Erkennung:** `grf_sha256` für jede Cheat-GRF (high), `grf_entry` für charakteristische Einträge
(nodelay alle 29 `.act` = high/kick; übrige medium/flag), vollständige Liste in `grf_entries.tsv`.
Robuster als Einzel-Hashes sind strukturelle Prüfungen der tatsächlich geladenen Ressourcen [vermutet]:
Body-`.act` mit weniger Frames als die Referenz, Texturen mit 1×1 px, wo die Referenz größer ist,
`.str` ohne Ebenen.

**Clientseitig nicht erkennbar / Restlücke:** loses `data\`-Verzeichnis oder neu gepackte GRFs mit
leicht veränderten Inhalten umgehen Hash-Listen [vermutet]; deshalb sollte OdinGuard die geladenen
Dateien gegen die eigenen Server-GRFs prüfen (Positivliste statt Negativliste). Sichtvorteile
(transparente Bäume, veränderte Karten) erzeugen kein messbares Serververhalten.

## WOERO-Paket (sonstiges)

`woero.exe` (Client eines anderen Servers, 2010, Sektion `.Heler`), `AI/` (Homunkulus-AI-Skripte +
`lua.exe` 5.0, GPLv3) und `System/*.lub` sind keine Cheats [gelesen/gemessen]; nur Hashes in `hashes.txt`,
`woero.exe` als `log`-Signatur. Die AI-Ordner sind per `OG-ALLOW-RULE-005` ausgenommen.

## Umsetzungshinweise

- Wire-Kinds laut `proto/og_proto.h`: `process`=1, `window_title`=2, `module`=3, `window_class`=7,
  `file_name`=8, `file_sha256`=9, `driver`=10, `grf_sha256`=11, Allowlist-Hash=64.
  **Fehlen noch:** `grf_entry`, `import_pattern`, `string_pattern`, `registry` – brauchen eine Client-Erweiterung.
- `og_signature` in `odinguard_athena.conf` kennt nur `process/window/module` mit Teilstring-Treffer;
  generische Teilstrings wie `rcx` oder `dd` würden Fehlalarme erzeugen → exakte Namen aus
  `signatures.toml` verwenden.
- Prozess- und Fenster-Signaturen anderer Prozesse brauchen eine systemweite Aufzählung (Toolhelp/EnumWindows);
  DLL-Signaturen von Class_DD/Autopot liegen in **Hilfsprozessen**, nicht im Client.

## Restlücken (gesamt)

1. Treiber-Eingaben (Class_DD) sind clientseitig nicht als synthetisch erkennbar → Timing-Analyse auf dem Server.
2. Umbenannte/neu gebaute Autopots und Makros → Reaktionszeit- und Intervall-Analyse (`behaviour.md` B1/B2).
3. Passives Mitlesen von Paketen außerhalb des Prozesses → Informationsminimierung (B5).
4. Sicht-GRFs mit neuen Inhalten → Positivliste der eigenen GRFs/Ressourcen statt Hash-Negativliste.
5. Nodelay-Wirkung ist serverseitig begrenzt (Server erzwingt Delays), Erkennung über Ablehnungsraten (B3).
