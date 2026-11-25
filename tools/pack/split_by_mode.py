#!/usr/bin/env python3
"""Splits the staging folders of the competitive pack by game mode so an operator can pick WoE, battleground and PvP
maps one by one (recipe categories flat-maps / floor, variants woe, bg, pvp).

  split_by_mode.py <staging root>     (default: ~/Dev/OdinGuard-assets/competitive)

Reads   <root>/flat-maps-woe-bg-pvp and <root>/floor-flat-woe-bg-pvp   (made by build_categories.sh)
Writes  <root>/flat-maps-{woe,bg,pvp} and <root>/floor-flat-{woe,bg,pvp}

A map belongs to a mode by its name; its .rsw, .gnd and .gat go to that mode. The ground files an .rsw names (rush_cas01
uses prtg_cas02's ground) are copied to the rsw's mode as well, so a mode never points at a ground of another mode that
the operator did not pick. Identical copies in several folders are harmless (same bytes, same path). Standard library only.
"""
import os, re, shutil, sys

MODES = {
    'pvp': re.compile(r'^(pvp_\S+)$', re.I),
    'bg': re.compile(r'^(bat_\w+|guild_vs\d+(_\d+)?|rush_cas\d+|region_\d+)$', re.I),
    'woe': re.compile(r'^(((aldeg|gefg|payg|prtg|arug|schg)_cas\d+|(arug|schg)_dun\d+|te_(aldecas\d+|prtcas\d+|alde_gld|prt_gld))'
                      r'|nguild_\w+|n_castle|gld_dun\d+(_\d+)?|gld2_\w+|guild_room|gvg_\S+|job3_gvg)$', re.I),
}


def mode_of(stem):
    for m in ('pvp', 'bg', 'woe'):   # bg before woe: rush_cas01 is a battleground map
        if MODES[m].match(stem):
            return m
    return None


def rsw_ground(path):
    d = open(path, 'rb').read()
    gnd = d[46:86].split(b'\0')[0].decode('cp949', 'replace')
    gat = d[86:126].split(b'\0')[0].decode('cp949', 'replace')
    return gnd, gat


def split(src, dst_prefix):
    data = os.path.join(src, 'data')
    files = {f.lower(): f for f in os.listdir(data) if os.path.isfile(os.path.join(data, f))}
    out = {m: set() for m in MODES}
    for lf, f in files.items():
        stem, ext = os.path.splitext(lf)
        m = mode_of(stem)
        if m:
            out[m].add(lf)
        if ext == '.rsw' and m:
            for g in rsw_ground(os.path.join(data, f)):
                if g.lower() in files:
                    out[m].add(g.lower())
    for m, names in out.items():
        dst = os.path.join(os.path.dirname(src), '%s-%s' % (dst_prefix, m))
        shutil.rmtree(dst, ignore_errors=True)
        os.makedirs(os.path.join(dst, 'data'))
        for lf in sorted(names):
            shutil.copyfile(os.path.join(data, files[lf]), os.path.join(dst, 'data', files[lf]))
        print('%s-%s: %d files' % (dst_prefix, m, len(names)))
    left = [f for lf, f in files.items() if not any(lf in n for n in out.values())]
    if left:
        print('not in any mode (left out): %s' % ', '.join(sorted(left)[:12]) + (' ...' if len(left) > 12 else ''))


def main():
    root = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/Dev/OdinGuard-assets/competitive')
    for src, prefix in (('flat-maps-woe-bg-pvp', 'flat-maps'), ('floor-flat-woe-bg-pvp', 'floor-flat')):
        p = os.path.join(root, src)
        if os.path.isdir(os.path.join(p, 'data')):
            split(p, prefix)
        else:
            print('missing %s (run build_categories.sh %s first)' % (p, src), file=sys.stderr)


if __name__ == '__main__':
    main()
