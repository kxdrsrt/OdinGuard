# 07 – LGP-Zellkacheln und darkworld-Kategorien

Stand: 2026-11-24 (Teil 2 neu: Kategorien des Competitive-Packs). Werkzeuge: ausschließlich RustyGRF (`rgrf` 0.x CLI und MCP `rustygrf`) für alle
GRF-Zugriffe, Python + Pillow nur für extrahierte Dateien (Bildstatistik, STR/GND/GAT/RSW-Parser,
Kacheln zeichnen). Keine Quelldatei wurde verändert; alles Extrahierte liegt unter
`analysis/_work/{lgp,dark,competitive}/` (per `.gitignore` nicht versioniert); die Staging-Ordner des
Competitive-Packs unter `~/Dev/OdinGuard-assets/competitive/` (außerhalb des Repos).

Kennzeichnung: **[gemessen]** = mit RustyGRF/Python an den Dateien gemessen · **[gelesen]** = aus
Quelltext/Konfiguration gelesen (roBrowserLegacy, xAthena, Client-Strings) · **[vermutet]** =
plausible Ableitung, nicht im Client verifiziert · **[Vorschlag]** = Empfehlung.

Vanilla-Basis: `client-dev/ragnarok/data.grf` (2018er Client, 202 354 Einträge), ergänzend
`data kro latest.grf` (261 389) und `official_data (chris).grf` (186 507, nur sprite/texture).
Vergleiche über den **dekodierten Inhalt** (`rgrf hash`, MD5) und Pfade ohne Groß/Klein-Unterschied
(der Client vergleicht Namen case-insensitiv). `rgrf diff` vergleicht dagegen Pfade case-sensitiv und
den gespeicherten Strom; es meldet daher für `dark.grf` gegen kRO 2438 „changed“, 850 „only_b“,
28 „same“ und gegen official_data 0 Überschneidungen (dark schreibt `data\Texture\…`) **[gemessen]**.

---

## Teil 1 – LGP-Kacheln

### 1.0 Stand 2026-11-24: flache Farb- und Formzellen je Skill (umgesetzt, im Spiel getestet)

Vollständige Analyse: `analysis/lgp/lgp-audit.md` (243 Bodenskills, Mod-GRFs, Client-Units) mit Nachtrag §9
(Messung im Spiel), Liste aller Skills: `analysis/lgp/lgp-skills.tsv` (Spalten `lgp_colour`, `lgp_shape`). Die
Abschnitte 1.1–1.6 unten sind der frühere Stand und bleiben als Herleitung stehen.

**So sieht es aus:** Jede Zelle eines Bodenskills wird für Spieler mit `@lgp` zu einer flachen Kachel
(wie Gepards `@lgp` und die darkworld-/WoE-RO-Optik), die schwere Animation kommt nicht.
**Die Farbe sagt, welcher Skill es ist, die Form sagt, was er tut:**
**Quadrat = schützend/hilfreich, Dreieck = feindlich (Schaden oder Schwächung)**, damit sich z. B. Meteor Storm
(rotes Dreieck) von Volcano (rotes Quadrat) und Storm Gust (blaues Dreieck) von Deluge (blaues Quadrat) unterscheidet.

| Farbe | Dreieck (feindlich) | Quadrat (schützend) |
|---|---|---|
| blau | Storm Gust, Water Ball, Ice Meteor | Deluge, Suiton |
| gelb | Lord of Vermilion, Thunderstorm, Raigekisai | Warmth of the Stars |
| rot | Meteor Storm, Fire Pillar, Kaensin, Bakuenryu | Volcano, Warmth of the Sun |
| braun | Heaven's Drive, Quagmire, Desperado, Arrow Shower (auch Söldner), Hammer Fall, Earthquake (MVP) | – |
| cyan | Grand Cross, Magnus Exorcismus | B.S. Sacramenti, Ganbantein, Basilica, Ice Wall |
| orange | Fire Wall, Bomb | – |
| grün | – | Pneuma, Sanctuary, Slim Pitcher |
| limette | – | Violent Gale |
| violett | Venom Dust, Gravitation Field, Grand Cross of Darkness, Evil Land (MVPs) | Warmth of the Moon |
| rosa | – | Safety Wall, Gospel, Sheltering Bliss |
| weiß | – | Land Protector, Tatami |
| hellgrau | – | Blinding Mist (Wall of Fog) |
| gold (Lieder) | Lullaby, Down Tempo, Classical Pluck, Unchained Serenade, Hip Shaker, Slow Grace | übrige Lieder und Tänze, Wand of Hermode |
| grau | alle Fallen (Skid Trap … Claymore, auch Söldner), Fiber Lock, Ground Drift | – |

**Symbole in der Kachelmitte** (nur bei `@square 1`, also in der eigenen Skillfarbe; die einfarbigen Sets bleiben schlicht):

| Symbol | Skills |
|---|---|
| Schwert mit **+** | schadenserhöhende Felder: Volcano, Deluge, Violent Gale |
| **+** | sonstige Feldbuffs: Sanctuary, B.S. Sacramenti, Battle Chant (Gospel), Slim Pitcher |
| **−** | Schwächungszonen: Quagmire, Blinding Mist, Venom Dust, Evil Land, Ugly Dance |
| Spinnennetz | Fiber Lock (graues Quadrat) |
| Schild | Schutz: Pneuma, Safety Wall, Basilica, Improvised Defense, Siegfried, Wand of Hermode |
| ! | alle Fallen |
| Flamme | Fire Wall, Crimson Fire Formation, Raging Fire Dragon |
| Mond / Sonne / Stern | Sheltering Bliss und Warmth of the Moon / Warmth of the Sun / Warmth of the Stars und Fortune's Kiss |
| Lieder | Note (Whistle, Humming), Doppelnote (Bragi), Apfel (Apple of Idun), Herz (Service for You), Kreuz (Assassin Cross), Z (Lullaby), Münze (Mental Sensing), Wirbel (Eternal Chaos), Trommel (Drum), Ring (Nibelungen), Verbot (Loki's Veil, Ganbantein), Raute (Into the Abyss), Blitz (Dissonance), Pfeil nach unten (Don't Forget Me) |

Land Protector, Eiswand, Storm Gust, Meteor Storm usw. haben bewusst kein Symbol (Land Protector schützt nur).
Die Zuordnung steht in `og_lgp_symbol` (Serverkonfig) und in `lgp-skills.tsv` (Spalte `lgp_symbol`).

79 Skills. Nicht erfasst (mit Absicht): Warp Portal (das Portal selbst), Scribble, Beschwörungen (Flora, Marine
Sphere), Gildenauren. Skills, deren Effekt am Ziel hängt (Magnum Break, Bowling Bash, Napalm Vulcan …), haben keine
Bodenzellen und damit auch keine LGP.

**Technik:**

1. **Client-Patch (`client/src/og_lgp.c`, Modul `lgp`):** Der 2018er Client hat nur zwei freie flache Units
   (0xc8/0xc9) und tönt beide blau. Der Server schreibt Farbe und Form als Marker (`0x7C000000 | Symbol<<16 | Form<<8 | Farbe`)
   in die „Creator-ID“ des Unit-Pakets; der Client hält sie im Unit-Objekt bei `+0x3d0` (im Spiel gemessen). Der
   Patch ersetzt in den Effekt-Fällen 671/672 den Aufruf des Quad-Helfers: Er liest den Marker, lädt
   `effect\odinlgp_<farbe>[_t][_<symbol>].tga` aus `odin.grf` (`_t` = Dreieck) und setzt die Tönung auf Weiß mit normaler
   Alpha-Mischung. Jede Stelle wird vorher an ihren Bytes erkannt; ein anderer Client-Build bleibt unverändert
   (dann blaue Quadrate/Dreiecke). Der Client meldet die Fähigkeit schon im Handshake (`OG_FLAG_LGP_PALETTE`) und
   im Heartbeat, damit der Server sie ab dem ersten Zauber nutzt. Es werden keine Unit-IDs mehr umgeleitet; die
   Farbe hängt nicht an freien IDs.
2. **Ausweichkacheln** (Client ohne Patch): 0xc8 = Quadrat, 0xc9 = Dreieck, beide blau.
3. **Server (xAthena, `og_map.cpp`, Hooks in `clif.cpp`):**
   - `og_lgp_color: <name>,<Farbnummer>`, `og_lgp_attack: <Skill-IDs>` (Dreiecke),
     `og_lgp: [style,]<skill>,<farbe|unit>` und `og_lgp_symbol_def` / `og_lgp_symbol: <symbol>,<Skill-IDs>`.
   - Unsichtbare Zellen (Storm Gust …) **und** sichtbare Units (Fire Wall, Pneuma, Sanctuary, Ice Wall …) werden
     pro Betrachter ersetzt; bei der Eiswand bleibt die Zellsperre. Lite-Units gehen immer mit Stufe 1 raus.
   - **Flash-Zellen:** Skills ohne Unit (Arrow Shower, Hammer Fall, Ganbantein …), kurzlebige Units (Heaven's
     Drive, 100 ms) und Einzelzellen-Units mit Fläche (Meteor-Einschläge) bekommen für `og_lgp_flash_ms`
     (Konfig 1500, Code-Standard 1000) eine Kachel pro Zelle; IDs 100.000.000+ (der Client ignoriert IDs ab 0x7E000000).
   - `@square 1` = jeder Skill in seiner Farbe, `@square 2..15` = alle Skills in einer Farbe (Reihenfolge der
     `og_lgp_color`-Zeilen, die Form bleibt), `@circle` = Sage-Bänder für SG/LoV/MS, `@aoes` = nur SG/LoV/MS
     (**Achtung:** wer `@aoes` an hat, sieht für alle anderen Skills die normalen Effekte; `@lgp` schaltet es zurück).
4. **Texturen:** `assets/odin-grf/lgp/data/texture/effect/odinlgp_<farbe>[_t][_<symbol>].tga`: 14 Farben × 2 Formen ×
   (ohne + 25 Symbole) = 728 Dateien (komprimiert ≈ 1,4 MB) und `hanmoon1/2.tga` (Fallback), 128×128 TGA32, erzeugt mit
   `analysis/lgp/tools/make_lgp_tiles.py` (Vorschau: `analysis/lgp/lgp-symbols-sheet.png`). Wer ein Symbol oder eine
   Farbe ergänzt, trägt es in `make_lgp_tiles.py`, `client/src/og_lgp.c` und der Serverkonfig ein.
5. **Sound:** Die schwere Bodenanimation trägt den Skill-Sound; LGP-Spieler bekommen sie nicht. Der Server schickt
   deshalb den Sound allein (Paket ZC_SOUND 0x1d3, vom Zauberer aus) für die Skills in `og_lgp_sound_skills`
   (23 Skills: Storm Gust, Meteor Storm, Thunderstorm, Fire Pillar, Fire Wall, Eiswand, Safety Wall, Quagmire,
   Sanctuary, Magnus, B.S. Sacramenti, Grand Cross, Hammer Fall, Venom Dust, alle Fallen; Zuordnung
   `assets/lgp-sound/sounds.tsv`, Zuordnung teils **[vermutet]**). Das Paket erlaubt nur 23 Zeichen Dateiname, die
   Originale heißen länger (`effect\wizard_stormgust.wav`): `tools/pack/build_lgp_sounds.sh <Client-Ordner>` kopiert
   sie unter `data\wav\og_<skill id>.wav` aus dem eigenen Client des Betreibers in einen Ordner außerhalb des
   Repos; im Profil `[assets.odin_grf] lgp-sound = "<Ordner>"`, dann landen sie in `odin.grf` (ohne den Ordner gibt
   der Build nur eine Warnung). Einstellbar: global `og_lgp_sound: yes/no`, je Spieler `@lgpsound [on|off]`
   (Standard an, wird gespeichert), und `/sound` des Clients schaltet den Effekt-Ton weiterhin stumm **[vermutet,
   nicht gehört]**. Ein Client ohne die Dateien bleibt still.
6. **Test-NPC:** „LGP Test“ in Prontera (156,186) bzw. `@lgptest [skill]` (Admins) wirkt alle Skills nacheinander,
   jeweils auf leerem Feld (Skript-Befehl `og_clearskillunits`: sonst verdecken Lieder und Felder des Vorgängers die
   nächsten Skills), nie auf der eigenen Zelle (kleine Skills lägen unter dem Charakter) und mit den nötigen
   Verbrauchsgütern (Gems per `bNoGemStone`, Fallen, Flaschen, Steine). Beim Ansprechen während des Laufs lässt sich
   abbrechen (auch `@lgptest stop`). Die Liste erzeugt `analysis/lgp/tools/gen_lgp_test_npc.py` aus der Serverkonfig.

**Wall of Fog / Heaven's Drive (gemessen):** Beide funktionieren. Die Fog-Kacheln waren schon vorher da, aber
weiß auf hellem Pflaster kaum zu erkennen; jetzt hellgrau mit „−“. Heaven's Drive besteht nur aus einer
100-ms-Unit und lebt von den Flash-Zellen (5×5 ohne Ecken, braune Dreiecke); die Anzeigedauer ist jetzt 1,5 s.

**Getestet (gemessen, VM, echter Client):** der komplette Durchlauf des Test-NPCs (76 Skills), Farben, Formen und
Symbole, Flash-Zellen verschwinden nach Ablauf, `@lgp off` zeigt die normalen Effekte.
**Nicht geprüft:** Fallback-Optik ohne Patch, zwei Betrachter gleichzeitig.

### 1.1 So arbeitet die aktuelle OdinGuard-LGP **[gelesen]**

`@lgp` setzt `og_flags` Bit 2. Für Skills aus `og_lgp` (`conf/odinguard_athena.conf`) bekommt ein
Betrachter mit LGP statt `UNT_DUMMYSKILL` (0x86, im Client unsichtbar) eine echte Ground-Unit
(Paket 0x09ca) und keinen `ZC_NOTIFY_GROUNDSKILL`-Effekt:

| Skill | Unit heute | Client-Textur (Abschnitt 1.2) |
|---|---|---|
| 89 Storm Gust | 0x9b Deluge | `ring_blue.tga` |
| 83 Meteor Storm | 0x9a Volcano | `ring_red.tga` |
| 85 Lord of Vermilion | 0x9c Violent Gale | `ring_yellow.tga` |
| 91 Heaven's Drive | 0x9d Land Protector | `aaa copy.bmp` |
| 539/540/541 (NJ) | 0x9b / 0x9c / 0x9d | wie oben |

Problem **[Vorschlag]**: 91 und 541 auf 0x9d lassen ein Heaven's Drive wie ein echtes Land Protector
aussehen – im WoE irreführend. Diese beiden sollten auf eine andere Unit (0x9a/0x9c oder 0xc9).

### 1.2 Ground-Unit → Textur im 2018er Client

Zuordnung Unit → Effekt aus roBrowserLegacy (`SkillUnit.js`, `EffectTable.js`, Effektklassen)
**[gelesen]**, abgeglichen mit den Texturnamen im `alpha.exe` (`strings`) **[gemessen]** und den
Dateien in `data.grf` (Format **[gemessen]**). „Skill auf M-LR“: Unit-Name in
`db/pre-re|re/skill_db.yml` von xAthena gesucht **[gemessen]**; 3rd-Job-Units sind auf M-LR durch die
3rd-Job-Sperre ungenutzt **[gelesen, Checkpoint 001]**.

| Unit | Name | Darstellung (roBrowser) | Textur `data\texture\effect\…` | Format [gemessen] | flach? | Skill in DB |
|---|---|---|---|---|---|---|
| 0x7e | Safety Wall | Zylinder | `alpha_down.tga` + `safetywall.str` | 64×64 TGA32 | nein | ja |
| 0x80/0x81 | Warp Portal / Pre-Warp | Zylinder | `ring_blue.tga`, `alpha1.tga` | 64×64 TGA32 | nein | ja |
| 0x83 | Sanctuary | Zylinder (4 Seiten) | `magic_green.tga` | 64×64 TGA32 | nein | ja |
| 0x84 | Magnus Exorcismus | Zylinder (4 Seiten) | `ring_red.tga` | 64×64 TGA32 | nein | ja |
| 0x85 | Pneuma | STR | `pneuma1-3.str` | – | nein | ja |
| 0x86 | Dummy-Skill | nichts | – | – | – | 61 Skills |
| 0x87 | Fire Pillar (wartend) | Zylinder | `magic_red.tga` | 64×64 TGA32, 16 428 B | nein | ja |
| 0x8f–0x99 | Fallen | RSM-Modelle | `model\외부소품\트랩…` | – | nein | ja |
| **0x9a** | Volcano | PropertyGround: kurzer Trichter pro Zelle | `ring_red.tga` | 64×64 TGA32, 16 428 B | Boden-nah | ja |
| **0x9b** | Deluge | PropertyGround | `ring_blue.tga` | 64×64 TGA32, 16 428 B | Boden-nah | ja |
| **0x9c** | Violent Gale | PropertyGround | `ring_yellow.tga` | 64×64 TGA32, 16 428 B | Boden-nah | ja |
| **0x9d** | Land Protector | **FlatTexture** (flaches Quad pro Zelle) | `aaa copy.bmp` | 128×128 BMP8, 17 310 B | **ja** | ja |
| 0x9e–0xaf | Lieder/Tänze | flache Farbzelle (Alpha 0.05) + schwebende Noten | `zz.bmp`, `lens_*.bmp`, `melody_*.bmp`, `safeline.bmp`, … | 32×128 BMP u. a. | Farbzelle ja (Farbe fest im Code) | ja |
| 0xb3 | Gospel | Farbzelle + `cross_old.bmp` | – | – | teils | ja |
| 0xb4 | Basilica | Zylinder | `alpha_down.tga` | 64×64 TGA32 | nein | ja |
| 0xb7 | Spider Web (Fiber Lock) | **FlatTexture** 128 | `spiderweb.tga` | 128×128 TGA32, 65 580 B | **ja** | ja |
| 0xc7 | Evil Land (NPC) | schwebende Kachel + Farbzelle | `curse.bmp` | 128×128 BMP8, 17 462 B, 12 525 px Magenta | fast | ja (NPC) |
| **0xc8** | Dark Runner | Bodensymbol „on the ground“ (EF_BOTTOM_RUNNER) | `hanmoon1.tga` | 128×128 TGA32, 65 580 B | [vermutet] ja | **nein** |
| **0xc9** | Dark Transfer | Bodensymbol (EF_BOTTOM_TRANSFER) | `hanmoon2.tga` | 128×128 TGA32, 65 580 B | [vermutet] ja | **nein** |
| 0xcc–0xd0 | Manhole … Bloody Lust (Shadow Chaser) | rotierende Aura | – | – | nein | 3rd Job |
| 0xd2–0xd9, 0xe5 | Ranger-Fallen | RSM | – | – | nein | 3rd Job |
| 0xe0–0xfe | Warlock/Sorcerer/Genetic/Kagerou … | in roBrowser ohne Effekt | z. B. `makibishi.bmp` (24×24 BMP8) | – | unbekannt | 3rd Job/erweitert |

Beobachtungen:

* `ring_*.tga` sind senkrechte Flammen-Verläufe (Seitentextur), keine Bodenbilder **[gemessen,
  Kontaktbogen]**. Volcano/Deluge/Violent Gale zeichnen pro Zelle einen niedrigen Trichter, dessen
  unterer Rand die Zelle umläuft – daher sah der Test „hellblaue Zellen“ **[gelesen + Ingame-Befund
  aus 05-tests]**.
* In `alpha.exe` stehen `effect\curse.bmp`, `effect\hanmoon1.tga`, `effect\hanmoon2.tga`,
  `effect\earth_quake.wav` direkt hintereinander, getrennt von `hanmoon3-7` (Mild-Wind-Block) und
  neben dem Block `aaa copy.bmp`, `safeline.bmp`, `zz.bmp` (LP, Loki, Lullaby) **[gemessen]**. Das
  stützt, dass hanmoon1/2 die Bodensymbole der Units 0xc8/0xc9 sind **[vermutet]**.
* `hanmoon1/2` nutzt sonst nur „Mild Wind (Earth/Wind)“ des Taekwon/Soul Linker (Aura-Symbol)
  **[gelesen]**.

### 1.3 Funde in den GRFs

| Fund | Quelle | Befund [gemessen] | Bedeutung |
|---|---|---|---|
| `ring_blue.tga`, `ring_red.tga`, `ring_purple/brown/jadu`, `magic_blue/green/sky/violet.tga` | `WOERO/woerol.grf` und `dark2.grf` (identische MD5) | 256×256 TGA24, 92,2 % Magenta, nur unterste ~20 Zeilen farbig (blau 0/102/255, rot 254/60/29 …) | **Das ist der WoE-RO-LGP-Look**: vom Trichter bleibt nur der Bodenrand → flache, farbige Zellquadrate. Mit Deluge/Volcano-Units ergibt das genau die blauen Kacheln. |
| `aaa copy.bmp` | woerol, dark2 (gleich) | 128×128 BMP24, weißer Rauten-Rahmen auf Schwarz | Land Protector als Zellumriss |
| `ring_yellow.tga` | dark2 | 64×64 TGA32, 98,9 % Alpha 0 | Violent Gale fast unsichtbar |
| `hanmoon1-7.tga`, `ring_line(r).tga` | dark2, woerol | 1×1 Magenta | Mild-Wind-Symbole entfernt |
| `spiderweb.tga` | woerol: 128×128 rotes Achteck; dark2/woe: 1024×1024 blauer Kreis | Fiber Lock stark hervorgehoben (Kategorie `effects`) |
| `stormgust.str`, `lord.str`, `meteor1-4.str` | `Thoth.grf` | SG: 2 Ebenen mit `21.bmp` (64×64 grau), 192×96 Bildschirm-Raute; LoV/MS auf `0lordbl.bmp` reduziert | vereinfachte Bildschirmeffekte, **keine** Zellkacheln |
| `Pneuma01-10.bmp` + `pneuma1-3.str` | dark.grf, dark2.grf, woe.grf | 32×32 Ziffern 10…1 | Pneuma-Restzeit-Countdown → Kategorie `pneuma` |

Kopiert nach `analysis/_work/lgp/found/` (25 Dateien, Präfix = Quelle) mit
`found/PROVENANCE.tsv` (Quell-GRF, GRF-Pfad, MD5, Größe, Messung). Diese Kopien sind
Analysematerial; ausgeliefert wird nur, was ein Betreiber über die Kategorien in Teil 2 wählt.

### 1.4 Bewertung der Kandidaten

| Variante | Unit(s) | Textur | Kollision mit normalem Spiel | Sicherheit |
|---|---|---|---|---|
| A „eigene Unit“ | 0xc8 (+0xc9) | `hanmoon1.tga` (+`hanmoon2.tga`) | Units von **keinem** Skill genutzt [gemessen]; Textur nur bei TK „Mild Wind Earth/Wind“ – würde dort als Quadrat erscheinen (selten, kosmetisch) | Darstellung im 2018er Client **nicht verifiziert** |
| B „WoE-RO-Band“ | 0x9b (alle drei Skills) | `ring_blue.tga` als Bodenband | Deluge sieht aus wie die LGP (ist heute schon so); `ring_blue` nutzen auch Warp-Portal-Säule, Teleport-, Wasser-Cast-, SP-Heal- und Lv-99-Aura → werden zu flachen Bändern (rein kosmetisch, für alle gleich) | Look durch WoE-RO belegt; Geometrie [gelesen] |
| B+ Farben | 0x9a / 0x9c zusätzlich | `ring_red` / `ring_yellow` | ring_red: auch Magnus, Grand Cross, Feuer-Cast; ring_yellow: auch Magnum Break, gelbe Cast-Aura, Reflect Shield | wie B, mehr Nebenwirkungen |
| C Land Protector | 0x9d | `aaa copy.bmp` | LP ist WoE-entscheidend → **nicht** verwenden | – |
| D Spider Web / Fire Pillar | 0xb7 / 0x87 | `spiderweb.tga` / `magic_red.tga` | echte Skills im PvP/WoE → nicht verwenden | – |
| E Evil Land | 0xc7 | `curse.bmp` | NPC-Skill; `curse.bmp` evtl. weitere Nutzer; schwebend | unklar |

### 1.5 Empfehlung **[Vorschlag]**

> **Überholt durch `analysis/lgp/lgp-audit.md` (2026-11-24):** Der 2018er Client tönt die Units 0xc8/0xc9
> fest blau (additiv); eine orange `hanmoon2.tga` erscheint dunkel. `hanmoon1/2.tga` sind jetzt blau gefüllt
> bzw. blauer Umriss; weitere Farben nur über den dort beschriebenen Client-Patch (`odinlgp_*.tga`).

1. **Sofort (Estland-/WoE-RO-Look, belegt):** Variante B. `og_lgp` für 89, 83, 85 auf `0x9b`;
   `odin.grf` überschreibt **eine** Datei: `data\texture\effect\ring_blue.tga` (64×64, TGA 32 bit,
   unten 4 Zeilen Blau mit Alpha 230, Rest Alpha 0; Größe wie Original 16 428 B). Ergebnis: alle
   drei Skills als flache blaue Zellquadrate, wie auf WoE-RO/EstlandRO.
2. **Ziel (ohne Deluge-Verwechslung, nach Ingame-Test):** Variante A. SG/LoV/MS → `0xc8`
   (`hanmoon1.tga`, blaue Kachel 128×128 TGA32 mit Rahmen Alpha 210 und Füllung Alpha 70). Für
   Farben je Skill: Meteor → `0xc9` (`hanmoon2.tga`, orange). LoV bleibt blau auf `0xc8` (wie
   Estland) oder später auf eine dritte getestete Unit.
3. Heaven's Drive (91) und NJ 541 von `0x9d` wegnehmen (z. B. `0x9b` bzw. `0xc8`).

Testplan: auf dem Testserver `og_lgp: 89,0xc8` (bzw. `0xc9`) setzen, Map-Server neu starten,
`@lgp` an, Storm Gust wirken; erwartet: pro Zelle eine flache Kachel, keine „Cannot find file“.
Dasselbe mit `odin-lgp-test-primary.grf` bzw. `-fallback.grf` an erster Stelle in `DATA.INI`.

**Zielpfade für odin.grf** (Schreibweise wie im 2018er `data.grf`, reines ASCII, daher keine
cp949-Kodierung nötig; der Client vergleicht case-insensitiv) **[gemessen]**:

| Variante | Pfad im GRF | Datei aus `analysis/_work/lgp/own/` |
|---|---|---|
| B | `data\texture\effect\ring_blue.tga` | `stage_fallback/data/texture/effect/ring_blue.tga` |
| B+ | `data\texture\effect\ring_red.tga`, `data\texture\effect\ring_yellow.tga` | `stage_fallback/…` (rot, violett) |
| A | `data\texture\effect\hanmoon1.tga` | `stage_primary/data/texture/effect/hanmoon1.tga` (blau) |
| A | `data\texture\effect\hanmoon2.tga` | `stage_primary/data/texture/effect/hanmoon2.tga` (orange) |
| (nur Referenz) | `data\texture\effect\aaa copy.bmp` (mit Leerzeichen) | `bmp/lgp128_*_fill.bmp` |

Koreanische Pfade (nicht nötig für LGP) müssen in cp949 stehen, z. B. `이팩트` = `c0 cc c6 d1 c6 ae`,
`유저인터페이스` = `c0 af c0 fa c0 ce c5 cd c6 e4 c0 cc bd ba`; `rgrf build` erhält sie, wenn der
Ordner mit `--names cp949` extrahiert wurde.

### 1.6 Erzeugte Kacheln **[gemessen]**

`analysis/_work/lgp/own/` (neu gezeichnet mit `analysis/_work/lgp/tools/make_tiles.py`, keine
Fremd-Assets, frei verwendbar):

* `bmp/lgp32_<farbe>_frame.bmp` – 32×32 BMP24, 2 px Rahmen, Innen Magenta (FF00FF).
* `bmp/lgp32_<farbe>_fill.bmp` – 32×32 BMP24, Rahmen + dunkle Füllung (wirkt bei additivem Blending
  halbtransparent).
* `bmp/lgp128_<farbe>_fill.bmp` – 128×128 BMP24 (Größe wie `aaa copy.bmp`/`curse.bmp`).
* `tga/lgp32_*` und `tga/lgp128_<farbe>_alpha.tga` – TGA32, Rahmen Alpha 210, Füllung Alpha 70.
* `tga/ringband64_<farbe>_alpha.tga` – 64×64 TGA32, 4-px-Bodenband.
* Farben: `blue` (Storm Gust), `violet` und `yellow` (LoV), `red` und `orange` (Meteor).
* Bühnen mit exakten Zielpfaden: `stage_primary/` (hanmoon1 blau, hanmoon2 orange) und
  `stage_fallback/` (ring_blue blau, ring_red rot, ring_yellow violett).
* Test-GRFs: `own/odin-lgp-test-primary.grf` (689 B, 2 Einträge) und
  `own/odin-lgp-test-fallback.grf` (403 B, 3 Einträge), beide `rgrf verify`: 0 Fehler.
  TGA-Kopf wie Original (Typ 2, 32 bit, Ursprung unten, Deskriptor 0x08).

---

## Teil 2 – darkworld-Kategorien (Competitive-Pack)

**Entscheidung des Eigentümers (2026-11-24) [gelesen]:** Alle darkworld-Änderungen dürfen
ausgeliefert werden, wenn der Betreiber sie auswählt – jeder Spieler erhält dasselbe Pack
(`odin-dark.grf`), also einen gleichen Standard wie auf WoE RO / EstlandRO. Das schließt flache
Maps, durchsichtige Bäume/Objekte, verkleinerte WoE-Objekte, den Pneuma-Countdown, nodelay-ACTs und
dunkle Bodenkacheln ein. Lizenz der darkworld-Dateien laut Eigentümer: frei (mit GRF Editor erstellt).
Jede Kategorie ist **einzeln wählbar**; die Befunde unten beschreiben, was eine Kategorie ändert,
damit der Betreiber bewusst wählt.

### 2.1 Diff-Statistik **[gemessen]**

Erneut geprüft am 2026-11-24 mit `rgrf diff` gegen `client-dev/ragnarok/data.grf` (Pfade ohne
Groß/Klein-Unterschied): dark 2552 geändert / 736 neu / 28 gleich; dark2 2871 / 785 / 33;
woe 257 / 63 / 21; woebg 229 / 36 / 20; nodelay 56 / 7 / 0; Thoth 372 / 110 / 9;
WOERO/woero 1095 / 2893 / 1861; WOERO/woerol 673 / 52 / 14 **[gemessen]**. Die Tabelle unten ist der
ältere Inhaltsvergleich (MD5) gegen data.grf ∪ kRO ∪ official_data.

`dark.grf` 78 113 579 B, 3316 Einträge; `dark2.grf` 78 907 885 B, 3689 Einträge. Inhaltsvergleich
gegen data.grf (2018) ∪ kRO ∪ official_data:

| Gruppe | dark: geändert / neu / gleich | dark2: geändert / neu / gleich | entpackt (dark2) |
|---|---|---|---|
| Map `.gat` | 728 / 87 / 1 | 729 / 88 / 1 | 1409 MB |
| Map `.gnd` | 729 / 87 / 0 | 730 / 88 / 0 | 1200 MB |
| Map `.rsw` | 729 / 87 / 0 | 730 / 88 / 0 | 54 MB |
| Boden-/Modelltexturen `data\texture\*` (ohne effect/UI) | 147 / 339 / 6 | 147 / 341 / 6 | 20 MB |
| Effekt-Texturen `texture\effect` | 25 / 10 / 0 | 243 / 39 / 5 | 12,5 MB |
| Effekt-STR | 4 / 0 / 0 | 97 / 2 / 0 | 1,5 MB |
| UI `texture\유저인터페이스` | 170 / 25 / 19 | 170 / 25 / 19 | 76 MB |
| Sprites Monster/sonstige | 41 / 0 / 1 | 39 / 0 / 1 | 1,7 MB |
| Sprites `이팩트` | 14 / 0 / 1 | 16 / 0 / 1 | 0,1 MB |
| Text (`indoorrswtable`, `viewpointtable`, Skill-Texte) | 3 / 0 / 0 | 5 / 0 / 0 | – |
| `.extra`, Thumbs.db u. a. | 0 / 61 / 1 | 0 / 74 / 1 | – |
| Modelle `.rsm`, Lua | 0 | 0 | – |

dark ↔ dark2: 3286 gleich, 14 geändert, 16 nur dark (Tarot-Karten, dragon_fly), 389 nur dark2
(266 Effekt-Texturen, 95 STR) **[gemessen, `rgrf diff`]**.

### 2.2 Maps

Alle 816 (dark) bzw. 818 (dark2) `.rsw` haben **0 Modelle**, Licht (45,45, ambient/diffus 1,0),
Wasserhöhe 100 (unter dem Boden) **[gemessen]**. Die `.gnd` sind flach (Höhe 0), ohne Wand-Flächen,
mit 1 Lightmap und je Zelle einer Kachel `c<a>c<b>c<c>c<d>.bmp`, deren Name die GAT-Typen der vier
Zellen kodiert (Reihenfolge (2x,2y),(2x+1,2y),(2x,2y+1),(2x+1,2y+1); 100 % Treffer an prtg_cas01 und
schg_cas01) **[gemessen]**. Optik: dunkelgrün = begehbar, schwarz = blockiert, beige = snipable,
dunkelblau = Wasser; WoE-SE-Burgen zusätzlich Markierungen (`points\*`, `woe\*`: Pfeile „UPPER STAGE“,
Ziffern, farbige Felder an Geräte-/Barrikadenpositionen).

Betroffene WoE/BG/PvP-Maps (in beiden GRFs) **[gemessen]**: aldeg_cas01–05, arug_cas01–09 (+
arug_cas07rsw), bat_a01, bat_b01, bat_b03–05, bat_c01, bat_room, gefg_cas01–05, gld2_ald/gef/pay/prt,
gld_dun01–04, guild_vs1–5, payg_cas01–05, prtg_cas01–05, pvp_2vs2, pvp_area, pvp_room,
pvp_y_1-2…8-2, schg_cas01–08, te_aldecas1–5, te_prtcas01–05, te_alde_gld, te_prt_gld, valkyrie
(85 Maps, 261 Dateien je GRF). bat_a02/b02/c02 fehlen (auch nicht in den Vanilla-GRFs).
Beispiel prtg_cas01: 329 Modelle → 0, 1758 Wand-Zellen → 0, Höhenspanne 86,7 → 0, 4894 Lightmaps → 1.

GAT-Zelltypen (816 Maps gegen data.grf): 661 identisch, 100 nicht in Vanilla, 12 andere Größe, 43 mit
Abweichungen (meist alte Kartenversionen, z. B. morocc, yuno, airplane) **[gemessen]**. WoE/BG/PvP:
aldeg_cas01 129 Zellen 1→0 (nur Randspalte x=238 von 240), te_prt_gld 88 Zellen 1→0, arug_cas03 2
Zellen 0→1; alle anderen identisch. Höhen sind überall 0. Der Server nutzt seinen eigenen mapcache,
Begehbarkeit ändert sich dadurch serverseitig nicht; wo die dark-GAT von der Client-GAT abweicht,
können Klick-/Pfadanzeige und Server auseinanderlaufen. Die OdinGuard-Flachmaps (2.5) behalten
deshalb die GAT-Typen des Clients.

### 2.3 Zuordnung jeder Änderung zu einer Kategorie

Neu klassifiziert für alle Einträge von dark, dark2, woe, woebg, nodelay, Thoth, WOERO/woero,
woerol, woerop (Inhalts-MD5 aus `rgrf --names cp949 --json hash`; Ergebnis je Eintrag:
`analysis/_work/competitive/entries.tsv`, Werkzeug `analysis/_work/competitive/tools/classify.py`)
**[gemessen]**:

| Gruppe | Befund [gemessen] | Kategorie |
|---|---|---|
| Maps `.rsw` (0 Modelle), `.gnd` (flach, ohne Wände), `.gat` (Höhe 0), `.extra` | Wände, Bäume, Säulen, Burgteile weg; Gelände eben | `flat-maps` (ausgeliefert wird die RustyGRF-Erzeugung, 2.5) |
| Bodenkacheln `c*.bmp`, `points\*`, `woe\*`, `NSA` (336) | 64×64 BMP24, 2–3 Farben, Zellraster nach GAT-Typ | `floor` |
| Modelltexturen `나무잡초꽃`, `기타마을`, `내부소품`, `모로코`, `게페니아` | 126 zu ≥ 98 % transparent oder deutlich transparenter, 17 kleiner/dunkler | `trees` |
| Sprites `barricade`, `flora`, `geographer`, `hydra`, `mandragora`, `parasite`, `s_empel_1/2.act` | 19×24-Bilder statt bis 109×139; Emperium 96 statt 176 Frames | `barricades` |
| `Pneuma01-10.bmp` + `pneuma1-3.str` (Effekt 141) | Countdown 10…1 über Pneuma | `pneuma` |
| nodelay.grf: 29 Körper-ACTs + 27 SPR | gleiche Intervalle, weniger Frames (Stalker 456 → 280) | `nodelay` |
| Effekt-Texturen/STR dark2 (175 BMP, 111 TGA, 96 STR, 3 EZV; 42 STR stehen in GRF Editors Effekttabelle, z. B. stormgust = 89) | transparent, einfarbig, verkleinert; `ring_*` = WoE-RO-Bodenband; `lockon128`/`magic_target` größer; `spiderweb` 1024×1024 | `effects` |
| UI `texture\유저인터페이스` (194) | Skin, 8 voll transparent, 36 verkleinert | `ui` |
| Status-Sprites (stun, sleep, `얼음땡`, curse), `숫자`, `msg`, `bluemsg`, `sight`, `smoke`, `cursors` | größer/andere Schrift | `other` |
| MVP-/Boss-ACTs (22), `dragon_fly` | Ebenen umskaliert (0,5–3,0) | `other` |
| `indoorrswtable.txt`, `viewpointtable.txt`, `PetTalkTable.xml`, `data\asura*.tga` | Kamera-Tabellen geändert/leer u. a. | `other` |
| `ba_frostjoke.txt`, `dc_scream.txt` | Texte durch `@`-Befehle eines anderen Servers ersetzt | nicht ausgeliefert |
| `Thumbs.db`, Ordner-Platzhalter (nodelay 7, Thoth 16 DES-Einträge), Readmes, `data\_lua\*` (Thoth) | keine Client-Daten | nicht ausgeliefert |

Weitere GRFs (eingeordnet, aber **nicht** Quelle der Staging-Ordner – je Pfad gibt es genau eine
Quelle; die anderen Fassungen stehen als Alternative in `entries.tsv`) **[gemessen]**: `woe.grf`
(barricades 10, pneuma 13, effects 48, ui 234, other 34; dazu, wie in woebg und Thoth, 9 geänderte
Lied-Sounds `wav\effect\*.wav`, die dark/dark2 nicht haben), `woebg.grf` (effects 19, ui 234, other 30), `Thoth.grf` (36 vereinfachte
WoE/BG-Maps mit 1 Textur und Rest-Modellen, z. B. prtg_cas01 18 Modelle; effects 74, ui 201,
barricades 9), `WOERO/woerol.grf` (effects 319, trees 176, ui 203), `WOERO/woero.grf` (vollständige
Client-Daten, 5849 Einträge), `WOERO/woerop.grf` (38 986 Paletten). `desu.grf` – Edit-Log nennt
„Removed Blindness“, „Removed Auras“, vergrößerte MVPs, „tiled maps“ **[gelesen]**.

Quelle je Pfad: `dark2.grf`, nur wo dark2 den Pfad nicht hat `dark.grf` (14 Tarot-Karten,
`dragon_fly`); nodelay aus `nodelay.grf`. Einträge, die dem Client gleichen, entfallen. Listen mit
Quell-GRF, Archivpfad, Zielpfad, MD5, Größe: `assets/competitive/manifests/<kategorie>.tsv`.

### 2.4 Kategorien, Staging-Ordner und Größen **[gemessen]**

Staging: `~/Dev/OdinGuard-assets/competitive/<ordner>/data/...` (außerhalb des Repos), Rezept:
`assets/competitive/recipe.toml`, Erzeugung: `tools/pack/build_categories.sh`. Test-GRFs:
`<rgrf-out>/odin-dark-<ordner>.grf` (RustyGRF `grf_build`, zlib 9, 0x200; `grf_verify`
jeweils 0 Fehler, 0 Warnungen). MB = 10⁶ Byte.

| Kategorie | Ordner | Dateien | entpackt | GRF | Standard | Quelle (sha256) |
|---|---|---|---|---|---|---|
| floor | `floor` (Original-Geometrie, WoE/BG/PvP) | 178 (68 GND + 110 Kacheln) | 236,4 MB | 34,94 MB | **an** | dark2.grf `045c71de…a748` + Client-GND/GAT |
| floor (mit flat-maps) | `floor-flat-woe-bg-pvp` | 178 | 135,9 MB | 4,93 MB | **an** (statt `floor`) | wie oben, auf den Flachmaps |
| floor (mit flat-maps all) | `floor-flat-all` | 1161 (1027 GND + 134 Kacheln) | 2353,1 MB | 91,29 MB | – | wie oben |
| flat-maps | `flat-maps-woe-bg-pvp` (79 Maps) | 215 | 184,3 MB | 5,15 MB | **an** | rustygrf flat_maps from client data |
| flat-maps | `flat-maps-all` (1054 Maps) | 3110 | 3298,3 MB | 105,61 MB | – | rustygrf flat_maps from client data |
| trees | `trees` | 143 | 4,17 MB | 1,08 MB | **an** | dark2.grf |
| barricades | `barricades` | 14 | 0,12 MB | 0,016 MB | aus | dark2.grf |
| pneuma | `pneuma` | 13 | 0,28 MB | 0,014 MB | **an** | dark2.grf |
| nodelay | `nodelay` | 56 | 7,41 MB | 2,52 MB | aus | nodelay.grf `0dba7a75…8f11` |
| effects | `effects` | 385 | 13,79 MB | 0,82 MB | aus | dark2.grf (+ dark.grf `f7baefcd…06ae`) |
| ui | `ui` | 194 | 68,06 MB | 10,54 MB | aus | dark2.grf |
| other | `other` | 60 | 3,08 MB | 0,49 MB | aus | dark2.grf (+ dark.grf) |

Kombinationen: Standardauswahl (flat-maps-woe-bg-pvp + floor-flat-woe-bg-pvp + trees + pneuma)
**481 Dateien, 7,71 MB**; dieselbe mit Bereich `all` **3400 Dateien, 119,16 MB** (`rgrf merge`,
verify ok). `rgrf map check` mit DATA.INI (Pack zuerst, dann cdata, core, cpal, data): Standard 79
Maps / 4162 Dateien, `floor` 79 / 13 913, `all` 1056 Maps / 45 409 Dateien – 0 neue Probleme (die 3
gemeldeten – `air_evt`, `soccer3` ohne GND/GAT – bestehen schon im Client). Optional
`rgrf image reduce` (8-bit-BMP): ui 10,54 → 9,63 MB, trees 1,08 → 0,74 MB.

Bereich `woe-bg-pvp` (Regex in `tools/pack/competitive.py`): aldeg/gefg/payg/prtg/arug/schg_cas*,
arug/schg_dun01, te_aldecas1–5, te_prtcas01–05, te_alde_gld, te_prt_gld, n_castle, rush_cas01–04,
gld_dun01–04, gld2_*, guild_vs1–5, bat_a01, bat_b01, bat_c01–08, bat_room, pvp_2vs2, pvp_room,
pvp_y_1-2…8-2 (79 Maps im Dev-Client; rush_cas*/bat_c02–08 nutzen die GND/GAT einer anderen Map,
z. B. rush_cas01 → prtg_cas02). `valkyrie` ist nicht mehr enthalten (keine WoE-Map).

Kombinationsregel: Sind `floor` und `flat-maps` gewählt, wird `floor-flat-<bereich>` **nach**
`flat-maps-<bereich>` kopiert (ersetzt deren `.gnd`); `floor` allein (Original-Geometrie) entfällt
dann. Reihenfolge: `copy_order` in `recipe.toml` (Studio kopiert in der Reihenfolge der Profilliste,
spätere Ordner überschreiben).

### 2.5 Flache Maps aus RustyGRF statt aus darkworld **[gemessen]**

`rgrf map flat --flatten --remove-light --remove-shadow <Client-Ordner> [Maps]` liest die Maps über
die DATA.INI des Clients (das Skript legt dafür eine DATA.INI ohne `odin*.grf` an) und schreibt nur
die geänderten `.rsw/.gnd/.gat`. Damit ist das Ergebnis für jeden Client reproduzierbar. Vergleich mit
dark2 für die 72 WoE/BG/PvP-Maps, die beide haben (`analysis/_work/competitive/cmp_flat.json`):

| Merkmal | RustyGRF flat | darkworld dark2 |
|---|---|---|
| Modelle/Lichter/Sounds/Effekte in `.rsw` | 0 (72/72) | 0 (72/72) |
| Wasserhöhe | 100 (unter dem Boden) | 100 |
| Höhen GND/GAT | 0 (72/72) | 0 (72/72) |
| Wandflächen | 0 | 0 |
| Lightmaps | 1 | 1 |
| GAT-Zelltypen | = Client | 69/72 gleich; aldeg_cas01 (129 Zellen), te_prt_gld (88), arug_cas03 (2) weichen ab |
| Bodentexturen | Originaltexturen, leere Zellen bleiben leer (z. B. prtg_cas01 4291 von 12 100 Zellen mit Boden) | jede Zelle eine Kachel `c*.bmp` (12 100 / 12 100) |

Die Kachel-Optik von darkworld entsteht in OdinGuard durch `competitive.py floor --fill-void` auf den
RustyGRF-Flachmaps (Kachelname aus den 4 GAT-Zellen, Kachel-UV wie in dark, WoE-Markierungen
`points\*`/`woe\*` aus der dark2-GND übernommen, wo die 4 GAT-Zellen gleich sind: 31 339 Zellen,
fast nur SE-Burgen arug/schg_cas*). GND 1.8/1.9 (124 neuere Maps) werden mit unverändertem Wasserteil
geschrieben; Lesen→Schreiben ist für alle 1028 Flach-GNDs bytegleich. `1@exsr` bleibt ohne Kacheln
(seine `.gat` ändert `map flat` nicht und schreibt sie daher nicht ins Flach-Archiv).

Der frühere Kandidat `analysis/_work/dark/odin-dark-candidate.grf` (59 Maps, 23 MB) ist durch den
Ordner `floor` ersetzt (68 Bodendateien, gleiche Methode, zusätzlich rush/bat_c-Grounds,
UV der Kachel jetzt immer ganz statt teils aus der Original-Fläche).

### 2.6 Ladereihenfolge, Pfade, Kodierung **[gemessen]**

`DATA.INI`: `0=odin.grf`, `1=odin-dark.grf`, danach die GRFs des Clients (Dev-Client: cdata, core,
cpal, data). Prüfung aller Staging-Pfade gegen den Dev-Client (`analysis/_work/competitive/conflicts.json`):

* **odin.grf vor odin-dark.grf:** `effects` enthält `texture\effect\hanmoon1.tga`/`hanmoon2.tga`
  (darkworld: 1×1 Magenta), die auch odin.grf (LGP-Kacheln) hat – odin.grf gewinnt, die LGP bleibt.
  Sonst keine Überschneidung mit odin.grf (`data\odinguard\profile.txt`, hanmoon1/2).
* **odin-dark.grf vor den Client-GRFs:** überschreibt auch Dateien aus `cdata.grf` (effects 8, other
  11, flat-maps-all 21) und `cpal.grf` (nodelay: 49 von 56 Körper-Sprites kommen im Dev-Client aus
  cpal.grf; die nodelay-Sprites ersetzen sie – eigene Färbungen/Paletten prüfen **[vermutet]**).
* **Groß/Klein:** 22 Pfade unterscheiden sich vom data.grf-Eintrag nur in der Schreibweise
  (trees 19, z. B. `JA_FF1.bmp`; ui 1 `prt_sewB1.bmp`; flat-maps-all 2 `Tha_t05.gat/.gnd`). Der Client
  vergleicht ohne Groß/Klein **[gelesen, Teil 1]**; `rgrf build` lehnt Kollisionen ab, im Pack gibt es
  keine. Alle Zielpfade sind klein geschrieben (Staging auf macOS ist case-insensitiv).
* **Kodierung:** alle Namen sind cp949-darstellbar; extrahiert mit `--names cp949`, gebaut mit
  `--names cp949`. Die Rohbytes stimmen mit data.grf überein (z. B. `몬스터` = `b8 f3 bd ba c5 cd`,
  verglichen mit `rgrf --names raw ls`).
* **Zwischen Kategorien:** Überschneidungen nur bei den Map-Varianten (gewollt, s. Kombinationsregel)
  und den Kachel-Texturen (identische Dateien).

### 2.7 Was RustyGRF (noch) nicht kann **[gemessen]**

* `map flat` hat keine Option für GRF Editors Kachel-Texturen (GAT-codierte `c*.bmp`, „custom
  textures“); deshalb setzt `tools/pack/competitive.py floor` die Kacheln auf die GND (Python).
* Kein Befehl, um GND-Flächen neu zu texturieren oder leere Zellen zu füllen (dasselbe).
* `map info` zeigt bei GND keine Zahl der Lightmaps, Flächen, Wände und Höhen; der Vergleich lief über
  einen eigenen Parser.
* „Simplify maps“ (Modelle teilweise behalten, wie Thoth) gibt es nur als Ganz-oder-gar-nicht
  (`--remove-models`/`--remove-objects`), keine Auswahl einzelner Modelle.
* `extract` nimmt nur ein Muster (keine Liste/Manifest-Datei), `ls`-Muster kennen keine
  Zeichenklassen (`[an]`); `-q` unterdrückt die Zusammenfassung von `extract` nicht.
* Kein Bild-/Sprite-Vergleich (Transparenz-Anteil, Größe gegenüber Vanilla) als Befehl; Werte für
  `trees` stammen aus der früheren Pillow-Messung.
* `map flat` endet mit Code 3, wenn Maps des Clients Dateien fehlen (air_evt, soccer3), obwohl das
  Archiv geschrieben ist; das Skript wertet 3 als Erfolg.

### 2.8 Offene Punkte

* Ingame-Test, ob 0xc8/0xc9 im 2018er Client flach pro Zelle gezeichnet werden (Variante A).
* Ob die Original-Geometrie von Volcano/Deluge/VG vier Seiten (Quadrat) oder rund ist; bestimmt, ob
  Variante B Quadrate oder Ringe zeigt (WoE-RO-Spieler sahen Quadrate).
* Ob die `cps.dll` des Clients LZMA kann (sonst zlib/zopfli).
* Ingame-Test der Kategorien im 2018er Client: Optik `floor`/`floor-flat-*`, nodelay mit den
  cpal.grf-Sprites, Kamera-Tabellen aus `other`.
* Studio: `recipe.toml` lesen (Standard, Varianten, Kombinationsregel) statt der freien Ordnerliste.

---

## Herkunftsliste

| Datei / Ordner | Herkunft |
|---|---|
| `analysis/_work/lgp/found/*` | Kopien aus dark2.grf, woerol.grf, Thoth.grf, desu.grf (`found/PROVENANCE.tsv`), nur Analyse |
| `analysis/_work/lgp/own/*` | eigene Zeichnungen (`tools/make_tiles.py`, 2026-11-23) |
| `analysis/_work/dark/pack/data/texture/**` (135) | `dark.grf`, `data\Texture\…` (MD5 in `uebernehmbar.tsv`; in dark2 identisch) |
| `analysis/_work/dark/pack/data/*.gnd` (59) | abgeleitet aus `client-dev/ragnarok/data.grf` (Gravity-Original) + Kachelzuordnung aus dark.grf |
| `~/Dev/OdinGuard-assets/competitive/{trees,barricades,pneuma,effects,ui,other}/` | `dark2.grf` (sha256 `045c71de65804624e28fe0a7c4e8225a6408ead27831911a5f9e1f21daa1a748`), einzelne Pfade `dark.grf` (`f7baefcdcb8475a4c37e268329cefedcb75357165b7cf1f069c662596ba906ae`); Liste je Datei in `assets/competitive/manifests/` |
| `~/Dev/OdinGuard-assets/competitive/nodelay/` | `nodelay.grf` (`0dba7a75f6a5f48c2588fb64387b6529d594e4c0ab04b27879eb4911508c8f11`) |
| `~/Dev/OdinGuard-assets/competitive/flat-maps-*/` | `rgrf map flat` aus `client-dev/ragnarok` (data.grf `cc0f830827bf9468316e4c56a727140ebf9034c7be17ee282c6b073c843d5737`, cdata, core, cpal) |
| `~/Dev/OdinGuard-assets/competitive/floor*/` | GND des Clients bzw. der Flachmaps + Kacheln und WoE-Markierungen aus dark2.grf (`tools/pack/competitive.py floor`) |
| `analysis/_work/lgp/ref/*.js` | roBrowserLegacy (GitHub MrAntares/roBrowserLegacy, master), nur gelesen |
| Unit-Nutzung | xAthena `db/pre-re/skill_db.yml`, `db/re/skill_db.yml`, `src/map/skill.hpp` |

Quell-GRFs (unverändert): `<Sammlung Cheat-Software und Cheat-GRFs>/`
(`dark.grf`, `dark2.grf`, `woe.grf`, `woebg.grf`, `Thoth.grf`, `nodelay.grf`, `WOERO/woero.grf`, `WOERO/woerol.grf`, `WOERO/woerop.grf`,
`Desu GRF 3-15-2018/desu.grf`), `<Sammlung Burning Legacy>/` (`burn.grf`,
`kdom.grf`: keine LGP-/Zellkachel-Funde, nur Listing).

## Reproduktion

Skripte unter `analysis/_work/{lgp,dark}/tools/`: `strinfo.py` (STR), `imgstat.py`, `sheet.py`
(Kontaktbögen), `idx.py`/`cat.py` (Hash-Index, Gruppen), `rofmt.py` (GAT/GND/RSW), `texstats.py`,
`sprcmp.py`, `gatall.py`, `classify.py`, `build_fair_gnd.py`, `make_tiles.py`. Die großen
Zwischendaten (Hash-JSON der Vanilla-GRFs, extrahierte Maps) wurden gelöscht; neu erzeugen mit
`rgrf --names cp949 --json hash <grf>` und `rgrf --names cp949 extract <grf> maps/<quelle> 'data\<map>.gnd'`.

Competitive-Pack: `DARK_DIR=<Ordner mit dark.grf, dark2.grf, nodelay.grf> CLIENT_DIR=<Client mit DATA.INI>
tools/pack/build_categories.sh [ordner …]` (rgrf + Python 3.9, idempotent; zweiter Lauf bytegleich
geprüft; `TEST_GRF_DIR=…` baut und prüft je Ordner eine Test-GRF). Klassifikation und Prüfungen:
`analysis/_work/competitive/tools/{classify.py,cmp_flat.py,conflicts.py}`.
