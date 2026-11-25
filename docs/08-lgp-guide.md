# LGP guide – lite ground skills, colours, symbols, rings and sound

For players, GMs and server owners. Everything here is part of the OdinGuard module `lgp`; technical background and
measurements are in [07-darkworld-lgp.md](07-darkworld-lgp.md) (German), the client patch in
[04-reverse-engineering.md](04-reverse-engineering.md).

> **Scope:** the LGP features were built and tested for **pre-renewal** (PACKETVER 20180620, client 2018-06-20, skill
> ids and units of the pre-renewal skill database). All other OdinGuard features are also compatible with
> renewal; LGP on a renewal server is untested.

## 1. What is LGP, and what is the difference between `@lgp` and `@square`?

**LGP = "lite ground skills".** Ground skills (Storm Gust, Meteor Storm, Lord of Vermilion, Safety Wall, Pneuma,
traps, songs, ...) normally play a heavy animation that hides the floor and the fight. With LGP switched on you do not
get that animation: every cell of the skill is drawn as a **flat, clearly coloured tile** instead (the look of Gepard
Shield's `@lgp` and of the darkworld / WoE-RO clients). Everybody sees their own setting; other players do not need
to switch anything on.

| Command | What it does |
|---|---|
| `@lgp` | **Switches LGP on or off.** This is the master switch. With LGP on, every skill is drawn in *its own colour* (colour set 1). |
| `@square` | **Chooses the colour set** (it also switches LGP on). `@square 1` = every skill in its own colour (same as plain `@lgp`), `@square 2` to `@square 18` = *one single colour for every skill* (the colour order is listed below). `@square off` = LGP off. |

So `@lgp` and `@square` do **not** do the same thing: `@lgp` is the on/off switch, `@square <n>` selects *which
colouring* is used while it is on. `@square 1` and `@lgp on` end up in the same state; `@square 2..18` is for players
who want, for example, all skill cells in one high-contrast colour (`@square 5` = every skill red).

Colour numbers for `@square 2..18`: 2 blue, 3 cyan, 4 yellow, 5 red, 6 orange, 7 green, 8 purple, 9 brown, 10 pink,
11 white, 12 gold, 13 grey, 14 light grey, 15 lime, 16 light green, 17 peach, 18 sky blue (order of the `og_lgp_color:` lines in the server config).

## 2. Shape = what the skill does, colour = which skill, symbol = what kind

| Shape | Meaning |
|---|---|
| **Square** | protective or helpful (Safety Wall, Pneuma, Sanctuary, Deluge, songs that help, ...) |
| **Triangle** | hostile: damage or harm to enemies (Storm Gust, Meteor Storm, Quagmire, traps, Dissonance, ...) |

So Meteor Storm (red triangle) and Volcano (red square), or Storm Gust (blue triangle) and Deluge (blue square) can
never be mixed up.

Colours (colour set 1) follow the **original effects**, measured in the real client (method and table:
[analysis/lgp/original-colours.md](../analysis/lgp/original-colours.md)): blue = Storm Gust, Deluge, Thunderstorm,
Suiton; sky blue = Ice Wall, Ring of Nibelungen; red = Meteor Storm, Volcano, Magnus Exorcismus; orange = Fire Wall,
Demonstration, Kaensin; yellow = Lord of Vermilion (by request, the original flash is orange); green = Sanctuary,
Don't Forget Me; light green = Pneuma, Basilica; lime = Violent Gale; purple = poison and gravity, Lullaby, Drum on the
Battlefield, Rokisweil; pink = Safety Wall, Gospel, Service for You; peach = Grand Cross, Dissonance, Ugly Dance;
brown = earth (Heaven's Drive, Quagmire, Arrow Shower, Hammer Fall); white = Land Protector; light grey = Wall of Fog;
grey = traps and Fiber Lock. Songs and dances differ in the original (blue, violet, red, green, cyan), so they differ
here too; the symbol in the middle tells them apart as well.

Symbols in the middle of the tile (only in colour set 1; `@square 2..18` stays plain):

| Symbol | Skills |
|---|---|
| sword with **+** | damage-boosting fields: Volcano, Deluge, Violent Gale |
| **+** | other helpful fields: Sanctuary, B.S. Sacramenti, Battle Chant, Slim Pitcher |
| **−** | harmful zones: Quagmire, Blinding Mist (Wall of Fog), Venom Dust, Evil Land, Ugly Dance |
| shield | protection: Pneuma, Safety Wall, Basilica, Improvised Defense, Siegfried, Wand of Hermode |
| spider web | Fiber Lock |
| **!** | all traps |
| flame | Fire Wall, Crimson Fire Formation, Raging Fire Dragon |
| moon / sun / star | Moonlit, Warmth of the Moon / Sun / Stars, Fortune's Kiss |
| songs | one symbol each: note (Whistle, Humming), double note (Poem of Bragi), apple (Apple of Idun), heart (Service for You), cross (Assassin Cross), Z (Lullaby), coin (Mental Sensing), swirl (Eternal Chaos), drum (Drum on the Battlefield), ring (Nibelungen), no-entry (Loki's Veil, Ganbantein), diamond (Into the Abyss), bolt (Dissonance), arrow down (Don't Forget Me) |

Land Protector, Ice Wall, Storm Gust, Meteor Storm and the like have no symbol on purpose. The complete skill list
with colour, shape and symbol is `analysis/lgp/lgp-skills.tsv` (columns `lgp_colour`, `lgp_shape`, `lgp_symbol`);
a picture of every symbol is `analysis/lgp/lgp-symbols-sheet.png`.

## 3. Player commands

All commands can be typed with `@` (or the server's own command symbol). They are saved with the character.
Every command has a one-second delay between uses.

| Command | Meaning |
|---|---|
| `@lgp [on\|off]` | LGP on/off. Without an argument it toggles. |
| `@square [on\|off\|1-18]` | Colour set, see section 1. `@square` without argument = on, colour set 1. |
| `@aoes [on\|off]` | Only Storm Gust, Lord of Vermilion and Meteor Storm are drawn as tiles; **all other skills show their normal effects**. `@lgp` or `@square` switches this back. |
| `@circle [on\|off\|1-5]` | Rings of tiles around **your own character** (independent of LGP, also works together with it): 1 = 9x9 ring (cyan), 2 = 7x7 ring (orange), 3 = 5x5 ring (yellow), 4 = all three rings, 5 = ring at 9 cells (light grey, sight distance). Without an argument it toggles look 1. The rings follow you; `@circle off` removes them. |
| `@lgpsound [on\|off]` | Sound of the ground skills for LGP players. LGP removes the animation, and with it the sound, so the server sends the sound alone for the main skills (Storm Gust, Meteor Storm, Thunderstorm, Fire Pillar, Fire Wall, Ice Wall, Safety Wall, Quagmire, Sanctuary, Magnus, B.S. Sacramenti, Grand Cross, Hammer Fall, Venom Dust, all traps). The client's own `/sound` switch still mutes it. |

`@circle` used to be a different, now removed look (the round Deluge bands for Storm Gust); that did not make sense
for a feature that should *simplify* the effects, so rings around the own character replaced it.

## 4. Admin commands

| Command | Meaning |
|---|---|
| `@lgptest [skill id]` | (group 98+) casts every LGP skill one after the other on an empty field (or one skill). `@lgptest stop` aborts. |
| NPC **LGP Test** (Prontera 156,186) | The same through a menu: run everything, by colour, one skill, seconds per skill. Talk to it during a run to abort. The NPC gives itself the items the skills consume (gems via `bNoGemStone`, traps, bottles, stones). |
| `@gatdebug [radius]` | (group 98+) draws the **server's walk grid** around you as tiles: green = walkable, purple = snipable (not walkable, shots pass). Compare it with what the client floor shows; used to check competitive maps. |
| `@ogwarp <map> <x> <y>` | (group 98+) warps into maps that `@mapmove` refuses (nowarpto battleground maps). Logging in directly on a battleground map sends you back to the save point. |
| `@oginfo [char]`, `@ogflags [char]` | Guard information; unrelated to LGP, listed for completeness. |

## 5. `!` commands (client commands, typed in the chat)

These are answered by the OdinGuard client itself, not by the server. They need the OdinGuard client.

| Command | Meaning |
|---|---|
| `!ping` | Latency to the server. |
| `!vsync [on\|off]` | Removes or restores the frame limit. |
| `!dark [on\|off]` | Switches the [competitive pack](09-competitive-pack.md) (`odin-dark.grf`) on or off in `DATA.INI`. **Restart the game** to apply it. Without argument it shows the current state. The choice is stored **on the player's PC** (in the client's `DATA.INI`), so it stays after a restart and for every account on that PC; a patch keeps it. While the pack is loaded the window title ends with `\| Darkworld / Competitive Mode ON`. Servers can still require the pack on some maps. |
| `!crash` | Writes a crash report (folder `odinguard-crash`) for bug reports. |

The `@`-commands of this guide are server commands; operators can switch off, rename or limit every command in the
profile (`[commands.<id>]`, e.g. `[commands.circle] enabled = false`).

## 6. Server configuration

LGP is enabled by the module `lgp` in the profile. All other settings are in `conf/odinguard_athena.conf` (defaults)
and `conf/import/odinguard_conf.txt` (generated by the Studio; the import file wins).

| Key | Meaning |
|---|---|
| `og_lgp_enable: yes/no` | Master switch of the module on the server. |
| `og_lgp_color: <name>,<number>` | A colour (name and the number in the client patch). The order of the lines is the order of `@square 2..18`. |
| `og_lgp: <skill id>,<colour>` | Colour of a skill. Skills not listed are not drawn as tiles. Style prefixes (`set2,...`) give a skill a different look in a colour set. |
| `og_lgp_attack: <ids>` | Skills shown as triangles (hostile). All others are squares. |
| `og_lgp_symbol_def: <name>,<number>` / `og_lgp_symbol: <name>,<ids>` | Symbols of the client patch and which skill gets which. `none` removes a symbol. |
| `og_lgp_aoes: <ids>` | The skills `@aoes` limits LGP to (default Storm Gust, Lord of Vermilion, Meteor Storm). |
| `og_lgp_flash_ms: <ms>` | How long cells of skills without a unit (Arrow Shower, Hammer Fall, Heaven's Drive, ...) stay visible (config 1500). |
| `og_lgp_sound: yes/no` | Sound for LGP players (see `@lgpsound`). The sounds are `data\wav\og_<skill id>.wav` in `odin.grf`, see below. |
| `og_lgp_sound_skills: <ids>` | Skills whose sound is sent **when they are cast** (ground skills, self skills such as Basilica, Magnus, Kaensin, Suiton ...). |
| `og_lgp_sound_unit_skills: <ids>` | Skills whose sound belongs to the **unit**: songs and dances, Gospel. Sent once when the unit appears (not once per cell). The client plays the long original from the caster's skill-use packet, so LGP players do not get that packet for these skills (with `@lgpsound off` they get it as before). |
| `og_lgp_sound_trap_skills: <ids>` | Traps: **silent when set**, the sound is sent when the trap goes off. |
| `og_cmd_<id>`, `og_cmdgroup_<id>`, `og_cmdrate_<id>`, `og_cmdname_<id>` | Per command: on/off, lowest group level, delay in ms, new name of a `!` command. |

Per-player flags (character variables, `@lgp`-style commands set them; script variables, not for players to edit):

| Variable | Meaning |
|---|---|
| `og_flags` | bit 1 (value 2) = LGP on, bit 3 (value 8) = only SG/LoV/MS (`@aoes`), bit 4 (value 16) = no LGP sound. Bit 0 belongs to the aura feature. |
| `og_lgp_style` | 0 = colour set 1, 2..18 = one colour for every skill (the `@square` number). |
| `og_circle` | 0 = off, 1..5 = `@circle` look. |

## 7. Client side and the files you ship

* **Without the OdinGuard client patch** (any other client): LGP still works with plain **blue squares** (protective)
  and **blue triangles** (hostile). Colours and symbols need the patch.
* **With the patch** (OdinGuard `odinguard.dll` in the 2018-06-20 client): the guard announces the capability in the
  handshake and the heartbeat; from then on the server sends colour, shape and symbol in the unit packet and the client
  loads `data\texture\effect\odinlgp_<colour>[_t][_<symbol>].tga` from `odin.grf` (`_t` = triangle). 14 colours × 2
  shapes × (plain + 25 symbols) = 728 textures, about 1.4 MB. Every patched code site is checked against its bytes
  first, so a different client build stays untouched.
* **Skill sounds:** the original sound files have names longer than the 23 characters the sound packet allows, so the
  Studio copies them as `data\wav\og_<id>.wav` into `odin.grf`. The copies are made from the operator's own client
  (never from this repository): `og lgp-sounds --client <client folder> --out <folder>` (the Studio's own `og`, no other
  tool needed), then
  `[assets.odin_grf] lgp-sound = "<folder>"` in the profile.
  Which file belongs to which skill is `assets/lgp-sound/sounds.tsv` (taken from the client's own effect table: the
  sound is the one the original animation plays, e.g. Thunderstorm `magician_thunderstorm.wav`, Raigekisai
  `ef_thunderstorm.wav`). The column `trim_ms` cuts a file: the songs and dances run 30 seconds in the client, in LGP
  they play the same 3 second part the battleground packs (`woebg.grf`) use (the ranges were found by comparing the
  two files sample by sample), all other long sounds are cut to 0-3 s. **LGP off = the original sound in full length**:
  the server only sends these sounds to players who have LGP on, everyone else hears the client's own effect.

## 8. Troubleshooting

| Problem | Cause / fix |
|---|---|
| Blue squares and triangles only | The client has no OdinGuard patch (or another build); colours need the patch. In the first seconds after login the capability may not be known yet. |
| Some skills show their normal effect | `@aoes` is on (only SG/LoV/MS are tiles). Type `@lgp`. |
| A skill shows no tile | It is not in `og_lgp` (not covered on purpose: Warp Portal, Scribble, summons, guild auras). |
| Tiles of a skill disappear while another skill is running | Cells of different skills on the same spot hide each other (the client draws one unit per cell). |
| No sound | `@lgpsound`, the client's `/sound`, the skill is in none of the `og_lgp_sound_*` lists, or `odin.grf` lacks `og_<id>.wav` (rebuild the sounds and the release). Traps only sound when they go off. |
| Rings do not show | `@circle` needs the OdinGuard client (it is the server's per-player guard session that drives the rings); without the colour patch they are plain blue. Try `@circle off`, then `@circle 1`. |
