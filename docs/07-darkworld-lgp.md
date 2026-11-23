# 07 – LGP-Zellkacheln und darkworld-Fairness

Stand: 2026-11-23. Werkzeuge: ausschließlich RustyGRF (`rgrf` 0.x CLI und MCP `rustygrf`) für alle
GRF-Zugriffe, Python + Pillow nur für extrahierte Dateien (Bildstatistik, STR/GND/GAT/RSW-Parser,
Kacheln zeichnen). Keine Quelldatei wurde verändert; alles Extrahierte liegt unter
`analysis/_work/{lgp,dark}/` (per `.gitignore` nicht versioniert).

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
| `spiderweb.tga` | woerol: 128×128 rotes Achteck; dark2/woe: 1024×1024 blauer Kreis | Fiber Lock stark hervorgehoben (Sichtvorteil) |
| `stormgust.str`, `lord.str`, `meteor1-4.str` | `Thoth.grf` | SG: 2 Ebenen mit `21.bmp` (64×64 grau), 192×96 Bildschirm-Raute; LoV/MS auf `0lordbl.bmp` reduziert | vereinfachte Bildschirmeffekte, **keine** Zellkacheln |
| `Pneuma01-10.bmp` + `pneuma1-3.str` | dark.grf, woe.grf | 32×32 Ziffern 10…1 | Pneuma-Restzeit-Countdown → **Informationsvorteil**, nicht übernehmen |

Kopiert nach `analysis/_work/lgp/found/` (25 Dateien, Präfix = Quelle) mit
`found/PROVENANCE.tsv` (Quell-GRF, GRF-Pfad, MD5, Größe, Messung). Diese Dateien sind
Analysematerial und werden **nicht** ausgeliefert.

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

## Teil 2 – darkworld-Fairness

### 2.1 Diff-Statistik **[gemessen]**

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
Begehbarkeit ändert sich dadurch serverseitig nicht; clientseitig drohen Klick-/Pfad-Desyncs.

### 2.3 Klassifikation

| Gruppe | Befund [gemessen] | Klasse |
|---|---|---|
| Maps `.rsw` (0 Modelle) | Wände, Bäume, Säulen, Burgteile unsichtbar | **UNFAIR** (Sicht durch Wände/Objekte) |
| Maps `.gnd` (flach, ohne Wände) | Gelände eben, keine Wandflächen | **UNFAIR** |
| Maps `.gat` (Höhe 0, einzelne Typänderungen) | s. o. | unfair/riskant, verworfen |
| `.extra` (60) | XML `<models /><lights />` (Begleitdaten der flachen Maps) | verworfen |
| Bodenkacheln `c*.bmp`, `points\*`, `woe\*`, `NSA` (286+34+15+1) | 64×64 BMP24, 2–3 Farben, dunkel, Zellraster | **kosmetisch** (nur statische GAT-/Karteninfo) |
| Modelltexturen `나무잡초꽃` (101), `기타마을` (36), `내부소품` (7), `모로코` (2), `게페니아` (1) | 117 zu ≥98 % Magenta/transparent, 9 deutlich transparenter, Rest kleiner/dunkler | **UNFAIR** (Bäume/Objekte weg) |
| Effekt-Texturen dark2 (287) | 139 voll transparent, 152 transparenter, 163 einfarbig, 129 verkleinert | grau: Effekt-Reduktion (≈ clientseitige LGP für *alle* Skills); einzelne unfair |
| Effekt-STR dark2 (99) | vereinfachte Skill-Effekte (z. B. `stormgust.str` 8 Ebenen statt 41 in kRO) | grau |
| `Pneuma01-10` + `pneuma*.str` | Countdown 10…1 über Pneuma | **UNFAIR** (Restzeit-Info) |
| `tarot01-14.tga` (dark) | Karten als Textlabel | grau (Info-Aufbereitung) |
| `lockon128.tga` 600×600, `magic_target.tga` Fadenkreuz | größere Ziel-/AoE-Marker | grau (UX) |
| `foot_l/r*.tga` | Fußspuren grün eingefärbt | [vermutet] Sichtvorteil → verworfen |
| `spiderweb.tga` | Fiber Lock knallig/riesig | unfair-grau |
| Sprites Pflanzen (flora, geographer, hydra, mandragora, parasite) | Sprite 19×24 statt bis 109×139 | **UNFAIR** (Bio-Cannibalize-Blocker kaum sichtbar) |
| Sprite `barricade` | 23 kleine Bilder statt 52×126 | **UNFAIR** (WoE-SE-Barrikade) |
| MVP-/Boss-ACTs (22 Dateien) | Ebenen umskaliert (Faktor 0,5 bis 3,0 gegenüber Vanilla, z. B. Eddga 1,0→2,0, Dark Illusion 1,0→0,5) | PvM, nicht relevant, verworfen |
| `s_empel_1/2.act` | 96 statt 176 Frames | grau |
| `status-stun/sleep.spr`, `얼음땡.spr` | Status-Grafik vergrößert (45×45 statt 17×17; 163×200 statt 104×121) | grau (Statuserkennung) |
| `status-curse.spr` | 1×1 (entfernt) | kosmetisch |
| `숫자`, `msg`, `bluemsg`, `sight`, `smoke`, `cursors` | andere Schrift, kleiner | kosmetisch, nicht map-bezogen |
| UI (214) | 8 voll transparent, 36 verkleinert, Skin/Branding | kosmetisch, nicht map-bezogen, verworfen |
| `indoorrswtable.txt` | leer (Vanilla: 149 Einträge, u. a. bat_room) | Kamera-Freiheit in Innenmaps, verworfen |
| `viewpointtable.txt` (dark2) | alle Einträge auskommentiert | Kamera, verworfen |
| `ba_frostjoke.txt`, `dc_scream.txt` | Texte durch `@dance`-Befehle ersetzt | serverspezifisch, verworfen |

Weitere GRFs (nur Analyse) **[gemessen]**: `nodelay.grf` – 29 Job-ACTs mit gleichen Intervallen,
aber weniger Frames (z. B. Stalker 456 → 280) → kürzere Animationssperre (**UNFAIR**, „nodelay“).
`desu.grf` – Edit-Log nennt „Removed Blindness“, „Removed Auras“, vergrößerte MVPs, „tiled maps“
**[gelesen]**.

### 2.4 Übernehmbar / verworfen

* Eindeutige Pfade in dark ∪ dark2: **3705**. **Übernehmbar: 135** (97 `c*.bmp`, 24 `points\*`,
  14 `woe\*`), alle aus `dark.grf`, in `dark2.grf` identisch. **Verworfen: 3570.**
* Liste übernehmbar mit Original- und Zielpfad: `analysis/_work/dark/uebernehmbar.tsv`
  (Original `data\Texture\<name>` in dark.grf → Ziel `data\texture\<name>`).
* Jede Datei beider GRFs mit Gruppe, Vergleich zu Vanilla, Entscheidung, Begründung, MD5:
  `analysis/_work/dark/klassifikation_dark_dark2.tsv`.
* Verworfen (dark, 3181): 2448 Map-Dateien (flach, unfair), 214 UI, 204 ungenutzte Dark-Kacheln,
  117 + 9 transparente Modelltexturen, 60 `.extra`, 57 Sprites, 40 Effekte, 27 sonstige Modelltexturen,
  5 Texte/Sonstiges. dark2 zusätzlich 390 Effekt-Dateien.

### 2.5 Kandidat `odin-dark-candidate.grf` **[gemessen]** / **[Vorschlag]**

Die Dark-Kacheln wirken nur über eine `.gnd`. Statt der flachen dark-GND wurde je Map eine **faire
GND** erzeugt (`analysis/_work/dark/tools/build_fair_gnd.py`): Vanilla-GND aus data.grf (2018) mit
allen Höhen, Wänden und Lightmaps; nur die Bodenflächen (Top-Surfaces) zeigen die Dark-Kachel der
Zelle (UV und Farbe aus dark, Lightmap/Schatten aus Vanilla). Kacheln, deren 4 GAT-Zellen in dark
anders sind als in Vanilla, werden aus der Vanilla-GAT neu berechnet. `.rsw` (alle Modelle) und
`.gat` bleiben Vanilla und werden nicht mitgeliefert.

* Inhalt: 59 `.gnd` (aldeg_cas01–05, arug_cas01–03, bat_a01, bat_b01, bat_c01, bat_room,
  gefg_cas01–05, gld2_ald/gef/pay/prt, guild_vs1–5, payg_cas01–05, prtg_cas01–05, pvp_2vs2,
  pvp_room, pvp_y_1-2…8-2, schg_cas01–03, te_aldecas1–5, te_prtcas01–05) + 135 Texturen.
* Prüfung: Höhen, Wandflächen und Lightmaps aller 59 GND byte-gleich zu Vanilla (eigener Parser);
  `rgrf map check` mit DATA.INI (Kandidat, cdata, core, cpal, data): **59 Maps, 11 959 Dateien,
  0 Probleme**; `rgrf verify`: **194 Einträge, 0 Fehler, 0 Warnungen**.
* Größe: `analysis/_work/dark/odin-dark-candidate.grf` **23 008 687 B** (zlib 9; entpackt
  171,9 MB; davon GND 22,95 MB gepackt, Texturen 60 KB).

| Variante | Größe | Änderung | Hinweis |
|---|---|---|---|
| zlib (Build) | 23 008 687 B | – | jeder Client |
| `rgrf image reduce` (8-bit-BMP, 135 Bilder verlustfrei) | 22 983 146 B | −0,11 % | jeder Client |
| reduce + zopfli neu komprimiert | 21 082 887 B | −8,4 % | jeder Client, 73 s |
| reduce + LZMA | 14 397 277 B | −37,4 % | nur mit LZMA-fähiger `cps.dll` |

Alle Varianten `rgrf verify` 0 Fehler. Die GND-Größe kommt fast nur von den Vanilla-Lightmaps.

Fairness des Kandidaten **[Vorschlag]**: Wände, Modelle, Höhen und Schatten bleiben; geändert ist nur
die Bodenfarbe mit GAT-Raster (Information, die der Client ohnehin hat) und statische Markierungen
der WoE-SE-Burgen. Vor Auslieferung im Client prüfen (Optik, Wasser, Performance) und über
OdinGuard-Ressourcenregeln für alle gleich ausliefern.

### 2.6 Offene Punkte

* Ingame-Test, ob 0xc8/0xc9 im 2018er Client flach pro Zelle gezeichnet werden (Variante A).
* Ob die Original-Geometrie von Volcano/Deluge/VG vier Seiten (Quadrat) oder rund ist; bestimmt, ob
  Variante B Quadrate oder Ringe zeigt (WoE-RO-Spieler sahen Quadrate).
* Ob die `cps.dll` des Clients LZMA kann (sonst zlib/zopfli).
* Ob die WoE-SE-Markierungen (`points\*`, `woe\*`) gewollt sind oder nur das neutrale Raster.
* Herkunft/Urheberrecht der dark-Kacheln (Autor unbekannt; einfache 2–3-Farben-Raster). Alternative:
  Kacheln mit `make_tiles.py`-Logik selbst zeichnen und `build_fair_gnd.py` darauf zeigen lassen.

---

## Herkunftsliste

| Datei / Ordner | Herkunft |
|---|---|
| `analysis/_work/lgp/found/*` | Kopien aus dark2.grf, woerol.grf, Thoth.grf, desu.grf (`found/PROVENANCE.tsv`), nur Analyse |
| `analysis/_work/lgp/own/*` | eigene Zeichnungen (`tools/make_tiles.py`, 2026-11-23) |
| `analysis/_work/dark/pack/data/texture/**` (135) | `dark.grf`, `data\Texture\…` (MD5 in `uebernehmbar.tsv`; in dark2 identisch) |
| `analysis/_work/dark/pack/data/*.gnd` (59) | abgeleitet aus `client-dev/ragnarok/data.grf` (Gravity-Original) + Kachelzuordnung aus dark.grf |
| `analysis/_work/lgp/ref/*.js` | roBrowserLegacy (GitHub MrAntares/roBrowserLegacy, master), nur gelesen |
| Unit-Nutzung | xAthena `db/pre-re/skill_db.yml`, `db/re/skill_db.yml`, `src/map/skill.hpp` |

Quell-GRFs (unverändert): `/Users/k/Downloads/RANDOM RESOURCES/RO CHEAT SOFTWARE and CHEAT GRFs etc/`
(`dark.grf`, `dark2.grf`, `woe.grf`, `woebg.grf`, `Thoth.grf`, `nodelay.grf`, `WOERO/woerol.grf`,
`Desu GRF 3-15-2018/desu.grf`), `/Users/k/Downloads/RANDOM RESOURCES/Burning Legacy/` (`burn.grf`,
`kdom.grf`: keine LGP-/Zellkachel-Funde, nur Listing).

## Reproduktion

Skripte unter `analysis/_work/{lgp,dark}/tools/`: `strinfo.py` (STR), `imgstat.py`, `sheet.py`
(Kontaktbögen), `idx.py`/`cat.py` (Hash-Index, Gruppen), `rofmt.py` (GAT/GND/RSW), `texstats.py`,
`sprcmp.py`, `gatall.py`, `classify.py`, `build_fair_gnd.py`, `make_tiles.py`. Die großen
Zwischendaten (Hash-JSON der Vanilla-GRFs, extrahierte Maps) wurden gelöscht; neu erzeugen mit
`rgrf --names cp949 --json hash <grf>` und `rgrf --names cp949 extract <grf> maps/<quelle> 'data\<map>.gnd'`.
