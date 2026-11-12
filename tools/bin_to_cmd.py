#!/usr/bin/env python3
"""
bin_to_cmd.py - Authentic TRSDOS / LS-DOS 6.x Load Module (.CMD) Packager

Converts flat binary files (assembled via ZDS II or AgonDev) into authentic
TRS-80 Model 4 (TRSDOS 6.3.1 / LS-DOS 6.3) Load Module Format (.CMD) files.

Load Module Record Structure:
  Type 01: Load Block
    Byte 0: 0x01
    Byte 1: Length byte L = (N + 2) & 0xFF (where N is data bytes, 1..256)
            When N = 256, L = 0x02 (due to 8-bit wrap; loader DJNZ loops 256 times)
    Byte 2: Load address LSB (addr & 0xFF)
    Byte 3: Load address MSB ((addr >> 8) & 0xFF)
    Bytes 4..4+N-1: N payload data bytes

  Type 02: Transfer Address (Execution Entry Point)
    Byte 0: 0x02
    Byte 1: Length = 0x02
    Byte 2: Entry address LSB (entry & 0xFF)
    Byte 3: Entry address MSB ((entry >> 8) & 0xFF)

  Type 05: Module Header / Comment (Optional)
    Byte 0: 0x05
    Byte 1: Length = len(name)
    Bytes 2..: ASCII module name characters (skipped by standard loader)

Usage:
  python3 bin_to_cmd.py [options] <input.bin> [-o <output.cmd>]
  python3 bin_to_cmd.py --dump <file.cmd>
"""

import argparse
import os
import sys


def parse_address(addr_str: str) -> int:
    """Parse address in hex (with or without 0x prefix, or trailing 'h') or decimal."""
    s = addr_str.strip().lower()
    if s.endswith("h"):
        return int(s[:-1], 16)
    if s.startswith("0x") or s.startswith("&h"):
        return int(s.replace("&h", "").replace("0x", ""), 16)
    try:
        # Try hex first if it contains a-f
        if any(c in "abcdef" for c in s):
            return int(s, 16)
        # If purely numeric, check if hex was intended (standard in TRS-80 docs)
        # If user provides 2600, in TRS-80 contexts this is hex 0x2600
        return int(s, 16)
    except ValueError:
        return int(s, 10)


def pack_bin_to_cmd(
    bin_data: bytes,
    load_addr: int = 0x2600,
    entry_addr: int = 0x2600,
    chunk_size: int = 256,
    module_name: str = None,
    verbose: bool = False,
) -> bytes:
    """Pack flat binary bytes into TRSDOS / LS-DOS Load Module Format (.CMD)."""
    if chunk_size < 1 or chunk_size > 256:
        raise ValueError("Chunk size must be between 1 and 256 bytes")
    if load_addr < 0 or load_addr > 0xFFFF:
        raise ValueError(f"Load address 0x{load_addr:04X} out of 16-bit range")
    if entry_addr < 0 or entry_addr > 0xFFFF:
        raise ValueError(f"Entry address 0x{entry_addr:04X} out of 16-bit range")

    cmd = bytearray()

    # Optional Record Type 05: Module Name Header
    if module_name:
        name_bytes = module_name.encode("ascii", errors="replace")[:31]
        cmd.append(0x05)
        cmd.append(len(name_bytes))
        cmd.extend(name_bytes)
        if verbose:
            print(f"[+] Record Type 05 (Module Name): '{module_name}' ({len(name_bytes)} bytes)")

    # Record Type 01: Chunked Load Blocks
    total_len = len(bin_data)
    offset = 0
    curr_addr = load_addr
    block_num = 0

    while offset < total_len:
        chunk = bin_data[offset : offset + chunk_size]
        n = len(chunk)
        # LS-DOS convention: length byte L = (N + 2) & 0xFF
        # When N == 256, (256 + 2) & 0xFF = 2
        rlen = (n + 2) & 0xFF

        cmd.append(0x01)  # Type 01: Load Block
        cmd.append(rlen)  # Length byte
        cmd.append(curr_addr & 0xFF)         # Addr LSB
        cmd.append((curr_addr >> 8) & 0xFF)  # Addr MSB
        cmd.extend(chunk)

        if verbose:
            print(
                f"[+] Record Type 01 [Block {block_num:2d}]: Addr=0x{curr_addr:04X}, "
                f"DataLen={n:3d} B, RecLen=0x{rlen:02X} ({rlen})"
            )

        offset += n
        curr_addr = (curr_addr + n) & 0xFFFF
        block_num += 1

    # Record Type 02: Execution Transfer Address
    cmd.append(0x02)  # Type 02: Transfer Address
    cmd.append(0x02)  # Length = 2 bytes
    cmd.append(entry_addr & 0xFF)         # Entry LSB
    cmd.append((entry_addr >> 8) & 0xFF)  # Entry MSB

    if verbose:
        print(f"[+] Record Type 02 (Transfer Addr): Entry=0x{entry_addr:04X}")

    return bytes(cmd)


def dump_cmd(cmd_data: bytes) -> None:
    """Disassemble and display records in an LS-DOS Load Module (.CMD)."""
    print(f"=== LS-DOS LOAD MODULE DUMP ({len(cmd_data)} bytes) ===")
    pos = 0
    rec_idx = 0
    total_payload = 0

    while pos < len(cmd_data):
        rtype = cmd_data[pos]
        if pos + 1 >= len(cmd_data):
            print(f"[{pos:04X}] Truncated record type 0x{rtype:02X}")
            break
        rlen = cmd_data[pos + 1]

        if rtype == 0x01:
            if pos + 3 >= len(cmd_data):
                print(f"[{pos:04X}] Truncated Type 01 record header")
                break
            addr = cmd_data[pos + 2] | (cmd_data[pos + 3] << 8)
            dlen = 256 if rlen == 2 else (rlen - 2 if rlen >= 2 else 0)
            if pos + 4 + dlen > len(cmd_data):
                print(f"[{pos:04X}] Type 01: payload overflow (dlen={dlen}, remaining={len(cmd_data)-pos-4})")
                dlen = max(0, len(cmd_data) - pos - 4)

            print(
                f"[{pos:04X}] Rec {rec_idx:2d}: TYPE 01 (Load Block)     "
                f"LoadAddr=0x{addr:04X}  DataLen={dlen:3d} B  (rlen=0x{rlen:02X})"
            )
            total_payload += dlen
            pos += 4 + dlen

        elif rtype == 0x02:
            if pos + 3 >= len(cmd_data):
                print(f"[{pos:04X}] Truncated Type 02 record header")
                break
            entry = cmd_data[pos + 2] | (cmd_data[pos + 3] << 8)
            print(f"[{pos:04X}] Rec {rec_idx:2d}: TYPE 02 (Transfer Addr) EntryPoint=0x{entry:04X}")
            pos += 2 + rlen

        elif rtype == 0x05:
            name_bytes = cmd_data[pos + 2 : pos + 2 + rlen]
            name_str = name_bytes.decode("ascii", errors="replace")
            print(f"[{pos:04X}] Rec {rec_idx:2d}: TYPE 05 (Module Name)    Name='{name_str}'")
            pos += 2 + rlen

        elif rtype < 0x20:
            print(f"[{pos:04X}] Rec {rec_idx:2d}: TYPE 0x{rtype:02X} (Control Rec)  Len={rlen}")
            pos += 2 + rlen

        else:
            print(f"[{pos:04X}] Invalid record type 0x{rtype:02X} (>= 0x20)")
            break

        rec_idx += 1

    print(f"=== End of module. Total code/data payload: {total_payload} bytes ===")


def unpack_cmd(cmd_data: bytes) -> tuple[int, int, bytes]:
    """Unpack a .CMD file into (load_addr, entry_addr, payload_bytes)."""
    pos = 0
    payload = bytearray()
    first_addr = None
    entry_addr = None

    while pos < len(cmd_data):
        rtype = cmd_data[pos]
        rlen = cmd_data[pos + 1]
        if rtype == 0x01:
            addr = cmd_data[pos + 2] | (cmd_data[pos + 3] << 8)
            if first_addr is None:
                first_addr = addr
            dlen = 256 if rlen == 2 else (rlen - 2)
            payload.extend(cmd_data[pos + 4 : pos + 4 + dlen])
            pos += 4 + dlen
        elif rtype == 0x02:
            entry_addr = cmd_data[pos + 2] | (cmd_data[pos + 3] << 8)
            pos += 2 + rlen
        else:
            pos += 2 + rlen

    return first_addr or 0x2600, entry_addr or 0x2600, bytes(payload)


def main():
    parser = argparse.ArgumentParser(
        description="Authentic TRSDOS / LS-DOS 6.x Load Module (.CMD) Packager"
    )
    parser.add_argument("input", nargs="?", help="Input flat binary file (.bin) or .CMD file for dump")
    parser.add_argument("-o", "--output", help="Output .CMD file path (default: <input>.cmd)")
    parser.add_argument(
        "-l",
        "--load",
        default="2600",
        help="Load address in hex or decimal (default: 2600h)",
    )
    parser.add_argument(
        "-e",
        "--entry",
        default=None,
        help="Entry / transfer address in hex or decimal (default: same as load address)",
    )
    parser.add_argument(
        "-c",
        "--chunk",
        type=int,
        default=256,
        help="Maximum chunk size per Type 01 record (1..256, default: 256)",
    )
    parser.add_argument(
        "-n",
        "--name",
        default=None,
        help="Optional module name for Type 05 record (up to 31 chars)",
    )
    parser.add_argument(
        "-d",
        "--dump",
        action="store_true",
        help="Disassemble and dump records in an existing .CMD file",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Display detailed block conversion logs",
    )

    args = parser.parse_args()

    if not args.input:
        parser.print_help()
        sys.exit(1)

    if not os.path.isfile(args.input):
        print(f"Error: input file '{args.input}' not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.input, "rb") as f:
        in_data = f.read()

    # Dump mode
    if args.dump:
        dump_cmd(in_data)
        sys.exit(0)

    # Packaging mode
    load_addr = parse_address(args.load)
    entry_addr = parse_address(args.entry) if args.entry else load_addr

    out_path = args.output
    if not out_path:
        base, _ = os.path.splitext(args.input)
        out_path = base + ".cmd"

    cmd_data = pack_bin_to_cmd(
        in_data,
        load_addr=load_addr,
        entry_addr=entry_addr,
        chunk_size=args.chunk,
        module_name=args.name,
        verbose=args.verbose,
    )

    with open(out_path, "wb") as f:
        f.write(cmd_data)

    print(f"[✓] Successfully packaged '{args.input}' ({len(in_data)} bytes)")
    print(f"    Load Address:  0x{load_addr:04X}")
    print(f"    Entry Point:   0x{entry_addr:04X}")
    print(f"    Chunk Size:    {args.chunk} bytes")
    print(f"    Output CMD:    '{out_path}' ({len(cmd_data)} bytes)")


if __name__ == "__main__":
    main()
