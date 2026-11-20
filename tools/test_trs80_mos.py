#!/usr/bin/env python3
"""
tools/test_trs80_mos.py - Automated Parity & Regression Tests for *trs80 MOS Utility
Tests DiskDISK, JV1, JV3, Reed HDV containers and all subcommands:
  dir, copy (extract/inject/clone/-l/-r), delete, rename, format, check, free, dump, attr, map
"""

import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST_BIN = os.path.join(REPO_ROOT, "src", "mos", "trs80", "obj", "trs80_host")
BOOT_DSK = "/Users/richardlucente/development/git/fujinet-firmware/build/dist/SD/boot_volume.dsk"
HELLO_CMD = os.path.join(REPO_ROOT, "src", "hello", "HELLO.CMD")


def run_trs80(*args):
    return subprocess.run([HOST_BIN, *args], capture_output=True, text=True)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


class TestTrs80MosUtility(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(HOST_BIN):
            subprocess.run(["make", "-C", os.path.join(REPO_ROOT, "src", "mos", "trs80"), "host"], check=True)
        cls.tmpdir = tempfile.mkdtemp(prefix="trs80_test_")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmpdir, ignore_errors=True)

    def test_01_dir_and_check_boot_volume(self):
        if not os.path.isfile(BOOT_DSK):
            self.skipTest("boot_volume.dsk not present")

        # Audit boot_volume.dsk (68 active in-use files, zero double allocs/missing GAT/HIT errors)
        r = run_trs80("check", BOOT_DSK)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("Format: DiskDISK", r.stdout)
        self.assertIn("Active files:      68", r.stdout)
        self.assertIn("Double allocs:     0", r.stdout)
        self.assertIn("Missing GAT bits:  0", r.stdout)
        self.assertIn("HIT errors/stale:  0 / 0", r.stdout)
        self.assertIn("RESULT: STRUCTURALLY SOUND", r.stdout)

        # Dir with -a shows all 68 active files including SYS overlays
        r = run_trs80("dir", BOOT_DSK, "-a")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("68 file(s)", r.stdout)
        self.assertIn("SDMOUNT/CMD", r.stdout)
        self.assertIn("BOOT/SYS", r.stdout)

        # Wildcard pattern filter
        r = run_trs80("dir", f"{BOOT_DSK},FN*.CMD")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("FNSTATUS/CMD", r.stdout)
        self.assertNotIn("HELLO/CMD", r.stdout)

        # Free & dump
        r = run_trs80("free", BOOT_DSK)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("DiskDISK", r.stdout)

        r = run_trs80("dump", f"{BOOT_DSK},HELLO/CMD", "-s", "0")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("0000: 01 ", r.stdout)  # LS-DOS /CMD Record Type 01

    def test_02_extract_parity_against_hello_cmd(self):
        if not (os.path.isfile(BOOT_DSK) and os.path.isfile(HELLO_CMD)):
            self.skipTest("boot_volume.dsk or HELLO.CMD not present")

        out_hello = os.path.join(self.tmpdir, "extracted_hello.cmd")
        r = run_trs80("copy", f"{BOOT_DSK},HELLO/CMD", out_hello)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(sha256_file(out_hello), sha256_file(HELLO_CMD))

    def test_03_format_inject_clone_rename_attr_delete_diskdisk(self):
        d1 = os.path.join(self.tmpdir, "work1.dsk")
        d2 = os.path.join(self.tmpdir, "work2.dsk")

        # Format two fresh DiskDISK images
        for path, vol in [(d1, "WORK1"), (d2, "WORK2")]:
            r = run_trs80("format", path, "-f", "diskdisk", "-c", "40", "-s", "2", "-v", vol)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            r = run_trs80("check", path)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("RESULT: STRUCTURALLY SOUND", r.stdout)

        # Create multi-sector binary test payload (12 KB -> spans 2 cylinders / multiple extents!)
        big_src = os.path.join(self.tmpdir, "big.bin")
        big_data = bytes((i * 37 + 13) & 0xFF for i in range(12345))
        with open(big_src, "wb") as f:
            f.write(big_data)

        # Inject into d1
        r = run_trs80("copy", big_src, f"{d1},BIGFILE/BIN")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = run_trs80("check", d1)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

        # Extract back and verify exact byte-for-byte parity
        big_out = os.path.join(self.tmpdir, "big_out.bin")
        r = run_trs80("copy", f"{d1},BIGFILE/BIN", big_out)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(sha256_file(big_src), sha256_file(big_out))

        # Clone d1 -> d2
        r = run_trs80("copy", f"{d1},BIGFILE/BIN", f"{d2},CLONED/BIN")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        clone_out = os.path.join(self.tmpdir, "clone_out.bin")
        r = run_trs80("copy", f"{d2},CLONED/BIN", clone_out)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(sha256_file(big_src), sha256_file(clone_out))

        # Test -l line ending conversion (LF -> CR on inject, CR -> LF on extract)
        txt_src = os.path.join(self.tmpdir, "notes.txt")
        with open(txt_src, "wb") as f:
            f.write(b"LINE ONE\nLINE TWO\r\nLINE THREE\n")
        r = run_trs80("copy", "-l", txt_src, f"{d1},NOTES/TXT")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        txt_raw = os.path.join(self.tmpdir, "notes_raw.txt")
        r = run_trs80("copy", f"{d1},NOTES/TXT", txt_raw)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(txt_raw, "rb") as f:
            self.assertEqual(f.read(), b"LINE ONE\rLINE TWO\rLINE THREE\r")
        txt_lf = os.path.join(self.tmpdir, "notes_lf.txt")
        r = run_trs80("copy", "-l", f"{d1},NOTES/TXT", txt_lf)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(txt_lf, "rb") as f:
            self.assertEqual(f.read(), b"LINE ONE\nLINE TWO\nLINE THREE\n")

        # Rename, attr, delete
        r = run_trs80("rename", f"{d1},NOTES/TXT", "README/DOC")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = run_trs80("attr", f"{d1},README/DOC", "+S", "5")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("SYS=1", r.stdout)
        self.assertIn("PROT=5", r.stdout)

        r = run_trs80("delete", f"{d1},BIGFILE/BIN")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = run_trs80("check", d1)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("Active files:      1", r.stdout)
        self.assertIn("RESULT: STRUCTURALLY SOUND", r.stdout)

    def test_04_jv1_jv3_and_reed_hdv_containers(self):
        # 1. JV1 format + inject + check
        jv1 = os.path.join(self.tmpdir, "floppy.jv1")
        r = run_trs80("format", jv1, "-f", "jv1", "-c", "40", "-s", "1", "-t", "10", "-d", "17", "-v", "JV1TEST")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

        payload_path = os.path.join(self.tmpdir, "msg.txt")
        with open(payload_path, "wb") as f:
            f.write(b"Hello from JV1/JV3/HDV container test!\r")

        r = run_trs80("copy", payload_path, f"{jv1},HELLO/TXT")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = run_trs80("check", jv1)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("Format: JV1", r.stdout)
        self.assertIn("RESULT: STRUCTURALLY SOUND", r.stdout)

        # 2. Construct a synthetic JV3 image with REVERSED sector order from the JV1 image!
        jv3 = os.path.join(self.tmpdir, "floppy.jv3")
        with open(jv1, "rb") as f:
            jv1_bytes = f.read()
        num_sec = len(jv1_bytes) // 256  # 400 sectors (40 cyls * 10 spt)
        hdr = bytearray(b"\xFF" * 8704)
        hdr[0x21FF] = 0x00  # RW
        data_out = bytearray()
        # Store sectors in reverse order to prove JV3 indexed lookup works!
        idx = 0
        for lsn in range(num_sec - 1, -1, -1):
            trk = lsn // 10
            sec = lsn % 10
            hdr[idx * 3 + 0] = trk
            hdr[idx * 3 + 1] = sec
            hdr[idx * 3 + 2] = 0x00  # SD, side 0, 256B
            data_out.extend(jv1_bytes[lsn * 256 : (lsn + 1) * 256])
            idx += 1
        with open(jv3, "wb") as f:
            f.write(hdr)
            f.write(data_out)

        r = run_trs80("check", jv3)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("Format: JV3", r.stdout)
        self.assertIn("Active files:      1", r.stdout)
        self.assertIn("RESULT: STRUCTURALLY SOUND", r.stdout)

        jv3_out = os.path.join(self.tmpdir, "from_jv3.txt")
        r = run_trs80("copy", f"{jv3},HELLO/TXT", jv3_out)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(sha256_file(payload_path), sha256_file(jv3_out))

        # 3. Reed HDV container
        hdv = os.path.join(self.tmpdir, "disk.hdv")
        r = run_trs80("format", hdv, "-f", "hdv", "-c", "60", "-s", "2", "-t", "18", "-d", "1", "-v", "HDVTEST")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = run_trs80("copy", payload_path, f"{hdv},HELLO/TXT")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = run_trs80("check", hdv)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("Format: Reed HDV", r.stdout)
        self.assertIn("RESULT: STRUCTURALLY SOUND", r.stdout)

    def test_05_map_recursive_hierarchy(self):
        tree = os.path.join(self.tmpdir, "maproot")
        os.makedirs(os.path.join(tree, "base"), exist_ok=True)
        os.makedirs(os.path.join(tree, "games", "rpg"), exist_ok=True)
        for rel in ["base/boot.dsk", "games/rpg/quest.dsk", "games/boot.dsk"]:
            with open(os.path.join(tree, rel), "wb") as f:
                f.write(b"\x00" * 512)
        out_map = os.path.join(self.tmpdir, "out_sdmap.txt")
        r = run_trs80("map", tree, "-o", out_map)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(out_map, "r") as f:
            content = f.read()
        self.assertIn("base/boot.dsk,0x", content)
        self.assertIn("games/boot.dsk,0x", content)
        self.assertIn("games/rpg/quest.dsk,0x", content)


if __name__ == "__main__":
    unittest.main(verbosity=2)
