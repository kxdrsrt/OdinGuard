# LGP-Audit – Lite Ground Skills (Bodenskills als flache Farbzellen)

Stand: 2026-11-24. Ziel: **jeder** Boden-/Flächenskill bekommt für Spieler mit `@lgp` statt der schweren
Animation eine flache Farbzelle pro Zelle, in der erwarteten Farbe (Storm Gust blau, Lord of Vermilion
gelb, Meteor Storm rot …), wie Gepards LGP und die darkworld-/WoE-RO-GRFs.

Kennzeichnung: **[gemessen]** = mit Werkzeug an Dateien/Binärcode gemessen (Python/Pillow, RustyGRF `rgrf`,
statische Disassemblierung von `alpha.exe` mit Capstone – nichts ausgeführt) · **[gelesen]** = aus
Quelltext/Konfiguration gelesen (xAthena, roBrowserLegacy) · **[vermutet]** = plausible Ableitung, im Spiel
nicht geprüft.

Quellen (nur gelesen): xAthena `/Users/k/Dev/xAthena-HR` (Branch `development` = HR, `m-lr` = M-LR),
Client `client-dev/ragnarok` (`alpha.exe` 2018-06-20, SHA-256 `9f7a0e09…b127`; `data.grf`, `cdata.grf`,
`core.grf`), Mod-GRFs aus `RANDOM RESOURCES` (WOERO `woero/woerol/woerop`, `dark`, `dark2`, `woe`, `woebg`,
`Thoth`, `nodelay`, Burning Legacy `burn`, `kdom`, zusätzlich `desu`), roBrowserLegacy (GitHub
`MrAntares/roBrowserLegacy` @ `2f9a9d6`). Arbeitsordner (git-ignoriert): `analysis/_work/lgp2/`
(Skripte `tools/`, Daten `data/`, Auszüge `mods/` und `vanilla/`, Kontaktbögen `sheets/`).

---

## 0. Kurzfassung

1. **Server:** HR (`development`) und M-LR (`m-lr`) sind **beide Pre-Renewal** (`#define PRERE` in
   `src/custom/defines_pre.hpp`, `deploy/Dockerfile` ohne Renewal-Flag) **[gelesen]**. Skill-Datenbank,
   Skill-Bäume und Job-Liste sind auf beiden Branches gleich (Parser-Vergleich: 0 abweichende Zeilen)
   **[gemessen]**. 3rd/4th Jobs, Doram, Summoner existieren in `db/pre-re/job_stats.yml`/`skill_tree.yml`
   nicht → deren Skills sind nicht nutzbar **[gelesen, Checkpoint 001]**.
2. **243** Skills haben eine Unit oder Ziel „Ground“ (201 mit Unit, 42 nur Positionseffekt). **94** davon
   sind auf HR und M-LR nutzbar (Job-Baum, Söldner, Gilde, Abracadabra, Item-Autospell, Heirat), **149**
   nicht (124 3rd/4th/Renewal-Jobs, 14 NPC/Monster, 10 Homunculus S/Elementar, 1 sonstiger) **[gemessen]**.
3. **Heutige LGP greift nur bei unsichtbaren Dummy-Units** (`unit_id == UNT_DUMMYSKILL` in
   `clif_getareachar_skillunit`) und unterdrückt `ZC_NOTIFY_GROUNDSKILL` **[gelesen]**. Damit sind von den
   94 nutzbaren Skills nur **11** (Klasse A) ohne Serveränderung abdeckbar; **8** (Klasse B: Fire Wall,
   Pneuma, Sanctuary, Magnus, Quagmire, Venom Dust, Gravitation, Kaensin) brauchen eine kleine
   Hook-Erweiterung, **6** (Klasse P: Arrow Shower, Hammer Fall …) haben gar keine Unit („Flash-Zellen“ nötig),
   **33** sind optional (Lieder, Volcano/Deluge/VG, Basilica …), **36** werden nie ersetzt (Fallen, LP,
   Eiswand, Warp …).
4. **Wichtigster Client-Befund [gemessen, statisch]:** Die Units 0xc8/0xc9 (`hanmoon1/2.tga`, heute
   OdinGuard-Standard) werden vom 2018er Client **immer blau eingefärbt** (Vertexfarbe `0x37,0x37,0xff`,
   additiv). Die orange `hanmoon2.tga` aus v1 wäre im Spiel dunkel/unsichtbar. Ohne Client-Eingriff gibt es
   **keine zweite echte Farbe** auf einer flachen, ungenutzten Unit: alle anderen flachen Units sind von
   genutzten Skills belegt (LP 0x9d, Spider Web 0xb7) oder drehen sich, haben eine weiße Aura und teilen ihre
   Textur mit Adrenaline Rush/Liedern (0xcc, 0xcf).
5. **Vorschlag:** zweistufig. **Stufe G (sofort, nur GRF):** 0xc8 = blaue gefüllte Zelle, 0xc9 = blaue
   Umriss-Zelle (neue `hanmoon1/2.tga`). **Stufe P (Ziel, OdinGuard-DLL patcht 2 Aufrufe + 7 Sprungtabellen-
   Einträge):** 9 echte Farben auf 9 Units (blau 0xc8, cyan 0xc9, gelb 0xba, rot 0xd1, orange 0xdf, grün
   0xec, violett 0x103, braun 0x106, rosa 0x107) mit eigenen Texturen `odinlgp_<farbe>.tga` – **null
   Kollateralschaden**, weil kein anderer Effekt diese Dateinamen lädt und diese Unit-IDs im Vanilla-Client
   nichts zeichnen. Texturen liegen fertig in `assets/odin-grf/lgp/data/texture/effect/`.
6. **WoE-RO-Look belegt [gemessen]:** WoE-RO (`woerol.grf`, identisch in `dark2.grf`) ersetzt die
   Bodentexturen der Sage-Units durch 256×256-Bänder: `ring_blue` blau (0,102,255), `ring_red` rot
   (254,60,29), `magic_green` **gelb** (204,204,51). Zusammen mit dem Server (SG→Deluge, MS→Volcano,
   LoV→Violent Gale) ergibt das SG blau, MS rot, LoV gelb – die Farben, die Spieler erwarten.

---

## 1. Server

### 1.1 Build, Branches, Job-Sperre [gelesen]

| Punkt | HR (`development`) | M-LR (`m-lr`, = `origin/m-lr` `d22568c61`) |
|---|---|---|
| Modus | Pre-Renewal: `src/custom/defines_pre.hpp` `#define PRERE` | gleich |
| Docker | `./configure --enable-packetver=20180620` (kein Renewal-Schalter) | gleich |
| Skill-DB | `db/pre-re/skill_db.yml` + `db/import/skill_db.yml` (MO_BODYRELOCATION Delay, GD_CHARGESHOUT_*) | gleich [gemessen: 243 Zeilen, 0 Unterschiede] |
| 3rd Jobs | nicht in `db/pre-re/job_stats.yml`/`skill_tree.yml` → `pc_jobchange` lehnt ab | gleich |
| LGP-Code | `og_map_lgp_unit` mit Stilen (`square`, `circle`, `set2..14`), `@aoes` | ältere Fassung ohne Stile, `og_map_toggle_lgp` |
| `og_lgp` | 89/85/539→0xc8, 83→0xc9, 91/540/541→0x9c, circle/set2-Zeilen | 89/539→0x9b, 83→0x9a, 85/540→0x9c, **91/541→0x9d (Land-Protector-Optik!)** |

M-LR liegt beim OdinGuard-Stand hinter `development` (12 Dateien, u. a. `og_map.cpp`, `conf/odinguard_athena.conf`)
**[gemessen, `git diff --stat`]**. Solange M-LR Heaven's Drive und Raigekisai als Land Protector zeichnet, ist
das im WoE irreführend → beim nächsten Merge mitnehmen.

### 1.2 So arbeitet die LGP heute [gelesen]

* `clif_getareachar_skillunit` (`src/map/clif.cpp` ~5600): nur wenn Header `0x09ca`, `visible` und
  **`unit_id == UNT_DUMMYSKILL`** und `og_lgp_unit(skill_id) != 0`, schreibt der Server pro Betrachter die
  Unit aus `og_map_lgp_unit` ins Paket. Echte Units (Fire Wall, Sanctuary, Magnus, Pneuma-Mitte …)
  bleiben unverändert.
* `clif_skill_poseffect` (~6330): Betrachter mit LGP bekommen das `ZC_NOTIFY_GROUNDSKILL` nicht
  (Storm Gust, LoV, HD … und bei Meteor jeder einzelne Einschlag, der aus dem Unit-Timer kommt).
* `RangedSingleUnit`-Skills (Pneuma, Basilica) schicken alle Zellen außer der Mitte als `UNT_DUMMYSKILL`
  → mit einem `og_lgp`-Eintrag werden schon heute die Randzellen zu Farbzellen, die Mitte bleibt die
  Original-Animation **[gelesen, Zeile 5550]**.
* Gildenauren (`state.guildaura`) und Graffiti verlassen die Funktion vorher; versteckte Fallen
  (`visible == false`) bleiben `UNT_DUMMYSKILL`.
* **Achtung Eiswand:** der LGP-Zweig macht `return` vor `clif_changemapcell` – Ice Wall (87) darf nie in
  `og_lgp`, sonst sieht der LGP-Spieler die Zellsperre nicht.
* `og_lgp: 540,…` ist wirkungslos: 540 ist `NJ_HUUJIN` (Einzelziel, keine Unit) **[gelesen]**.

### 1.3 Nutzbarkeit [gemessen]

Quelle je Skill (`tools/server_skills.py`): Job-Bäume `db/pre-re/skill_tree.yml` (+import), Gilde,
Homunculus (pre-re), Söldner (`db/mercenary_db.yml`), Abracadabra (`db/abra_db.yml`, 155 Einträge),
Item-`itemskill`/`bAutoSpell*` (ohne auskommentierte Skripte), Heirat (WE_CALLPARTNER). HR und M-LR
liefern identische Mengen.

---

## 2. Liste aller Boden-/Flächenskills (Lieferung 1)

**Klassen:** **A** = unsichtbare Dummy-Units + schwere Animation, funktioniert mit dem heutigen Hook ·
**B** = eigene sichtbare Unit, braucht die Hook-Erweiterung (Abschnitt 5.3) · **P** = nur Positionseffekt
(keine Unit), braucht „Flash-Zellen“ · **O** = optional (Info-Unit, Standard: Vanilla lassen) ·
**N** = nie ersetzen. „Client heute“: Unit→Client-Effekt (aus der Unit-Sprungtabelle von `alpha.exe`,
Abschnitt 4) bzw. `Skill:`/`Pos:` = Effekt-ID des Skills laut roBrowser `SkillEffect.js` **[gelesen]**.
„WoE/BG/PvP“: ja, außer Familien-/Kosmetik-/Warp-Skills.

### 2.1 Nutzbar auf HR und M-LR (94; alle 1./2./Trans-/Erweitert-Klassen, Söldner, Gilde)

| ID | Skill | Ziel | Unit (Server) | Layout | Client heute | WoE/BG/PvP | LGP-Klasse | Farbe |
|---|---|---|---|---|---|---|---|---|
| 12 | MG_SAFETYWALL (Safety Wall) | Ground | UNT_SAFETYWALL 0x7e | – | Unit→315 EF_GLASSWALL2; STR safetywall | ja | O | pink |
| 18 | MG_FIREWALL (Fire Wall) | Ground | UNT_FIREWALL 0x7f | -1 | Unit→25 EF_FIREWALL; Code 0x920160; Pos: EF_FIREWALL | ja | B | orange |
| 21 | MG_THUNDERSTORM (Thunderstorm) | Ground | UNT_DUMMYSKILL 0x86 | – | unsichtbar (Dummy) | ja | A | yellow |
| 25 | AL_PNEUMA (Pneuma) | Ground | UNT_PNEUMA 0x85 | 1 | STR (eigener Pfad, pneuma1-3.str); Pos: EF_PNEUMA | ja | B | green |
| 27 | AL_WARP (Warp Portal) | Ground | UNT_WARP_ACTIVE 0x81 | – | Unit→316 EF_READYPORTAL2; roBrowser CYLINDER | nein (WoE/BG gesperrt) | N | - |
| 47 | AC_SHOWER (Arrow Shower) | Ground | (kein Unit) | – |  | ja | P | brown |
| 69 | PR_BENEDICTIO (B.S. Sacramenti) | Ground | (kein Unit) | – | Skill: EF_BENEDICTIO | ja | P | cyan |
| 70 | PR_SANCTUARY (Sanctuary) | Ground | UNT_SANCTUARY 0x83 | -1 | Unit→319 EF_BOTTOM_SANC; roBrowser CYLINDER; Skill: EF_SANCTUARY; Pos: EF_BOTTOM_SANC | ja | B | green |
| 79 | PR_MAGNUS (Magnus Exorcismus) | Ground | UNT_MAGNUS 0x84 | -1 | Unit→318 EF_BOTTOM_MAG; roBrowser CYLINDER; Skill: EF_MAGNUS; Pos: EF_BOTTOM_MAG | ja | B | cyan |
| 80 | WZ_FIREPILLAR (Fire Pillar) | Ground | UNT_FIREPILLAR_WAITING 0x87 | – | Unit→138 EF_FIREPILLARON; Feuersäule wartend; Skill: EF_FIREPILLAR; Pos: EF_FIREPILLARON | ja | O | red |
| 83 | WZ_METEOR (Meteor Storm) | Ground | UNT_DUMMYSKILL 0x86 | – | unsichtbar (Dummy); Skill: EF_METEORSTORM | ja | A | red |
| 85 | WZ_VERMILION (Lord of Vermilion) | Ground | UNT_DUMMYSKILL 0x86 | 1:5/2:5/3:5/4:5/5:5/6:5/7:5/8:5/9:5/10:5/11:7 | unsichtbar (Dummy); Skill: EF_LORD | ja | A | yellow |
| 86 | WZ_WATERBALL (Water Ball) | Attack | UNT_DUMMYSKILL 0x86 | 1:0/2:1/3:1/4:2/5:2/6:2/7:2/8:2/9:2/10:2 | unsichtbar (Dummy) | ja | A | blue |
| 87 | WZ_ICEWALL (Ice Wall) | Ground | UNT_ICEWALL 0x8d | -1 | Unit→74 EF_ICEWALL; Eiswand-Block (3D); Pos: EF_ICEWALL | ja | N | - |
| 89 | WZ_STORMGUST (Storm Gust) | Ground | UNT_DUMMYSKILL 0x86 | 4 | unsichtbar (Dummy); Skill: EF_STORMGUST | ja | A | blue |
| 91 | WZ_HEAVENDRIVE (Heaven's Drive) | Ground | UNT_DUMMYSKILL 0x86 | 2 | unsichtbar (Dummy); Skill: EF_HEAVENSDRIVE | ja | A | brown |
| 92 | WZ_QUAGMIRE (Quagmire) | Ground | UNT_QUAGMIRE 0x8e | 2 | Unit→1161; roBrowser ?; Pos: EF_QUAGMIRE | ja | B | brown |
| 110 | BS_HAMMERFALL (Hammer Fall) | Ground | (kein Unit) | – | Skill: EF_CRASHEARTH | ja | P | brown |
| 115 | HT_SKIDTRAP (Skid Trap) | Ground | UNT_SKIDTRAP 0x90 | – | Fallen-Modell (RSM, eigener Pfad); Skill: EF_SKIDTRAP | ja | N | - |
| 116 | HT_LANDMINE (Land Mine) | Ground | UNT_LANDMINE 0x93 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 117 | HT_ANKLESNARE (Ankle Snare) | Ground | UNT_ANKLESNARE 0x91 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 118 | HT_SHOCKWAVE (Shockwave Trap) | Ground | UNT_SHOCKWAVE 0x94 | – | Fallen-Modell (RSM, eigener Pfad); Skill: EF_SHOCKWAVE | ja | N | - |
| 119 | HT_SANDMAN (Sandman) | Ground | UNT_SANDMAN 0x95 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 120 | HT_FLASHER (Flasher) | Ground | UNT_FLASHER 0x96 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 121 | HT_FREEZINGTRAP (Freezing Trap) | Ground | UNT_FREEZINGTRAP 0x97 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 122 | HT_BLASTMINE (Blast Mine) | Ground | UNT_BLASTMINE 0x8f | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 123 | HT_CLAYMORETRAP (Claymore Trap) | Ground | UNT_CLAYMORETRAP 0x98 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 125 | HT_TALKIEBOX (Talkie Box) | Ground | UNT_TALKIEBOX 0x99 | – | Fallen-Modell (RSM, eigener Pfad) | nein | N | - |
| 130 | HT_DETECTING (Detect) | Ground | (kein Unit) | – | Skill: EF_DETECTING | ja | N | - |
| 140 | AS_VENOMDUST (Venom Dust) | Ground | UNT_VENOMDUST 0x92 | -1 | Unit→171 EF_VENOMDUST2; Code 0x942be0; Skill: EF_VENOMDUST; Pos: EF_VENOMDUST2 | ja | B | purple |
| 220 | RG_GRAFFITI (Scribble) | Ground | UNT_GRAFFITI 0xb0 | – | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] | nein | N | - |
| 221 | RG_FLAGGRAFFITI (Piece) | Ground | (kein Unit) | – |  | nein | N | - |
| 222 | RG_CLEANER (Remover) | Ground | (kein Unit) | – |  | ja | N | - |
| 229 | AM_DEMONSTRATION (Bomb) | Ground | UNT_DEMONSTRATION 0xb1 | – | Unit→302 EF_DEMONSTRATION; roBrowser SPR; Pos: EF_DEMONSTRATION | ja | O | orange |
| 232 | AM_CANNIBALIZE (Summon Flora) | Ground | (kein Unit) | – |  | selten | N | - |
| 233 | AM_SPHEREMINE (Summon Marine Sphere) | Ground | (kein Unit) | – |  | ja | N | - |
| 254 | CR_GRANDCROSS (Grand Cross) | Self | UNT_DUMMYSKILL 0x86 | -1 | unsichtbar (Dummy); Skill: EF_GRANDCROSS | ja | A | cyan |
| 264 | MO_BODYRELOCATION (Snap) | Ground | (kein Unit) | – | Skill: EF_MAKEBLUR | ja | N | - |
| 285 | SA_VOLCANO (Volcano) | Ground | UNT_VOLCANO 0x9a | 3 | Unit→239 EF_BOTTOM_VO; PropertyGround (prim 0x24, 8-eckiger Trichter); Pos: EF_BOTTOM_VO | ja | O | red |
| 286 | SA_DELUGE (Deluge) | Ground | UNT_DELUGE 0x9b | 3 | Unit→240 EF_BOTTOM_DE; PropertyGround (prim 0x24, 8-eckiger Trichter); Pos: EF_BOTTOM_DE | ja | O | blue |
| 287 | SA_VIOLENTGALE (Whirlwind) | Ground | UNT_VIOLENTGALE 0x9c | 3 | Unit→241 EF_BOTTOM_VI; PropertyGround (prim 0x24, 8-eckiger Trichter); Pos: EF_BOTTOM_VI | ja | O | green |
| 288 | SA_LANDPROTECTOR (Magnetic Earth) | Ground | UNT_LANDPROTECTOR 0x9d | 1:3/2:3/3:4/4:4/5:5 | Unit→242 EF_BOTTOM_LA; Flachquad (prim 0x27), Modus 1; Pos: EF_BOTTOM_LA | ja | N | - |
| 306 | BD_LULLABY (Lullaby) | Self | UNT_LULLABY 0x9e | 4 | Unit→278 EF_BOTTOM_LULLABY; Lied-/Fluchzelle (prim 0x2f), Modus 1; Skill: EF_BOTTOM_LULLABY | ja | O | - |
| 307 | BD_RICHMANKIM (Mental Sensing) | Self | UNT_RICHMANKIM 0x9f | 4 | Unit→279 EF_BOTTOM_RICHMANKIM; Lied-/Fluchzelle (prim 0x2f), Modus 3; Skill: EF_BOTTOM_RICHMANKIM | ja | O | - |
| 308 | BD_ETERNALCHAOS (Down Tempo) | Self | UNT_ETERNALCHAOS 0xa0 | 4 | Unit→280 EF_BOTTOM_ETERNALCHAOS; Wirbel (prim 0x32), Modus 8; Skill: EF_BOTTOM_ETERNALCHAOS | ja | O | - |
| 309 | BD_DRUMBATTLEFIELD (Battle Theme) | Self | UNT_DRUMBATTLEFIELD 0xa1 | 4 | Unit→281 EF_BOTTOM_DRUMBATTLEFIELD; Lied-/Fluchzelle (prim 0x2f), Modus 2; Skill: EF_BOTTOM_DRUMBATTLEFIELD | ja | O | - |
| 310 | BD_RINGNIBELUNGEN (Harmonic Lick) | Self | UNT_RINGNIBELUNGEN 0xa2 | 4 | Unit→282 EF_BOTTOM_RINGNIBELUNGEN; Lied-/Fluchzelle (prim 0x2f), Modus 7; Skill: EF_BOTTOM_RINGNIBELUNGEN | ja | O | - |
| 311 | BD_ROKISWEIL (Classical Pluck) | Self | UNT_ROKISWEIL 0xa3 | 4 | Unit→283 EF_BOTTOM_ROKISWEIL; Code 0x94a780; Skill: EF_BOTTOM_ROKISWEIL | ja | O | - |
| 312 | BD_INTOABYSS (Power Chord) | Self | UNT_INTOABYSS 0xa4 | 4 | Unit→284 EF_BOTTOM_INTOABYSS; Lied-/Fluchzelle (prim 0x2f), Modus 5; Skill: EF_BOTTOM_INTOABYSS | ja | O | - |
| 313 | BD_SIEGFRIED (Acoustic Rhythm) | Self | UNT_SIEGFRIED 0xa5 | 4 | Unit→285 EF_BOTTOM_SIEGFRIED; Wirbel (prim 0x32), Modus 4; Skill: EF_BOTTOM_SIEGFRIED | ja | O | - |
| 317 | BA_DISSONANCE (Unchained Serenade) | Self | UNT_DISSONANCE 0xa6 | 3 | Unit→290; Ensemble-Ring (prim 0x30), Modus 0 | ja | O | - |
| 319 | BA_WHISTLE (Perfect Tablature) | Self | UNT_WHISTLE 0xa7 | 3 | Unit→286 EF_BOTTOM_WHISTLE; Lied-/Fluchzelle (prim 0x2f), Modus 0; Skill: EF_BOTTOM_WHISTLE | ja | O | - |
| 320 | BA_ASSASSINCROSS (Impressive Riff) | Self | UNT_ASSASSINCROSS 0xa8 | 3 | Unit→287 EF_BOTTOM_ASSASSINCROSS; Ensemble-Ring (prim 0x30), Modus 1; Skill: EF_BOTTOM_ASSASSINCROSS | ja | O | - |
| 321 | BA_POEMBRAGI (Magic Strings) | Self | UNT_POEMBRAGI 0xa9 | 3 | Unit→288 EF_BOTTOM_POEMBRAGI; Lied-/Fluchzelle (prim 0x2f), Modus 8; Skill: EF_BOTTOM_POEMBRAGI | ja | O | - |
| 322 | BA_APPLEIDUN (Song of Lutie) | Self | UNT_APPLEIDUN 0xaa | 3 | Unit→289 EF_BOTTOM_APPLEIDUN; Lied-/Fluchzelle (prim 0x2f), Modus 6; Skill: EF_BOTTOM_APPLEIDUN | ja | O | - |
| 325 | DC_UGLYDANCE (Hip Shaker) | Self | UNT_UGLYDANCE 0xab | 3 | Unit→290; Ensemble-Ring (prim 0x30), Modus 0 | ja | O | - |
| 327 | DC_HUMMING (Focus Ballet) | Self | UNT_HUMMING 0xac | 3 | Unit→291 EF_BOTTOM_HUMMING; Lied-/Fluchzelle (prim 0x2f), Modus 0; Skill: EF_BOTTOM_HUMMING | ja | O | - |
| 328 | DC_DONTFORGETME (Slow Grace) | Self | UNT_DONTFORGETME 0xad | 3 | Unit→292 EF_BOTTOM_DONTFORGETME; Ensemble-Ring (prim 0x30), Modus 2; Skill: EF_BOTTOM_DONTFORGETME | ja | O | - |
| 329 | DC_FORTUNEKISS (Lady Luck) | Self | UNT_FORTUNEKISS 0xae | 3 | Unit→293 EF_BOTTOM_FORTUNEKISS; Lied-/Fluchzelle (prim 0x2f), Modus 4; Skill: EF_BOTTOM_FORTUNEKISS | ja | O | - |
| 330 | DC_SERVICEFORYOU (Gypsy's Kiss) | Self | UNT_SERVICEFORYOU 0xaf | 3 | Unit→294 EF_BOTTOM_SERVICEFORYOU; Ensemble-Ring (prim 0x30), Modus 4; Skill: EF_BOTTOM_SERVICEFORYOU | ja | O | - |
| 336 | WE_CALLPARTNER (I miss You) | Self | UNT_CALLFAMILY 0xb2 | – | Unit→341; roBrowser ? | nein | N | - |
| 362 | HP_BASILICA (Basilica) | Self | UNT_BASILICA 0xb4 | 2 | Unit→374 EF_BOTTOM_BASILICA; Code 0x946f20; Pos: EF_BOTTOM_BASILICA | ja | O | cyan |
| 369 | PA_GOSPEL (Battle Chant) | Self | UNT_GOSPEL 0xb3 | -1 | Unit→370 EF_BOTTOM_GOSPEL; roBrowser FUNC; Skill: EF_BOTTOM_GOSPEL | ja | O | pink |
| 395 | CG_MOONLIT (Sheltering Bliss) | Self | UNT_MOONLIT 0xb5 | 4 | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch]; Skill: EF_SPHEREWIND2 | ja | O | pink |
| 404 | PF_FOGWALL (Blinding Mist) | Ground | UNT_FOGWALL 0xb6 | -1 | Unit→405; Ring (prim 0x40), Modus 2 | ja | O | cyan |
| 405 | PF_SPIDERWEB (Fiber Lock) | Attack | UNT_SPIDERWEB 0xb7 | – | Unit→404 EF_BOTTOM_SPIDER; Flachquad (prim 0x27), Modus 2; Pos: EF_BOTTOM_SPIDER | ja | N | - |
| 409 | WE_CALLPARENT (Call Parent) | Self | UNT_CALLFAMILY 0xb2 | – | Unit→341; roBrowser ? | nein | N | - |
| 410 | WE_CALLBABY (Call Baby) | Self | UNT_CALLFAMILY 0xb2 | – | Unit→341; roBrowser ? | nein | N | - |
| 428 | SG_SUN_WARM (Warmth of the Sun) | Self | UNT_DUMMYSKILL 0x86 | – | unsichtbar (Dummy) | ja | O | red |
| 429 | SG_MOON_WARM (Warmth of the Moon) | Self | UNT_DUMMYSKILL 0x86 | – | unsichtbar (Dummy) | ja | O | purple |
| 430 | SG_STAR_WARM (Warmth of the Stars) | Self | UNT_DUMMYSKILL 0x86 | – | unsichtbar (Dummy) | ja | O | yellow |
| 478 | CR_SLIMPITCHER (Aid Condensed Potion) | Ground | (kein Unit) | – |  | ja | P | green |
| 483 | HW_GANBANTEIN (Ganbantein) | Ground | (kein Unit) | – |  | ja | P | cyan |
| 484 | HW_GRAVITATION (Gravitation Field) | Ground | UNT_GRAVITATION 0xb8 | 2 | Unit→522; Code 0x958890 | ja | B | purple |
| 488 | CG_HERMODE (Wand of Hermode) | Self | UNT_HERMODE 0xb9 | 3 | Unit→517 EF_BOTTOM_HERMODE; Code 0x949950; Pos: EF_BOTTOM_HERMODE | ja | O | - |
| 491 | CR_CULTIVATION (Plant Cultivation) | Ground | (kein Unit) | – |  | nein | N | - |
| 516 | GS_DESPERADO (Desperado) | Self | UNT_DUMMYSKILL 0x86 | 3 | unsichtbar (Dummy); Skill: EF_DESPERADO | ja | A | brown |
| 521 | GS_GROUNDDRIFT (Ground Drift) | Ground | UNT_GROUNDDRIFT_WIND 0xbe | – | Unit→638 EF_LIGHTNING_S; Code 0x950d30 | ja | N | - |
| 527 | NJ_TATAMIGAESHI (Improvised Defense) | Self | UNT_TATAMIGAESHI 0xbc | -1 | Unit→631 EF_TATAMI; Code 0x950d30; Pos: EF_TATAMI | ja | N | - |
| 529 | NJ_SHADOWJUMP (Shadow Leap) | Ground | (kein Unit) | – |  | ja | N | - |
| 535 | NJ_KAENSIN (Crimson Fire Formation) | Self | UNT_KAEN 0xbd | -1 | Unit→634 EF_KAEN; Code 0x950d30; Pos: EF_KAEN | ja | B | red |
| 536 | NJ_BAKUENRYU (Raging Fire Dragon) | Attack | UNT_DUMMYSKILL 0x86 | 2 | unsichtbar (Dummy); Skill: EF_BAKU | ja | A | red |
| 538 | NJ_SUITON (Hidden Water) | Ground | UNT_SUITON 0xbb | 1:1/2:1/3:1/4:2/5:2/6:2/7:3/8:3/9:3/10:4 | Unit→620; PropertyGround (prim 0x24, 8-eckiger Trichter) | ja | O | blue |
| 539 | NJ_HYOUSYOURAKU (Ice Meteor) | Self | UNT_DUMMYSKILL 0x86 | 3 | unsichtbar (Dummy); Skill: EF_HYOUSYOURAKU | ja | A | blue |
| 541 | NJ_RAIGEKISAI (Lightning Strike of Destruction) | Self | UNT_DUMMYSKILL 0x86 | 1:2/2:2/3:3/4:3/5:4 | unsichtbar (Dummy); Skill: EF_THUNDERSTORM2 | ja | A | yellow |
| 8208 | MA_SHOWER (Arrow_Shower) | Ground | (kein Unit) | – |  | ja | P | brown |
| 8209 | MA_SKIDTRAP (Skid_Trap) | Ground | UNT_SKIDTRAP 0x90 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 8210 | MA_LANDMINE (Land_Mine) | Ground | UNT_LANDMINE 0x93 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 8211 | MA_SANDMAN (Sandman) | Ground | UNT_SANDMAN 0x95 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 8212 | MA_FREEZINGTRAP (Freezing_Trap) | Ground | UNT_FREEZINGTRAP 0x97 | – | Fallen-Modell (RSM, eigener Pfad) | ja | N | - |
| 10006 | GD_LEADERSHIP (Great Leadership) | None | UNT_GD_LEADERSHIP 0xc1 | 2 | Unit→641 EF_FREEZING_S; Code 0x950d30 | ja | N | - |
| 10007 | GD_GLORYWOUNDS (Glorious Wounds) | None | UNT_GD_GLORYWOUNDS 0xc2 | 2 | Unit→642 EF_FLARE_S; Code 0x950d30 | ja | N | - |
| 10008 | GD_SOULCOLD (Cold Heart) | None | UNT_GD_SOULCOLD 0xc3 | 2 | Unit→74 EF_ICEWALL; Eiswand-Block (3D) | ja | N | - |
| 10009 | GD_HAWKEYES (Sharp Gaze) | None | UNT_GD_HAWKEYES 0xc4 | 2 | Unit→74 EF_ICEWALL; Eiswand-Block (3D) | ja | N | - |

Zählung (nutzbar): A 11 · B 8 · P 6 · O 33 · N 36. Mit Farbe belegt: 39 (A 11, B 8, P 6, O 14).

Nicht Teil von LGP (kein Boden-Unit, Zielskills mit Splash): Napalm Vulcan, Earth Spike, Magnum Break,
Sightrasher, Meteor Assault, Bowling Bash, Frost Nova – ihre Animation hängt am Ziel/Caster, nicht an
Bodenzellen **[gelesen]**.

### 2.2 Nicht nutzbar (149: 3rd/4th/Renewal-Jobs, NPC, Homunculus S, Elementare)

Farben nach Element-Regel (Fire rot, Water blau, Wind gelb, Earth braun, Poison/Dark violett, Holy cyan,
Ghost rosa) **[vermutet]** – nur für spätere Renewal-Server, vollständig in `lgp-skills.tsv`.

<details><summary>Tabelle (149 Zeilen)</summary>

| ID | Skill | Kategorie | Unit | Farbe (Element-Regel) | Palette |
|---|---|---|---|---|---|
| 339 | NPC_GRANDDARKNESS | NPC/Monster | UNT_DUMMYSKILL 0x86 | purple | 0x103 |
| 388 | WS_SYSTEMCREATE | sonstige (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 653 | NPC_EARTHQUAKE | NPC/Monster | UNT_EARTHQUAKE 0xc6 | purple | 0x103 |
| 670 | NPC_EVILLAND | NPC/Monster | UNT_EVILLAND 0xc7 | purple | 0x103 |
| 706 | NPC_VENOMFOG | NPC/Monster | UNT_VENOMFOG 0xfd | purple | 0x103 |
| 709 | NPC_ICEMINE | NPC/Monster | UNT_ICEMINE 0xfe | - | - |
| 711 | NPC_FLAMECROSS | NPC/Monster | UNT_FLAMECROSS 0xff | purple | 0x103 |
| 719 | NPC_HELLBURNING | NPC/Monster | UNT_HELLBURNING 0x100 | red | 0xd1 |
| 725 | NPC_REVERBERATION | NPC/Monster | UNT_REVERBERATION 0xda | purple | 0x103 |
| 739 | NPC_CLOUD_KILL | NPC/Monster | UNT_CLOUD_KILL 0xe0 | purple | 0x103 |
| 744 | NPC_ELECTRICWALK | NPC/Monster | UNT_ELECTRICWALK 0xdd | yellow | 0xba |
| 745 | NPC_FIREWALK | NPC/Monster | UNT_FIREWALK 0xdc | red | 0xd1 |
| 769 | NPC_RAINOFMETEOR | NPC/Monster | UNT_DUMMYSKILL 0x86 | red | 0xd1 |
| 770 | NPC_GROUNDDRIVE | NPC/Monster | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 780 | NPC_CANE_OF_EVIL_EYE | NPC/Monster | UNT_CANE_OF_EVIL_EYE 0x11d | purple | 0x103 |
| 2005 | RK_WINDCUTTER | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | yellow | 0xba |
| 2008 | RK_DRAGONBREATH | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | red | 0xd1 |
| 2032 | GC_POISONSMOKE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_POISONSMOKE 0xe1 | purple | 0x103 |
| 2044 | AB_EPICLESIS | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_EPICLESIS 0xca | cyan | 0xc9 |
| 2213 | WL_COMET | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 2216 | WL_EARTHSTRAIN | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_EARTHSTRAIN 0xcb | brown | 0x106 |
| 2237 | RA_DETONATOR | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | - | - |
| 2238 | RA_ELECTRICSHOCKER | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ELECTRICSHOCKER 0xd8 | - | - |
| 2239 | RA_CLUSTERBOMB | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_CLUSTERBOMB 0xd9 | - | - |
| 2249 | RA_MAGENTATRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_MAGENTATRAP 0xd2 | - | - |
| 2250 | RA_COBALTTRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_COBALTTRAP 0xd3 | - | - |
| 2251 | RA_MAIZETRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_MAIZETRAP 0xd4 | - | - |
| 2252 | RA_VERDURETRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_VERDURETRAP 0xd5 | - | - |
| 2253 | RA_FIRINGTRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FIRINGTRAP 0xd6 | - | - |
| 2254 | RA_ICEBOUNDTRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ICEBOUNDTRAP 0xd7 | - | - |
| 2260 | NC_COLDSLOWER | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | blue | 0xc8 |
| 2273 | NC_NEUTRALBARRIER | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_NEUTRALBARRIER 0xe2 | purple | 0x103 |
| 2274 | NC_STEALTHFIELD | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_STEALTHFIELD 0xe3 | purple | 0x103 |
| 2281 | NC_SILVERSNIPER | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2282 | NC_MAGICDECOY | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2299 | SC_MANHOLE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_MANHOLE 0xcc | purple | 0x103 |
| 2300 | SC_DIMENSIONDOOR | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DIMENSIONDOOR 0xcd | purple | 0x103 |
| 2301 | SC_CHAOSPANIC | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_CHAOSPANIC 0xce | purple | 0x103 |
| 2302 | SC_MAELSTROM | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_MAELSTROM 0xcf | purple | 0x103 |
| 2303 | SC_BLOODYLUST | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_BLOODYLUST 0xd0 | purple | 0x103 |
| 2304 | SC_FEINTBOMB | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FEINTBOMB 0xd1 | purple | 0x103 |
| 2317 | LG_OVERBRAND | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2319 | LG_BANDING | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_BANDING 0xec | purple | 0x103 |
| 2321 | LG_RAYOFGENESIS | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | cyan | 0xc9 |
| 2414 | WM_REVERBERATION | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_REVERBERATION 0xda | purple | 0x103 |
| 2417 | WM_DOMINION_IMPULSE | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2418 | WM_SEVERE_RAINSTORM | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_SEVERE_RAINSTORM 0xdb | purple | 0x103 |
| 2419 | WM_POEMOFNETHERWORLD | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_NETHERWORLD 0xde | purple | 0x103 |
| 2426 | WM_GREAT_ECHO | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2429 | WM_SOUND_OF_DESTRUCTION | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2443 | SO_FIREWALK | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FIREWALK 0xdc | red | 0xd1 |
| 2444 | SO_ELECTRICWALK | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ELECTRICWALK 0xdd | yellow | 0xba |
| 2446 | SO_EARTHGRAVE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 2447 | SO_DIAMONDDUST | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | blue | 0xc8 |
| 2449 | SO_PSYCHIC_WAVE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_PSYCHIC_WAVE 0xdf | purple | 0x103 |
| 2450 | SO_CLOUD_KILL | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_CLOUD_KILL 0xe0 | purple | 0x103 |
| 2452 | SO_WARMER | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_WARMER 0xe4 | red | 0xd1 |
| 2453 | SO_VACUUM_EXTREME | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_VACUUM_EXTREME 0xeb | purple | 0x103 |
| 2455 | SO_ARRULLO | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2465 | SO_FIRE_INSIGNIA | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FIRE_INSIGNIA 0xf1 | red | 0xd1 |
| 2466 | SO_WATER_INSIGNIA | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_WATER_INSIGNIA 0xf2 | blue | 0xc8 |
| 2467 | SO_WIND_INSIGNIA | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_WIND_INSIGNIA 0xf3 | yellow | 0xba |
| 2468 | SO_EARTH_INSIGNIA | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_EARTH_INSIGNIA 0xf4 | brown | 0x106 |
| 2479 | GN_THORNS_TRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_THORNS_TRAP 0xe5 | - | - |
| 2482 | GN_WALLOFTHORN | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_WALLOFTHORN 0xe6 | purple | 0x103 |
| 2483 | GN_CRAZYWEED | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2484 | GN_CRAZYWEED_ATK | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 2485 | GN_DEMONIC_FIRE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DEMONIC_FIRE 0xe7 | red | 0xd1 |
| 2486 | GN_FIRE_EXPANSION | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2487 | GN_FIRE_EXPANSION_SMOKE_POWDER | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FIRE_EXPANSION_SMOKE_POWDER 0xe8 | purple | 0x103 |
| 2488 | GN_FIRE_EXPANSION_TEAR_GAS | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FIRE_EXPANSION_TEAR_GAS 0xe9 | purple | 0x103 |
| 2490 | GN_HELLS_PLANT | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_HELLS_PLANT 0xea | purple | 0x103 |
| 2518 | SR_RIDEINLIGHTNING | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2555 | RL_B_TRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_B_TRAP 0x104 | - | - |
| 2564 | RL_FALLEN_ANGEL | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 2567 | RL_FIRE_RAIN | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FIRE_RAIN 0x105 | purple | 0x103 |
| 3006 | KO_BAKURETSU | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 3008 | KO_MUCHANAGE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 3009 | KO_HUUMARANKA | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 3010 | KO_MAKIBISHI | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_MAKIBISHI 0xfc | purple | 0x103 |
| 3020 | KO_ZENKAI | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ZENKAI_WATER 0xf8 | purple | 0x103 |
| 5004 | RK_DRAGONBREATH_WATER | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | blue | 0xc8 |
| 5006 | NC_MAGMA_ERUPTION | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_MAGMA_ERUPTION 0x101 | purple | 0x103 |
| 5010 | SC_ESCAPE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ANKLESNARE 0x91 | purple | 0x103 |
| 5013 | LG_KINGS_GRACE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_KINGS_GRACE 0x102 | purple | 0x103 |
| 5216 | AG_RAIN_OF_CRYSTAL | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_RAIN_OF_CRYSTAL 0x10a | blue | 0xc8 |
| 5217 | AG_MYSTERY_ILLUSION | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_MYSTERY_ILLUSION 0x10b | purple | 0x103 |
| 5218 | AG_VIOLENT_QUAKE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_VIOLENT_QUAKE 0x10e | brown | 0x106 |
| 5219 | AG_VIOLENT_QUAKE_ATK | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 5221 | AG_STRANTUM_TREMOR | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_STRANTUM_TREMOR 0x10d | brown | 0x106 |
| 5222 | AG_ALL_BLOOM | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ALL_BLOOM 0x10f | red | 0xd1 |
| 5223 | AG_ALL_BLOOM_ATK | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | red | 0xd1 |
| 5224 | AG_ALL_BLOOM_ATK2 | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | red | 0xd1 |
| 5226 | AG_CRYSTAL_IMPACT_ATK | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | blue | 0xc8 |
| 5227 | AG_TORNADO_STORM | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_TORNADO_STORM 0x110 | yellow | 0xba |
| 5229 | AG_FLORAL_FLARE_ROAD | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FLORAL_FLARE_ROAD 0x111 | red | 0xd1 |
| 5230 | AG_ASTRAL_STRIKE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ASTRAL_STRIKE 0x112 | purple | 0x103 |
| 5231 | AG_ASTRAL_STRIKE_ATK | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 5267 | IG_CROSS_RAIN | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_CROSS_RAIN 0x113 | cyan | 0xc9 |
| 5279 | CD_PNEUMATICUS_PROCELLA | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_PNEUMATICUS_PROCELLA 0x114 | brown | 0x106 |
| 5319 | ABC_ABYSS_STRIKE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | red | 0xd1 |
| 5321 | ABC_ABYSS_SQUARE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ABYSS_SQUARE 0x115 | purple | 0x103 |
| 5331 | WH_DEEPBLINDTRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DEEPBLINDTRAP 0x5174 | - | - |
| 5332 | WH_SOLIDTRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_SOLIDTRAP 0x5175 | - | - |
| 5333 | WH_SWIFTTRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_SWIFTTRAP 0x5176 | - | - |
| 5335 | WH_FLAMETRAP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FLAMETRAP 0x5177 | - | - |
| 5340 | BO_ACIDIFIED_ZONE_WATER | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ACIDIFIED_ZONE_WATER 0x116 | blue | 0xc8 |
| 5341 | BO_ACIDIFIED_ZONE_GROUND | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ACIDIFIED_ZONE_GROUND 0x117 | brown | 0x106 |
| 5342 | BO_ACIDIFIED_ZONE_WIND | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ACIDIFIED_ZONE_WIND 0x118 | yellow | 0xba |
| 5343 | BO_ACIDIFIED_ZONE_FIRE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_ACIDIFIED_ZONE_FIRE 0x119 | red | 0xd1 |
| 5369 | EM_DIAMOND_STORM | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | blue | 0xc8 |
| 5370 | EM_LIGHTNING_LAND | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_LIGHTNING_LAND 0x11a | yellow | 0xba |
| 5371 | EM_VENOM_SWAMP | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_VENOM_SWAMP 0x11b | purple | 0x103 |
| 5372 | EM_CONFLAGRATION | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_CONFLAGRATION 0x11c | red | 0xd1 |
| 5373 | EM_TERRA_DRIVE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 5409 | NW_WILD_FIRE | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 5410 | NW_BASIC_GRENADE | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 5411 | NW_HASTY_FIRE_IN_THE_HOLE | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 5412 | NW_GRENADES_DROPPING | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_GRENADES_DROPPING 0x120 | purple | 0x103 |
| 5415 | NW_MISSION_BOMBARD | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_MISSION_BOMBARD 0x123 | purple | 0x103 |
| 5422 | SOA_TOTEM_OF_TUTELARY | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_TOTEM_OF_TUTELARY 0x124 | purple | 0x103 |
| 5429 | SOA_TALISMAN_OF_BLACK_TORTOISE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 5445 | SH_HYUN_ROKS_BREEZE | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_HYUN_ROKS_BREEZE 0x125 | purple | 0x103 |
| 5455 | HN_METEOR_STORM_BUSTER | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | red | 0xd1 |
| 5457 | HN_JACK_FROST_NOVA | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_JACK_FROST_NOVA 0x127 | blue | 0xc8 |
| 5459 | HN_GROUND_GRAVITATION | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_GROUND_GRAVITATION 0x128 | brown | 0x106 |
| 5471 | SKE_TWINKLING_GALAXY | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_TWINKLING_GALAXY 0x11e | purple | 0x103 |
| 5472 | SKE_STAR_BURST | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_STAR_BURST 0x969 | purple | 0x103 |
| 5473 | SKE_STAR_CANNON | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_STAR_CANNON 0x11f | purple | 0x103 |
| 5476 | SS_TOKEDASU | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 5479 | SS_SHINKIROU | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_SHINKIROU 0x126 | purple | 0x103 |
| 5480 | SS_KAGEGARI | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 5483 | SS_FUUMASHOUAKU | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_FUUMASHOUAKU 0x122 | purple | 0x103 |
| 5484 | SS_FUUMAKOUCHIKU | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 5485 | SS_KUNAIWAIKYOKU | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_KUNAIWAIKYOKU 0x12a | purple | 0x103 |
| 5486 | SS_KUNAIKAITEN | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_KUNAIKAITEN 0x129 | purple | 0x103 |
| 5487 | SS_KUNAIKUSSETSU | 3rd/4th/Renewal-Job (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | brown | 0x106 |
| 5489 | SS_REIKETSUHOU | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | blue | 0xc8 |
| 5501 | NW_MIDNIGHT_FALLEN | 3rd/4th/Renewal-Job (nicht nutzbar) | (kein Unit) | purple | 0x103 |
| 8020 | MH_POISON_MIST | Homunculus S/Elementar (nicht nutzbar) | UNT_POISON_MIST 0xf5 | purple | 0x103 |
| 8025 | MH_XENO_SLASHER | Homunculus S/Elementar (nicht nutzbar) | UNT_DUMMYSKILL 0x86 | yellow | 0xba |
| 8033 | MH_STEINWAND | Homunculus S/Elementar (nicht nutzbar) | UNT_SAFETYWALL 0x7e | purple | 0x103 |
| 8041 | MH_LAVA_SLIDE | Homunculus S/Elementar (nicht nutzbar) | UNT_LAVA_SLIDE 0xf6 | red | 0xd1 |
| 8043 | MH_VOLCANIC_ASH | Homunculus S/Elementar (nicht nutzbar) | UNT_VOLCANIC_ASH 0xf7 | purple | 0x103 |
| 8044 | MH_BLAST_FORGE | Homunculus S/Elementar (nicht nutzbar) | UNT_LAVA_SLIDE 0xf6 | red | 0xd1 |
| 8403 | EL_FIRE_MANTLE | Homunculus S/Elementar (nicht nutzbar) | UNT_FIRE_MANTLE 0xed | red | 0xd1 |
| 8406 | EL_WATER_BARRIER | Homunculus S/Elementar (nicht nutzbar) | UNT_WATER_BARRIER 0xee | purple | 0x103 |
| 8409 | EL_ZEPHYR | Homunculus S/Elementar (nicht nutzbar) | UNT_ZEPHYR 0xef | purple | 0x103 |
| 8412 | EL_POWER_OF_GAIA | Homunculus S/Elementar (nicht nutzbar) | UNT_POWER_OF_GAIA 0xf0 | purple | 0x103 |

</details>

Monster mit Boden-Units im pre-re `mob_skill_db.txt` **[gemessen]**: NPC_EVILLAND (0xc7) 17 Einträge
(Banshee, Beelzebub …), NPC_EARTHQUAKE (0xc6) 20 (Baphomet, Hardrock Mammoth, Ifrit …),
NPC_GRANDDARKNESS 9; Storm Gust 37, Meteor 87, LoV 48, HD 43 Monster-Einträge (LGP gilt auch für sie).

---

## 3. Was die Mod-GRFs aus diesen Skills machen (Lieferung 2)

Vorgehen **[gemessen]**: Für jeden Skill alle Dateien, die der Client lädt – STR aus roBrowsers
`EffectTable.js` (inkl. `%d`-Reihen), Texturen aus den Effekt-Fällen in `alpha.exe`, Texturen in den STR
(Parser `tools/str_textures.py`, 3 225 Vanilla-STR) –, dann MD5 gegen Vanilla (`rgrf --json hash`,
Reihenfolge cdata > core > data). Geänderte Dateien entpackt nach `_work/lgp2/mods/<grf>/` und vermessen
(`tools/looks.py`: Anteil sichtbarer Pixel – Magenta, Alpha 0 und Schwarz zählen bei additiver Mischung als
unsichtbar –, mittlere Farbe; STR: Ebenen vorher→nachher). WOERO lädt `woero > woerop > woerol > data`
(`woe.ini`); `woero.grf` hat nur 11 Effektdateien (Item-Bilder), `woerop.grf` nur Paletten, `nodelay.grf`
nur Körper-Sprites → für Bodenskills zählt **woerol** **[gemessen]**.

### 3.1 Kernbefunde

| Skill | Vanilla (2018) | WoE-RO `woerol` | darkworld `dark2` (`dark`) | `woe` / `woebg` | `Thoth` | Burning Legacy |
|---|---|---|---|---|---|---|
| Storm Gust | `stormgust.str` 41 Ebenen | STR 8 Ebenen: 4 `puyan`-Wolken (grau) + `twirl_soft` 192×96-Ellipse; Blitz/Schnee-Texturen leer | gleiche STR; `puyan` leer, `twirl_soft` dünner weißer Ring | – | STR 24 Ebenen, nur 2 aktiv: `21.bmp` (Kopie `twirl_soft`) 192×96 | – |
| Lord of Vermilion | `lord.str` 35 Ebenen | STR 3 Ebenen, alle 24 Texturen leer → praktisch unsichtbar | STR 2 Ebenen: `twirl_thunderstorm.bmp` **grüner** Ring 192×96 | – | 7 Ebenen, Explosion blau getönt (0,64,128) | – |
| Meteor Storm | `meteor1-4.str` | 6 Ebenen, `0lordbl` (orange) klein, Feuertexturen leer | 11 Ebenen (Komet, Explosion, reduziert) | – | 6 Ebenen `0lordbl` | – |
| Volcano / Deluge / Violent Gale (Units 0x9a/9b/9c) | `ring_red`/`ring_blue`/`magic_green` 64×64 Flammenwände | **256×256, 92 % Magenta, nur Zeilen 236–255 farbig: rot (254,60,29), blau (0,102,255), gelb (204,204,51)** | identisch zu woerol (MD5 gleich) | – | – | – |
| Land Protector (0x9d) | `aaa copy.bmp` Glyphe | weißer Rauten-Rahmen 128×128 | identisch | – | – | – |
| Fiber Lock / Spider Web (0xb7) | weißes Netz | rotes Achteck 128×128 | blauer Kreis 1024×1024 | blauer Kreis (wie dark2) | – | – |
| Pneuma | `pneuma1-3.str` | 2 von 3 Texturen leer | STR 9 Ebenen + `Pneuma01-10.bmp` grüner Countdown (76,255,0) (auch `dark`) | Countdown wie dark (nur `woe`) | – | – |
| Magnus | `magnus.str` 50 Ebenen | 2 Ebenen; `ring_red`-Band rot, Kreuz gelb | wie woerol | – | – | `kdom`: nur `freeze_ice_part.bmp` |
| Sanctuary | `sanctuary.str` 25 | 5 Ebenen, `magic_green`-Band gelb | wie woerol | – | – | – |
| Safety Wall | `safetywall.str` 13 | 2 Ebenen, Texturen leer, `safeline` rosa Strich | wie woerol | – | – | – |
| Warp, Fog Wall | `ring_blue`, `ring_white` | Bänder blau / grau | `ring_white` leer | – | – | – |
| `hanmoon1/2.tga` (0xc8/0xc9, TK Mild Wind) | 128×128 Symbol | 1×1 Magenta (leer) | 1×1 Magenta | – | – | – |

Burning Legacy: `burn.grf` enthält keine `texture\effect`-Dateien (nur Effekt-Lua), `kdom.grf` ändert für
Bodenskills nur `freeze_ice_part.bmp` **[gemessen]**. `desu.grf` (zusätzlich geprüft) macht `ring_blue`,
`ring_yellow`, `ring_white` unsichtbar (Deluge/Warp ohne Boden) und kürzt STR (SG 4 Ebenen).

**Ergebnis:** Keine der GRFs hat echte flache Zellkacheln für SG/LoV/MS. Der „flache bunte Zellen“-Look von
WoE-RO entsteht aus **Server-LGP + Bodenband-Texturen der Sage-Units** (achteckiger Trichter, nur der
farbige Fußring bleibt). darkworld übernimmt die WoE-RO-Bänder und ersetzt die Bildschirm-Animationen durch
billboard-Ellipsen (192×96, kameraabhängig). Die erwarteten Farben sind dadurch belegt: SG blau, MS rot,
LoV gelb (über die umgefärbte `magic_green.tga`).

### 3.2 Alle nutzbaren Skills × GRF

Text je Zelle = Messung der geänderten Dateien des Skills (inkl. Treffer-STR wie `firehit`), „–“ = keine
Änderung. Kontaktbögen (links Vanilla, rechts Mod, additiv auf Schwarz):
`_work/lgp2/sheets/mod_woerol.png` (53 Texturen), `mod_dark2.png` (77), `mod_desu.png` (45),
`mod_woe.png`, `mod_woebg.png`, `mod_kdom.png`.

<details><summary>Tabelle (48 Skills mit Änderungen)</summary>

| Skill | woerol | dark | dark2 | woe | woebg | thoth | burn | kdom | desu |
|---|---|---|---|---|---|---|---|---|---|
| 12 MG_SAFETYWALL | STR safetywall.str: 13→2 Ebenen; 2/2 Texturen ausgeblendet | – | STR safetywall.str: 13→2 Ebenen; 2/2 Texturen ausgeblendet | – | – | – | – | Farbe grau | STR safetywall.str: 13→7 Ebenen; 1/2 Texturen ausgeblendet; Farbe pink |
| 18 MG_FIREWALL | STR firehit1.str: 10→6 Ebenen, firehit2.str: 10→4 Ebenen, firehit3.str: 10→4 Ebenen; 1/1 Texturen ausgeblendet | – | STR firehit1.str: 10→3 Ebenen, firehit2.str: 10→3 Ebenen, firehit3.str: 10→3 Ebenen; 1/2 Texturen ausgeblendet; Farbe rot | – | – | STR firehit1.str: 10→10 Ebenen, firehit2.str: 10→10 Ebenen, firehit3.str: 10→10 Ebenen | – | – | STR firehit1.str: 10→3 Ebenen, firehit2.str: 10→3 Ebenen, firehit3.str: 10→3 Ebenen; Farbe rot |
| 25 AL_PNEUMA | 2/3 Texturen ausgeblendet; Farbe weiß | STR pneuma1.str: 8→9 Ebenen, pneuma2.str: 8→9 Ebenen, pneuma3.str: 8→9 Ebenen | STR pneuma1.str: 8→9 Ebenen, pneuma2.str: 8→9 Ebenen, pneuma3.str: 8→9 Ebenen; 1/1 Texturen ausgeblendet | STR pneuma1.str: 8→9 Ebenen, pneuma2.str: 8→9 Ebenen, pneuma3.str: 8→9 Ebenen | – | – | – | – | STR pneuma1.str: 8→9 Ebenen, pneuma2.str: 8→9 Ebenen, pneuma3.str: 8→9 Ebenen; Farbe pink |
| 27 AL_WARP | 1 reduziert; Farbe blau | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1/1 Texturen ausgeblendet |
| 69 PR_BENEDICTIO | STR benedictio.str: 33→7 Ebenen; 1/1 Texturen ausgeblendet | – | STR benedictio.str: 33→7 Ebenen; 1/1 Texturen ausgeblendet | – | – | – | – | – | STR benedictio.str: 33→11 Ebenen |
| 70 PR_SANCTUARY | STR sanctuary.str: 25→5 Ebenen; 1/2 Texturen ausgeblendet; 1 reduziert; Farbe gelb | – | STR sanctuary.str: 25→5 Ebenen; 1/3 Texturen ausgeblendet; 1 reduziert; Farbe gelb/weiß | – | – | – | – | – | STR sanctuary.str: 25→11 Ebenen |
| 79 PR_MAGNUS | STR holyhit.str: 10→5 Ebenen, magnus.str: 50→2 Ebenen; 5/7 Texturen ausgeblendet; 1 reduziert; Farbe blau/rot | – | STR holyhit.str: 10→5 Ebenen, magnus.str: 50→2 Ebenen; 5/7 Texturen ausgeblendet; 1 reduziert; Farbe blau/rot | – | – | – | – | Farbe grau | STR holyhit.str: 10→3 Ebenen, magnus.str: 50→27 Ebenen; 2/3 Texturen ausgeblendet; 1 reduziert; Farbe grau |
| 80 WZ_FIREPILLAR | STR firepillarbomb.str: 11→2 Ebenen; 5/5 Texturen ausgeblendet | – | STR firepillarbomb.str: 11→2 Ebenen; 5/9 Texturen ausgeblendet; 4 reduziert; Farbe orange | – | – | – | – | – | 2/9 Texturen ausgeblendet; 7 reduziert; Farbe orange/rot/violett |
| 83 WZ_METEOR | STR firehit1.str: 10→6 Ebenen, firehit2.str: 10→4 Ebenen, firehit3.str: 10→4 Ebenen, meteor1.str: 19→6 Ebenen …; 6/6 Texturen ausgeblendet | – | STR firehit1.str: 10→3 Ebenen, firehit2.str: 10→3 Ebenen, firehit3.str: 10→3 Ebenen, meteor1.str: 19→11 Ebenen …; 6/10 Texturen ausgeblendet; 3 reduziert; Farbe orange/rot | – | – | STR firehit1.str: 10→10 Ebenen, firehit2.str: 10→10 Ebenen, firehit3.str: 10→10 Ebenen, meteor1.str: 19→6 Ebenen … | – | – | STR firehit1.str: 10→3 Ebenen, firehit2.str: 10→3 Ebenen, firehit3.str: 10→3 Ebenen, meteor1.str: 19→11 Ebenen …; 1/9 Texturen ausgeblendet; 7 reduziert; Farbe grau/orange/rot/violett |
| 85 WZ_VERMILION | STR lord.str: 35→3 Ebenen, windhit1.str: 10→3 Ebenen, windhit2.str: 10→3 Ebenen, windhit3.str: 10→3 Ebenen; 24/24 Texturen ausgeblendet | – | STR lord.str: 35→2 Ebenen, windhit1.str: 10→3 Ebenen, windhit2.str: 10→3 Ebenen, windhit3.str: 10→3 Ebenen; 20/24 Texturen ausgeblendet; 4 reduziert; Farbe blau | – | – | STR lord.str: 35→7 Ebenen, windhit1.str: 10→10 Ebenen, windhit2.str: 10→10 Ebenen, windhit3.str: 10→10 Ebenen | – | Farbe grau | STR lord.str: 35→19 Ebenen, windhit1.str: 10→3 Ebenen, windhit2.str: 10→3 Ebenen, windhit3.str: 10→3 Ebenen; 2/9 Texturen ausgeblendet; 7 reduziert; Farbe blau/grau/orange/violett |
| 87 WZ_ICEWALL | – | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1 reduziert; Farbe blau |
| 89 WZ_STORMGUST | STR storm_min.str: 24→8 Ebenen, stormgust.str: 41→8 Ebenen; 8/8 Texturen ausgeblendet | – | STR storm_min.str: 24→8 Ebenen, stormgust.str: 41→8 Ebenen; 10/12 Texturen ausgeblendet; 2 reduziert; Farbe blau/weiß | – | – | STR storm_min.str: 24→24 Ebenen, stormgust.str: 41→24 Ebenen | – | – | STR stormgust.str: 41→4 Ebenen; 2/4 Texturen ausgeblendet; 2 reduziert; Farbe blau/weiß |
| 91 WZ_HEAVENDRIVE | STR earthhit.str: 7→2 Ebenen; 1/1 Texturen ausgeblendet | – | STR earthhit.str: 7→2 Ebenen; 2/3 Texturen ausgeblendet; Farbe rot | – | – | – | – | Farbe grau | STR earthhit.str: 7→3 Ebenen; 3/3 Texturen ausgeblendet |
| 92 WZ_QUAGMIRE | STR quagmire.str: 5→3 Ebenen | – | STR quagmire.str: 5→3 Ebenen; 2/3 Texturen ausgeblendet; 1 reduziert; Farbe cyan | – | – | STR quagmire.str: 5→5 Ebenen | – | – | STR quagmire.str: 5→3 Ebenen |
| 110 BS_HAMMERFALL | STR crashearth.str: 13→2 Ebenen; 2/2 Texturen ausgeblendet | – | STR crashearth.str: 13→2 Ebenen; 3/4 Texturen ausgeblendet; 1 reduziert; Farbe rot | – | – | STR crashearth.str: 13→13 Ebenen | – | – | STR crashearth.str: 13→4 Ebenen; 2/4 Texturen ausgeblendet; 2 reduziert; Farbe grau/rot |
| 118 HT_SHOCKWAVE | 9/11 Texturen ausgeblendet; 2 reduziert; Farbe blau | – | 3/11 Texturen ausgeblendet; 8 reduziert; Farbe blau | – | – | – | – | – | 6 reduziert; Farbe blau |
| 119 HT_SANDMAN | 1/1 Texturen ausgeblendet | – | 1/1 Texturen ausgeblendet | – | – | – | – | Farbe grau | 1/1 Texturen ausgeblendet |
| 122 HT_BLASTMINE | 3/3 Texturen ausgeblendet | – | 3/7 Texturen ausgeblendet; 4 reduziert; Farbe orange | – | – | – | – | – | 1/7 Texturen ausgeblendet; 6 reduziert; Farbe orange/rot/violett |
| 123 HT_CLAYMORETRAP | 4/4 Texturen ausgeblendet | – | 4/4 Texturen ausgeblendet | – | – | – | – | – | 2/3 Texturen ausgeblendet; 1 reduziert; Farbe rot |
| 140 AS_VENOMDUST | STR venomdust.str: 9→3 Ebenen | – | – | – | – | – | – | – | – |
| 254 CR_GRANDCROSS | 1 reduziert; Farbe rot | – | 1 reduziert; Farbe rot | – | – | – | – | – | – |
| 285 SA_VOLCANO | 1 reduziert; Farbe rot | – | 1 reduziert; Farbe rot | – | – | – | – | – | – |
| 286 SA_DELUGE | 1 reduziert; Farbe blau | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1/1 Texturen ausgeblendet |
| 287 SA_VIOLENTGALE | 2 reduziert; Farbe gelb | – | 1/2 Texturen ausgeblendet; 1 reduziert; Farbe gelb | – | – | – | – | – | 1/1 Texturen ausgeblendet |
| 288 SA_LANDPROTECTOR | 1 reduziert; Farbe weiß | – | 1 reduziert; Farbe weiß | – | – | – | – | – | – |
| 308 BD_ETERNALCHAOS | – | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1 reduziert; Farbe blau |
| 310 BD_RINGNIBELUNGEN | – | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1 reduziert; Farbe blau |
| 311 BD_ROKISWEIL | 1/1 Texturen ausgeblendet | – | 1/1 Texturen ausgeblendet | – | – | – | – | – | Farbe pink |
| 313 BD_SIEGFRIED | – | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1 reduziert; Farbe blau |
| 317 BA_DISSONANCE | 1 reduziert; Farbe rot | – | 1 reduziert; Farbe rot | – | – | – | – | – | – |
| 319 BA_WHISTLE | – | – | Farbe weiß | – | – | – | – | – | Farbe weiß |
| 320 BA_ASSASSINCROSS | 1 reduziert; Farbe rot | – | 1 reduziert; Farbe rot | – | – | – | – | – | – |
| 322 BA_APPLEIDUN | Farbe gelb | – | Farbe gelb | – | – | – | – | – | Farbe rot |
| 325 DC_UGLYDANCE | 1 reduziert; Farbe rot | – | 1 reduziert; Farbe rot | – | – | – | – | – | – |
| 327 DC_HUMMING | – | – | Farbe weiß | – | – | – | – | – | Farbe weiß |
| 328 DC_DONTFORGETME | 1 reduziert; Farbe gelb | – | 1 reduziert; Farbe gelb | – | – | – | – | – | – |
| 329 DC_FORTUNEKISS | – | – | Farbe rot | – | – | – | – | – | 1/1 Texturen ausgeblendet |
| 330 DC_SERVICEFORYOU | 1/1 Texturen ausgeblendet | – | 1/1 Texturen ausgeblendet | – | – | – | – | – | Farbe pink |
| 404 PF_FOGWALL | 1 reduziert; Farbe grau | – | 1/1 Texturen ausgeblendet | – | – | – | – | – | 1/1 Texturen ausgeblendet |
| 405 PF_SPIDERWEB | Farbe rot | – | Farbe blau | Farbe blau | Farbe blau | – | – | – | – |
| 483 HW_GANBANTEIN | 1 reduziert; Farbe grau | – | 1/1 Texturen ausgeblendet | – | – | – | – | – | 1/1 Texturen ausgeblendet |
| 488 CG_HERMODE | Farbe weiß | – | 1/1 Texturen ausgeblendet | 1/1 Texturen ausgeblendet | – | – | – | – | 1/1 Texturen ausgeblendet |
| 536 NJ_BAKUENRYU | 12/12 Texturen ausgeblendet | – | 12/12 Texturen ausgeblendet | – | – | STR fire dragon.str: 6→6 Ebenen | – | – | 2 reduziert; Farbe orange/violett |
| 538 NJ_SUITON | 1 reduziert; Farbe blau | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1/1 Texturen ausgeblendet |
| 539 NJ_HYOUSYOURAKU | 2/2 Texturen ausgeblendet | – | 2/2 Texturen ausgeblendet | – | – | – | – | Farbe grau | 1/2 Texturen ausgeblendet; 1 reduziert; Farbe grau |
| 541 NJ_RAIGEKISAI | 1/1 Texturen ausgeblendet | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1 reduziert; Farbe blau |
| 10008 GD_SOULCOLD | – | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1 reduziert; Farbe blau |
| 10009 GD_HAWKEYES | – | – | 1 reduziert; Farbe blau | – | – | – | – | – | 1 reduziert; Farbe blau |

</details>

---

## 4. Client: Units 0x7e–0x12a im 2018er Client (Lieferung 3)

### 4.1 Methode [gemessen, statisch]

`alpha.exe` wurde nicht ausgeführt. Capstone-Disassemblierung der unverschlüsselten Code-Sektion:

* **Unit→Effekt:** einzige Sprungtabelle mit Basis `unit − 0x7e`: `0xb0b38d add eax,-0x7e; cmp eax,0x8a;
  jmp [eax*4+0xb0ceb4]` (Units 0x7e–0x108). Jeder Fall `push <Effekt-ID>; jmp 0xb0b3bd` → Effektstart
  `0x9c7980`. Units > 0x108 und fehlende Fälle springen auf `0xb0cea4` (kein Effekt).
* **Effekt→Darstellung:** zwei Effekt-Switches (`0x934a5e`: ID 0–1240, Tabelle `0x938314`; `0x92c19e`:
  ID 491–1224, Tabelle `0x933d74`). Plausibilitätsprobe: Fall 242 lädt `aaa copy.bmp` (LP), 239–241 laden
  `ring_red`/`ring_blue`/**`magic_green`** (Volcano/Deluge/VG; roBrowser nennt fälschlich `ring_yellow`),
  671/672 laden `hanmoon1/2`.
* **Flachquad-Helfer `0x949b00(textur, modus)`** erzeugt Primitive 0x27 (Render `0x8e41a0`, Process
  `0x8dc9d0`): ein Quad mit 4 Ecken in Höhe −2 über dem Boden, Radius `r = size·(0,8 + 0,1·(1+sin φ))`
  (φ += 3°/Frame → „Atmen“ ±11 %), Ecken bei `A, A+90°, A+180°, A+270°`, UV 0..1.

| Modus | Einheiten | size → r | Winkel A | Alpha | Tönung (+0x304) | Mischung |
|---|---|---|---|---|---|---|
| 1 | 0x9d LP | 7 → 5,6–7,0 | 0 (Raute) | 100 | 5.0 → weiß | additiv (SRCALPHA, ONE) |
| 2 | 0xb7 Spider Web | 12 → 9,6–12 | 0 | 100 | 5.0 → weiß | additiv |
| 3 | **0xc8/0xc9** | 10 → 8–10 | **225° (achsparallel)** | 250 | **111.0 → (0x37,0x37,0xff) blau** | additiv |
| 4 | 0xcc Manhole, 0xcf Maelstrom | 12 → 9,6–12 | +6°/Frame (dreht) | 200 | 4.0 → weiß | additiv |

Tönungs-Tabelle im Render (`+0x304`): 101 rot, 102 gelb, 111 blau (0x37,0x37,0xff), 112 grün,
113 blau, 114 dunkelrot, 15 grün, 17/18 blau, 100 weiß, **0 und 110 weiß mit normaler Alpha-Mischung
(SRCALPHA, INVSRCALPHA)**, sonst weiß additiv. Eine Zelle = 5 Welteinheiten [vermutet, roBrowser-Skalierung];
die WoE-RO-LP-Textur (Raute mit Ecken an den Kantenmitten) passt genau zu „A = 0 ergibt eine Welt-Raute“ –
daher die Annahme, dass Modus 3 (A = 225°) achsparallele Quadrate zeichnet **[vermutet, Ingame-Test]**.

Weitere Renderer: `0x94b8c0` = PropertyGround (Primitive 0x24, **8 Segmente à 45°**, Trichter) für
Volcano/Deluge/VG/Suiton; `0x94a340` (Prim 0x2f) Lied-/Fluchzellen, Texturen je Modus teils fest im Code;
`0x94abd0` (Prim 0x30) Ensemble-Ringe; `0x94a030` (Prim 0x40) Fog Wall; `0x9793e0` (Prim 0x3f) weiße
`ring_white`-Aura, die Manhole/Maelstrom zusätzlich zeichnen.

### 4.2 Unit-Tabelle

Kollateral = Zahl der Code-Stellen in `alpha.exe`, die denselben Texturnamen laden, und der Vanilla-STR,
die ihn benutzen **[gemessen]**. Zum Vergleich: `ring_blue.tga` 57 Code-Stellen, `ring_red.tga` 40,
`ring_white.tga` 33, `ring_yellow.tga` 29, `magic_green.tga` 9, `bbbb.bmp` 4 + 4 STR (u. a.
`adrenaline.str`), `twirl.bmp` 4 + `poison.str`, `hanmoon1/2.tga` je 2 (Unit 0xc8/0xc9 und TK Mild Wind,
Effekte 467/468), `aaa copy.bmp` 1 + `adrenaline.str`.

<details><summary>Tabelle 0x7e–0x12a (173 Units)</summary>

| Unit | UNT (xAthena) | Client-Effekt | Darstellung (alpha.exe) | Textur(en) | flach | Kollateral (Textur) | Skills HR/M-LR | andere Skills (DB) |
|---|---|---|---|---|---|---|---|---|
| 0x7e | SAFETYWALL | 315 EF_GLASSWALL2 | STR safetywall | safetywall | nein |  | MG_SAFETYWALL | MH_STEINWAND |
| 0x7f | FIREWALL | 25 EF_FIREWALL | Code 0x920160 |  | nein |  | MG_FIREWALL | GN_WALLOFTHORN |
| 0x80 | WARP_WAITING | 317 EF_PORTAL2 | roBrowser 3D/CYLINDER | effect/icearrow.tga | nein |  | AL_WARP | – |
| 0x81 | WARP_ACTIVE | 316 EF_READYPORTAL2 | roBrowser CYLINDER |  | nein |  | AL_WARP | – |
| 0x82 | BENEDICTIO | 91 EF_BENEDICTIO | Code 0x917410 |  | nein |  | – | – |
| 0x83 | SANCTUARY | 319 EF_BOTTOM_SANC | roBrowser CYLINDER |  | nein |  | PR_SANCTUARY | – |
| 0x84 | MAGNUS | 318 EF_BOTTOM_MAG | roBrowser CYLINDER |  | nein |  | PR_MAGNUS | – |
| 0x85 | PNEUMA | –  | STR (eigener Pfad, pneuma1-3.str) | pneuma1.str, pneuma2.str, pneuma3.str | nein |  | AL_PNEUMA | – |
| 0x86 | DUMMYSKILL | –  | unsichtbar (Dummy) |  | nein |  | MG_THUNDERSTORM, WZ_METEOR, WZ_VERMILION, WZ_WATERBALL, WZ_S | NPC_GRANDDARKNESS, NPC_RAINOFMETEOR, NPC_GROUNDDRI |
| 0x87 | FIREPILLAR_WAITING | 138 EF_FIREPILLARON | Feuersäule wartend |  | nein |  | WZ_FIREPILLAR | – |
| 0x88 | FIREPILLAR_ACTIVE | 97 EF_FIREPILLARBOMB | Code 0x91fcb0 |  | nein |  | WZ_FIREPILLAR | – |
| 0x89 | HIDDEN_TRAP | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x8a | TRAP | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x8b | HIDDEN_WARP_NPC | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x8c | USED_TRAPS | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x8d | ICEWALL | 74 EF_ICEWALL | Eiswand-Block (3D) |  | nein |  | WZ_ICEWALL | – |
| 0x8e | QUAGMIRE | 1161  | roBrowser ? |  | nein |  | WZ_QUAGMIRE | – |
| 0x8f | BLASTMINE | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_BLASTMINE | – |
| 0x90 | SKIDTRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_SKIDTRAP, MA_SKIDTRAP | – |
| 0x91 | ANKLESNARE | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_ANKLESNARE | SC_ESCAPE |
| 0x92 | VENOMDUST | 171 EF_VENOMDUST2 | Code 0x942be0 |  | nein |  | AS_VENOMDUST | – |
| 0x93 | LANDMINE | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_LANDMINE, MA_LANDMINE | – |
| 0x94 | SHOCKWAVE | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_SHOCKWAVE | – |
| 0x95 | SANDMAN | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_SANDMAN, MA_SANDMAN | – |
| 0x96 | FLASHER | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_FLASHER | – |
| 0x97 | FREEZINGTRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_FREEZINGTRAP, MA_FREEZINGTRAP | – |
| 0x98 | CLAYMORETRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_CLAYMORETRAP | – |
| 0x99 | TALKIEBOX | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | HT_TALKIEBOX | – |
| 0x9a | VOLCANO | 239 EF_BOTTOM_VO | PropertyGround (prim 0x24, 8-eckiger Trichter) | ring_red.tga | teils | ring_red.tga: 40 Code-Refs, 0 STR | SA_VOLCANO | – |
| 0x9b | DELUGE | 240 EF_BOTTOM_DE | PropertyGround (prim 0x24, 8-eckiger Trichter) | ring_blue.tga | teils | ring_blue.tga: 57 Code-Refs, 0 STR | SA_DELUGE | – |
| 0x9c | VIOLENTGALE | 241 EF_BOTTOM_VI | PropertyGround (prim 0x24, 8-eckiger Trichter) | magic_green.tga | teils | magic_green.tga: 9 Code-Refs, 0 STR | SA_VIOLENTGALE | – |
| 0x9d | LANDPROTECTOR | 242 EF_BOTTOM_LA | Flachquad (prim 0x27), Modus 1 | aaa copy.bmp | ja | aaa copy.bmp: 1 Code-Refs, 1 STR | SA_LANDPROTECTOR | – |
| 0x9e | LULLABY | 278 EF_BOTTOM_LULLABY | Lied-/Fluchzelle (prim 0x2f), Modus 1 | zz.bmp | teils | zz.bmp: 1 Code-Refs, 1 STR | BD_LULLABY | – |
| 0x9f | RICHMANKIM | 279 EF_BOTTOM_RICHMANKIM | Lied-/Fluchzelle (prim 0x2f), Modus 3 | pocket.bmp | teils | pocket.bmp: ? Code-Refs, 1 STR | BD_RICHMANKIM | – |
| 0xa0 | ETERNALCHAOS | 280 EF_BOTTOM_ETERNALCHAOS | Wirbel (prim 0x32), Modus 8 | twirl.bmp | nein | twirl.bmp: 4 Code-Refs, 1 STR | BD_ETERNALCHAOS | – |
| 0xa1 | DRUMBATTLEFIELD | 281 EF_BOTTOM_DRUMBATTLEFIELD | Lied-/Fluchzelle (prim 0x2f), Modus 2 |  | teils |  | BD_DRUMBATTLEFIELD | – |
| 0xa2 | RINGNIBELUNGEN | 282 EF_BOTTOM_RINGNIBELUNGEN | Lied-/Fluchzelle (prim 0x2f), Modus 7 | twirl.bmp | teils | twirl.bmp: 4 Code-Refs, 1 STR | BD_RINGNIBELUNGEN | – |
| 0xa3 | ROKISWEIL | 283 EF_BOTTOM_ROKISWEIL | Code 0x94a780 | safeline.bmp | nein | safeline.bmp: ? Code-Refs, 8 STR | BD_ROKISWEIL | – |
| 0xa4 | INTOABYSS | 284 EF_BOTTOM_INTOABYSS | Lied-/Fluchzelle (prim 0x2f), Modus 5 |  | teils |  | BD_INTOABYSS | – |
| 0xa5 | SIEGFRIED | 285 EF_BOTTOM_SIEGFRIED | Wirbel (prim 0x32), Modus 4 | twirl.bmp | nein | twirl.bmp: 4 Code-Refs, 1 STR | BD_SIEGFRIED | – |
| 0xa6 | DISSONANCE | 290  | Ensemble-Ring (prim 0x30), Modus 0 | ring_red.tga | nein | ring_red.tga: 40 Code-Refs, 0 STR | BA_DISSONANCE | – |
| 0xa7 | WHISTLE | 286 EF_BOTTOM_WHISTLE | Lied-/Fluchzelle (prim 0x2f), Modus 0 | melody_b.bmp | teils | melody_b.bmp: ? Code-Refs, 1 STR | BA_WHISTLE | – |
| 0xa8 | ASSASSINCROSS | 287 EF_BOTTOM_ASSASSINCROSS | Ensemble-Ring (prim 0x30), Modus 1 | ring_red.tga | nein | ring_red.tga: 40 Code-Refs, 0 STR | BA_ASSASSINCROSS | – |
| 0xa9 | POEMBRAGI | 288 EF_BOTTOM_POEMBRAGI | Lied-/Fluchzelle (prim 0x2f), Modus 8 |  | teils |  | BA_POEMBRAGI | – |
| 0xaa | APPLEIDUN | 289 EF_BOTTOM_APPLEIDUN | Lied-/Fluchzelle (prim 0x2f), Modus 6 | idun_apple.bmp | teils |  | BA_APPLEIDUN | – |
| 0xab | UGLYDANCE | 290  | Ensemble-Ring (prim 0x30), Modus 0 | ring_red.tga | nein | ring_red.tga: 40 Code-Refs, 0 STR | DC_UGLYDANCE | – |
| 0xac | HUMMING | 291 EF_BOTTOM_HUMMING | Lied-/Fluchzelle (prim 0x2f), Modus 0 | melody_a.bmp | teils | melody_a.bmp: ? Code-Refs, 1 STR | DC_HUMMING | – |
| 0xad | DONTFORGETME | 292 EF_BOTTOM_DONTFORGETME | Ensemble-Ring (prim 0x30), Modus 2 | magic_green.tga | nein | magic_green.tga: 9 Code-Refs, 0 STR | DC_DONTFORGETME | – |
| 0xae | FORTUNEKISS | 293 EF_BOTTOM_FORTUNEKISS | Lied-/Fluchzelle (prim 0x2f), Modus 4 | kiss.bmp | teils |  | DC_FORTUNEKISS | – |
| 0xaf | SERVICEFORYOU | 294 EF_BOTTOM_SERVICEFORYOU | Ensemble-Ring (prim 0x30), Modus 4 | safeline.bmp | nein | safeline.bmp: ? Code-Refs, 8 STR | DC_SERVICEFORYOU | – |
| 0xb0 | GRAFFITI | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | RG_GRAFFITI | – |
| 0xb1 | DEMONSTRATION | 302 EF_DEMONSTRATION | roBrowser SPR | xc7 | nein |  | AM_DEMONSTRATION | – |
| 0xb2 | CALLFAMILY | 341  | roBrowser ? |  | nein |  | WE_CALLPARTNER, WE_CALLPARENT, WE_CALLBABY | – |
| 0xb3 | GOSPEL | 370 EF_BOTTOM_GOSPEL | roBrowser FUNC |  | nein |  | PA_GOSPEL | – |
| 0xb4 | BASILICA | 374 EF_BOTTOM_BASILICA | Code 0x946f20 | alpha_down.tga, alpha_down.tga | nein |  | HP_BASILICA | – |
| 0xb5 | MOONLIT | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | CG_MOONLIT | – |
| 0xb6 | FOGWALL | 405  | Ring (prim 0x40), Modus 2 | ring_white.tga | nein | ring_white.tga: 33 Code-Refs, 0 STR | PF_FOGWALL | – |
| 0xb7 | SPIDERWEB | 404 EF_BOTTOM_SPIDER | Flachquad (prim 0x27), Modus 2 | spiderweb.tga | ja | spiderweb.tga: 1 Code-Refs, 0 STR | PF_SPIDERWEB | – |
| 0xb8 | GRAVITATION | 522  | Code 0x958890 |  | nein |  | HW_GRAVITATION | – |
| 0xb9 | HERMODE | 517 EF_BOTTOM_HERMODE | Code 0x949950 | white02.bmp | nein |  | CG_HERMODE | – |
| 0xba | KAENSIN | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0xbb | SUITON | 620  | PropertyGround (prim 0x24, 8-eckiger Trichter) | ring_blue.tga | teils | ring_blue.tga: 57 Code-Refs, 0 STR | NJ_SUITON | – |
| 0xbc | TATAMIGAESHI | 631 EF_TATAMI | Code 0x950d30 |  | nein |  | NJ_TATAMIGAESHI | – |
| 0xbd | KAEN | 634 EF_KAEN | Code 0x950d30 |  | nein |  | NJ_KAENSIN | – |
| 0xbe | GROUNDDRIFT_WIND | 638 EF_LIGHTNING_S | Code 0x950d30 |  | nein |  | GS_GROUNDDRIFT | – |
| 0xbf | GROUNDDRIFT_DARK | 639 EF_BLIND_S | Code 0x950d30 |  | nein |  | – | – |
| 0xc0 | GROUNDDRIFT_POISON | 640 EF_POISON_S | Code 0x950d30 |  | nein |  | – | – |
| 0xc1 | GROUNDDRIFT_WATER | 641 EF_FREEZING_S | Code 0x950d30 |  | nein |  | GD_LEADERSHIP | – |
| 0xc2 | GROUNDDRIFT_FIRE | 642 EF_FLARE_S | Code 0x950d30 |  | nein |  | GD_GLORYWOUNDS | – |
| 0xc3 | DEATHWAVE | 74 EF_ICEWALL | Eiswand-Block (3D) |  | nein |  | GD_SOULCOLD | – |
| 0xc4 | WATERATTACK | 74 EF_ICEWALL | Eiswand-Block (3D) |  | nein |  | GD_HAWKEYES | – |
| 0xc5 | WINDATTACK | 138 EF_FIREPILLARON | Feuersäule wartend |  | nein |  | – | – |
| 0xc6 | EARTHQUAKE | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | NPC_EARTHQUAKE |
| 0xc7 | EVILLAND | 674 EF_BOTTOM_EVILLAND | Lied-/Fluchzelle (prim 0x2f), Modus 6 | curse.bmp | teils | curse.bmp: 2 Code-Refs, 0 STR | – | NPC_EVILLAND |
| 0xc8 | DARK_RUNNER | 671 EF_BOTTOM_RUNNER | Flachquad (prim 0x27), Modus 3 | hanmoon1.tga | ja | hanmoon1.tga: 2 Code-Refs, 0 STR | – | – |
| 0xc9 | DARK_TRANSFER | 672 EF_BOTTOM_TRANSFER | Flachquad (prim 0x27), Modus 3 | hanmoon2.tga | ja | hanmoon2.tga: 2 Code-Refs, 0 STR | – | – |
| 0xca | EPICLESIS | 883 EF_GLASSWALL4 | Code 0x975150 | ef_epitree.tga, alpha_down.tga | nein |  | – | AB_EPICLESIS |
| 0xcb | EARTHSTRAIN | 142 EF_HEAVENSDRIVE | Code 0x922d30 |  | nein |  | – | WL_EARTHSTRAIN |
| 0xcc | MANHOLE | 822 EF_BOTTOM_MANHOLE | Flachquad (prim 0x27), Modus 4 | twirl.bmp | ja | twirl.bmp: 4 Code-Refs, 1 STR | – | SC_MANHOLE |
| 0xcd | DIMENSIONDOOR | 825 EF_FORESTLIGHT6 | Code 0x471830 | cloud11.tga, 구체15.bmp | nein |  | – | SC_DIMENSIONDOOR |
| 0xce | CHAOSPANIC | 827 EF_BOTTOM_ANI | Code 0x972030 |  | nein |  | – | SC_CHAOSPANIC |
| 0xcf | MAELSTROM | 828 EF_BOTTOM_MAELSTROM | Flachquad (prim 0x27), Modus 4 | bbbb.bmp | ja | bbbb.bmp: 4 Code-Refs, 4 STR | – | SC_MAELSTROM |
| 0xd0 | BLOODYLUST | 829 EF_BOTTOM_BLOODYLUST | Code 0x972030 |  | nein |  | – | SC_BLOODYLUST |
| 0xd1 | FEINTBOMB | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | SC_FEINTBOMB |
| 0xd2 | MAGENTATRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | – | RA_MAGENTATRAP |
| 0xd3 | COBALTTRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | – | RA_COBALTTRAP |
| 0xd4 | MAIZETRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | – | RA_MAIZETRAP |
| 0xd5 | VERDURETRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | – | RA_VERDURETRAP |
| 0xd6 | FIRINGTRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | – | RA_FIRINGTRAP |
| 0xd7 | ICEBOUNDTRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | – | RA_ICEBOUNDTRAP |
| 0xd8 | ELECTRICSHOCKER | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | – | RA_ELECTRICSHOCKER |
| 0xd9 | CLUSTERBOMB | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | – | RA_CLUSTERBOMB |
| 0xda | REVERBERATION | 856 EF_BOT_REVERB | Code 0x9725a0 | melody_a.bmp | nein | melody_a.bmp: ? Code-Refs, 1 STR | – | NPC_REVERBERATION, WM_REVERBERATION |
| 0xdb | SEVERE_RAINSTORM | 857 EF_RAIN_PARTICLE | Code 0x979180 | 번개1.bmp | nein | 번개1.bmp: ? Code-Refs, 1 STR | – | WM_SEVERE_RAINSTORM |
| 0xdc | FIREWALK | 920 EF_FIREWALL2 | Code 0x976b90 |  | nein |  | – | NPC_FIREWALK, SO_FIREWALK |
| 0xdd | ELECTRICWALK | 926 EF_SHOCKWAVE2 | Code 0x93d5b0 |  | nein |  | – | NPC_ELECTRICWALK, SO_ELECTRICWALK |
| 0xde | NETHERWORLD | 860 EF_BOT_REVERB2 | Code 0x9725a0 | melody_a.bmp | nein | melody_a.bmp: ? Code-Refs, 1 STR | – | WM_POEMOFNETHERWORLD |
| 0xdf | PSYCHIC_WAVE | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | SO_PSYCHIC_WAVE |
| 0xe0 | CLOUD_KILL | 1080  | roBrowser ? |  | nein |  | – | NPC_CLOUD_KILL, SO_CLOUD_KILL |
| 0xe1 | POISONSMOKE | 772  | Code 0x91c530 |  | nein |  | – | GC_POISONSMOKE |
| 0xe2 | NEUTRALBARRIER | 783  | Wirbel (prim 0x32), Modus 8 | slash01.tga | nein |  | – | NC_NEUTRALBARRIER |
| 0xe3 | STEALTHFIELD | 784  | Wirbel (prim 0x32), Modus 8 | 썬더스톰파티클.tga | nein |  | – | NC_STEALTHFIELD |
| 0xe4 | WARMER | 929 EF_DEMONICFIRE4 | Code 0x97adf0 |  | nein |  | – | SO_WARMER |
| 0xe5 | THORNS_TRAP | –  | Fallen-Modell (RSM, eigener Pfad) |  | nein |  | – | GN_THORNS_TRAP |
| 0xe6 | WALLOFTHORN | 912  | Code 0x950d30 |  | nein |  | – | GN_WALLOFTHORN |
| 0xe7 | DEMONIC_FIRE | 916  | Code 0x97adf0 |  | nein |  | – | GN_DEMONIC_FIRE |
| 0xe8 | FIRE_EXPANSION_SMOKE_POWDER | 917  | Code 0x97adf0 |  | nein |  | – | GN_FIRE_EXPANSION_SMOKE_POWDER |
| 0xe9 | FIRE_EXPANSION_TEAR_GAS | 918  | Code 0x97adf0 |  | nein |  | – | GN_FIRE_EXPANSION_TEAR_GAS |
| 0xea | HELLS_PLANT | 919  | Code 0x950d30 |  | nein |  | – | GN_HELLS_PLANT |
| 0xeb | VACUUM_EXTREME | 921  | Code 0x950d30 |  | nein |  | – | SO_VACUUM_EXTREME |
| 0xec | BANDING | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | LG_BANDING |
| 0xed | FIRE_MANTLE | 25 EF_FIREWALL | Code 0x920160 |  | nein |  | – | EL_FIRE_MANTLE |
| 0xee | WATER_BARRIER | 939  | Code 0x94b030 | ring_blue.tga | nein | ring_blue.tga: 57 Code-Refs, 0 STR | – | EL_WATER_BARRIER |
| 0xef | ZEPHYR | 465  | Code jmp |  | nein |  | – | EL_ZEPHYR |
| 0xf0 | POWER_OF_GAIA | 1088  | Ring (prim 0x40), Modus 4 | ring_yellow.tga | nein | ring_yellow.tga: 29 Code-Refs, 0 STR | – | EL_POWER_OF_GAIA |
| 0xf1 | FIRE_INSIGNIA | 916  | Code 0x97adf0 |  | nein |  | – | SO_FIRE_INSIGNIA |
| 0xf2 | WATER_INSIGNIA | 916  | Code 0x97adf0 |  | nein |  | – | SO_WATER_INSIGNIA |
| 0xf3 | WIND_INSIGNIA | 916  | Code 0x97adf0 |  | nein |  | – | SO_WIND_INSIGNIA |
| 0xf4 | EARTH_INSIGNIA | 916  | Code 0x97adf0 |  | nein |  | – | SO_EARTH_INSIGNIA |
| 0xf5 | POISON_MIST | 959 EF_POISON_MIST | Code 0x91b710 |  | nein |  | – | MH_POISON_MIST |
| 0xf6 | LAVA_SLIDE | 964 EF_LAVA_SLIDE | Code 0x91b710 |  | nein |  | – | MH_LAVA_SLIDE, MH_BLAST_FORGE |
| 0xf7 | VOLCANIC_ASH | 975 EF_VOLCANIC_ASH | Code 0x91b710 |  | nein |  | – | MH_VOLCANIC_ASH |
| 0xf8 | ZENKAI_WATER | 992  | roBrowser ? |  | nein |  | – | KO_ZENKAI |
| 0xf9 | ZENKAI_LAND | 993  | roBrowser ? |  | nein |  | – | – |
| 0xfa | ZENKAI_FIRE | 994  | roBrowser ? |  | nein |  | – | – |
| 0xfb | ZENKAI_WIND | 995  | roBrowser ? |  | nein |  | – | – |
| 0xfc | MAKIBISHI | 1007  | roBrowser ? |  | nein |  | – | KO_MAKIBISHI |
| 0xfd | VENOMFOG | 1020 EF_VENOMFOG | Code 0x91b710 |  | nein |  | – | NPC_VENOMFOG |
| 0xfe | ICEMINE | 1026  | Code 0x950d30 |  | nein |  | – | NPC_ICEMINE |
| 0xff | FLAMECROSS | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | NPC_FLAMECROSS |
| 0x100 | HELLBURNING | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | NPC_HELLBURNING |
| 0x101 | MAGMA_ERUPTION | 1050 EF_NC_MAGMA_ERUPTION | Code 0x93b4b0 |  | nein |  | – | NC_MAGMA_ERUPTION |
| 0x102 | KINGS_GRACE | 1051  | Code 0x928fe0 |  | nein |  | – | LG_KINGS_GRACE |
| 0x103 | GLITTERING_GREED | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x104 | B_TRAP | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | RL_B_TRAP |
| 0x105 | FIRE_RAIN | 1064  | roBrowser ? |  | nein |  | – | RL_FIRE_RAIN |
| 0x106 | CATNIPPOWDER | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x107 | NYANGGRASS | –  | kein Fall im Unit-Switch (0xb0b38d) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x108 | GROUNDDRIFT_NEUTRAL | 1120  | Code 0x950d30 |  | nein |  | – | – |
| 0x109 | CREATINGSTAR | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x10a | RAIN_OF_CRYSTAL | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | AG_RAIN_OF_CRYSTAL |
| 0x10b | MYSTERY_ILLUSION | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | AG_MYSTERY_ILLUSION |
| 0x10c | SEEDTRAP | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x10d | STRANTUM_TREMOR | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | AG_STRANTUM_TREMOR |
| 0x10e | VIOLENT_QUAKE | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | AG_VIOLENT_QUAKE |
| 0x10f | ALL_BLOOM | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | AG_ALL_BLOOM |
| 0x110 | TORNADO_STORM | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | AG_TORNADO_STORM |
| 0x111 | FLORAL_FLARE_ROAD | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | AG_FLORAL_FLARE_ROAD |
| 0x112 | ASTRAL_STRIKE | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | AG_ASTRAL_STRIKE |
| 0x113 | CROSS_RAIN | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | IG_CROSS_RAIN |
| 0x114 | PNEUMATICUS_PROCELLA | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | CD_PNEUMATICUS_PROCELLA |
| 0x115 | ABYSS_SQUARE | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | ABC_ABYSS_SQUARE |
| 0x116 | ACIDIFIED_ZONE_WATER | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | BO_ACIDIFIED_ZONE_WATER |
| 0x117 | ACIDIFIED_ZONE_GROUND | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | BO_ACIDIFIED_ZONE_GROUND |
| 0x118 | ACIDIFIED_ZONE_WIND | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | BO_ACIDIFIED_ZONE_WIND |
| 0x119 | ACIDIFIED_ZONE_FIRE | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | BO_ACIDIFIED_ZONE_FIRE |
| 0x11a | LIGHTNING_LAND | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | EM_LIGHTNING_LAND |
| 0x11b | VENOM_SWAMP | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | EM_VENOM_SWAMP |
| 0x11c | CONFLAGRATION | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | EM_CONFLAGRATION |
| 0x11d | CANE_OF_EVIL_EYE | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | NPC_CANE_OF_EVIL_EYE |
| 0x11e | TWINKLING_GALAXY | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | SKE_TWINKLING_GALAXY |
| 0x11f | STAR_CANNON | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | SKE_STAR_CANNON |
| 0x120 | GRENADES_DROPPING | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | NW_GRENADES_DROPPING |
| 0x121 | UNKNOWN_2 | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | – |
| 0x122 | FUUMASHOUAKU | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | SS_FUUMASHOUAKU |
| 0x123 | MISSION_BOMBARD | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | NW_MISSION_BOMBARD |
| 0x124 | TOTEM_OF_TUTELARY | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | SOA_TOTEM_OF_TUTELARY |
| 0x125 | HYUN_ROKS_BREEZE | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | SH_HYUN_ROKS_BREEZE |
| 0x126 | SHINKIROU | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | SS_SHINKIROU |
| 0x127 | JACK_FROST_NOVA | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | HN_JACK_FROST_NOVA |
| 0x128 | GROUND_GRAVITATION | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | HN_GROUND_GRAVITATION |
| 0x129 | KUNAIKAITEN | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | SS_KUNAIKAITEN |
| 0x12a | KUNAIWAIKYOKU | –  | außerhalb des Unit-Switch (> 0x108) – zeichnet nichts [statisch] |  | nein |  | – | SS_KUNAIWAIKYOKU |

</details>

### 4.3 Bewertung der Kandidaten für die Palette

| Unit | flach pro Zelle aus 1 Textur | von Skill auf HR/M-LR genutzt | Textur exklusiv | Farbe frei | Urteil |
|---|---|---|---|---|---|
| 0x9d LP | ja (Modus 1) | **ja** (SA_LANDPROTECTOR) | fast (`adrenaline.str`) | ja | nein – LP ist WoE-entscheidend |
| 0xb7 Spider Web | ja (Modus 2, 2,4 Zellen groß) | **ja** (PF_SPIDERWEB) | ja | ja | nein |
| **0xc8 Dark Runner** | ja (Modus 3) | nein | fast (TK Mild Wind Erde) | **nein, immer blau** | **ja: Blau** |
| **0xc9 Dark Transfer** | ja (Modus 3) | nein | fast (TK Mild Wind Wind) | **nein, immer blau** | **ja: zweites Blau (Umriss)** |
| 0xcc Manhole | Quad dreht sich + weiße Ring-Aura | nein (3rd) | nein (`twirl.bmp`: Lieder Ring of Nibelungen, Eternal Chaos, Siegfried, `poison.str`) | ja | nein |
| 0xcf Maelstrom | dreht + Aura | nein (3rd) | nein (`bbbb.bmp`: Adrenaline Rush u. a.) | ja | nein |
| 0x9a/9b/9c Sage | Trichter (Achteck), nur mit Band-Textur flach | **ja** | nein (40/57/9 Code-Stellen) | ja | nur WoE-RO-Kompatibilität (Variante W) |
| 0x9e–0xaf Lieder | Farbzelle fest im Code + Symbole | **ja** | – | nein | nein |
| 0xc7 Evil Land | Prim 0x2f, `curse.bmp` | Monster (17 Einträge) | nein (2 Stellen) | ? | nein |
| 0xc3/0xc4/0xc5 | Eiswand-/Feuersäulen-Effekt | nein | – | – | nur mit Patch (Reserve) |
| 0xba, 0xd1, 0xdf, 0xec, 0x103, 0x106, 0x107 | **zeichnen nichts** (kein Fall) | nein (Kaensin-Alt-ID, 3rd, Summoner; kein Monster) | – | – | **ja, mit Patch** |
| > 0x108 | zeichnen nichts | 4th Jobs | – | – | Reserve |

Ergebnis: Ohne Client-Eingriff gibt es genau **eine** Farbe (Blau, zwei Formen). Mehr Farben gehen nur,
wenn der Client für ungenutzte Unit-IDs den Flachquad-Helfer mit eigener Textur und weißer Tönung aufruft.
Eine „Texturüberschreibung nur für LGP-Units“ ist in einer GRF allein nicht möglich (der Client lädt nach
Dateinamen), aber genau das leistet der Patch in 5.2: eigene Dateinamen, die nur die LGP-Units laden.

---

## 5. Vorschlag (Lieferung 4)

### 5.1 Palette

| Farbe | Unit (Stufe P) | Datei (wie der Client sie lädt) | Stufe G (ohne Patch) | Belegt durch |
|---|---|---|---|---|
| blau | 0xc8 | `data\texture\effect\odinlgp_blue.tga` | 0xc8 `hanmoon1.tga` (blau gefüllt) | WoE-RO SG = Deluge-Blau [gemessen] |
| cyan/weiß | 0xc9 | `…\odinlgp_cyan.tga` | 0xc8 | Holy/Eis [vermutet] |
| gelb | 0xba | `…\odinlgp_yellow.tga` | 0xc9 `hanmoon2.tga` (blauer Umriss) | WoE-RO LoV = gelbes `magic_green` [gemessen] |
| rot | 0xd1 | `…\odinlgp_red.tga` | 0xc9 | WoE-RO MS = `ring_red` [gemessen] |
| orange | 0xdf | `…\odinlgp_orange.tga` | 0xc9 | Fire Wall [vermutet] |
| grün | 0xec | `…\odinlgp_green.tga` | 0xc8 | Pneuma/Sanctuary [vermutet] |
| violett | 0x103 | `…\odinlgp_purple.tga` | 0xc8 | Gift/Gravitation [vermutet] |
| braun | 0x106 | `…\odinlgp_brown.tga` | 0xc9 | Erde (HD, Quagmire) [vermutet] |
| rosa | 0x107 | `…\odinlgp_pink.tga` | 0xc9 | Safety Wall/Gospel [vermutet] |
| Reserve | 0xc3 (vanilla Eiswand!) | `…\odinlgp_white.tga` (noch nicht gezeichnet) | – | nur mit Fähigkeits-Flag |

Format **[gemessen am Original `hanmoon1.tga`]**: TGA Typ 2, 32 bit, Ursprung unten links, Deskriptor
0x08, TGA-2.0-Fußzeile, 128×128, 65 580 Byte. Gepards Original-Farben selbst sind nicht belegt (keine
Gepard-Daten im Bestand); „[vermutet]“-Farben folgen Element und Vanilla-Optik.

### 5.2 Client-Patch (Stufe P) – Spezifikation für `odinguard.dll` [gemessen, statisch]

Wie die bestehenden UI-Hooks (`client/src/og_ui.c`): erst nach dem Entpacken, jede Stelle mit
Byte-Signatur prüfen, bei Abweichung inaktiv bleiben.

1. **Sprungtabelle** `0xb0ceb4` (4 Byte je Unit, Index `unit − 0x7e`): die Einträge für 0xba (`0xb0cfa4`),
   0xd1 (`0xb0d000`), 0xdf (`0xb0d038`), 0xec (`0xb0d06c`), 0x103 (`0xb0d0c8`), 0x106 (`0xb0d0d4`),
   0x107 (`0xb0d0d8`) zeigen heute auf `0xb0cea4` (Bytes `a4 ce b0 00`); neu `0xb0b6f6` (Fall von 0xc8,
   `f6 b6 b0 00`) → alle starten Effekt 671.
2. **Aufruf ersetzen** in Fall 671 (`0x930654: 6a 03 68 68 66 d3 00 e8 a0 94 01 00` = `push 3; push
   "effect\hanmoon1.tga"; call 0x949b00`) und 672 (`0x930674 …`): `call` auf einen DLL-Stub (thiscall,
   `ret 8`). Der Stub liest die Unit-ID des Besitzers (`[this+0x128]` = auslösender Akteur, Job über
   `0x9c6a30` wie im Unit-Switch) **[vermutet: Besitzer = CSkill]**, wählt die Textur
   `effect\odinlgp_<farbe>.tga`, ruft `0x949b00(textur, 3)` und setzt danach am erzeugten Primitive
   (`[this+0x11c74]`) **`+0x304 = 110.0f`** → weiße Tönung, normale Alpha-Mischung, gleiche Geometrie wie
   Modus 3. Für TK Mild Wind (Effekte 467/468, andere Aufrufstelle `0x947490`) ändert sich nichts.
3. Integritätsmodul (`ch-code`) muss die gepatchten Bytes kennen (Referenz-Image oder Ausnahme).
4. Die DLL meldet die Fähigkeit („LGP-Palette“) im Handshake; der Server nutzt die Palette-Unit nur dann,
   sonst die Spalte „Stufe G“ (`fallback` in der TSV).

### 5.3 Server-Änderungen (Vorschlag, nicht umgesetzt)

* `og_lgp`-Zeilen aus `lgp-skills.tsv` (Spalte `palette_unit`, Klassen A/B/P; O nur auf Wunsch).
* Hook-Erweiterung für Klasse B: in `clif_getareachar_skillunit` die Bedingung
  `unit_id == UNT_DUMMYSKILL` erweitern auf „Skill steht in `og_lgp` und ist keine Falle/kein Ice Wall“.
  Dann zeigen Fire Wall, Sanctuary, Magnus, Quagmire, Venom Dust, Gravitation, Kaensin und die Pneuma-Mitte
  Farbzellen.
* Klasse P („Flash-Zellen“): für Arrow Shower, Hammer Fall, Ganbantein, Benedictio, Slim Pitcher pro
  Betrachter kurzlebige Units (`0x09ca` mit eigener ID, nach ~500 ms `0x0120`) statt des Positionseffekts.
* `og_map_lgp_unit`: Fallback auf 0xc8/0xc9 ohne Fähigkeits-Flag; `540` entfernen; M-LR auf den Stand von
  `development` bringen.

### 5.4 Skill → Farbe (nutzbare Skills)

| Farbe | Skills (Klasse) |
|---|---|
| blau | 89 Storm Gust (A), 86 Water Ball (A), 539 Hyousyouraku (A); optional 286 Deluge, 538 Suiton |
| cyan | 254 Grand Cross (A), 79 Magnus (B), 69 Benedictio (P), 483 Ganbantein (P); optional 362 Basilica, 404 Fog Wall |
| gelb | 85 LoV (A), 21 Thunderstorm (A), 541 Raigekisai (A); optional 430 Star Warmth |
| rot | 83 Meteor Storm (A), 536 Bakuenryu (A), 535 Kaensin (B); optional 80 Fire Pillar, 285 Volcano, 428 Sun Warmth |
| orange | 18 Fire Wall (B); optional 229 Demonstration |
| grün | 25 Pneuma (B), 70 Sanctuary (B), 478 Slim Pitcher (P); optional 287 Violent Gale |
| violett | 140 Venom Dust (B), 484 Gravitation Field (B); optional 429 Moon Warmth |
| braun | 91 Heaven's Drive (A), 516 Desperado (A), 92 Quagmire (B), 47/8208 Arrow Shower (P), 110 Hammer Fall (P) |
| rosa | optional 12 Safety Wall, 369 Gospel, 395 Moonlit Water Mill |
| – (nie) | Fallen, 288 Land Protector, 87 Ice Wall, 27 Warp, Graffiti, Familienrufe, Ground Drift, Tatami, Spider Web, Gildenauren; Lieder optional/Vanilla |

Vollständig (alle 243): `analysis/lgp/lgp-skills.tsv` (`skill_id, name, unit, colour, palette_unit,
source`; `source` enthält Klasse, Fallback-Unit ohne Patch und Beleg).

---

## 6. Texturen (Lieferung 5)

`assets/odin-grf/lgp/data/texture/effect/` (eigene Zeichnungen, `analysis/lgp/tools/make_lgp_tiles.py`):

| Datei | Inhalt |
|---|---|
| `hanmoon1.tga` | Stufe G: blaue gefüllte Zelle (Füllung Alpha 150, Rand 3 px dunkler, Alpha 255) |
| `hanmoon2.tga` | Stufe G: heller blauer Umriss (Rand Alpha 250, Innen Alpha 35) |
| `odinlgp_{blue,cyan,yellow,red,orange,green,purple,brown,pink}.tga` | Stufe P: gefüllte Zelle, Füllung Alpha 140, Rand 3 px in 40 % Helligkeit |

Alle: 128×128 TGA32 wie das Original, Kachel = achsparalleles Quadrat 45 px in der Mitte (bei r = 8–10
→ 0,8–1,0 Zelle, Lücke zum Nachbarn bleibt sichtbar), Rest Alpha 0. Test-GRF aller 11 Dateien:
`rgrf build` + `rgrf verify` 0 Fehler, 5 372 Byte **[gemessen]**. Kontaktbogen mit simulierter Draufsicht
(3×3 Zellen, darkworld- und Vanilla-Boden, Patch = Alpha-Mischung weiß; ohne Patch = additiv blau):
`analysis/lgp/lgp-palette-sheet.png`.

---

## 7. Offene Risiken (Ingame prüfen)

1. **Ausrichtung/Größe Modus 3:** Achsparallele Quadrate und r = 8–10 (Zelle = 5) sind aus dem Code
   abgeleitet; falls die Kachel als Raute oder zu groß erscheint, nur den Winkel (225°→270°) bzw. die
   Kachelgröße im Generator ändern.
2. **Blau-Tönung 0xc8/0xc9:** aus der Render-Funktion gelesen (Zweig `+0x304 == 111`); im Spiel bestätigen,
   dass `hanmoon2` blau statt orange erscheint.
3. **Patch-Stub:** Besitzer-Akteur über `[this+0x128]` und Job-Getter `0x9c6a30` nicht dynamisch geprüft;
   `alpha.exe` ist gepackt (WARP), Adressen gelten nur für diese Build.
4. **Unit-IDs ohne Fall** (0xba … 0x107): zeichnen im Vanilla-Client nichts – andere Codepfade, die diese
   IDs kennen, wurden nicht gefunden, aber nicht ausgeschlossen.
5. **TK Mild Wind** (Erde/Wind) zeigt mit Stufe G das blaue Quadrat bzw. den Umriss unter dem Taekwon.
6. Meteor Storm: der Server unterdrückt pro Einschlag den Positionseffekt; die Dummy-Units bleiben bis
   zum letzten Einschlag sichtbar (gewollt).
7. Additive Mischung (Stufe G) auf hellem Boden ist schwach; darkworld-Boden gibt guten Kontrast.
8. Eiswand nie in `og_lgp` (Zellsperre wird sonst nicht gesendet).

---

## 8. Erzeugte Dateien

| Datei | Inhalt |
|---|---|
| `analysis/lgp/lgp-audit.md` | dieses Dokument |
| `analysis/lgp/lgp-skills.tsv` | 243 Skills → Farbe → Palette-Unit |
| `analysis/lgp/lgp-palette-sheet.png` | Kontaktbogen der Kacheln + Simulation |
| `analysis/lgp/tools/make_lgp_tiles.py` | Generator der Kacheln und des Kontaktbogens |
| `assets/odin-grf/lgp/data/texture/effect/*.tga` | 11 Kacheln (2 ersetzt, 9 neu) |
| `docs/07-darkworld-lgp.md` | Hinweis in 1.5: orange `hanmoon2` überholt |
| `analysis/_work/lgp2/` (ignoriert) | `tools/` (server_skills, rob_effects, exe_refs, cases, unit_table, skill_files, looks, mod_sheets, build_tables …), `data/` (JSON/TSV, Switch-Dumps), `mods/`, `vanilla/`, `sheets/`, `robrowser/` |

---

## 9. Nachtrag: Umsetzung und Messung im Spiel (2026-11-24)

Umgesetzt in OdinGuard (`client/src/og_lgp.c`, Modul `lgp` 3.0.0) und xAthena (`og_lgp_color`, Hook für Klasse B,
Flash-Zellen für Klasse P). Getestet im echten Client `alpha.exe` 2018-06-20 in der VM gegen einen lokalen Server
mit der BurningRO-Konfiguration; Bilder aus der VM (`CopyFromScreen`), Ablauf per Tastatur **[gemessen]**.

**Abweichung von 5.1 (gemessen):** Vier der sieben Unit-IDs aus 5.1 sind nicht frei, obwohl der Unit-Switch
(`0xb0b38d`) keinen Fall für sie hat. Ein Diagnose-Hook auf die Unit-Methode `0xb0b370` und Screenshots zeigten:

| Unit | Befund im Spiel | Ursache (statisch nachgesehen) |
|---|---|---|
| 0xb0 | keine Kachel | Paket-Handler `0xa52c53` behandelt 0xb0 als Graffiti (Text) |
| 0xe5 | grüne Pflanzen-Sprites statt Kachel | Akteur-Nachrichten-Handler `0xb0f821` (Fall für 0xe5) |
| 0x104, 0x106 | großer weißer Schein über der Fläche | derselbe Handler (Fälle 26/27) |
| 0xd1, 0x107 | nicht verwendet | derselbe Handler kennt sie (Fälle 16/28) |
| 0xba | bei Stufe 2–5 andere Effekte | `0x9dff81` macht aus 0xba bei Stufe 2–5 0xbb–0xbe; der Server schickt Lite-Units deshalb immer mit Stufe 1 |

**Endgültige Palette (alle neun Farben im Spiel bestätigt, Stufen 1–10):** blau 0xc8, cyan 0xc9, gelb 0xba,
rot 0x8b, orange 0xdf, grün 0xec, violett 0x103, braun 0x89, rosa 0xb5. 0x89 (Hidden Trap) und 0x8b (Hidden
Warp NPC) schickt kein Server, 0xb5 (Moonlit) zeichnet der Client sonst nicht. Fallback ohne Patch: 0xc8/0xc9 (blau).

**Bestätigt im Spiel:** Storm Gust (blau, 9×9), Lord of Vermilion (gelb, 11×11), Meteor Storm (rote Einschläge),
Thunderstorm (gelb), Pneuma (grün, 3×3 inkl. Mitte), Fire Wall (orange), Sanctuary (grün), Magnus (cyan),
Quagmire (braun), Venom Dust (violett), Gravitation (violett), Safety Wall (rosa), Deluge (blau); Flash-Zellen bei
Arrow Shower, Hammer Fall (je 5×5/7×7 braun) und Heaven's Drive (5×5 braun; seine Units leben nur 100 ms) – sie
verschwinden nach `og_lgp_flash_ms` wieder. `@square 2..10` färbt alle Skills in einer Farbe, `@circle` zeigt die
Sage-Bänder. Mit `@lgp off` kommen die normalen Effekte.

**Flash-Zellen-IDs:** Der Client ignoriert Units mit IDs ab `0x7E000000` (gemessen); die Flash-Zellen nutzen die
Lücke 100.000.000–109.999.999 zwischen Account- und NPC-IDs.

**Nicht im Spiel geprüft:** der Fallback (Client ohne Patch: 0xc8 gefüllt, 0xc9 Umriss, beide blau getönt) und ein
zweiter Betrachter ohne LGP neben einem mit LGP (die Server-Logik ist pro Betrachter).
