#!/usr/bin/env python3
"""Regenerates the data arrays and the menus of the LGP test NPC (xAthena npc/added/lgp_test.txt) from the server
config (og_lgp_color / og_lgp / og_lgp_attack / og_lgp_symbol). Skill names, test levels and cast modes are taken
from the arrays already in the NPC file, so run it after changing colours or symbols:

  gen_lgp_test_npc.py <odinguard_athena.conf> <lgp_test.txt>
"""
import re
import sys

conf_path, npc_path = sys.argv[1:3]
conf = open(conf_path).read().splitlines()
npc = open(npc_path).read()

colors, sym_defs, lgp, attack, symbol = [], {}, {}, set(), {}
for line in conf:
    m = re.match(r"(og_\w+):\s*(.+?)\s*(//.*)?$", line)
    if not m:
        continue
    k, v = m.group(1), m.group(2)
    if k == "og_lgp_color":
        colors.append(v.split(",")[0])
    elif k == "og_lgp_symbol_def":
        n, i = v.split(",")
        sym_defs[n] = int(i)
    elif k == "og_lgp_attack":
        attack.update(int(x) for x in v.split(","))
    elif k == "og_lgp_symbol":
        n, *ids = v.split(",")
        for i in ids:
            symbol[int(i)] = n
    elif k == "og_lgp":
        p = [x.strip() for x in v.split(",")]
        if len(p) == 2 and p[1] in colors:
            lgp[int(p[0])] = p[1]


def arr(name):
    m = re.search(r"setarray \." + re.escape(name) + r"\[0\],(.*?);\n", npc, re.S)
    body = m.group(1)
    return [x.strip().strip('"') for x in re.findall(r'"[^"]*"|[^,\s]+', body)]


sid = [int(x) for x in arr("sid")]
slv = arr("slv")
smode = arr("smode")
sname = arr("sname$")
info = {s: (slv[i], smode[i], sname[i]) for i, s in enumerate(sid)}
# skills added after the first NPC version: id -> (level, self cast, name)
info.setdefault(405, ("1", "0", "Fiber Lock"))
sid = sorted(set(sid) | {405}, key=lambda x: sid.index(x) if x in sid else 10**6)
missing = sorted(set(lgp) - set(info))
if missing:
    print("skills without NPC test data (kept out):", missing)

rows = []
for c, cname in enumerate(colors):
    for s in sid:
        if lgp.get(s) == cname:
            rows.append((c, s))
order = [s for _, s in rows]
cstart, cend, cnames, crgb = [], [], [], []
rgb = {"blue": 0x5AA0FF, "cyan": 0x40E0E0, "yellow": 0xFFE000, "red": 0xFF5030, "orange": 0xFF9020, "green": 0x30D060,
       "purple": 0xB060F0, "brown": 0xC08040, "pink": 0xFF70B0, "white": 0xF0F0F0, "gold": 0xFFBE3C, "grey": 0x969696,
       "lightgrey": 0xD0D6E0, "lime": 0xAAEB28, "lightgreen": 0x96FFA0, "peach": 0xFFAA78, "skyblue": 0x78C8FF}
scol = []
for c, cname in enumerate(colors):
    idx = [i for i, (cc, _) in enumerate(rows) if cc == c]
    if not idx:
        continue
    cstart.append(idx[0])
    cend.append(idx[-1])
    cnames.append(cname)
    crgb.append("0x%06X" % rgb[cname])
    scol += [len(cnames) - 1] * len(idx)


def q(x):
    return '"' + x + '"'


def setarr(name, vals):
    return "\tsetarray ." + name + "[0]," + ",".join(vals) + ";\n"


new = ""
new += setarr("sid", [str(s) for s in order])
new += setarr("slv", [info[s][0] for s in order])
new += setarr("smode", [info[s][1] for s in order])
new += setarr("scol", [str(x) for x in scol])
new += setarr("shostile", ["1" if s in attack else "0" for s in order])
new += setarr("ssym$", [q(symbol.get(s, "-")) for s in order])
new += setarr("cstart", [str(x) for x in cstart])
new += setarr("cend", [str(x) for x in cend])
new += setarr("sname$", [q(info[s][2]) for s in order])
new += setarr("cname$", [q(x) for x in cnames])
new += setarr("crgb", crgb)

start = npc.index("\tsetarray .sid[0]")
end = npc.index("\t.n = getarraysize(.sid);")
npc = npc[:start] + new + npc[end:]
open(npc_path, "w").write(npc)
print(len(order), "skills,", len(cnames), "colour groups")
