# Cheat-Inventar und Erkennungsmerkmale

Stand: 2026-11-23, erweitert 2026-11-24 (zweite Runde: tiefere statische Analyse, öffentlich bekannte Werkzeuge,
Migration der `og_signature`-Zeilen). Zweck: Erkennungs- und Abwehrsignaturen für OdinGuard (Client 2018-06-20, 32 Bit).
Methode: ausschließlich statische Analyse. Kein Programm wurde ausgeführt (auch nicht in Wine),
keine Binärdatei wurde kopiert oder entpackt auf Platte geschrieben. Archiv-Einträge wurden mit
`bsdtar -xO` gestreamt und nur gehasht, eingebettete Treiber nur im Speicher gehasht.
Runde 2 (2026-11-24): zusätzlich statische Disassemblierung (Capstone, String-Querverweise auf API-Aufrufe),
statisches Dekodieren der SmartAssembly-Ressourcen von RoMedic (DES + SimpleZip, Schlüssel aus den
FieldRVA-Daten) und des eingebetteten AHK-Skripts von `autopot.exe`. Die dekodierten Zwischenstände lagen nur
im git-ignorierten Arbeitsordner `analysis/_work/cheatre/` und wurden danach gelöscht; nichts davon ist im Repo.
Öffentlich bekannte Werkzeuge, die nicht im Ordner liegen (OpenKore, WPE, Cheat Engine …), stammen
ausschließlich aus öffentlichem Wissen – es wurden keine Binärdateien heruntergeladen.

Kennzeichnung der Aussagen:

- **[gemessen]** – direkt mit Werkzeug ermittelt (SHA-256/MD5, PE-Header, Importe/Exporte, Ressourcen,
  Authenticode, GRF-Inhalte, ACT-Framezahlen, Texturgrößen).
- **[gelesen]** – aus Klartext gelesen (Skripte, ini, readme, Edit-Log, Versionsressource).
- **[vermutet]** – Schlussfolgerung, nicht direkt belegt.

Werkzeuge: `shasum`, `i686-w64-mingw32-objdump -p`, Python `pefile`/`dnfile`/`capstone`/eigene String-Extraktion,
REA `inspect_pe_resources`, RustyGRF `rgrf` (`info`, `hash`, `diff`, `act info`, `cat` nach stdout).
Referenzen für „unverändert“: `data kro latest.grf`, `official_data (chris).grf` und die GRFs des
Dev-Clients (`data.grf`, `core.grf`, `cdata.grf`, `cpal.grf`).

Dateien in diesem Ordner:

| Datei | Inhalt |
|---|---|
| `signatures.toml` | 300 Signaturen (Schema 1; 170 aus Runde 1, 130 aus Runde 2), Konventionen im Kopf der Datei |
| `allowlist.toml` | 37 Einträge: SHA-256 der legitimen Client-Dateien + generische Regeln (inkl. Fehlalarm-Schutz Runde 2) |
| `behaviour.md` | Serverseitige Verhaltenssignaturen mit Schwellwerten |
| `hashes.txt` | SHA-256 aller untersuchten Dateien (auch Archiv-Einträge, eingebettete Treiber, Legit-Client) |
| `grf_entries.tsv` | Ergänzung: alle 10 246 abweichenden oder unlesbaren GRF-Einträge (Pfad, MD5, CRC32, Größe) |

Generatoren (nicht versioniert, `analysis/_work/` ist git-ignoriert): `analysis/_work/cheatsig/gen.py`,
`analysis/_work/cheatre/gen_add.py` (+ `validate.py`, `xr.py` für String→API-Querverweise).
`signatures/signatures.toml` und `signatures/allowlist.toml` sind byte-identische Kopien dieser Dateien.

## Übersicht

| Tool | Typ | Angriffsfläche | Greifende OdinGuard-Erkennung | Restlücke → Server |
|---|---|---|---|---|
| Autopot Fresh | Autopot, Memory-Reader (AutoHotkey) | Client-Prozessspeicher (lesen), Fensternachrichten | Datei-Hash, Prozess, AHK-Fensterklasse/-Ressource, Skript-Strings, AHK-Prozesse (log) | Umbenannte/neu kompilierte Varianten → Reaktionszeit-Analyse |
| Class_DD | Makro/Input-Treiber (Kernel) | Eingabe auf Treiberebene; x86-DLL zusätzlich Speicher-Lesen/Schreiben | Treiber/Service, Service-Registry, Event `ddevent`, Datei-Hash (DLL + Treiber + Skripte), Exporte, PDB-/Export-Namen | Eingaben nicht als „injected“ markiert → Intervall-Analyse |
| RoMedic | Autopot, Memory-Reader (.NET) | Client-Prozessspeicher (lesen/schreiben), Fensternachrichten | Datei-Hash, Prozess, Fenstertitel (jetzt gemessen), Typ-/Ressourcen-Strings | Neu obfuskierte Builds → Reaktionszeit-Analyse |
| RCX (in WOERO/RCX) | Paket-Mitleser + Overlay | Hook-DLL im Client-Prozess, liest empfangene Pakete (ws2_32 `recv`) | Modul `rcxdraw.dll`, Mutex `RCXDraw`, Prozesse, Fensterklassen `RCXDraw`/`RCX`, Hashes, Versions-/PDB-Strings | Reines Mitlesen erzeugt kein Serververhalten; Info-Minimierung serverseitig |
| RoExt (dinput.dll) | Eingabe-/Fenster-Erweiterung (dinput-Proxy) | Ersetzt DirectInput des Clients | Datei-Hash, Export-/Versions-Muster | – (statisch eindeutig) |
| nodelay.grf | Datei-Modifikation (Animation) | Client-Ressourcen | GRF-Hash, 29 `.act`-Einträge | Aktionsraten gegen Server-Delays |
| Thoth/woe/woebg/desu/dark/dark2/woerol.grf | Datei-Modifikation (Sicht/Effekte) | Client-Ressourcen | GRF-Hash, Eintrags-Hashes, Inhaltsklassen (1×1-Texturen, leere `.str`) | Sichtvorteil nicht serverseitig messbar |
| WOERO (woero.exe, woero.grf, woerop.grf) | Fremd-Client eines anderen Servers | – | nur `log` | – |
| OpenKore / xKore (öffentlich) | Bot (Perl), xKore 1 mit Hook-DLL im Client | außerhalb des Clients; xKore 1: `send`/`recv`-Umleitung im Client | Prozesse, Fenstertitel (flag), Modul `netredirect.dll` | xKore 0/2/3 laufen ohne Client → nur serverseitig (B8) |
| WPE PRO, rPE (öffentlich) | Paket-Editoren | Hook-DLL im Client (`send`/`recv`) | Prozess, Titel, Modul `wpespy.dll` | umbenannte Builds → Protokoll-Prüfung (B4) |
| Cheat Engine, ArtMoney, ReClass (öffentlich) | Speicher-Scanner/-Editoren, Speedhack | Client-Speicher von außen; CE injiziert Hilfs-DLLs | Prozesse (flag), CE-Module im Client (kick), Treiber/Registry | externes Lesen ohne Injektion nur über Handle-Überwachung |
| Speedhack-Tools (öffentlich) | Zeitfunktions-Hooks | `timeGetTime`/`GetTickCount`/`QueryPerformanceCounter` | Prozesse, CE-`speedhack-i386.dll` | Zeitdrift gegen Server (B4.5) |
| Makro-Tools, AutoHotkey/AutoIt (öffentlich) | Eingabe-Automatisierung | synthetische Eingaben | Prozesse/Fensterklassen (log/flag) | Hardware-/Treiber-Makros nur serverseitig (B2) |
| Debugger, Injektoren (öffentlich) | Analyse/Injektion | Client-Prozess | Prozesse/Klassen (log), ScyllaHide-Modul, Injektor-Prozesse (flag) | manuell gemappter Code ohne Modul → Speicher-Integrität |

## Signaturen nach Art und Konfidenz [gemessen]

Stand nach Runde 2 (2026-11-24); in Klammern der Stand nach Runde 1.

| kind | high | medium | low | gesamt | Wire-Kind |
|---|---|---|---|---|---|
| grf_entry | 34 | 72 | 0 | 106 (106) | – (Analyse) |
| process | 3 | 41 | 14 | 58 (5) | 1 |
| string_pattern | 17 | 7 | 3 | 27 (7) | – (Analyse) |
| file_sha256 | 21 | 0 | 0 | 21 (19) | 9 |
| file_name | 2 | 12 | 6 | 20 (5) | 8 |
| window_title | 0 | 12 | 6 | 18 (4) | 2 |
| module | 8 | 5 | 0 | 13 (5) | 3 |
| grf_sha256 | 10 | 0 | 0 | 10 (10) | 11 |
| window_class | 1 | 5 | 2 | 8 (3) | 7 |
| driver | 2 | 3 | 2 | 7 (2) | 10 |
| registry | 2 | 5 | 0 | 7 (1) | 17 |
| import_pattern | 2 | 0 | 1 | 3 (3) | – (Analyse) |
| mutex | 1 | 1 | 0 | 2 (0) | 16 |
| **Summe** | **103** | **163** | **34** | **300 (170)** | 164 wire-fähig (54) |

Nach Gruppe (Runde 1 → Runde 2): GRF 114 → 114, RCX 18 → 26, DD 16 → 31, APOT 6 → 8, AHK 5 → 13,
ROMED 4 → 10, ROEXT 4 → 4, WOERO 3 → 3; neu: DEBUG 22, CE 14, MACRO 10, ROMISC 9, INJECT 9, OPENKORE 7,
GRFNAME 6, WPE 4, SPEED 4, RPE 2, ARTMONEY 2, THOTH 1, DESU 1.
Aktionen der 130 neuen Einträge: 29 kick, 60 flag, 41 log, kein `ban`/`flagban`.

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
- **Runde 2 [gemessen]:** Das Skript-RCDATA (2047 Byte, Klartext, erste Zeile `; <COMPILER: v1.1.26.01>`)
  wurde statisch aus der Ressource gelesen; inhaltlich identisch mit `autopot.ahk` (nur Einrückung/Leerzeilen
  entfernt). Ziel-Fenster: aktives Fenster, dessen Titel „Ragnaclan“ enthält; Ziel-Prozess = PID dieses Fensters,
  `OpenProcess(0x1F0FFF)` (Vollzugriff), `ReadProcessMemory` auf feste Adressen (HP, Max-HP, SP, Max-SP, Status),
  Tasten per **`ControlSend`** (vom Tool *gepostete* `WM_KEYDOWN/WM_KEYUP`, keine `SendInput`-Eingabe),
  Hotkey F1 (bei AHK üblicherweise per `RegisterHotKey` [vermutet]), Meldungen „Ragnapot running./stopped.“. Kein Mutex, keine Registry,
  einzige Datei `ragnapot.ini` im Arbeitsordner. Das versteckte AHK-Hauptfenster eines kompilierten Skripts
  heißt nach dem vollen Exe-Pfad [vermutet, AHK-Verhalten] → `OG-APOT-008`.
- **Neue Signaturen:** `OG-APOT-007/008`, `OG-AHK-006` (Compiler-Kopfzeile), `OG-AHK-007..013`
  (AHK-Interpreter-Prozesse und Installer-Schlüssel, nur `log`).
- **Clientseitig zusätzlich möglich [vermutet]:** Tastennachrichten, die im Fenster-Thread des Clients ankommen,
  ohne dass vorher Raw-Input/Low-Level-Eingabe stattfand (gepostete Nachrichten), sind ein generisches Merkmal
  von `ControlSend`/`PostMessage`-Autopots (auch RoMedic) – unabhängig vom Werkzeugnamen.

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
- **Runde 2 [gemessen, Disassemblierung]:** Installationsablauf der DLL: Treiber-Ressource → Datei
  **`C:\dd71800x32.sys`** bzw. **`C:\dd71800x64.sys`** → `CreateServiceW(dd71800x32|64, Display_dd71800x32|64,
  SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START)` → `StartServiceW`; Gegenstück `ControlService(STOP)` +
  `DeleteService` beim Entladen (Service-Schlüssel kann danach fehlen, die `.sys` bleibt). Treiber: Gerät
  `\Device\dd71800x64` + `\DosDevices\dd71800x64` mit DACL `D:P(A;;GA;;;WD)` (jeder darf zugreifen),
  sucht Klassentreiber über `\Driver\…` (`ObReferenceObjectByName`) und ruft den **kbdclass-Service-Callback**
  direkt auf (Debug-String `keyclsSCallback_ZJMS`) – Eingaben entstehen unterhalb von Win32k, deshalb kein
  `LLKHF_INJECTED`. x86-Treiber zusätzlich `KeStackAttachProcess`/`MmIsAddressValid`/`ProbeForRead`
  (Speicher anderer Prozesse lesen/schreiben = `DD_ReadMem/DD_WriteMem`) und TDI-Verbindung über
  `\Device\Tcp` (Online-Prüfung beim Laden, passt zum README). Treiber-PDBs
  `c:\dd7180~1\drv\objfre_win7_x86\i386\dd71800x32.pdb`, `c:\dd7180~2\drv\objfre_win7_amd64\amd64\dd71800x64.pdb`.
  `DD_SnapPic` legt Bilder in **`C:\DD Snap\`** ab. String `ddevent` steht neben den Statusmeldungen
  (Nutzung als Event-Name vermutet). Export-Name der DLLs: `dd71800x32.dll` / `dd71800x64.dll` (bleibt bei Umbenennung).
- **Neue Signaturen:** `OG-DD-017..031` (Service-Registry, Klassen-GUID-Schlüssel, Event `ddevent`,
  PDB-/Export-/Display-Strings, `keyclsSCallback_ZJMS`, `C:\DD Snap\`, Skript-Hashes, `class_dd.ahk`).

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
- **Runde 2 [gemessen]:** Die SmartAssembly-Strings liegen als Base64-Liste in der Ressource
  `{eb872c90-…}`; die UI-Assembly `{532f8f66-…}` (6,7 MB) und `WPFDeveloperTools.Controls` `{5511e926-…}`
  sind DES-verschlüsselt + SimpleZip-komprimiert (Schlüssel/IV als statische Felddaten im Image) und wurden
  statisch dekodiert. Ergebnisse:
  - Hauptfenster-Titel **„RoMedic“** (aus `mainwindow.baml`; vorher nur vermutet), zweites Fenster „Server List“,
    WPF-Fensterklasse `HwndWrapper[RoMedic.exe;;<GUID>]` (kein exakter Klassenname möglich).
  - **Zielauswahl:** eine editierbare Server-Liste (Prozess-/Fensternamen), Standard: VisionRo, „Awesome GvG
    Server“/AWSM, ToMREv5, IcariaRo, GwRo, RuneRo, „A N S R O“, `ro.bin`; Status „Awaiting for Client“.
    Unser Client kann einfach ergänzt werden – ein eigener Exe-Name schützt nicht.
  - **Mechanismus:** sechs Byte-Muster-Suchen über den Client-Speicher (`VirtualQueryEx` + `ReadProcessMemory`)
    nach den Werten HpIndex, HpMassive, HpWithoutDef, Logged, CurrentMap, Status; Reaktion über
    `PostMessageA` (gepostete Tasten), außerdem `WriteProcessMemory` importiert; Status-Heilung für Fluch/Stille.
    Musterbytes werden bewusst nicht dokumentiert.
  - Einstellungen über .NET-`Settings1` (Datei `user.config` unter `%LOCALAPPDATA%\RoMedic\…` [vermutet]),
    kein Mutex, keine Registry gefunden.
- **Neue Signaturen:** `OG-ROMED-005..010`; `OG-ROMED-003` (Titel) ist jetzt belegt (Note aktualisiert).
  `utf16:`-Muster `OG-ROMED-008/009` stehen nur im **Prozessspeicher** von RoMedic im Klartext.

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
- **Runde 2 [gemessen, Disassemblierung von `RCXDraw.exe`/`RCXDrawLauncher.exe`]:**
  `CreateMutexA(NULL, TRUE, "RCXDraw")` (Einzelinstanz), `RegisterClassExA`/`CreateWindowExA` mit Klasse und
  Titel `RCXDraw`, `LoadLibraryA("RCXDraw.dll")` → `InstallHook` (globaler Fenster-Hook; so landet die DLL im
  Client), Timer 1000 ms: `FindWindowA("Ragnarok")` (Fensterklasse des RO-Clients) und `FindWindowA("RCX")`
  (Hauptfenster von RCX) – fehlen beide, `RemoveHook` und Ende.
  `RCX.ini` [gelesen]: Hook-Ziel ist ws2_32 **`recv`** (`APIHookType` 0 = Adresssuche in ws2_32, 1 = Patch des
  Import-Eintrags im Client), `Recv32HookCount`, `ROWindowClass` (anpassbar!), Paketlog `log\PacketLog_WS2.txt`,
  GPS-Server `home.planetleaf.com:19802`, `AccountCollector` (verschlüsselt gespeicherte Login-Daten, automatische
  Eingabe), `SendCommands\*.txt` (beim Login automatisch gesendete Chat-Befehle), ROReplay (`*.ror`) laut
  `history.txt`. Aus dem gepackten `RCXDraw.dll` sind nur Fragmente lesbar („VirtualProtect“, „OriginalFirstThunk“,
  „Password“, „…ft\Ragna…“) → IAT-Patch und Lesen der Gravity-Registry [vermutet].
  `RCXJPN.dll`-PDB `D:\Projects\RO\RCX_Build\Release\RCXJPN.pdb`.
- **Neue Signaturen:** `OG-RCX-019..026` (Mutex, Fensterklasse `RCX`, PDB, „RCXDraw Launcher“, Paketlog,
  GPS-Host, `ragmap.ini`, `PacketRecv2RCX`).
- **Clientseitig zusätzlich möglich [vermutet]:** Integritätsprüfung des Client-Importeintrags und des
  Funktionsanfangs von ws2_32 `recv`/`send`/`WSARecv` erkennt RCX, WPE, rPE und xKore 1 unabhängig vom Namen
  (passt zu `OG_KIND_API_HOOK`).

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
- **Runde 2 [gemessen]:** lädt die System-`dinput.dll` über `GetSystemDirectoryW` + `\dinput.dll` nach, liest
  `.\dinput.ini`, patcht Code im eigenen Prozess (`VirtualProtect` + `NtFlushInstructionCache`) und bezieht
  `gdi32 GetPhysicalMonitors` sowie `ddraw DdEntry38/DdEntry56` (Fenster-/Anzeige-Hooks für Fenstermodus und
  Mausfreiheit [vermutet]). Kein Mutex, keine Registry, keine Fensterklasse → keine neuen Signaturen.

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

**Runde 2:** zusätzlich die verbreiteten Dateinamen als `file_name` (`OG-THOTH-001` `thoth.grf`,
`OG-DESU-001` `desu.grf`, `OG-GRFNAME-001..006` `nodelay.grf`, `dark.grf`, `dark2.grf`, `woerol.grf`,
`woebg.grf`, `woe.grf`; generische Namen nur `log`). Name allein ist schwach – die Hash-/Eintragsregeln entscheiden.

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
Runde 2 [gemessen]: `woero.exe` enthält einen eigenen Anti-Cheat-Rest des Fremd-Servers (Strings `FindHack`,
`findhack.exe`, `BAN_DETECTED_SPEEDHACK`, `NPX.DLL`), liest `.\woe.ini` und `Software\Gravity Soft\Ragnarok`;
Sektion `.Heler` (925 Byte) ist vermutlich ein nachträglich angehängter Patch [vermutet]. Keine neuen Signaturen.

## Öffentlich bekannte Werkzeuge (nicht im Ordner, nur öffentliches Wissen)

Keine Probe vorhanden, keine Binärdatei geladen. Namen stammen aus öffentlicher Dokumentation/Verbreitung;
daher Konfidenz höchstens `medium` (umbenennbar) und `low`, wo der Exe-Name nur vermutet ist. Debugger,
Systemwerkzeuge und allgemeine Makro-Interpreter sind **nie** `kick`/`ban` (`OG-ALLOW-RULE-010`).

| Gruppe | Werkzeug | Mechanismus (hohe Ebene) | Signaturen | Aktion |
|---|---|---|---|---|
| OPENKORE | OpenKore / xKore | Perl-Bot; xKore 0 = eigener Client-Ersatz, xKore 1 = Hook-DLL `NetRedirect.dll` im Client leitet `send`/`recv` zum Bot, xKore 2/3 = lokaler Proxy zwischen Client und Server | Prozesse `openkore.exe`, `wxstart.exe`, `vxstart.exe`; Titel „OpenKore“; Modul/Datei `netredirect.dll`; `openkore.pl` | Modul kick, Prozesse kick/flag, Titel flag |
| WPE | WPE PRO | injiziert `WpeSpy.dll`, hookt ws2_32 `send/recv/sendto/recvfrom`, Filter/Replay | `wpepro.exe`, `wpe pro.exe`, Titel „WPE PRO“, Modul `wpespy.dll` | Modul kick |
| RPE | rPE (Redox Packet Editor) | Paket-Editor über Hook-DLL | `rpe.exe`, Titel „Redox Packet Editor“ | kick/flag |
| CE | Cheat Engine | externer Speicher-Scan/-Schreibzugriff; injiziert bei Bedarf `speedhack-i386.dll` (Zeit-APIs), `vehdebug-i386.dll`, `allochook-i386.dll`, `luaclient-i386.dll`, `winhook-i386.dll`; optional Kernel-Treiber DBK | 4 Prozessnamen, Titel, 5 Module, `dbk32/64.sys`, Dienst `CEDRIVER73` [vermutet], `HKCU\Software\Cheat Engine` | Module kick, Rest flag/log |
| ARTMONEY | ArtMoney | externer Speicher-Scan/-Schreibzugriff | `artmoney.exe`, Titel | flag |
| SPEED | SpeedGear, Speeder | systemweite Hooks der Zeitfunktionen | `speedgear.exe`, `speeder.exe` [vermutet], Titel | flag/log |
| MACRO | AutoIt, TinyTask, Pulover's Macro Creator, Jitbit Macro Recorder, Mini Mouse Macro | synthetische Eingaben (`SendInput`/Nachrichten) | Prozesse, Fensterklassen `AutoIt v3`/`AutoIt v3 GUI`, Titel, AutoIt-Installer-Schlüssel | log/flag |
| ROMISC | PotND, meth4u, xRag, xLike, RO Tools | RO-spezifische Autopots/Bots (aus der alten conf-Liste) | Prozesse (Exe-Namen vermutet), Titel | Prozess kick (wie bisher), Titel flag, „RO Tools“ nur log |
| DEBUG | x64dbg/x32dbg, OllyDbg, Immunity, IDA, WinDbg, Scylla, Process Hacker/System Informer, ReClass.NET, ScyllaHide, TitanHide | Debugging/Speicheranalyse | 15 Prozesse, Klassen `OLLYDBG`/`WinDbgFrameClass`, Modul `hooklibraryx86.dll` (im Client = wird debuggt), Treiber `TitanHide`, `KProcessHacker2/3`, `KSystemInformer` | log, ScyllaHide/TitanHide/ReClass flag |
| INJECT | Extreme Injector, Xenos, GH Injector, Winject, RemoteDLL, kdmapper | DLL-Injektion (`CreateRemoteThread`+`LoadLibrary`, Thread-Hijack, Manual Map) bzw. Laden unsignierter Treiber | Prozesse, Titel | flag |

**Clientseitig nicht sichtbar:** manuell gemappte DLLs (Xenos/GH Injector „manual map“) erscheinen in keiner
Modulliste → nur Speicher-Integrität (ausführbare private Seiten, Hooks in ws2_32/Zeit-APIs) hilft;
xKore 2/3 und reines OpenKore laufen ganz ohne Client → nur serverseitig (`behaviour.md` B4, B8);
Hardware-/Treiber-Makros (Gaming-Peripherie, DD) → nur Timing (B2).

## Migration der `og_signature`-Zeilen (xAthena-HR `conf/odinguard_athena.conf`)

Alte Regeln waren Teilstring-Treffer; neu exakte Namen. Fenstertitel-Regeln wurden von `kick` auf `flag`
gesenkt, weil Browser-Tabs/Foren-Seiten sonst Fehlalarme erzeugen (`OG-ALLOW-RULE-007`).

| conf-Zeile | neue Signatur(en) |
|---|---|
| `process,kick,openkore` | `OG-OPENKORE-001` (+ `-002`, `-003`, Modul `-005`) |
| `window,kick,OpenKore` | `OG-OPENKORE-004` (flag) |
| `process,kick,wpepro` | `OG-WPE-001` (+ `-004`) |
| `window,kick,WPE PRO` | `OG-WPE-002` (flag) |
| `module,kick,wpespy` | `OG-WPE-003` (`wpespy.dll`, kick) |
| `process,kick,rpe.exe` | `OG-RPE-001` |
| `window,kick,Redox Packet Editor` | `OG-RPE-002` (flag) |
| `process,kick,potnd` / `window,kick,PotND` | `OG-ROMISC-001` / `-002` (flag) |
| `process,kick,meth4u` | `OG-ROMISC-003` |
| `process,kick,xrag` / `window,kick,xRag` | `OG-ROMISC-004` / `-005` (flag) |
| `process,kick,xlike` / `window,kick,xLike` | `OG-ROMISC-006` / `-007` (flag) |
| `process,kick,rotools` / `window,kick,RO Tools` | `OG-ROMISC-008` / `-009` (**log**: trifft „Pro Tools“) |
| `process,kick,cheatengine` | `OG-CE-001..004` (flag; Prozess allein beweist keine Nutzung gegen den Client) |
| `window,kick,Cheat Engine` | `OG-CE-005` (flag) |
| `module,kick,speedhack-i386` | `OG-CE-006` (`speedhack-i386.dll`, kick) |
| `module,kick,vehdebug-i386` | `OG-CE-007` (`vehdebug-i386.dll`, kick) |
| `process,log,x32dbg` | `OG-DEBUG-001` (log) |
| `process,log,ollydbg` | `OG-DEBUG-004` (log) |

Verlust durch exakte Namen: Varianten wie `openkore_2.exe` oder `cheatengine-x86_64-SSE4-AVX2.exe` mit
anderen Suffixen treffen nicht mehr automatisch; die bekannten Release-Namen sind einzeln aufgeführt.

## Abdeckung

**Gruppen und Anzahl (Runde 1 → Runde 2):** GRF 114 → 114 · RCX 18 → 26 · DD 16 → 31 · APOT 6 → 8 ·
AHK 5 → 13 · ROMED 4 → 10 · ROEXT 4 → 4 · WOERO 3 → 3 · neu: DEBUG 22 · CE 14 · MACRO 10 · ROMISC 9 ·
INJECT 9 · OPENKORE 7 · GRFNAME 6 · WPE 4 · SPEED 4 · RPE 2 · ARTMONEY 2 · THOTH 1 · DESU 1.
**Gesamt 170 → 300**, davon wire-fähig (vom Client prüfbar) 54 → 164.

**Nach Art (vorher → nachher):** process 5 → 58 · window_title 4 → 18 · window_class 3 → 8 · module 5 → 13 ·
file_name 5 → 20 · file_sha256 19 → 21 · driver 2 → 7 · registry 1 → 7 · mutex 0 → 2 · grf_sha256 10 → 10 ·
grf_entry 106 → 106 · import_pattern 3 → 3 · string_pattern 7 → 27.

**Am stärksten (wenig Fehlalarm, hohe Konfidenz):** Hook-/Cheat-Module **im Client-Prozess**
(`rcxdraw.dll`, `wpespy.dll`, `netredirect.dll`, `speedhack-i386.dll` u. a. CE-Module), Mutex `RCXDraw`,
DD-Treiber (`dd71800x32/x64` als Dienst, Service-Schlüssel, Treiberdatei), Datei-/GRF-Hashes,
eindeutige Strings (PDB-Pfade, „RCXDraw Launcher“, „Ragnapot running.“, `RoMedic.MainWindow`).

**Ehrliche Lücken:**

1. **Öffentliche Werkzeuge ohne Probe:** alle OPENKORE/WPE/RPE/CE/ARTMONEY/SPEED/MACRO/ROMISC/DEBUG/INJECT-
   Einträge beruhen auf öffentlichem Wissen; Exe-Namen sind umbenennbar, einige (`potnd.exe`, `meth4u.exe`,
   `xrag.exe`, `xlike.exe`, `rotools.exe`, `speeder.exe`, GH-Injector-Namen, `CEDRIVER73`) sind nur vermutet.
   Mit Proben sollten Hashes und Strings nachgezogen werden.
2. **Gepackte RCX-Teile:** `RCX.exe` und `RCXDraw.dll` sind MPRESS-gepackt; ohne Ausführung/Entpacker nur
   Fragmente lesbar. Mutex-/Klassennamen *innerhalb* von `RCX.exe` sind deshalb aus dem Launcher abgeleitet.
3. **Fenstertitel sind schwach:** Teilstrings treffen Browser-Tabs und Foren → höchstens `flag`, Client
   sollte Fenster von Browsern ignorieren (`OG-ALLOW-RULE-007`); „RO Tools“ trifft „Pro Tools“.
4. **`string_pattern`, `import_pattern`, `grf_entry`** haben keinen Wire-Kind – der Client prüft sie nicht;
   sie dienen Studio/Server-Werkzeugen und der manuellen Analyse.
5. **Speicherzugriff von außen** (RoMedic, Autopot, CE, ArtMoney, DD-Treiber) hinterlässt im Client keine
   Module; erkennbar nur über Handle-Überwachung (fehlende Kernel-Komponente) oder serverseitig (B1).
6. **Manual-Map-Injektion, xKore 2/3, Treiber-/Hardware-Makros** sind clientseitig per Signatur nicht sichtbar.
7. **Event `ddevent`** als Objektname ist nur vermutet (keine direkte Referenz gefunden); `OG-DD-020` ist daher `flag`.
8. Neue Builds, Umbenennungen und frische Obfuskierung (SmartAssembly, MPRESS) umgehen Hash- und
   Namenslisten; die serverseitigen Verhaltensregeln (`behaviour.md`) bleiben die zweite Linie.

## Umsetzungshinweise

- Wire-Kinds laut `proto/og_proto.h`: `process`=1, `window_title`=2, `module`=3, `window_class`=7,
  `file_name`=8, `file_sha256`=9, `driver`=10, `grf_sha256`=11, `mutex`=16, `registry`=17, Allowlist-Hash=64.
  **Ohne Wire-Kind (nur Analyse/Studio):** `grf_entry`, `import_pattern`, `string_pattern`.
- `og_signature` in `odinguard_athena.conf` kennt nur `process/window/module` mit Teilstring-Treffer;
  generische Teilstrings wie `rcx` oder `dd` würden Fehlalarme erzeugen → exakte Namen aus
  `signatures.toml` verwenden. Alle 22 bisherigen `og_signature`-Zeilen sind jetzt in `signatures.toml`
  abgebildet (Tabelle „Migration“ unten); die conf-Datei selbst wurde nicht geändert.
- Prozess- und Fenster-Signaturen anderer Prozesse brauchen eine systemweite Aufzählung (Toolhelp/EnumWindows);
  DLL-Signaturen von Class_DD/Autopot liegen in **Hilfsprozessen**, nicht im Client.

## Restlücken (gesamt)

1. Treiber-Eingaben (Class_DD) sind clientseitig nicht als synthetisch erkennbar → Timing-Analyse auf dem Server.
2. Umbenannte/neu gebaute Autopots und Makros → Reaktionszeit- und Intervall-Analyse (`behaviour.md` B1/B2).
3. Passives Mitlesen von Paketen außerhalb des Prozesses → Informationsminimierung (B5).
4. Sicht-GRFs mit neuen Inhalten → Positivliste der eigenen GRFs/Ressourcen statt Hash-Negativliste.
5. Nodelay-Wirkung ist serverseitig begrenzt (Server erzwingt Delays), Erkennung über Ablehnungsraten (B3).
6. Öffentliche Werkzeuge ohne Probe, gepackte RCX-Teile, Manual-Map-Injektion und xKore 2/3 → siehe „Abdeckung“.
