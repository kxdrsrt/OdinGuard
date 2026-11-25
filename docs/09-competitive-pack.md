# Competitive pack – flat WoE / battleground / PvP maps for everyone

The competitive pack (`odin-dark.grf`) gives **every player the same competitive look**: flat maps, a dark floor
grid that shows the walk grid, see-through trees, small WoE objects, and so on (the "darkworld" / WoE RO / EstlandRO
style). It is optional per server and **granular**: the operator chooses *what* it contains and *where* it is required.
German details and measurements: [07-darkworld-lgp.md](07-darkworld-lgp.md) part 2.

> **Scope:** built and tested for **pre-renewal** (client 2018-06-20). The mechanism (GRF, DATA.INI, enforcement) is
> not tied to pre-renewal, the map lists and textures are.

## 1. What the operator decides

Three independent decisions:

1. **Which categories go into `odin-dark.grf`** (Studio: *Competitive pack*, or `assets.competitive` in the profile).
   Categories: flat maps, dark floor, trees, barricades, pneuma countdown, nodelay sprites, effects, UI, other.
2. **For which game modes the maps are flat**: the map categories come per mode, `woe`, `bg`, `pvp`, a combined
   `woe-bg-pvp`, or `all` (every map of the client). Pick any combination, e.g. only battlegrounds flat:
   `competitive = ["flat-maps-bg", "floor-flat-bg", "trees"]`.
3. **Where the pack is required**: `assets.competitive_enforce = ["bg", "woe", "pvp"]` (or `["all"]`). Players
   without the accepted pack are sent back to their save point from such maps, with a message. Leave it empty and the
   pack is a **player choice**.

Presets: `competitive_enforce = []` = *none*, `["all"]` = *all competitive maps*, any mix in between.

```toml
[assets]
competitive_source = "/home/me/OdinGuard-assets/competitive"
competitive = ["flat-maps-woe", "floor-flat-woe", "flat-maps-bg", "floor-flat-bg", "trees", "pneuma"]
competitive_enforce = ["bg"]        # battlegrounds require the pack, WoE and PvP stay optional
```

## 2. How the player chooses

The pack is **not mandatory**; it ships with OdinGuard and the client, and every player decides for themselves
(unless the operator enforces it on some maps, see below). It is just a GRF in `DATA.INI`:

* **In game:** `!dark on` / `!dark off` (a client command answered by the guard) inserts or removes `odin-dark.grf`
  directly after `odin.grf` in `DATA.INI`. **Restart the game** afterwards, because the client reads `DATA.INI` only
  at start. `!dark` alone shows the current state.
* **Launcher / patcher:** the OpenThorPatcher setting "competitive visuals: on/off" writes the same line.
* **By hand:** add or delete the `odin-dark.grf` line.
OdinGuard accepts both states: the pack is an *optional protected file*: if present it must be the exact version the
operator built, if absent nothing is reported.

On maps the operator listed in `competitive_enforce` the choice is taken away: the client reports in its hello and
every heartbeat whether the accepted `odin-dark.grf` is loaded (`OG_FLAG_DARKPACK` / `OG_ALIVE_DARKPACK`), and the
server checks that when a player changes map and every 5 seconds. GMs from the group level `og_dark_gm_level`
(default 98) are exempt. The refusal is written as event `darkpack:required` (log only) and the player sees: *"This map
needs the competitive pack (odin-dark.grf) of this server."* Nobody is judged before the first heartbeat.

| Server key | Meaning |
|---|---|
| `og_dark_enforce: none` | `bg`, `woe`, `pvp` (comma separated), `all` or `none`. Generated from `competitive_enforce`. |
| `og_dark_gm_level: 98` | Group level from which a player is exempt. |

Mode of a map = its map flags: `battleground` = bg; `gvg_castle`, `gvg_dungeon`, `gvg`, `gvg_te` and TE castles = woe;
`pvp` = pvp.

## 3. The pack and the server must agree on the map

The client walks on the `.gat` it loads, the server decides with its `map_cache.dat`. A flat map made with GRF Editor's
*flat maps maker* (`rgrf map flat --flatten --remove-light --remove-shadow`) keeps the cells' walkability, but a pack
built from another client version than the server's map cache would not. Check it:

```sh
python3 tools/pack/verify_mapcache.py  db/map_cache.dat db/import/map_cache.dat  --pack ~/OdinGuard-assets/competitive/flat-maps-woe-bg-pvp
```

It lists every map the pack carries, compares size and cell types with the cache and exits with the number of maps
whose walkability differs. Measured on BurningRO: 73 maps compared; 3 groups (`pvp_y_*-2`, `te_prt_gld`,
`te_alde_gld`) differ by a few cells, but they differ the same way between the *original* client `.gat` and the
server cache, so that is not caused by the pack. Maps that are copies of others (`bat_a02..a04`, `bat_b02..b04`,
`rush_cas*`, `schg_cas06-08`, `arug_cas06-08`, `region_8`) use the original map's files through the client's
`resnametable.txt`; the server's map cache must contain the same cells for them (the verifier checks every map that is
both in the pack and in the cache).

## 4. Aliased maps need their minimaps

Maps that are copies (battleground `bat_b03`, rush castles, `bg_ball` ...) are listed in the client's
`data\resnametable.txt` for `.rsw/.gnd/.gat`, **and** their minimap `texture\<ui folder>\map\<name>.bmp` must exist, or
the client stops with *"Resource File Loading fail ... bat_b03.bmp"* when the server warps a player there. The OdinGuard
client tooling adds these aliases (`client-dev/addons/table-append/resnametable.txt`, merged by
`tools/client/append-tables.py`) for all battleground and WoE copies.

## 5. Building the pack

```sh
DARK_DIR=/path/with/dark.grf+dark2.grf+nodelay.grf CLIENT_DIR=/path/to/client  tools/pack/build_categories.sh
python3 tools/pack/split_by_mode.py            # woe / bg / pvp folders from the combined ones
og build myserver.profile.toml                  # builds odin-dark.grf from the selected folders
og grf-scan myserver.profile.toml --client /path/to/client --keep-previous   # before every patch
```

`split_by_mode.py` copies the grounds an `.rsw` names (rush maps use the castles' ground) into the rsw's mode so a mode
never points at a ground the operator did not pick; the mode folders of the dark floor (`floor-flat-woe|bg|pvp`) get
their `data/texture` tiles too. `competitive.py restyle` (part of `build_categories.sh`) draws **snipable** cells
(type 5: not walkable, shots pass, e.g. water edges and barricade gaps) with a red hatch, because the dark floor
otherwise makes them look like walkable ground while the cursor turns red. Use `@gatdebug` (see the LGP guide) to look
at the server's walk grid on a map. The staging folders are never part of the repository.
