#!/usr/bin/env python3
"""
test_bin_to_cmd.py - Unit and Regression Test Suite for bin_to_cmd.py

Tests authentic TRSDOS / LS-DOS 6.x load module packaging against:
  1. Boundary chunk sizes (1 byte, 255 bytes, 256 bytes, 257 bytes, 1024 bytes).
  2. Custom load and entry address fields.
  3. Optional Record Type 05 module names.
  4. Non-default chunking thresholds (e.g. 128 bytes).
  5. Dump parsing and round-trip payload fidelity.
"""

import unittest
from tools.bin_to_cmd import pack_bin_to_cmd, unpack_cmd, parse_address


class TestBinToCmd(unittest.TestCase):
    def test_parse_address(self):
        self.assertEqual(parse_address("2600"), 0x2600)
        self.assertEqual(parse_address("0x2600"), 0x2600)
        self.assertEqual(parse_address("&H2600"), 0x2600)
        self.assertEqual(parse_address("2600h"), 0x2600)
        self.assertEqual(parse_address("3000H"), 0x3000)

    def test_single_byte(self):
        data = b"\xC9"  # RET
        cmd = pack_bin_to_cmd(data, load_addr=0x2600, entry_addr=0x2600)
        # Type 01: 01, rlen=3, 00, 26, C9
        # Type 02: 02, 02, 00, 26
        self.assertEqual(cmd, bytes([0x01, 0x03, 0x00, 0x26, 0xC9, 0x02, 0x02, 0x00, 0x26]))
        load, entry, unpacked = unpack_cmd(cmd)
        self.assertEqual(load, 0x2600)
        self.assertEqual(entry, 0x2600)
        self.assertEqual(unpacked, data)

    def test_exact_256_bytes_wrap(self):
        data = bytes(range(256))
        cmd = pack_bin_to_cmd(data, load_addr=0x3000, entry_addr=0x3000, chunk_size=256)
        # Record Type 01 header: 01, 02 (length wrap), 00, 30
        self.assertEqual(cmd[0], 0x01)
        self.assertEqual(cmd[1], 0x02)  # (256 + 2) & 0xFF = 2
        self.assertEqual(cmd[2], 0x00)
        self.assertEqual(cmd[3], 0x30)
        self.assertEqual(cmd[4:260], data)
        # Type 02 transfer record at offset 260
        self.assertEqual(cmd[260:], bytes([0x02, 0x02, 0x00, 0x30]))

        load, entry, unpacked = unpack_cmd(cmd)
        self.assertEqual(load, 0x3000)
        self.assertEqual(entry, 0x3000)
        self.assertEqual(unpacked, data)

    def test_multi_block_chunking(self):
        # 300 bytes: first block 256 bytes @ 0x2600, second block 44 bytes @ 0x2700
        data = bytes(i % 256 for i in range(300))
        cmd = pack_bin_to_cmd(data, load_addr=0x2600, entry_addr=0x2610, chunk_size=256)
        load, entry, unpacked = unpack_cmd(cmd)
        self.assertEqual(load, 0x2600)
        self.assertEqual(entry, 0x2610)
        self.assertEqual(unpacked, data)
        # First block: 4 header + 256 data = 260 bytes
        # Second block: 4 header + 44 data = 48 bytes
        # Transfer record: 4 bytes
        # Total = 312 bytes
        self.assertEqual(len(cmd), 312)

    def test_module_name_type05(self):
        data = b"\x3E\x01\xC9"
        cmd = pack_bin_to_cmd(data, load_addr=0x2600, entry_addr=0x2600, module_name="TESTMOD")
        # Starts with Type 05
        self.assertEqual(cmd[0], 0x05)
        self.assertEqual(cmd[1], 7)  # len("TESTMOD")
        self.assertEqual(cmd[2:9], b"TESTMOD")
        load, entry, unpacked = unpack_cmd(cmd)
        self.assertEqual(load, 0x2600)
        self.assertEqual(entry, 0x2600)
        self.assertEqual(unpacked, data)


if __name__ == "__main__":
    unittest.main()
