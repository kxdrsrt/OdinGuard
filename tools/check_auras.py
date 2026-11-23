#!/usr/bin/env python3
"""Checks which hat effects (auras for the map-server's og_aura list) a client can draw.

A hat effect without its files shows "Cannot find File" and can crash the client at login, so only ids
reported as "ok" belong into og_aura. Effects drawn from a .str file are checked completely (the .str and
all its textures). Effects built into the client exe (hatEffectID) cannot be checked from the archives:
they are listed as "exe" and need a test in game (@aura); on the 2018-06-20 client most of them draw
nothing or miss files (see docs/05-tests.md).

Needs rgrf (RustyGRF) in PATH. Usage: check_auras.py <client folder> [--conf]
  --conf  print og_aura lines for the ids that are ok
"""
import re
import struct
import subprocess
import sys
from pathlib import Path

HAT_TABLE = "data\\luafiles514\\lua files\\hateffectinfo\\hateffectinfo.lub"
EFFECT_DIR = "data\\texture\\effect\\"


def rgrf(*args):
    return subprocess.run(["rgrf", "-q", *args], capture_output=True).stdout


def archives(client):
    ini = next((p for p in client.iterdir() if p.name.lower() == "data.ini"), None)
    if ini is None:
        sys.exit(f"no DATA.INI in {client}")
    order = []
    for line in ini.read_text(errors="replace").splitlines():
        m = re.match(r"\s*(\d+)\s*=\s*(\S.*?)\s*$", line)
        if m and (client / m.group(2)).is_file():
            order.append((int(m.group(1)), client / m.group(2)))
    return [p for _, p in sorted(order)]


def read_entry(grfs, entry):
    """The entry from the first archive that has it (like the client)."""
    for g in grfs:
        listed = rgrf("ls", str(g), entry).decode("cp1252", "replace").splitlines()
        if any(l.strip().lower() == entry.lower() for l in listed):
            return g, rgrf("cat", str(g), entry)
    return None, None


def hat_table(grfs):
    g, data = read_entry(grfs, HAT_TABLE)
    if data is None:
        sys.exit("hateffectinfo.lub not found")
    if data.startswith(b"\x1bLua"):
        data = rgrf("lua", "decompile", str(g), "--entry", HAT_TABLE)
    text = data.decode("cp1252", "replace")
    head, _, body = text.partition("hatEffectTable")
    ids = {m.group(1): int(m.group(2)) for m in re.finditer(r"(\w+)\s*=\s*(\d+)", head)}
    table = {}
    for m in re.finditer(r"\[HatEFID\.(\w+)\]\s*=\s*\{(.*?)\}", body, re.S):
        res = re.search(r'resourceFileName\s*=\s*"(.*?)"', m.group(2))
        eid = re.search(r"hatEffectID\s*=\s*(\d+)", m.group(2))
        # A later entry of the same id wins, as in Lua.
        table[ids[m.group(1)]] = (m.group(1), res.group(1).replace("\\\\", "\\") if res else None,
                                  int(eid.group(1)) if eid else None)
    return g, table


def str_textures(data):
    layers = struct.unpack_from("<I", data, 16)[0]
    off, tex = 36, set()
    for _ in range(layers):
        count = struct.unpack_from("<I", data, off)[0]
        off += 4
        for _ in range(count):
            tex.add(data[off:off + 128].split(b"\0")[0].decode("cp949", "replace"))
            off += 128
        off += 4 + struct.unpack_from("<I", data, off)[0] * 124
    return tex


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    client = Path(sys.argv[1])
    grfs = archives(client)
    have = set()
    for g in grfs:
        have.update(l.strip().lower() for l in rgrf("ls", str(g), EFFECT_DIR + "*").decode("cp1252", "replace").splitlines())
    src, table = hat_table(grfs)
    print(f"archives: {', '.join(g.name for g in grfs)}; hat effect table from {src.name}")
    ok = []
    for hid in sorted(table):
        name, res, eid = table[hid]
        if res is None:
            print(f"{hid:4} exe      {name} (client effect {eid}, test in game)")
            continue
        path = EFFECT_DIR + res
        if path.lower() not in have:
            print(f"{hid:4} MISSING  {name}: {path}")
            continue
        _, data = read_entry(grfs, path)
        base = path.rsplit("\\", 1)[0]
        missing = sorted(t for t in str_textures(data) if f"{base}\\{t}".lower() not in have)
        if missing:
            print(f"{hid:4} MISSING  {name}: textures {', '.join(missing)}")
        else:
            print(f"{hid:4} ok       {name}")
            ok.append((hid, name))
    if "--conf" in sys.argv:
        print()
        for hid, name in ok:
            print(f"og_aura: {re.sub(r'^HAT_EF_', '', name).lower()},{hid}")


if __name__ == "__main__":
    main()
