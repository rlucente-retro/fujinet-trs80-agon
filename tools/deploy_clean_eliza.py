import os

clean_path = '/tmp/eliza_clean.bas'
with open(clean_path, 'rb') as f:
    clean_data = f.read()

total_bytes = len(clean_data)
sectors = (total_bytes + 255) // 256
eof_offset = total_bytes % 256
if eof_offset == 0:
    eof_offset = 256 # 0 in TRSDOS DIR+3 means 256 bytes

print(f'Total bytes: {total_bytes}')
print(f'Sectors: {sectors}')
print(f'EOF offset in last sector: {eof_offset} (0x{eof_offset:02x})')

# Prepare 31 sectors of data
padded_data = bytearray(clean_data)
# Pad the last sector to 256 bytes with zeros
pad_len = sectors * 256 - len(padded_data)
padded_data.extend(b'\x00' * pad_len)
print(f'Padded payload length: {len(padded_data)} ({len(padded_data)//256} sectors)')

targets = [
    '/Users/richardlucente/development/git/fujinet-firmware/build/dist/SD/boot_volume.dsk',
    '/Users/richardlucente/development/git/trs-os-zds/TRSDOS_7/boot_volume.dsk'
]

for target in targets:
    if not os.path.exists(target):
        print(f'Skipping non-existent: {target}')
        continue
    with open(target, 'rb') as f:
        dsk = bytearray(f.read())
    
    # 1. Write payload at Wire LSN 361 (offset 361 * 256)
    start_offset = 361 * 256
    end_offset = start_offset + len(padded_data)
    dsk[start_offset:end_offset] = padded_data
    
    # 2. Update directory entry at Sector 45, Entry 2 (offset 45 * 256 + 2 * 32 = 0x2D40)
    dir_offset = 45 * 256 + 2 * 32
    # Verify filename is ELIZA BAS
    fname = dsk[dir_offset+5:dir_offset+13]
    ext = dsk[dir_offset+13:dir_offset+16]
    assert fname == b'ELIZA   ', f'Unexpected filename: {fname}'
    assert ext == b'BAS', f'Unexpected ext: {ext}'
    
    # DIR+0: 0x10 (normal user file)
    dsk[dir_offset+0] = 0x10
    # DIR+3: EOF offset
    dsk[dir_offset+3] = eof_offset if eof_offset < 256 else 0
    # DIR+4: LRL (0 = 256)
    dsk[dir_offset+4] = 0x00
    # DIR+20..21: num_rec (31)
    dsk[dir_offset+20] = sectors & 0xFF
    dsk[dir_offset+21] = (sectors >> 8) & 0xFF
    # DIR+22..23: Extent 0: Cylinder 10, byte 0x05 (start_gran=0, gran_len=5 -> 6 granules = 36 sectors)
    dsk[dir_offset+22] = 10
    dsk[dir_offset+23] = 0x05
    # Extents 1..3: 0xFF
    for i in range(24, 30):
        dsk[dir_offset+i] = 0xFF
        
    with open(target, 'wb') as f:
        f.write(dsk)
    print(f'Successfully updated {target}')

print('All targets updated.')
