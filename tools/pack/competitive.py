#!/usr/bin/env python3
"""Helpers for build_categories.sh (competitive pack staging). Python 3.9+, standard library only.

All GRF reading/writing is done by the rgrf CLI in build_categories.sh; this file only works on
extracted files:

  scope    <scope> <names-file>             print the map names of a scope (woe-bg-pvp | all)
  stage    <manifest.tsv> <cache> <out>      copy the entries of a manifest from extracted GRFs, check MD5
  floor    --maps-dir D --tiles T --out O   ground tiles on every top surface of the maps' .gnd
           [--markers M] [--fill-void] [--maps FILE]
  restyle  <staging dir>...                  make snipable cells (gat type 5) look blocked: hatch on the brown quadrants
  measure  <dir>...                          files and bytes per staging folder (TSV)
"""
import argparse, hashlib, os, re, shutil, struct, sys

SCOPES = {
    # WoE castles (FE, SE, TE, novice, rush), WoE/agit guild dungeons, battlegrounds, PvP/GvG arenas
    'woe-bg-pvp': re.compile(
        r'^((aldeg|gefg|payg|prtg|arug|schg)_cas\d+|(arug|schg)_dun\d+|te_(aldecas\d+|prtcas\d+|alde_gld|prt_gld)'
        r'|nguild_\w+|n_castle|rush_cas\d+|gld_dun\d+(_\d+)?|gld2_\w+|guild_vs\d+(_\d+)?|guild_room'
        r'|bat_\w+|pvp_\S+|gvg_\S+|job3_gvg)$', re.I),
    'all': re.compile(r'.*'),
}


def scope(args):
    names = sorted({l.strip().lower() for l in open(args.names_file, encoding='utf-8') if l.strip()})
    for n in names:
        n = re.sub(r'^data\\', '', n)
        n = re.sub(r'\.rsw$', '', n)
        if '\\' not in n and SCOPES[args.scope].match(n):
            print(n)


def md5(p):
    h = hashlib.md5()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()


def stage(args):
    n = bad = 0
    for line in open(args.manifest, encoding='utf-8'):
        if line.startswith('#') or not line.strip(): continue
        grf, apath, target, want, _size = line.rstrip('\n').split('\t')
        src = os.path.join(args.cache, os.path.splitext(os.path.basename(grf))[0], *apath.split('\\'))
        dst = os.path.join(args.out, *target.split('\\'))
        if not os.path.isfile(src):
            print('missing in cache: %s %s' % (grf, apath), file=sys.stderr); bad += 1; continue
        if md5(src) != want:
            print('md5 mismatch: %s %s' % (grf, apath), file=sys.stderr); bad += 1; continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst); n += 1
    print('%s: %d files staged, %d problems' % (os.path.basename(args.manifest), n, bad))
    sys.exit(1 if bad else 0)


# ---- GND 1.7-1.9 / GAT 1.2 (1.8/1.9 only add water data after the cubes; kept as it is) ----

def read_gat(p):
    b = open(p, 'rb').read()
    assert b[:4] == b'GRAT', p
    w, h = struct.unpack_from('<ii', b, 6)
    return w, h, [struct.unpack_from('<I', b, 14 + i * 20 + 16)[0] for i in range(w * h)]


def read_gnd(p):
    b = open(p, 'rb').read()
    assert b[:4] == b'GRGN' and b[4] == 1 and 7 <= b[5] <= 9, (p, b[4:6])
    w, h, zoom, tc, tl = struct.unpack_from('<iifii', b, 6)
    o = 26
    tex = [b[o + i * tl:o + (i + 1) * tl] for i in range(tc)]
    o += tc * tl
    lc, cw, ch, _ = struct.unpack_from('<4i', b, o)
    lm = b[o:o + 16 + lc * cw * ch * 4]
    o += 16 + lc * cw * ch * 4
    (sc,) = struct.unpack_from('<i', b, o); o += 4
    surfs = [bytearray(b[o + i * 40:o + (i + 1) * 40]) for i in range(sc)]
    o += sc * 40
    cubes = [bytearray(b[o + i * 28:o + (i + 1) * 28]) for i in range(w * h)]
    o += w * h * 28
    assert o <= len(b) and (b[5] > 7 or o == len(b)), (p, o, len(b))
    return dict(hdr=b[:6], w=w, h=h, zoom=zoom, tl=tl, tex=tex, lm=lm, lmcount=lc, surfs=surfs, cubes=cubes, tail=b[o:])


def write_gnd(g, p):
    out = bytearray(g['hdr'])
    out += struct.pack('<iifii', g['w'], g['h'], g['zoom'], len(g['tex']), g['tl'])
    for t in g['tex']: out += t
    out += g['lm']
    out += struct.pack('<i', len(g['surfs']))
    for s in g['surfs']: out += s
    for c in g['cubes']: out += c
    out += g['tail']
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(out)


def cstr(b):
    return b.split(b'\0')[0].decode('cp949', 'replace')


# GAT cell type -> digit of the tile name c<a>c<b>c<c>c<d>.bmp (naming of the darkworld tiles,
# cells in the order (2x,2y), (2x+1,2y), (2x,2y+1), (2x+1,2y+1))
CODE = {0: 0, 6: 0, 1: 1, 5: 5, 3: -1}
# UV of a whole tile as in the darkworld GNDs (u1..u4, v1..v4)
TILE_UV = struct.pack('<8f', 1, 0, 1, 0, 0, 0, 1, 1)
WHITE = b'\xff\xff\xff\xff'


def tile_name(types, gw, gh, x, y):
    out = []
    for dy in (0, 1):
        for dx in (0, 1):
            gx, gy = 2 * x + dx, 2 * y + dy
            t = types[gy * gw + gx] & 0x7fffffff if gx < gw and gy < gh else 1
            out.append(CODE.get(t, 1))
    return 'c%dc%dc%dc%d.bmp' % tuple(out)


def tile_file(tiles, name):
    return os.path.join(tiles, *name.lower().split('\\'))


def floor_map(m, gnd_p, gat_p, tiles, marker_dir, fill_void, out_dir, stats):
    g = read_gnd(gnd_p)
    gw, gh, types = read_gat(gat_p)
    dark = None
    if marker_dir:
        dg, da = os.path.join(marker_dir, 'data', m + '.gnd'), os.path.join(marker_dir, 'data', m + '.gat')
        if os.path.isfile(dg) and os.path.isfile(da):
            d = read_gnd(dg); dw, dh, dtypes = read_gat(da)
            if (d['w'], d['h'], dw, dh) == (g['w'], g['h'], gw, gh):
                dark = (d, [cstr(t) for t in d['tex']], dtypes)
    newtex, used = {}, set()
    st = dict(tile=0, marker=0, void_filled=0, void=0)
    W = g['w']
    for y in range(g['h']):
        for x in range(W):
            c = g['cubes'][y * W + x]
            top = struct.unpack_from('<i', c, 16)[0]
            if top < 0 and not fill_void:
                st['void'] += 1; continue
            name, uv, col = tile_name(types, gw, gh, x, y), TILE_UV, WHITE
            if dark:
                d, dnames, dtypes = dark
                dtop = struct.unpack_from('<i', d['cubes'][y * W + x], 16)[0]
                if dtop >= 0:
                    idx = [(2 * y + dy) * gw + 2 * x + dx for dy in (0, 1) for dx in (0, 1)]
                    if all(i < len(types) and (types[i] & 0x7fffffff) == (dtypes[i] & 0x7fffffff) for i in idx):
                        sd = d['surfs'][dtop]
                        dn = dnames[struct.unpack_from('<h', sd, 32)[0]]
                        if not re.match(r'^c-?\d', dn, re.I) and dn.lower() != 'backside.bmp' \
                                and os.path.isfile(tile_file(tiles, dn)):
                            name, uv, col = dn, bytes(sd[0:32]), bytes(sd[36:40])
                            st['marker'] += 1
            if not os.path.isfile(tile_file(tiles, name)):
                name = 'c1c1c1c1.bmp'
            if name not in newtex:
                newtex[name] = len(g['tex']) + len(newtex)
            used.add(name.lower())
            if top >= 0:
                s = bytearray(g['surfs'][top])  # keeps the lightmap (shadows) of the map
                st['tile'] += 1
            else:
                s = bytearray(40); st['void_filled'] += 1  # lightmap 0
            s[0:32] = uv
            struct.pack_into('<h', s, 32, newtex[name])
            s[36:40] = col
            struct.pack_into('<i', c, 16, len(g['surfs']))
            g['surfs'].append(s)
    for name in sorted(newtex, key=newtex.get):
        g['tex'].append(name.encode('cp949').ljust(g['tl'], b'\0'))
    # the old top surfaces stay in the file unreferenced; walls keep theirs
    write_gnd(g, os.path.join(out_dir, 'data', m + '.gnd'))
    stats[m] = st
    return used


def floor(args):
    maps = None
    if args.maps:
        maps = [l.strip().lower() for l in open(args.maps, encoding='utf-8') if l.strip()]
    src = os.path.join(args.maps_dir, 'data')
    have = {f[:-4].lower(): f[:-4] for f in os.listdir(src) if f.lower().endswith('.gnd')}
    todo = sorted(maps if maps is not None else have)
    used, stats, skipped = set(), {}, []
    for m in todo:
        base = os.path.join(src, have.get(m, m))
        if m not in have or not os.path.isfile(base + '.gat'):
            skipped.append(m); continue
        try:
            used |= floor_map(m, base + '.gnd', base + '.gat', args.tiles, args.markers, args.fill_void, args.out, stats)
        except AssertionError as e:
            skipped.append('%s (%s)' % (m, e))
    for name in sorted(used):
        d = os.path.join(args.out, 'data', 'texture', *name.split('\\'))
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copyfile(tile_file(args.tiles, name), d)
    tot = {k: sum(v[k] for v in stats.values()) for k in ('tile', 'marker', 'void_filled', 'void')}
    print('floor: %d maps, %d tile textures, %s; skipped %d %s' % (len(stats), len(used), tot, len(skipped), skipped[:10]))


def restyle(args):
    """Snipable cells (type 5: not walkable, shots pass) are drawn brown by the darkworld tiles and look like floor.
    A diagonal hatch in a warning colour on exactly those quadrants makes them read as 'blocked'. The quadrants are found
    by colour, so the tile naming and orientation do not matter. Needs Pillow."""
    from PIL import Image, ImageDraw
    n = 0
    for d in args.dirs:
        for root, _, files in os.walk(os.path.join(d, 'data', 'texture')):
            for f in files:
                if not re.match(r'^c-?\d+c-?\d+c-?\d+c-?\d+\.bmp$', f, re.I):
                    continue
                path = os.path.join(root, f)
                im = Image.open(path).convert('RGB')
                w, h = im.size
                hw, hh = w // 2, h // 2
                dr = ImageDraw.Draw(im)
                changed = False
                for qx in (0, 1):
                    for qy in (0, 1):
                        box = (qx * hw, qy * hh, (qx + 1) * hw, (qy + 1) * hh)
                        px = im.crop(box).resize((1, 1), Image.BOX).getpixel((0, 0))
                        if px[0] > px[2] + 15 and px[0] > 70:      # the brown of type 5
                            q = Image.new('RGB', (hw, hh), (62, 46, 40))
                            qd = ImageDraw.Draw(q)
                            for k in range(-hh, hw + hh, 8):
                                qd.line((k, 0, k + hh, hh), fill=(214, 96, 52), width=2)
                            im.paste(q, box[:2])
                            changed = True
                if changed:
                    im.save(path)
                    n += 1
    print('restyle: %d tile textures changed' % n)


def measure(args):
    print('folder\tfiles\tbytes')
    for d in args.dirs:
        n = b = 0
        for r, _, fs in os.walk(d):
            for f in fs:
                if f.startswith('.'): continue
                n += 1; b += os.path.getsize(os.path.join(r, f))
        print('%s\t%d\t%d' % (os.path.basename(d.rstrip('/')), n, b))


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest='cmd', required=True)
    a = sp.add_parser('scope'); a.add_argument('scope', choices=SCOPES); a.add_argument('names_file'); a.set_defaults(f=scope)
    a = sp.add_parser('stage'); a.add_argument('manifest'); a.add_argument('cache'); a.add_argument('out'); a.set_defaults(f=stage)
    a = sp.add_parser('floor')
    a.add_argument('--maps-dir', required=True, help='folder with data/<map>.gnd and .gat')
    a.add_argument('--tiles', required=True, help='texture root with the tile textures (lower-case names)')
    a.add_argument('--markers', help='folder with darkworld data/<map>.gnd/.gat: keep their WoE marker tiles where the GAT matches')
    a.add_argument('--fill-void', action='store_true', help='give cells without ground a tile too (flat maps)')
    a.add_argument('--maps', help='file with map names (default: every .gnd in --maps-dir)')
    a.add_argument('--out', required=True)
    a.set_defaults(f=floor)
    a = sp.add_parser('restyle'); a.add_argument('dirs', nargs='+'); a.set_defaults(f=restyle)
    a = sp.add_parser('measure'); a.add_argument('dirs', nargs='+'); a.set_defaults(f=measure)
    args = ap.parse_args(); args.f(args)


if __name__ == '__main__':
    main()
