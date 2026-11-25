#!/usr/bin/env python3
"""Checks that the map files of a competitive-pack folder match the server's map cache.

The client walks on the .gat it loads; the server decides with its map_cache.dat. If the two differ (a different map
version, or a flat map whose cells were changed), players rubber-band or walk through walls. This tool compares the
cell types of every map the pack replaces with the server's cache.

  verify_mapcache.py <map_cache.dat> [<map_cache.dat> ...] --pack <staging folder (contains data/)> [--original <GRF/dir list>]

Cache files later on the command line override earlier ones (db/map_cache.dat, then db/import/map_cache.dat).
Cell type bytes are compared as they are (GAT type 0-6); "walkability" uses 1, 2 and 5 as blocked (rAthena map_gat2cell).
Exit code: number of maps with a walkability difference. Python 3.9+, standard library only.
"""
import argparse, os, re, struct, sys, zlib


def read_cache(path, into):
    d = open(path, 'rb').read()
    total, count = struct.unpack_from('<IH', d, 0)
    p = 8
    for _ in range(count):
        name, xs, ys, ln = struct.unpack_from('<12shhi', d, p)
        p += 20
        into[name.split(b'\0')[0].decode().lower()] = (xs, ys, zlib.decompress(d[p:p + ln]))
        p += ln


def read_gat(path):
    d = open(path, 'rb').read()
    if d[:4] != b'GRAT':
        raise ValueError('not a GAT')
    w, h = struct.unpack_from('<ii', d, 6)
    cells = bytearray(w * h)
    for i in range(w * h):
        cells[i] = struct.unpack_from('<i', d, 14 + i * 20 + 16)[0] & 0xff
    return w, h, bytes(cells)


def gat_name(rsw):
    d = open(rsw, 'rb').read()
    return d[86:126].split(b'\0')[0].decode('cp949', 'replace')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cache', nargs='+')
    ap.add_argument('--pack', required=True)
    a = ap.parse_args()
    cache = {}
    for c in a.cache:
        read_cache(c, cache)
    data = os.path.join(a.pack, 'data')
    files = {f.lower(): f for f in os.listdir(data)}
    blocked = {1, 2, 5}
    bad = 0
    checked = 0
    for f in sorted(files):
        if not f.endswith('.rsw'):
            continue
        m = f[:-4]
        if m not in cache:
            continue
        gat = gat_name(os.path.join(data, files[f])).lower()
        # the pack's own .gat wins over the client's original: only maps whose ground the pack carries can be compared
        if gat not in files:
            print('%-14s uses %s (not in the pack: original ground, not checked)' % (m, gat))
            continue
        try:
            w, h, cells = read_gat(os.path.join(data, files[gat]))
        except Exception as e:
            print('%-14s %s unreadable: %s' % (m, gat, e)); bad += 1; continue
        xs, ys, cc = cache[m]
        checked += 1
        if (w, h) != (xs, ys):
            print('%-14s SIZE DIFFERS: pack %dx%d, server cache %dx%d' % (m, w, h, xs, ys)); bad += 1; continue
        # the cache stores cells in GAT order (x + y * xs)
        raw = sum(1 for i in range(w * h) if cells[i] != cc[i])
        walk = sum(1 for i in range(w * h) if (cells[i] in blocked) != (cc[i] in blocked))
        if walk:
            print('%-14s WALKABILITY DIFFERS in %d of %d cells (type bytes differ in %d)' % (m, walk, w * h, raw)); bad += 1
        elif raw:
            print('%-14s ok (walkability equal, %d cells with another water/snipe type)' % (m, raw))
        else:
            print('%-14s ok' % m)
    print('%d maps compared with the cache, %d with problems' % (checked, bad))
    return bad


if __name__ == '__main__':
    sys.exit(main())
