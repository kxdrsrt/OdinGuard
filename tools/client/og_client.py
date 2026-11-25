#!/usr/bin/env python3
"""Install, update and remove OdinGuard in a Ragnarok Online client folder (operator tool, Python 3, no dependencies).

    og_client.py status    CLIENT
    og_client.py install   CLIENT --release RELEASE/client [--pack] [--dry-run]
    og_client.py uninstall CLIENT [--keep-settings] [--dry-run]

What it touches (and nothing else):
  ijl15.dll        the game loads it at start. The client's own ijl15.dll (Intel's, or ROExt's proxy) is kept as
                   ijl15r.dll and the OdinGuard loader takes its name: exe -> ijl15.dll (loader) -> ijl15r.dll [-> ijl15x.dll]
  odinguard.dll    the guard
  odin.grf         assets of the guard (tiles, sounds, fallback icons); listed first in DATA.INI
  odin-dark.grf    competitive pack, only with --pack (players switch it with !dark, which edits DATA.INI itself)
  odinguard.ini    settings, copied only when missing
  DATA.INI         odin.grf (and odin-dark.grf) are put in front, numbering from 0 like the client's own !dark
  odinguard.install  what this tool did (renamed files, the DATA.INI it found), used by uninstall

Uninstall removes exactly that plus the files the guard writes while running (odinguard.log, odinguard-crash\\), puts
ijl15r.dll back as ijl15.dll and restores DATA.INI (the saved original when the other entries are unchanged, else
the same list without the OdinGuard entries). Running install twice only updates the files.
"""
import argparse, json, os, shutil, sys, time

LOADER, GUARD, ASSETS, PACK, INI = "ijl15.dll", "odinguard.dll", "odin.grf", "odin-dark.grf", "odinguard.ini"
PREV, DATA, STATE, DATA_BAK = "ijl15r.dll", "DATA.INI", "odinguard.install", "DATA.INI.pre-odinguard"
RUNTIME = ["odinguard.log", "odinguard-crash"]
ODIN_ENTRIES = {ASSETS.lower(), PACK.lower()}


class Fail(Exception):
    pass


def p(d, n):
    return os.path.join(d, n)


def exists_ci(d, name):
    """Windows file names are case-insensitive; the folder may come from there."""
    for n in os.listdir(d):
        if n.lower() == name.lower():
            return n
    return None


def is_loader(path):
    """The OdinGuard loader is a tiny proxy that names odinguard.dll; the originals are 9-400 KB without that name."""
    try:
        if os.path.getsize(path) > 64 * 1024:
            return False
        with open(path, "rb") as f:
            return b"odinguard.dll" in f.read()
    except OSError:
        return False


def parse_data(path):
    with open(path, "rb") as f:
        raw = f.read().decode("cp949", "replace")
    nl = "\r\n" if "\r\n" in raw else "\n"
    items, in_data = [], False
    for line in raw.splitlines():
        s = line.strip()
        if s.startswith("["):
            in_data = s.lower() == "[data]"
        elif in_data and "=" in s and not s.startswith((";", "//")):
            k, v = s.split("=", 1)
            if k.strip().isdigit() and v.strip():
                items.append((int(k), v.strip()))
    return nl, [v for _, v in sorted(items)]


def read_data(d):
    """-> (newline, [entries in client order], file name) or None."""
    n = exists_ci(d, DATA)
    if not n:
        return None
    nl, entries = parse_data(p(d, n))
    return nl, entries, n


def write_data(d, name, nl, entries):
    text = nl.join(["[Data]"] + [f"{i}={e}" for i, e in enumerate(entries)]) + nl
    with open(p(d, name), "wb") as f:
        f.write(text.encode("cp949", "replace"))


def state(d):
    ij = p(d, LOADER)
    info = {"loader_installed": os.path.exists(ij) and is_loader(ij), "prev_dll": os.path.exists(p(d, PREV)),
            "guard": os.path.exists(p(d, GUARD)), "assets": os.path.exists(p(d, ASSETS)),
            "pack": os.path.exists(p(d, PACK)), "manifest": os.path.exists(p(d, STATE))}
    data = read_data(d)
    info["data_ini"] = None if data is None else data[1]
    info["in_data_ini"] = [e for e in (info["data_ini"] or []) if e.lower() in ODIN_ENTRIES]
    return info


def check_client(d):
    if not os.path.isdir(d):
        raise Fail(f"{d} is not a folder")
    if not os.path.exists(p(d, LOADER)) and not os.path.exists(p(d, PREV)):
        raise Fail(f"{d} has no {LOADER}: this does not look like a Ragnarok client folder")


def put(src, dst):
    """Copy through a temporary file and replace atomically: a hard-linked or half-copied file is never written in place."""
    tmp = dst + ".og-tmp"
    shutil.copyfile(src, tmp)
    os.replace(tmp, dst)


def say(dry, msg):
    print(("[dry run] " if dry else "") + msg)


def install(d, rel, pack, dry):
    check_client(d)
    for need in (LOADER, GUARD, ASSETS):
        if not os.path.exists(p(rel, need)):
            raise Fail(f"release folder {rel} has no {need}")
    if pack and not os.path.exists(p(rel, PACK)):
        raise Fail(f"--pack: release folder {rel} has no {PACK}")
    if not is_loader(p(rel, LOADER)):
        raise Fail(f"{p(rel, LOADER)} is not the OdinGuard loader")
    st = state(d)
    manifest = {"version": 1, "time": time.strftime("%Y-%m-%d %H:%M:%S"), "renamed_ijl15": False, "data_ini_backup": None}
    if os.path.exists(p(d, STATE)):
        with open(p(d, STATE)) as f:
            manifest = json.load(f)
    if st["loader_installed"]:
        say(dry, "OdinGuard is already installed here: updating the files")
    else:
        if st["prev_dll"]:
            raise Fail(f"{PREV} exists but {LOADER} is not the OdinGuard loader: refusing to guess which one is the original. "
                       f"Remove or rename {PREV} yourself, or restore the client first.")
        say(dry, f"{LOADER} -> {PREV} (the client's own library stays in the chain)")
        if not dry:
            os.replace(p(d, LOADER), p(d, PREV))
        manifest["renamed_ijl15"] = True
    for n in (LOADER, GUARD, ASSETS) + ((PACK,) if pack else ()):
        say(dry, f"copy {n}")
        if not dry:
            put(p(rel, n), p(d, n))
    if not os.path.exists(p(d, INI)) and os.path.exists(p(rel, INI)):
        say(dry, f"copy {INI} (settings, only when missing)")
        if not dry:
            put(p(rel, INI), p(d, INI))
    data = read_data(d)
    if data is None:
        say(dry, f"no {DATA}: creating one with odin.grf only (add the game archives yourself)")
        entries, nl, name = [], "\r\n", DATA
    else:
        nl, entries, name = data
    rest = [e for e in entries if e.lower() not in ODIN_ENTRIES]
    keep_pack = any(e.lower() == PACK for e in entries)
    new = [ASSETS] + ([PACK] if (pack or keep_pack) else []) + rest
    if new != entries:
        say(dry, f"{DATA}: {', '.join(new[:3])} ... ({len(new)} entries, numbered from 0)")
        if not dry:
            if data is not None and not manifest.get("data_ini_backup") and not os.path.exists(p(d, DATA_BAK)):
                put(p(d, name), p(d, DATA_BAK))
                manifest["data_ini_backup"] = DATA_BAK
            write_data(d, name, nl, new)
    if not dry:
        with open(p(d, STATE), "w") as f:
            json.dump(manifest, f, indent=1)
    say(dry, "nothing was changed." if dry else "OdinGuard installed.")


def uninstall(d, keep_settings, dry):
    check_client(d)
    st = state(d)
    if not st["loader_installed"]:
        raise Fail("OdinGuard loader is not installed here (ijl15.dll is not the OdinGuard loader); nothing to do")
    if not st["prev_dll"]:
        raise Fail(f"{PREV} is missing, so there is no original {LOADER} to put back. Copy the client's own ijl15.dll "
                   f"(Intel's, 372736 bytes, or ROExt's proxy) into this folder as {PREV} and run uninstall again.")
    say(dry, f"{PREV} -> {LOADER} (the client's own library is back)")
    if not dry:
        os.replace(p(d, PREV), p(d, LOADER))
    for n in [GUARD, ASSETS, PACK, STATE] + ([] if keep_settings else [INI]) + RUNTIME:
        f = p(d, n)
        if os.path.isdir(f):
            say(dry, f"remove folder {n}")
            if not dry:
                shutil.rmtree(f)
        elif os.path.exists(f):
            say(dry, f"remove {n}")
            if not dry:
                os.remove(f)
    data = read_data(d)
    if data is not None:
        nl, entries, name = data
        rest = [e for e in entries if e.lower() not in ODIN_ENTRIES]
        bak = p(d, DATA_BAK)
        restored = False
        if os.path.exists(bak):
            old = parse_data(bak)[1]
            if [e for e in old if e.lower() not in ODIN_ENTRIES] == rest:
                say(dry, f"{DATA}: original restored from {DATA_BAK}")
                if not dry:
                    put(bak, p(d, name))
                restored = True
        if not restored and rest != entries:
            say(dry, f"{DATA}: OdinGuard entries removed ({len(rest)} entries left, numbered from 0)")
            if not dry:
                write_data(d, name, nl, rest)
        if os.path.exists(bak) and not dry:
            os.remove(bak)
    say(dry, "nothing was changed." if dry else "OdinGuard removed.")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("status")
    s.add_argument("client")
    i = sub.add_parser("install")
    i.add_argument("client")
    i.add_argument("--release", required=True, help="the client/ folder of an OdinGuard release")
    i.add_argument("--pack", action="store_true", help="also install odin-dark.grf (competitive pack)")
    i.add_argument("--dry-run", action="store_true")
    u = sub.add_parser("uninstall")
    u.add_argument("client")
    u.add_argument("--keep-settings", action="store_true")
    u.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "status":
            print(json.dumps(state(a.client), indent=1))
        elif a.cmd == "install":
            install(a.client, a.release, a.pack, a.dry_run)
        else:
            uninstall(a.client, a.keep_settings, a.dry_run)
    except Fail as e:
        print("error: " + str(e), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
