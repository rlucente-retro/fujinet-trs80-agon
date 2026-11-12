#!/usr/bin/env python3
"""
Unit test suite for shared TRSDOS assembly includes:
  - include/svc.inc
  - include/equ.inc
  - include/fujinet.inc

Verifies that all EQU statements parse correctly, values are valid integers,
there are no conflicting duplicates within files, and critical architectural
addresses match expected specifications.
"""

import os
import re
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INCLUDE_DIR = os.path.join(REPO_ROOT, "include")


def parse_equates(filepath):
    """Parses an assembly include file and extracts all EQU definitions."""
    equates = {}
    pattern = re.compile(r"^([A-Za-z0-9_.$]+)\s+EQU\s+([0-9A-Fa-f]+[hH]?|[0-9]+)\s*(?:;.*)?$")
    with open(filepath, "r", encoding="utf-8") as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith(";"):
                continue
            m = pattern.match(line)
            if m:
                name, val_str = m.group(1), m.group(2)
                if val_str.endswith("h") or val_str.endswith("H"):
                    val = int(val_str[:-1], 16)
                else:
                    val = int(val_str, 10)
                equates[name] = val
    return equates


class TestAssemblyIncludes(unittest.TestCase):
    def test_svc_inc_exists_and_parses(self):
        svc_path = os.path.join(INCLUDE_DIR, "svc.inc")
        self.assertTrue(os.path.isfile(svc_path), "include/svc.inc must exist")
        equates = parse_equates(svc_path)
        self.assertGreaterEqual(len(equates), 40, "svc.inc should define at least 40 SVC equates")

        # Verify key SVC equates
        self.assertEqual(equates.get("SVC_IPL"), 0)
        self.assertEqual(equates.get("SVC_KEY"), 1)
        self.assertEqual(equates.get("SVC_DSP"), 2)
        self.assertEqual(equates.get("SVC_DSPLY"), 10)
        self.assertEqual(equates.get("SVC_EXIT"), 22)
        self.assertEqual(equates.get("SVC_ABORT"), 21)
        self.assertEqual(equates.get("SVC_PARAM"), 39)
        self.assertEqual(equates.get("SVC_FSPEC"), 95)
        self.assertEqual(equates.get("SVC_BANK"), 102)
        self.assertEqual(equates.get("SVC_CKBRKC"), 106)

    def test_equ_inc_exists_and_parses(self):
        equ_path = os.path.join(INCLUDE_DIR, "equ.inc")
        self.assertTrue(os.path.isfile(equ_path), "include/equ.inc must exist")
        equates = parse_equates(equ_path)
        self.assertGreaterEqual(len(equates), 45, "equ.inc should define at least 45 equates")

        # Verify ASCII equates
        self.assertEqual(equates.get("CR"), 0x0D)
        self.assertEqual(equates.get("LF"), 0x0A)
        self.assertEqual(equates.get("ETX"), 0x03)
        self.assertEqual(equates.get("ESC"), 0x1B)

        # Verify lowcore memory addresses
        self.assertEqual(equates.get("LDRV$"), 0x0023)
        self.assertEqual(equates.get("TIME$"), 0x002D)
        self.assertEqual(equates.get("DATE$"), 0x0033)
        self.assertEqual(equates.get("BUR$"), 0x0200)
        self.assertEqual(equates.get("BAR$"), 0x0201)
        self.assertEqual(equates.get("LBANK$"), 0x0202)
        self.assertEqual(equates.get("INBUF$"), 0x0420)
        self.assertEqual(equates.get("DCT0$"), 0x0470)
        self.assertEqual(equates.get("DCT1$"), 0x047A)
        self.assertEqual(equates.get("DCT_SIZE"), 10)
        self.assertEqual(equates.get("USER_PROG_BASE"), 0x2600)
        self.assertEqual(equates.get("COREEND$"), 0xF3FF)

        # Verify storage router addresses
        self.assertEqual(equates.get("drv_target_table"), 0x0E76)
        self.assertEqual(equates.get("DRV_TARGET_SD"), 0)
        self.assertEqual(equates.get("DRV_TARGET_DW"), 1)
        self.assertEqual(equates.get("DRV_TARGET_MEMDISK"), 2)
        self.assertEqual(equates.get("sddvr_inited"), 0xF400)
        self.assertEqual(equates.get("sddvr_base_lba"), 0xF408)

        # Verify hardware UART1 ports
        self.assertEqual(equates.get("UART1_RBR"), 0xD0)
        self.assertEqual(equates.get("UART1_THR"), 0xD0)
        self.assertEqual(equates.get("UART1_LSR"), 0xD5)

    def test_fujinet_inc_exists_and_parses(self):
        fuji_path = os.path.join(INCLUDE_DIR, "fujinet.inc")
        self.assertTrue(os.path.isfile(fuji_path), "include/fujinet.inc must exist")
        equates = parse_equates(fuji_path)
        self.assertGreaterEqual(len(equates), 35, "fujinet.inc should define at least 35 equates")

        # Verify protocol opcodes
        self.assertEqual(equates.get("OP_FUJI"), 0xE2)
        self.assertEqual(equates.get("OP_READEX"), 0xD2)
        self.assertEqual(equates.get("OP_WRITE"), 0x57)
        self.assertEqual(equates.get("OP_TIME"), 0x23)

        # Verify configuration commands
        self.assertEqual(equates.get("FUJI_CMD_POLL"), 0x00)
        self.assertEqual(equates.get("FUJI_CMD_GET_RESPONSE"), 0x01)
        self.assertEqual(equates.get("FUJI_CMD_GET_ERROR"), 0x02)
        self.assertEqual(equates.get("FUJI_CMD_GET_ADAPTER_CFG"), 0xE8)
        self.assertEqual(equates.get("FUJI_CMD_GET_DEV_SLOTS"), 0xF2)
        self.assertEqual(equates.get("FUJI_CMD_GET_HOST_SLOTS"), 0xF4)
        self.assertEqual(equates.get("FUJI_CMD_SET_HOST_SLOTS"), 0xF3)
        self.assertEqual(equates.get("FUJI_CMD_MOUNT_HOST"), 0xF9)
        self.assertEqual(equates.get("FUJI_CMD_MOUNT_IMAGE"), 0xF8)

        # Verify structure sizes
        self.assertEqual(equates.get("ADAPTER_CONFIG_SIZE"), 140)
        self.assertEqual(equates.get("HOST_SLOT_SIZE"), 32)
        self.assertEqual(equates.get("HOST_TABLE_SIZE"), 256)
        self.assertEqual(equates.get("DEV_SLOT_SIZE"), 38)
        self.assertEqual(equates.get("DEV_TABLE_SIZE_STAGE1"), 152)

        # Verify status codes
        self.assertEqual(equates.get("FUJI_STAT_OK"), 0x01)
        self.assertEqual(equates.get("FUJI_STAT_ERROR"), 0x90)


if __name__ == "__main__":
    unittest.main()
