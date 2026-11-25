#!/usr/bin/env python3
"""Tests of tools/client/og_client.py: install, update, uninstall leave a client folder exactly as it was.
Run: python3 tests/client/test_og_client.py"""
import hashlib, os, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "client", "og_client.py")
LOADER = b"MZ" + b"\0" * 200 + b"odinguard.dll\0ijl15r.ijlInit\0" + b"\0" * 400
INTEL = (b"MZ" + b"INTEL-IJL15" * 3000).ljust(80000, b"\0")   # a real one is 372736 bytes; only "above 64 KB, no odinguard.dll" matters
ROEXT = b"MZ" + b"ROEXT-PROXY" + b"ijl15x.dll\0" * 3


def run(*args):
    p = subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def snapshot(d):
    out = {}
    for root, _, files in os.walk(d):
        for f in files:
            full = os.path.join(root, f)
            with open(full, "rb") as fh:
                out[os.path.relpath(full, d)] = hashlib.sha256(fh.read()).hexdigest()
    return out


def write(d, name, data):
    path = os.path.join(d, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("cp949"))


def read(d, name):
    with open(os.path.join(d, name), "rb") as f:
        return f.read()


class T(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.rel = os.path.join(self.tmp.name, "rel")
        write(self.rel, "ijl15.dll", LOADER)
        write(self.rel, "odinguard.dll", b"GUARD-v1")
        write(self.rel, "odin.grf", b"ODIN-v1")
        write(self.rel, "odin-dark.grf", b"DARK-v1")
        write(self.rel, "odinguard.ini", "[OdinGuard]\r\nSplashMs=1800\r\n")
        self.c = os.path.join(self.tmp.name, "client")

    def tearDown(self):
        self.tmp.cleanup()

    def plain(self, data_ini="[Data]\r\n1=cdata.grf\r\n2=core.grf\r\n3=data.grf\r\n"):
        write(self.c, "alpha.exe", b"EXE")
        write(self.c, "ijl15.dll", INTEL)
        write(self.c, "DATA.INI", data_ini)
        write(self.c, "data.grf", b"G" * 100)

    def roundtrip(self, *flags):
        before = snapshot(self.c)
        rc, out = run("install", self.c, "--release", self.rel, *flags)
        self.assertEqual(rc, 0, out)
        mid = snapshot(self.c)
        self.assertNotEqual(before, mid)
        write(self.c, "odinguard.log", "log")                      # the game writes these while running
        write(self.c, "odinguard-crash/crash-1.txt", "x")
        rc, out = run("uninstall", self.c)
        self.assertEqual(rc, 0, out)
        self.assertEqual(snapshot(self.c), before, "uninstall must give back exactly the original folder")
        return mid

    def test_plain_client_data_ini_from_one(self):
        self.plain()
        mid = self.roundtrip()
        self.assertEqual(mid["ijl15r.dll"], hashlib.sha256(INTEL).hexdigest())   # the Intel original stays in the chain
        self.assertEqual(mid["ijl15.dll"], hashlib.sha256(LOADER).hexdigest())

    def test_data_ini_after_install(self):
        self.plain()
        run("install", self.c, "--release", self.rel, "--pack")
        # the pack is copied but stays off until the player types !dark on
        self.assertTrue(os.path.exists(os.path.join(self.c, "odin-dark.grf")))
        self.assertEqual(read(self.c, "DATA.INI").decode(),
                         "[Data]\r\n0=odin.grf\r\n1=cdata.grf\r\n2=core.grf\r\n3=data.grf\r\n")

    def test_pack_on_and_the_players_choice_survives_an_update(self):
        self.plain()
        run("install", self.c, "--release", self.rel, "--pack", "--pack-on")
        on = "[Data]\r\n0=odin.grf\r\n1=odin-dark.grf\r\n2=cdata.grf\r\n3=core.grf\r\n4=data.grf\r\n"
        self.assertEqual(read(self.c, "DATA.INI").decode(), on)
        run("install", self.c, "--release", self.rel, "--pack")     # update without --pack-on keeps it on
        self.assertEqual(read(self.c, "DATA.INI").decode(), on)

    def test_roext_chain_with_pack(self):
        write(self.c, "alpha.exe", b"EXE")
        write(self.c, "ijl15.dll", ROEXT)
        write(self.c, "ijl15x.dll", INTEL)
        write(self.c, "DATA.INI", "[Data]\n0=cdata.grf\n1=data.grf\n")
        mid = self.roundtrip("--pack")
        self.assertEqual(mid["ijl15r.dll"], hashlib.sha256(ROEXT).hexdigest())
        self.assertEqual(mid["ijl15x.dll"], hashlib.sha256(INTEL).hexdigest())

    def test_install_twice_updates_only(self):
        self.plain()
        before = snapshot(self.c)
        run("install", self.c, "--release", self.rel)
        write(self.rel, "odinguard.dll", b"GUARD-v2")
        rc, out = run("install", self.c, "--release", self.rel)
        self.assertEqual(rc, 0, out)
        self.assertIn("already installed", out)
        self.assertEqual(read(self.c, "odinguard.dll"), b"GUARD-v2")
        self.assertEqual(read(self.c, "ijl15r.dll"), INTEL)        # the loader never overwrites the original
        run("uninstall", self.c)
        self.assertEqual(snapshot(self.c), before)

    def test_player_switched_the_pack_off_with_bang_dark(self):
        self.plain()
        run("install", self.c, "--release", self.rel, "--pack")
        write(self.c, "DATA.INI", "[Data]\r\n0=odin.grf\r\n1=cdata.grf\r\n2=core.grf\r\n3=data.grf\r\n")   # what !dark off writes
        rc, out = run("uninstall", self.c)
        self.assertEqual(rc, 0, out)
        self.assertEqual(read(self.c, "DATA.INI").decode(), "[Data]\r\n1=cdata.grf\r\n2=core.grf\r\n3=data.grf\r\n")

    def test_user_added_a_grf_after_install(self):
        self.plain()
        run("install", self.c, "--release", self.rel)
        write(self.c, "DATA.INI", "[Data]\r\n0=odin.grf\r\n1=skin.grf\r\n2=cdata.grf\r\n3=core.grf\r\n4=data.grf\r\n")
        run("uninstall", self.c)
        # the saved original no longer matches: the user's entry stays, the OdinGuard entry goes
        self.assertEqual(read(self.c, "DATA.INI").decode(), "[Data]\r\n0=skin.grf\r\n1=cdata.grf\r\n2=core.grf\r\n3=data.grf\r\n")

    def test_hard_linked_files_are_never_written_in_place(self):
        self.plain()
        run("install", self.c, "--release", self.rel)
        twin = os.path.join(self.tmp.name, "twin.grf")
        os.link(os.path.join(self.c, "odin.grf"), twin)            # e.g. a clone made with hard links
        write(self.rel, "odin.grf", b"ODIN-v2")
        self.assertEqual(run("install", self.c, "--release", self.rel)[0], 0)
        self.assertEqual(read(self.c, "odin.grf"), b"ODIN-v2")
        with open(twin, "rb") as f:
            self.assertEqual(f.read(), b"ODIN-v1", "the other link must keep its content")
        self.assertFalse([n for n in os.listdir(self.c) if n.endswith(".og-tmp")])

    def test_dry_run_changes_nothing(self):
        self.plain()
        before = snapshot(self.c)
        self.assertEqual(run("install", self.c, "--release", self.rel, "--dry-run")[0], 0)
        self.assertEqual(snapshot(self.c), before)
        run("install", self.c, "--release", self.rel)
        mid = snapshot(self.c)
        self.assertEqual(run("uninstall", self.c, "--dry-run")[0], 0)
        self.assertEqual(snapshot(self.c), mid)

    def test_keep_settings(self):
        self.plain()
        run("install", self.c, "--release", self.rel)
        write(self.c, "odinguard.ini", "[OdinGuard]\r\nSplashMs=0\r\n")
        run("uninstall", self.c, "--keep-settings")
        self.assertTrue(os.path.exists(os.path.join(self.c, "odinguard.ini")))

    def test_refuses_ambiguous_ijl15r(self):
        self.plain()
        write(self.c, "ijl15r.dll", b"SOMETHING")
        rc, out = run("install", self.c, "--release", self.rel)
        self.assertEqual(rc, 1)
        self.assertIn("ijl15r.dll exists", out)
        self.assertEqual(read(self.c, "ijl15.dll"), INTEL)

    def test_uninstall_without_original_explains_and_deletes_nothing(self):
        self.plain()
        run("install", self.c, "--release", self.rel)
        os.remove(os.path.join(self.c, "ijl15r.dll"))
        rc, out = run("uninstall", self.c)
        self.assertEqual(rc, 1)
        self.assertIn("ijl15r.dll is missing", out)
        self.assertTrue(os.path.exists(os.path.join(self.c, "odinguard.dll")))

    def test_not_a_client_and_not_installed(self):
        os.makedirs(self.c)
        self.assertEqual(run("install", self.c, "--release", self.rel)[0], 1)
        self.plain()
        self.assertEqual(run("uninstall", self.c)[0], 1)

    def test_release_must_have_the_loader(self):
        self.plain()
        write(self.rel, "ijl15.dll", INTEL)
        rc, out = run("install", self.c, "--release", self.rel)
        self.assertEqual(rc, 1)
        self.assertIn("not the OdinGuard loader", out)


if __name__ == "__main__":
    unittest.main(verbosity=1)
