#!/usr/bin/env python3
"""
tools/test_fnboot.py - Automated Host Tests for FNBOOT Pre-Boot Manager
Verifies:
  - sdmap.txt parsing & auto-discovery
  - fnboot.ini loading (MAPFILE, COLOR, DRV0..DRV7)
  - Exact binary mailbox staging in TRSDOS.bin:
      * sddvr_map_lba (0xF428) & sddvr_map_len (0xF42C)
      * drv_target_table (0x0E76)
      * dw_offset (0x0F78)
      * sddvr_base_lba (0xF408)
      * DCT0$..DCT7$ (0x0470..0x04BF)
"""

import os
import shutil
import struct
import subprocess
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FNBOOT_HOST = os.path.join(REPO_ROOT, "src", "mos", "fnboot", "obj", "fnboot_host")
TRSDOS_BIN = "/Users/richardlucente/development/git/trs-os-zds/TRSDOS_7/TRSDOS.bin"


class TestFnboot(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(FNBOOT_HOST):
            subprocess.run(["make", "-C", os.path.join(REPO_ROOT, "src", "mos", "fnboot"), "host"], check=True)
        cls.tmpdir = tempfile.mkdtemp(prefix="fnboot_test_")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmpdir, ignore_errors=True)

    def test_01_quick_boot_stages_mailbox_and_drives(self):
        if not os.path.isfile(TRSDOS_BIN):
            self.skipTest("TRSDOS.bin not present")

        # Create test sdmap.txt and fnboot.ini in isolated working directory
        map_path = os.path.join(self.tmpdir, "sdmap.txt")
        map_content = (
            "disks/base/testdisk1.dsk,0x00001F42\n"
            "disks/games/testdisk2.dsk,0x00001BF2\n"
        )
        with open(map_path, "w") as f:
            f.write(map_content)

        ini_path = os.path.join(self.tmpdir, "fnboot.ini")
        with open(ini_path, "w") as f:
            f.write(f"KERNEL={TRSDOS_BIN}\n")
            f.write(f"MAPFILE={map_path}\n")
            f.write("COLOR=3\n")
            f.write("DRV0=DW:0\n")
            f.write("DRV1=SD:disks/base/testdisk1.dsk:RW\n")
            f.write("DRV2=SD:disks/games/testdisk2.dsk:RO\n")
            f.write("DRV3=OFFLINE\n")

        r = subprocess.run(
            [FNBOOT_HOST, "-b"],
            cwd=self.tmpdir,
            capture_output=True,
            text=True,
        )
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("OSboot -3f fnboot.tmp", r.stdout)

        staged_path = os.path.join(self.tmpdir, "fnboot.tmp")
        self.assertTrue(os.path.isfile(staged_path))
        with open(staged_path, "rb") as f:
            k = f.read()
        self.assertEqual(len(k), 66048)

        # Verify sddvr_map_lba (0xF428) and sddvr_map_len (0xF42C)
        map_lba = struct.unpack_from("<I", k, 0xF428)[0]
        map_len = struct.unpack_from("<H", k, 0xF42C)[0]
        self.assertEqual(map_lba, 0x00000EA4)
        self.assertEqual(map_len, len(map_content))

        # Verify drv_target_table (0x0E76): :0=DW(1), :1=SD(0), :2=SD(0)
        self.assertEqual(k[0x0E76 + 0], 1)
        self.assertEqual(k[0x0E76 + 1], 0)
        self.assertEqual(k[0x0E76 + 2], 0)

        # Verify sddvr_base_lba (0xF408)
        self.assertEqual(struct.unpack_from("<I", k, 0xF408 + 0)[0], 0)
        self.assertEqual(struct.unpack_from("<I", k, 0xF408 + 4)[0], 0x00001F42)
        self.assertEqual(struct.unpack_from("<I", k, 0xF408 + 8)[0], 0x00001BF2)

        # Verify DCT1$ (0x047A) and DCT2$ (0x0484) active, Alien flag set, RO/RW bit 7
        dct1 = k[0x047A : 0x047A + 10]
        dct2 = k[0x0484 : 0x0484 + 10]
        dct3 = k[0x048E : 0x048E + 10]
        self.assertEqual(dct1[0], 0xC3)               # JP FDCDVR (active)
        self.assertEqual(dct1[3] & 0x80, 0x00)        # RW
        self.assertEqual(dct1[4] & 0x10, 0x10)        # Alien flag set (preserves :1 across IPL)
        self.assertEqual(dct2[0], 0xC3)               # JP FDCDVR (active)
        self.assertEqual(dct2[3] & 0x80, 0x80)        # RO
        self.assertEqual(dct3[0], 0xC9)               # RET (offline)

    def test_02_pure_local_sd_boot_drive0(self):
        if not os.path.isfile(TRSDOS_BIN):
            self.skipTest("TRSDOS.bin not present")

        sub = tempfile.mkdtemp(dir=self.tmpdir)
        map_path = os.path.join(sub, "custom_map.txt")
        with open(map_path, "w") as f:
            f.write("disks/base/testdisk1.dsk,0x00001F42\n")

        # No fnboot.ini -> host stub has FujiNet offline -> auto-selects first SD image for :0!
        r = subprocess.run(
            [FNBOOT_HOST, "-m", map_path, "-k", TRSDOS_BIN, "-b"],
            cwd=sub,
            capture_output=True,
            text=True,
        )
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(os.path.join(sub, "fnboot.tmp"), "rb") as f:
            k = f.read()

        # Drive :0 should be routed to Local SD (0) with Base LBA 0x00001F42!
        self.assertEqual(k[0x0E76 + 0], 0)
        self.assertEqual(struct.unpack_from("<I", k, 0xF408 + 0)[0], 0x00001F42)


if __name__ == "__main__":
    unittest.main(verbosity=2)
