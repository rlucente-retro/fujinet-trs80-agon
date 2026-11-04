import os

lines = [
    '10 REM     ELIZA/DOCTOR',
    '20 REM     CREATED BY JOSEPH WEIZENBAUM',
    '30 REM     THIS VERSION BY JEFF SHRAGER',
    '40 REM     MODIFIED FOR MITS 8K BASIC',
    '50 PRINT "HELLO FROM SHORT ELIZA TEST!"',
    '60 END'
]
short_text = '\r'.join(lines) + '\r\x1a'
short_bytes = short_text.encode('latin1')
print(f'Short bytes length: {len(short_bytes)}')

target = '/Users/richardlucente/development/git/fujinet-firmware/build/dist/SD/boot_volume.dsk'
with open(target, 'rb') as f:
    dsk = bytearray(f.read())

pad_sector = bytearray(short_bytes)
pad_sector.extend(b'\x00' * (256 - len(pad_sector)))
dsk[361*256:362*256] = pad_sector

dir_offset = 45 * 256 + 2 * 32
dsk[dir_offset+3] = len(short_bytes) # eof_offset
dsk[dir_offset+4] = 0 # LRL
dsk[dir_offset+20] = 1 # num_rec = 1
dsk[dir_offset+21] = 0
dsk[dir_offset+22] = 10 # Cylinder 10
dsk[dir_offset+23] = 0x00 # 1 granule
for i in range(24, 30):
    dsk[dir_offset+i] = 0xFF

with open(target, 'wb') as f:
    f.write(dsk)
print('Updated boot_volume.dsk with 1-sector ELIZA (with trailing 0x1A).')
