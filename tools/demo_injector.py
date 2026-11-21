#!/usr/bin/env python3
"""
tools/demo_injector.py - macOS Hardware-Keycode Demo Auto-Typer for fab-agon-emulator

Sends physical macOS virtual keycodes (kVK_ANSI_*) via CoreGraphics CGEvent
directly through the HID event tap so fab-agon-emulator's SDL3 -> PS/2 scancode
translator receives exact make/break sequences (including Shift, digits, colons,
quotes, slashes, parentheses, Enter, Esc, and Ctrl-C) with zero dropped keys.

Also acts as a live terminal teleprompter showing the current chapter, talking
point, command being typed, and a 1-second countdown between commands for smooth
screen recording.

Usage examples:
  python3 tools/demo_injector.py --list
  python3 tools/demo_injector.py --dry-run
  python3 tools/demo_injector.py --pause 10
  python3 tools/demo_injector.py --step
  python3 tools/demo_injector.py --start 1 --end 5
  python3 tools/demo_injector.py --test-keys 'trs80 dir /disks/base/testdisk1.dsk'
"""

import argparse
import ctypes
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# macOS CoreGraphics / ApplicationServices ctypes bindings (zero dependencies)
# ---------------------------------------------------------------------------
kCGHIDEventTap = 0
kCGEventFlagsChanged = 12
kCGEventFlagMaskShift = 0x00020000
kCGEventFlagMaskControl = 0x00040000
NX_DEVICELSHIFTKEYMASK = 0x00000002
NX_DEVICELCTLKEYMASK = 0x00000001

kVK_Return = 0x24
kVK_Tab = 0x30
kVK_Space = 0x31
kVK_Delete = 0x33      # Backspace
kVK_Escape = 0x35
kVK_Shift = 0x38       # Left Shift
kVK_Control = 0x3B     # Left Control

# US ANSI physical keycode map: char -> (virtual_keycode, needs_shift)
CHAR_TO_KEY: Dict[str, Tuple[int, bool]] = {
    # Letters (lowercase)
    "a": (0x00, False), "b": (0x0B, False), "c": (0x08, False), "d": (0x02, False),
    "e": (0x0E, False), "f": (0x03, False), "g": (0x05, False), "h": (0x04, False),
    "i": (0x22, False), "j": (0x26, False), "k": (0x28, False), "l": (0x25, False),
    "m": (0x2E, False), "n": (0x2D, False), "o": (0x1F, False), "p": (0x23, False),
    "q": (0x0C, False), "r": (0x0F, False), "s": (0x01, False), "t": (0x11, False),
    "u": (0x20, False), "v": (0x09, False), "w": (0x0D, False), "x": (0x07, False),
    "y": (0x10, False), "z": (0x06, False),
    # Letters (uppercase)
    "A": (0x00, True),  "B": (0x0B, True),  "C": (0x08, True),  "D": (0x02, True),
    "E": (0x0E, True),  "F": (0x03, True),  "G": (0x05, True),  "H": (0x04, True),
    "I": (0x22, True),  "J": (0x26, True),  "K": (0x28, True),  "L": (0x25, True),
    "M": (0x2E, True),  "N": (0x2D, True),  "O": (0x1F, True),  "P": (0x23, True),
    "Q": (0x0C, True),  "R": (0x0F, True),  "S": (0x01, True),  "T": (0x11, True),
    "U": (0x20, True),  "V": (0x09, True),  "W": (0x0D, True),  "X": (0x07, True),
    "Y": (0x10, True),  "Z": (0x06, True),
    # Digits
    "0": (0x1D, False), "1": (0x12, False), "2": (0x13, False), "3": (0x14, False),
    "4": (0x15, False), "5": (0x17, False), "6": (0x16, False), "7": (0x1A, False),
    "8": (0x1C, False), "9": (0x19, False),
    # Shifted digits
    ")": (0x1D, True),  "!": (0x12, True),  "@": (0x13, True),  "#": (0x14, True),
    "$": (0x15, True),  "%": (0x17, True),  "^": (0x16, True),  "&": (0x1A, True),
    "*": (0x1C, True),  "(": (0x19, True),
    # Punctuation & whitespace
    " ": (kVK_Space, False),
    "-": (0x1B, False), "_": (0x1B, True),
    "=": (0x18, False), "+": (0x18, True),
    "[": (0x21, False), "{": (0x21, True),
    "]": (0x1E, False), "}": (0x1E, True),
    "\\": (0x2A, False), "|": (0x2A, True),
    ";": (0x29, False), ":": (0x29, True),
    "'": (0x27, False), '"': (0x27, True),
    ",": (0x2B, False), "<": (0x2B, True),
    ".": (0x2F, False), ">": (0x2F, True),
    "/": (0x2C, False), "?": (0x2C, True),
    "`": (0x32, False), "~": (0x32, True),
}

SPECIAL_TOKENS: Dict[str, Tuple[int, int]] = {
    "ENTER":  (kVK_Return, 0),
    "TAB":    (kVK_Tab, 0),
    "BS":     (kVK_Delete, 0),
    "ESC":    (kVK_Escape, 0),
    "CTRL+C": (0x08, kCGEventFlagMaskControl),
}


class MacKeyInjector:
    """Posts physical HID keyboard events via macOS CoreGraphics."""

    def __init__(self, hold_s: float = 0.04, mod_s: float = 0.03, char_s: float = 0.05):
        self.hold_s = hold_s
        self.mod_s = mod_s
        self.char_s = char_s
        self._cg = ctypes.cdll.LoadLibrary(
            "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics"
        )
        self._app = ctypes.cdll.LoadLibrary(
            "/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices"
        )
        self._cf = ctypes.cdll.LoadLibrary(
            "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
        )

        self._app.AXIsProcessTrusted.restype = ctypes.c_bool

        self._cg.CGEventCreateKeyboardEvent.argtypes = [
            ctypes.c_void_p, ctypes.c_uint16, ctypes.c_bool
        ]
        self._cg.CGEventCreateKeyboardEvent.restype = ctypes.c_void_p

        self._cg.CGEventSetFlags.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        self._cg.CGEventSetFlags.restype = None

        self._cg.CGEventSetType.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        self._cg.CGEventSetType.restype = None

        self._cg.CGEventPost.argtypes = [ctypes.c_uint32, ctypes.c_void_p]
        self._cg.CGEventPost.restype = None

        self._cf.CFRelease.argtypes = [ctypes.c_void_p]
        self._cf.CFRelease.restype = None

    def is_trusted(self) -> bool:
        return bool(self._app.AXIsProcessTrusted())

    def _post(self, keycode: int, key_down: bool, flags: int = 0, flags_changed: bool = False) -> None:
        ev = self._cg.CGEventCreateKeyboardEvent(None, keycode, key_down)
        if not ev:
            raise RuntimeError(f"CGEventCreateKeyboardEvent failed for keycode 0x{keycode:02X}")
        try:
            if flags_changed:
                self._cg.CGEventSetType(ev, kCGEventFlagsChanged)
            self._cg.CGEventSetFlags(ev, flags)
            self._cg.CGEventPost(kCGHIDEventTap, ev)
        finally:
            self._cf.CFRelease(ev)

    def tap_key(self, keycode: int, flags: int = 0) -> None:
        mod_key: Optional[int] = None
        mod_flags = flags
        if flags & kCGEventFlagMaskShift:
            mod_key = kVK_Shift
            mod_flags = kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK
        elif flags & kCGEventFlagMaskControl:
            mod_key = kVK_Control
            mod_flags = kCGEventFlagMaskControl | NX_DEVICELCTLKEYMASK

        if mod_key is not None:
            # macOS Cocoa/SDL3 requires kCGEventFlagsChanged with device-side mask
            # for physical LShift/LCtrl so SDL3 emits Scancode::LShift / LCtrl -> PS/2.
            self._post(mod_key, True, mod_flags, flags_changed=True)
            time.sleep(self.mod_s)

        self._post(keycode, True, mod_flags)
        time.sleep(self.hold_s)
        self._post(keycode, False, mod_flags)
        time.sleep(self.mod_s)

        if mod_key is not None:
            self._post(mod_key, False, 0, flags_changed=True)
            time.sleep(self.mod_s)

    def send_sequence(self, seq: str, dry_run: bool = False) -> None:
        """
        Send a command sequence supporting special tokens:
          {ENTER}, {ESC}, {TAB}, {BS}, {CTRL+C}, {WAIT:1.5}
        """
        i = 0
        n = len(seq)
        while i < n:
            if seq[i] == "{":
                end = seq.find("}", i + 1)
                if end != -1:
                    token = seq[i + 1 : end]
                    if token.startswith("WAIT:"):
                        secs = float(token.split(":", 1)[1])
                        if not dry_run:
                            time.sleep(secs)
                        i = end + 1
                        continue
                    if token in SPECIAL_TOKENS:
                        kc, flg = SPECIAL_TOKENS[token]
                        if not dry_run:
                            self.tap_key(kc, flg)
                            time.sleep(self.char_s)
                        i = end + 1
                        continue
            ch = seq[i]
            if ch not in CHAR_TO_KEY:
                raise ValueError(f"Unsupported character {ch!r} in sequence {seq!r}")
            kc, shift = CHAR_TO_KEY[ch]
            if not dry_run:
                self.tap_key(kc, kCGEventFlagMaskShift if shift else 0)
                time.sleep(self.char_s)
            i += 1


def focus_emulator(process_hint: str = "fab-agon-emulator") -> bool:
    """Bring the fab-agon-emulator window to the foreground on macOS."""
    script = f'''
    tell application "System Events"
        set procs to (every process whose name contains "{process_hint}")
        if (count of procs) is 0 then
            return "NOT_FOUND"
        end if
        set frontmost of item 1 of procs to true
        return name of item 1 of procs
    end tell
    '''
    r = subprocess.run(["osascript", "-e", script], capture_output=True, text=True)
    out = r.stdout.strip()
    if r.returncode != 0 or out == "NOT_FOUND" or not out:
        return False
    time.sleep(0.35)
    return True


# ---------------------------------------------------------------------------
# Full Phase 1-4 Capabilities Demo Script (synced with DEMO-RECORDING-SCRIPT.md)
# ---------------------------------------------------------------------------
@dataclass
class DemoStep:
    section: str
    title: str
    display_cmd: str
    keys: str
    cue: str
    pause_override: Optional[float] = None


DEMO_STEPS: List[DemoStep] = [
    # --- Part 1: Quark / Console8 MOS Pre-Boot Disk Suite (*trs80) ---
    DemoStep(
        section="Part 1: MOS *trs80 Suite",
        title="Inspect local DiskDISK volume directory",
        display_cmd="trs80 dir /disks/base/testdisk1.dsk",
        keys="trs80 dir /disks/base/testdisk1.dsk{ENTER}",
        cue="Point out native FAT32 -> LS-DOS 6.3 directory listing (SDTEST1 with HELLO/CMD).",
    ),
    DemoStep(
        section="Part 1: MOS *trs80 Suite",
        title="Check geometry and free space",
        display_cmd="trs80 free /disks/base/testdisk1.dsk",
        keys="trs80 free /disks/base/testdisk1.dsk{ENTER}",
        cue="Show 40x2x18 DiskDISK geometry, allocated vs free granules, and free KiB.",
    ),
    DemoStep(
        section="Part 1: MOS *trs80 Suite",
        title="Run structural GAT/HIT/extent filesystem audit",
        display_cmd="trs80 check /disks/base/testdisk1.dsk",
        keys="trs80 check /disks/base/testdisk1.dsk{ENTER}",
        cue="Highlight 0-error structural audit (GAT bitmap, HIT hashes, and directory extents).",
    ),
    DemoStep(
        section="Part 1: MOS *trs80 Suite",
        title="Recursively map /disks and resolve FAT32 LBAs",
        display_cmd="trs80 map /disks -o /disks/demo_map.txt",
        keys="trs80 map /disks -o /disks/demo_map.txt{ENTER}",
        cue="Show recursive FatFs scan resolving 32-bit physical SD LBAs and contiguity.",
    ),

    # --- Part 2: FNBOOT Visual Pre-Boot Manager & Mailbox Staging ---
    DemoStep(
        section="Part 2: FNBOOT Pre-Boot Manager",
        title="Launch FNBOOT visual storage manager",
        display_cmd="fnboot",
        keys="fnboot{ENTER}",
        cue="Point out FujiNet ONLINE status (IP/Host/FW), auto-discovered /disks/sdmap.txt, and Drive :0 on DriveWire.",
    ),
    DemoStep(
        section="Part 2: FNBOOT Pre-Boot Manager",
        title="Assign Drive :1 -> Local MicroSD testdisk1.dsk (RW)",
        display_cmd="[1] -> [S] -> 2 {ENTER}",
        keys="1{WAIT:1.0}S{WAIT:1.0}2{ENTER}",
        cue="Drive :1 is now assigned to Local SD disks/base/testdisk1.dsk in Read/Write mode.",
    ),
    DemoStep(
        section="Part 2: FNBOOT Pre-Boot Manager",
        title="Assign Drive :2 -> Local MicroSD testdisk2.dsk (RO)",
        display_cmd="[2] -> [S] -> 3 {ENTER} -> [2] -> [T]",
        keys="2{WAIT:1.0}S{WAIT:1.0}3{ENTER}{WAIT:1.2}2{WAIT:1.0}T",
        cue="Drive :2 is assigned to Local SD disks/games/testdisk2.dsk and toggled to Read-Only (RO).",
    ),
    DemoStep(
        section="Part 2: FNBOOT Pre-Boot Manager",
        title="Save pre-boot profile to /fnboot.ini",
        display_cmd="[S]",
        keys="S",
        cue="Point out status bar confirming configuration saved to /fnboot.ini for instant fnboot -b quick boots.",
    ),
    DemoStep(
        section="Part 2: FNBOOT Pre-Boot Manager",
        title="Stage TRSDOS lowcore mailbox & boot LS-DOS 6.3",
        display_cmd="[B]",
        keys="B",
        cue="FNBOOT patches /fnboot.tmp and launches OSboot; LS-DOS 6.3 boots over FujiNet DriveWire :0.",
        pause_override=12.0,
    ),

    # --- Part 3: LS-DOS 6.3 FujiNet Network Utilities (OP_FUJI) ---
    DemoStep(
        section="Part 3: LS-DOS FujiNet Utilities",
        title="Show unified multi-tier drive map",
        display_cmd="DRVMAP",
        keys="DRVMAP{ENTER}",
        cue="Point out :0 on FujiNet DriveWire and :1 (RW) / :2 (RO) already pre-mounted on Local SD via FNBOOT mailbox.",
    ),
    DemoStep(
        section="Part 3: LS-DOS FujiNet Utilities",
        title="Query FujiNet adapter status over UART1 (OP_FUJI 0xE8)",
        display_cmd="FNSTATUS",
        keys="FNSTATUS{ENTER}",
        cue="Point out firmware version, hostname, SSID, IP, gateway, netmask, DNS, and MAC queried live over UART1.",
    ),
    DemoStep(
        section="Part 3: LS-DOS FujiNet Utilities",
        title="List configured FujiNet host slots (OP_FUJI 0xF4)",
        display_cmd="FNHOSTS",
        keys="FNHOSTS{ENTER}",
        cue="Displays all 8 FujiNet host slots (SD + TNFS servers).",
    ),
    DemoStep(
        section="Part 3: LS-DOS FujiNet Utilities",
        title="List FujiNet virtual device slots (OP_FUJI 0xF2)",
        display_cmd="FNDEVS",
        keys="FNDEVS{ENTER}",
        cue="Displays active Stage 1 virtual device slots (0-3) and mounted disk image paths.",
    ),
    DemoStep(
        section="Part 3: LS-DOS FujiNet Utilities",
        title="Directory listing of network boot volume (:0)",
        display_cmd="DIR :0",
        keys="DIR :0{ENTER}",
        cue="Reads Track 17 directory from FujiNet DriveWire Slot 0 (boot_volume.dsk).",
    ),

    # --- Part 4: Dynamic Local MicroSD Mounting (SDMOUNT) ---
    DemoStep(
        section="Part 4: Local MicroSD Hot-Mount",
        title="Hot-mount Local SD image to Drive :3 by path (no SETMAP needed)",
        display_cmd="SDMOUNT :3 (PATH=games/testdisk2.dsk)",
        keys="SDMOUNT :3 (PATH=games/testdisk2.dsk){ENTER}",
        cue="SDMOUNT reads /disks/sdmap.txt via SPI0 using FNBOOT's mailbox LBA and synchronizes DCT3 geometry.",
    ),
    DemoStep(
        section="Part 4: Local MicroSD Hot-Mount",
        title="Verify newly mounted Drive :3 via DIR and DRVMAP",
        display_cmd="DIR :3",
        keys="DIR :3{ENTER}",
        cue="Lists SDTEST2 directly from Local MicroSD via hardware SPI0.",
    ),

    # --- Part 5: Hybrid Cross-Tier Read/Write (UART1 -> SPI0 CMD24 RMW) ---
    DemoStep(
        section="Part 5: Hybrid Cross-Tier Copy",
        title="Copy FNSTATUS/CMD from FujiNet (:0) to Local MicroSD (:1)",
        display_cmd="COPY FNSTATUS/CMD:0 TO :1",
        keys="COPY FNSTATUS/CMD:0 TO :1{ENTER}",
        cue="Streams sectors over UART1 from FujiNet and commits 512B Read-Modify-Write blocks via SPI0 CMD24!",
        pause_override=12.0,
    ),
    DemoStep(
        section="Part 5: Hybrid Cross-Tier Copy",
        title="Verify FNSTATUS/CMD on Local MicroSD (:1)",
        display_cmd="DIR :1",
        keys="DIR :1{ENTER}",
        cue="FNSTATUS/CMD now appears alongside HELLO/CMD on Local SD volume SDTEST1.",
    ),
    DemoStep(
        section="Part 5: Hybrid Cross-Tier Copy",
        title="Execute copied FNSTATUS/CMD directly from Local SD (:1)",
        display_cmd="FNSTATUS:1",
        keys="FNSTATUS:1{ENTER}",
        cue="Authentic .CMD executable loads from Local MicroSD via SPI0 and queries FujiNet over UART1.",
    ),

    # --- Part 6: BASIC Local SD Save/Load + Write-Protect Enforcement ---
    DemoStep(
        section="Part 6: BASIC & Write Protection",
        title="Launch Model 4 Disk BASIC",
        display_cmd="BASIC",
        keys="BASIC{ENTER}",
        cue="Loads authentic LS-DOS 6.3 Disk BASIC from DriveWire :0.",
    ),
    DemoStep(
        section="Part 6: BASIC & Write Protection",
        title="Enter a BASIC program and save it to Local MicroSD (:1)",
        display_cmd='10 PRINT "FUJINET + AGON SD WRITE OK!" / SAVE "SDTEST/BAS:1"',
        keys='10 PRINT "FUJINET + AGON SD WRITE OK!"{ENTER}{WAIT:1.0}SAVE "SDTEST/BAS:1"{ENTER}',
        cue="BASIC writes SDTEST/BAS directly to Local MicroSD :1 via SPI0 CMD24 RMW.",
    ),
    DemoStep(
        section="Part 6: BASIC & Write Protection",
        title="Clear RAM, reload from Local SD (:1), and RUN",
        display_cmd='NEW / LOAD "SDTEST/BAS:1" / RUN',
        keys='NEW{ENTER}{WAIT:0.8}LOAD "SDTEST/BAS:1"{ENTER}{WAIT:1.5}RUN{ENTER}',
        cue="Confirms clean round-trip persistence on Local MicroSD :1.",
    ),
    DemoStep(
        section="Part 6: BASIC & Write Protection",
        title="Verify hardware Write-Protect enforcement on RO Drive :2",
        display_cmd='SAVE "BLOCKED/BAS:2"',
        keys='SAVE "BLOCKED/BAS:2"{ENTER}',
        cue="DCT write-protect bit blocks writes with 'Disk write protected' (Error 15) and zero SPI0 writes.",
    ),
    DemoStep(
        section="Part 6: BASIC & Write Protection",
        title="Return cleanly from BASIC to LS-DOS Ready",
        display_cmd="SYSTEM",
        keys="SYSTEM{ENTER}",
        cue="Returns to LS-DOS 6.3 Ready prompt. Demo complete! (Optional: reboot to MOS & run trs80 check.)",
        pause_override=2.0,
    ),
]


def countdown(seconds: float) -> None:
    whole = int(seconds)
    frac = seconds - whole
    for rem in range(whole, 0, -1):
        sys.stdout.write(f"\r   Next step in {rem:2d}s... (Ctrl-C to stop) ")
        sys.stdout.flush()
        time.sleep(1.0)
    if frac > 0.01:
        time.sleep(frac)
    sys.stdout.write("\r" + " " * 46 + "\r")
    sys.stdout.flush()


def main() -> int:
    ap = argparse.ArgumentParser(
        description="macOS hardware-keycode demo auto-typer for fab-agon-emulator"
    )
    ap.add_argument("--pause", type=float, default=10.0,
                    help="Default pause in seconds between commands (default: 10)")
    ap.add_argument("--char-delay", type=float, default=0.05,
                    help="Delay between keystrokes in seconds (default: 0.05)")
    ap.add_argument("--start", type=int, default=1,
                    help="First step number to execute (1-based, default: 1)")
    ap.add_argument("--end", type=int, default=len(DEMO_STEPS),
                    help=f"Last step number to execute (1-based, default: {len(DEMO_STEPS)})")
    ap.add_argument("--step", action="store_true",
                    help="Manual step mode: wait for Enter in terminal before each command")
    ap.add_argument("--list", action="store_true",
                    help="List all numbered demo steps and exit")
    ap.add_argument("--dry-run", action="store_true",
                    help="Validate all keycodes and preview script without typing")
    ap.add_argument("--test-keys", type=str, default=None,
                    help="Send a single test command + Enter to fab-agon-emulator and exit")
    ap.add_argument("--process", type=str, default="fab-agon-emulator",
                    help="Process name substring to focus (default: fab-agon-emulator)")
    args = ap.parse_args()

    if args.list:
        cur_sec = None
        for idx, st in enumerate(DEMO_STEPS, 1):
            if st.section != cur_sec:
                cur_sec = st.section
                print(f"\n=== {cur_sec} ===")
            print(f"  {idx:2d}. {st.title}")
            print(f"      CMD: {st.display_cmd}")
        return 0

    injector = MacKeyInjector(char_s=args.char_delay)

    # Validate all sequences up front
    for idx, st in enumerate(DEMO_STEPS, 1):
        injector.send_sequence(st.keys, dry_run=True)

    if args.dry_run:
        print(f"Dry run OK: validated all {len(DEMO_STEPS)} steps and hardware keycode mappings.")
        for idx, st in enumerate(DEMO_STEPS, 1):
            if args.start <= idx <= args.end:
                wait_s = st.pause_override if st.pause_override is not None else args.pause
                print(f"[{idx:2d}/{len(DEMO_STEPS)}] {st.section} | {st.title}")
                print(f"   Keys : {st.keys}")
                print(f"   Cue  : {st.cue}")
                print(f"   Pause: {wait_s:.1f}s")
        return 0

    if not injector.is_trusted():
        print("ERROR: macOS Accessibility permission is not granted for this terminal.")
        print("Enable it in: System Settings -> Privacy & Security -> Accessibility")
        return 1

    if args.test_keys is not None:
        injector.send_sequence(args.test_keys + "{ENTER}", dry_run=True)
        print(f"Focusing '{args.process}' and sending test command: {args.test_keys}")
        if not focus_emulator(args.process):
            print(f"ERROR: Could not find running process matching '{args.process}'.")
            return 1
        injector.send_sequence(args.test_keys + "{ENTER}")
        print("Done.")
        return 0

    s_idx = max(1, args.start)
    e_idx = min(len(DEMO_STEPS), args.end)
    total = len(DEMO_STEPS)

    print("========================================================================")
    print("  FujiNet + TRS-80 on Agon Light 2 — Automated Screen-Recording Driver")
    print("========================================================================")
    print(f"  Steps      : {s_idx}..{e_idx} of {total}")
    print(f"  Mode       : {'Manual step (Enter)' if args.step else f'Auto ({args.pause:.0f}s pause between steps)'}")
    print(f"  Target App : {args.process}")
    print("  Tip        : Start your screen recording now, make sure the emulator is")
    print("               at the Quark/Console8 MOS '*' prompt, then press Enter.")
    print("========================================================================")
    try:
        input("\nPress Enter to begin (or Ctrl-C to abort)... ")
    except KeyboardInterrupt:
        print("\nAborted.")
        return 0

    print("\nStarting in 3 seconds — focusing emulator window...")
    time.sleep(3.0)

    try:
        for idx in range(s_idx, e_idx + 1):
            st = DEMO_STEPS[idx - 1]
            print(f"\n[{idx:2d}/{total}] {st.section} — {st.title}")
            print(f"   CMD : {st.display_cmd}")
            print(f"   CUE : {st.cue}")

            if args.step:
                input("   Press Enter to send this command... ")

            if not focus_emulator(args.process):
                print(f"   WARNING: '{args.process}' not found! Waiting for it...")
                input("   Focus the emulator window and press Enter to retry... ")
                focus_emulator(args.process)

            injector.send_sequence(st.keys)

            if idx < e_idx and not args.step:
                wait_s = st.pause_override if st.pause_override is not None else args.pause
                countdown(wait_s)
    except KeyboardInterrupt:
        print("\n\nStopped cleanly by user.")
        return 0

    print("\n========================================================================")
    print("  Demo sequence complete!")
    print("========================================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
