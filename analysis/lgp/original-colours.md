# Original skill colours (measured)

LGP tiles must look like the skill they replace. For every LGP skill the **original effect** was cast in the real
2018-06-20 client (LGP off) and compared with an empty screenshot of the same scene; the colour the effect *adds* to
the picture is the effect's colour. Result per skill: [original-colours.tsv](original-colours.tsv).

## Method and its limits

* A test client in a VM, local test server, `@lgptest <skill id>` casts the skill, one screenshot of the empty scene
  before and two while the effect runs (`analysis/lgp/tools` has the tile generator, the capture loop was a shell
  script and is not part of the repository).
* Only pixels that changed are used, minus the casting character and the UI strip. Additive effects are the rule in RO,
  so the added colour (after - before) is the effect's colour. Pixels that already reached 245+ (saturated white) are
  left out, because on a bright map every pale effect turns blue-white and on a dark map every bright effect turns
  white. The first run was on Prontera's bright floor (wrong: pale effects looked cyan), the second on the dark sewer
  floor (`prt_sewb1`).
* Columns: `changed_pixels`, the circular mean `hue` of the added colour, its `saturation`, how uniform the hue is
  (`concentration`, 1 = one hue). **strong** = at least 5,000 pixels, concentration 0.8 and saturation 0.2; those
  decided a change. **weak** rows (thin rings, small effects, the sewer's own animated light) were not used: the old
  colour stays.
* Not measured: what a player's own client with other effect GRFs shows; Gepard's LGP (not available).

## What changed because of it

| Skill | Before | Measured | Now |
|---|---|---|---|
| Magnus Exorcismus | cyan | pale red (hue 11°, the `ring_red` texture) | red |
| Basilica | cyan | pale green (120°) | light green |
| Thunderstorm, Raigekisai | yellow | blue (249°) | blue |
| Ice Wall | cyan | light blue (209°) | sky blue |
| Grand Cross | cyan | pale orange (17°) | peach |
| Kaensin | red | orange (19°) | orange |
| Lullaby, Drum on the Battlefield, Rokisweil | gold / sky blue | violet (258°–282°) | purple |
| Rich Mankim | sky blue | orange-gold (35°) | gold |
| Eternal Chaos | gold | cyan (168°) | cyan |
| Siegfried, Poem of Bragi, Hermode | gold / sky blue | deep blue (229°–243°) | blue |
| Dissonance | gold | pale red (10°) | peach |
| Assassin Cross, Fortune's Kiss, Moonlit | gold / peach | red | red |
| Apple of Idun | gold | orange (31°) | orange |
| Whistle, Humming | gold / peach | violet | purple |
| Don't Forget Me | peach | green (106°) | green |
| Service for You | peach | pink-red (340°) | pink |

**Songs and dances are not one colour in the original** (an earlier question): they range from blue over violet to
red and green, so the LGP tiles now follow that. Unchanged because they already matched: Storm Gust blue, Meteor
Storm and Volcano red-orange, Deluge blue, Violent Gale green-yellow, Sanctuary and Pneuma pale green, Fire Wall and
Demonstration orange, Venom Dust magenta, Suiton blue. Lord of Vermilion measured orange (the flash dominates the
picture); it stays **yellow** on request.
