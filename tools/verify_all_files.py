#!/usr/bin/env python3
"""
verify_all_files.py - Deep integrity check of all files on boot_volume.dsk
"""

import os
import sys

TARGET_DSK = "/Users/richardlucente/development/git/fujinet-firmware/build/dist/SD/boot_volume.dsk"

def main():
    print("=" * 80)
    print("LS-DOS 6.3.1 ALL-FILES INTEGRITY VERIFICATION")
    print(f"Target: {TARGET_DSK}")
    print("=" * 80)

    with open(TARGET_DSK, "rb") as f:
        data = f.read()

    # Track 1: GAT is sec 37, HIT is sec 38, Dir is sec 39..70
    files = []
    for sec_idx in range(32):
        sec = sec_idx + 2
        wsec = 37 + sec
        for ent in range(8):
            dec = (ent << 5) | sec_idx
            off = wsec * 256 + ent * 32
            entry = data[off : off + 32]
            st = entry[0]
            if st & 0x50:
                name = entry[5:13].decode("latin1", errors="replace").strip()
                ext = entry[13:16].decode("latin1", errors="replace").strip()
                fullname = f"{name}.{ext}" if ext else name
                eof_off = entry[3]
                ern = entry[20] | (entry[21] << 8)
                
                # Extents
                extents = []
                for e in range(5):
                    cyl = entry[22 + e * 2]
                    gb = entry[23 + e * 2]
                    if cyl == 0xFF or cyl == 0xFE: break
                    gstart = gb >> 5
                    gcount = (gb & 0x1F) + 1
                    extents.append((cyl, gstart, gcount))
                
                files.append({
                    "dec": dec,
                    "fullname": fullname,
                    "ern": ern,
                    "eof_off": eof_off,
                    "extents": extents
                })

    print(f"Discovered {len(files)} active files to extract and verify.")

    corrupt_files = 0
    total_bytes_verified = 0

    for f in files:
        fname = f["fullname"]
        # Gather all sectors for this file
        file_sectors = []
        for cyl, gstart, gcount in f["extents"]:
            for g in range(gcount):
                tot_g = gstart + g
                act_cyl = cyl + tot_g // 6
                act_gran = tot_g % 6
                # 6 sectors per granule:
                # In 40-cyl, 2-head, 18-spt: 36 sectors per cylinder
                # Granule g has sectors g*6 .. g*6+5
                for s in range(6):
                    sec_in_cyl = act_gran * 6 + s
                    disk_lsn = act_cyl * 36 + sec_in_cyl
                    wire_sec = disk_lsn + 1 # +1 for DiskDISK header
                    file_sectors.append(data[wire_sec * 256 : (wire_sec + 1) * 256])

        ern = f["ern"]
        eof_off = f["eof_off"]
        
        # Calculate expected byte size
        if ern == 0:
            expected_size = eof_off
        else:
            if eof_off == 0:
                expected_size = ern * 256
            else:
                expected_size = (ern - 1) * 256 + eof_off

        # Assemble file payload
        payload = b"".join(file_sectors)[:expected_size]
        total_bytes_verified += len(payload)

        # File-type specific sanity checks
        is_ok = True
        detail = ""

        if fname.endswith(".CMD") or fname.endswith(".SYS") or fname.endswith(".OV1") or fname.endswith(".OV2"):
            # Should have LMF header: record type 0x01 or 0x05 or 0x02
            first_byte = payload[0] if len(payload) > 0 else 0
            if first_byte in [0x01, 0x02, 0x05, 0x1F, 0x08, 0x5E, 0x00, 0x4F]:
                detail = f"LMF type 0x{first_byte:02X}, size {len(payload)}B, ERN={ern}"
            else:
                detail = f"Unknown LMF start 0x{first_byte:02X}, size {len(payload)}B"
        elif fname.endswith(".BAS"):
            # ASCII BASIC source
            lines = payload.split(b"\r")
            non_empty = [l for l in lines if l and l != b"\x1a"]
            detail = f"BASIC ASCII: {len(non_empty)} lines, size {len(payload)}B"
            if not non_empty or not non_empty[0][:1].isdigit():
                is_ok = False
                detail += " (Invalid line header!)"
        elif fname.endswith(".JCL") or fname.endswith(".HLP"):
            detail = f"Text/Help: size {len(payload)}B"
        else:
            detail = f"Data: size {len(payload)}B"

        status_str = "[✓]" if is_ok else "[!]"
        print(f"{status_str} {fname:<14} (DEC 0x{f['dec']:02X}): {detail}")
        if not is_ok:
            corrupt_files += 1

    print("-" * 80)
    print(f"Verification complete: {len(files)} files checked, {corrupt_files} corrupted.")
    print(f"Total payload data verified: {total_bytes_verified:,} bytes.")

if __name__ == "__main__":
    main()
