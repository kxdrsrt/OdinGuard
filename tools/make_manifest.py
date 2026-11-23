#!/usr/bin/env python3
"""Build manifest.json for a client directory: SHA-256 of every exe/dll (and optionally the GRFs).

usage: make_manifest.py <client-dir> --build <name> [--grf] [-o manifest.json]
The server imports it into og_manifest (see server/sql/odinguard.sql).
"""
import argparse, hashlib, json, os, sys, time


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("client_dir")
    ap.add_argument("--build", required=True)
    ap.add_argument("--grf", action="store_true", help="also hash *.grf (slow for large archives)")
    ap.add_argument("-o", "--output", default="manifest.json")
    a = ap.parse_args()

    exts = {".exe", ".dll"} | ({".grf"} if a.grf else set())
    files = {}
    for name in sorted(os.listdir(a.client_dir)):
        p = os.path.join(a.client_dir, name)
        if os.path.isfile(p) and os.path.splitext(name)[1].lower() in exts:
            files[name] = sha256(p)
    if not files:
        sys.exit("no files found")
    manifest = {"build": a.build, "created": int(time.time()), "files": files}
    with open(a.output, "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"{len(files)} files -> {a.output}")


if __name__ == "__main__":
    main()
