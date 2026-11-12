#!/usr/bin/env python3
"""
tools/inject_file_to_dsk.py - Inject a host file into an LS-DOS 6.x DiskDISK image
Calculates authentic HIT hash, allocates free granules from GAT, formats Directory Entry (FPDE),
writes file payload, and updates GAT/HIT tables.
"""

import os
import sys

def compute_hit_hash(filename: str, ext: str = "") -> int:
    """Calculates authentic LS-DOS 6.x 8-bit HIT hash for 11-byte filespec."""
    name8 = filename.upper().ljust(8)[:8]
    ext3 = ext.upper().ljust(3)[:3]
    full11 = (name8 + ext3).encode("latin1")
    a = 0
    for b in full11:
        a = (a ^ b) & 0xFF
        a = ((a << 1) | (a >> 7)) & 0xFF
    if a == 0:
        a = 1
    return a

def inject_file(dsk_path: str, host_file: str, trs_name: str, trs_ext: str = "CMD"):
    with open(host_file, "rb") as f:
        payload = f.read()

    file_size = len(payload)
    sectors_needed = (file_size + 255) // 256
    if sectors_needed == 0:
        sectors_needed = 1

    with open(dsk_path, "rb") as f:
        disk_data = bytearray(f.read())

    assert disk_data[:8] == b"DiskDISK", "Target image must be DiskDISK format"

    # GAT is wire sector 37 (Cylinder 1, Sector 0)
    gat_offset = 37 * 256
    gat = disk_data[gat_offset : gat_offset + 256]

    # HIT is wire sector 38 (Cylinder 1, Sector 1)
    hit_offset = 38 * 256
    hit = disk_data[hit_offset : hit_offset + 256]

    cyls = 40
    heads = 2
    spt = 18
    sec_per_cyl = heads * spt # 36
    grans_per_cyl = 6
    sec_per_gran = 6

    grans_needed = (sectors_needed + sec_per_gran - 1) // sec_per_gran
    if grans_needed == 0:
        grans_needed = 1

    # Check if file already exists in directory; if so, replace it
    target_name8 = trs_name.upper().ljust(8)[:8]
    target_ext3 = trs_ext.upper().ljust(3)[:3]
    hit_val = compute_hit_hash(target_name8, target_ext3)

    existing_dec = None
    free_dec = None

    for sec_idx in range(32):
        wire_sec = 37 + 2 + sec_idx
        for ent in range(8):
            dec = (ent << 5) | sec_idx
            off = wire_sec * 256 + ent * 32
            entry = disk_data[off : off + 32]
            status = entry[0]
            if (status & 0x50):
                fname = entry[5:13].decode("latin1", errors="replace")
                fext = entry[13:16].decode("latin1", errors="replace")
                if fname == target_name8 and fext == target_ext3:
                    existing_dec = (dec, wire_sec, off)
                    break
            else:
                if free_dec is None:
                    free_dec = (dec, wire_sec, off)
        if existing_dec:
            break

    # If existing file found, reuse its DEC; otherwise use first free DEC
    if existing_dec:
        dec, dir_wire_sec, dir_entry_off = existing_dec
        print(f"[+] Overwriting existing file '{target_name8.strip()}.{target_ext3.strip()}' at DEC 0x{dec:02X}")
    else:
        assert free_dec is not None, "Disk directory is full (256 entries)"
        dec, dir_wire_sec, dir_entry_off = free_dec
        print(f"[+] Found free directory slot at DEC 0x{dec:02X} (Sector {dir_wire_sec}, offset 0x{dir_entry_off:06X})")

    # Allocate free granules from GAT (prefer cylinders 25..39 to avoid low system tracks)
    alloc_cyl = None
    alloc_gran = None

    # Search for contiguous granules needed
    for cyl in list(range(20, cyls)) + list(range(2, 20)):
        gat_byte = gat[cyl]
        for g in range(grans_per_cyl - grans_needed + 1):
            mask = ((1 << grans_needed) - 1) << g
            if (gat_byte & mask) == 0:
                alloc_cyl = cyl
                alloc_gran = g
                break
        if alloc_cyl is not None:
            break

    assert alloc_cyl is not None, "Disk is full; cannot allocate granules"
    print(f"[+] Allocated {grans_needed} granule(s) at Cylinder {alloc_cyl}, Granule {alloc_gran}")

    # Mark granules in GAT
    for g in range(grans_needed):
        gat[alloc_cyl] |= (1 << (alloc_gran + g))
    disk_data[gat_offset : gat_offset + 256] = gat

    # Update HIT entry
    hit[dec] = hit_val
    disk_data[hit_offset : hit_offset + 256] = hit

    # Calculate physical data start sector
    # wire_sec = 1 (container header) + alloc_cyl * 36 + alloc_gran * 6
    data_wire_sec = 1 + alloc_cyl * sec_per_cyl + alloc_gran * sec_per_gran
    data_byte_offset = data_wire_sec * 256

    # Write payload padded with zeroes
    padded_len = sectors_needed * 256
    padded_payload = payload.ljust(padded_len, b"\x00")
    disk_data[data_byte_offset : data_byte_offset + padded_len] = padded_payload

    # Format 32-byte Directory Entry (FPDE)
    # Byte 0: Status byte (0x10 = user file active, no passwords)
    # Byte 1-2: Passwords (0, 0)
    # Byte 3: EOF byte offset within last sector (0 = 256 bytes)
    # Byte 4: LRL (0)
    # Bytes 5-12: Name (8 chars)
    # Bytes 13-15: Ext (3 chars)
    # Bytes 16-19: Flags / dates (0)
    # Bytes 20-21: ERN (sectors allocated, little endian)
    # Bytes 22-23: Extent 0 [Cyl, (Gran << 5) | (Count - 1)]
    # Bytes 24-31: Unused extents [0xFF, 0x00, ...]
    fpde = bytearray(32)
    fpde[0] = 0x10
    eof_byte = file_size % 256
    fpde[3] = eof_byte
    fpde[4] = 0x00
    fpde[5:13] = target_name8.encode("latin1")
    fpde[13:16] = target_ext3.encode("latin1")
    fpde[20] = sectors_needed & 0xFF
    fpde[21] = (sectors_needed >> 8) & 0xFF
    fpde[22] = alloc_cyl
    fpde[23] = ((alloc_gran << 5) | (grans_needed - 1)) & 0xFF
    for e in range(1, 5):
        fpde[22 + e * 2] = 0xFF
        fpde[22 + e * 2 + 1] = 0x00

    disk_data[dir_entry_off : dir_entry_off + 32] = fpde

    # Commit to target disk image
    with open(dsk_path, "wb") as f:
        f.write(disk_data)

    print(f"[✓] Successfully injected '{target_name8.strip()}.{target_ext3.strip()}' ({file_size} bytes, {sectors_needed} sec) into {dsk_path}")
    print(f"    DEC:        0x{dec:02X}")
    print(f"    HIT:        0x{hit_val:02X}")
    print(f"    Extent:     Cylinder {alloc_cyl}, Granules {alloc_gran}..{alloc_gran+grans_needed-1}")
    print(f"    Wire Sector: {data_wire_sec}")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <target.dsk> <host_file> <TRS_NAME> [TRS_EXT]")
        sys.exit(1)
    dsk = sys.argv[1]
    host = sys.argv[2]
    tname = sys.argv[3]
    text = sys.argv[4] if len(sys.argv) > 4 else "CMD"
    inject_file(dsk, host, tname, text)
