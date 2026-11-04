import subprocess
import time
import os
import ctypes
import ctypes.util
from PIL import Image

CG = ctypes.cdll.LoadLibrary(ctypes.util.find_library('CoreGraphics'))
CF = ctypes.cdll.LoadLibrary(ctypes.util.find_library('CoreFoundation'))
CF.CFStringGetCString.restype = ctypes.c_bool
CF.CFStringGetCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
CF.CFDictionaryGetValue.restype = ctypes.c_void_p
CF.CFDictionaryGetValue.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
CF.CFNumberGetValue.restype = ctypes.c_bool
CF.CFNumberGetValue.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
CF.CFStringCreateWithCString.restype = ctypes.c_void_p
CF.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
CG.CGWindowListCopyWindowInfo.restype = ctypes.c_void_p
CG.CGWindowListCopyWindowInfo.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
CF.CFArrayGetCount.restype = ctypes.c_long
CF.CFArrayGetCount.argtypes = [ctypes.c_void_p]
CF.CFArrayGetValueAtIndex.restype = ctypes.c_void_p
CF.CFArrayGetValueAtIndex.argtypes = [ctypes.c_void_p, ctypes.c_long]

kOwnerName = CF.CFStringCreateWithCString(None, b'kCGWindowOwnerName', 0x08000100)
kWindowNumber = CF.CFStringCreateWithCString(None, b'kCGWindowNumber', 0x08000100)

def cf_to_str(cf_str):
    if not cf_str: return ''
    buf = ctypes.create_string_buffer(256)
    CF.CFStringGetCString(cf_str, buf, 256, 0x08000100)
    return buf.value.decode('utf-8', errors='replace')

def find_emulator_window():
    arr = CG.CGWindowListCopyWindowInfo(1, 0)
    count = CF.CFArrayGetCount(arr)
    for i in range(count):
        d = CF.CFArrayGetValueAtIndex(arr, i)
        owner = cf_to_str(CF.CFDictionaryGetValue(d, kOwnerName))
        if 'fab-agon-emulator' in owner:
            num = ctypes.c_uint32()
            CF.CFNumberGetValue(CF.CFDictionaryGetValue(d, kWindowNumber), 3, ctypes.byref(num))
            return num.value
    return None

def activate_emulator():
    subprocess.run([
        'osascript', '-e',
        'tell application "System Events" to set frontmost of (first process whose name contains "fab-agon-emulator") to true'
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.2)

def capture_screen(win_id, path):
    if win_id:
        subprocess.run(['screencapture', '-x', '-l', str(win_id), path])
    else:
        subprocess.run(['screencapture', '-x', path])

def ocr_screen(win_id):
    tmp_png = '/private/tmp/agon_ocr.png'
    tmp_out = '/private/tmp/agon_ocr_txt'
    capture_screen(win_id, tmp_png)
    if not os.path.exists(tmp_png):
        return ''
    # Crop to inner content if desired, or let tesseract process directly
    res = subprocess.run(['tesseract', tmp_png, tmp_out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if os.path.exists(tmp_out + '.txt'):
        with open(tmp_out + '.txt', 'r', encoding='latin1', errors='replace') as f:
            return f.read()
    return ''

def send_text(text):
    activate_emulator()
    escaped = text.replace('\\', '\\\\').replace('"', '\\"')
    script = f'''
tell application "System Events"
    keystroke "{escaped}"
end tell
'''
    subprocess.run(['osascript', '-e', script])
    time.sleep(0.3)

def send_enter():
    activate_emulator()
    script = '''
tell application "System Events"
    key code 36
end tell
'''
    subprocess.run(['osascript', '-e', script])
    time.sleep(0.3)

def wait_for_ocr_pattern(win_id, pattern, timeout=30, interval=1.0):
    start = time.time()
    while time.time() - start < timeout:
        txt = ocr_screen(win_id)
        if pattern.lower() in txt.lower():
            return True, txt
        time.sleep(interval)
    return False, ocr_screen(win_id)

print('Cleaning up any existing emulator instances...')
subprocess.run(['pkill', '-9', '-f', 'fab-agon-emulator'])
time.sleep(1)

emu_bin = '/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/fab-agon-emulator'
sdcard_img = '/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/sdcard.img'

print('Launching fab-agon-emulator with UART1 bridge and sdcard image...')
emu = subprocess.Popen(
    [emu_bin, '--uart1-device', '/tmp/agon-uart1', '--uart1-baud', '0', '--sdcard-img', sdcard_img],
    cwd='/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64',
    stdout=open('/tmp/verify_emu.log', 'w'), stderr=subprocess.STDOUT
)

win_id = None
for _ in range(20):
    time.sleep(0.5)
    win_id = find_emulator_window()
    if win_id:
        break

if not win_id:
    print('Error: fab-agon-emulator window not found!')
    emu.terminate()
    exit(1)

print(f'Emulator running with Window ID: {win_id}')

# Step 1: Wait for Drive? prompt
print('Step 1: Waiting for TRSDOS Drive? prompt...')
found, txt = wait_for_ocr_pattern(win_id, 'Drive', timeout=25)
print('OCR output at Drive prompt:\n', txt.strip())
if not found:
    print('Warning: "Drive" not found via OCR, proceeding with timed keystroke...')

send_text('0')
send_enter()

# Step 2: Wait for Date? prompt
print('Step 2: Waiting for Date prompt...')
found, txt = wait_for_ocr_pattern(win_id, 'Date', timeout=15)
print('OCR output at Date prompt:\n', txt.strip())
send_enter()

# Step 3: Wait for LS-DOS Ready
print('Step 3: Waiting for LS-DOS Ready prompt...')
found, txt = wait_for_ocr_pattern(win_id, 'Ready', timeout=15)
print('OCR output at LS-DOS Ready:\n', txt.strip())

# Step 4: Start BASIC
print('Step 4: Launching basic...')
send_text('basic')
send_enter()

# Step 5: Wait for BASIC Ready
print('Step 5: Waiting for BASIC Ready prompt...')
# BASIC takes ~10 seconds to load over DriveWire
time.sleep(6)
found, txt = wait_for_ocr_pattern(win_id, 'Microsoft', timeout=20)
print('OCR output after basic:\n', txt.strip())
capture_screen(win_id, '/tmp/verify_step1_basic_ready.png')

# Step 6: Merge "eliza/bas:0"
print('Step 6: Executing: merge "eliza/bas:0"')
send_text('merge "eliza/bas:0"')
send_enter()

# Step 7: Wait for merge to complete (BASIC prints Ready)
print('Step 7: Waiting for merge to complete...')
time.sleep(6) # 31 sectors take ~4-8 seconds
found, txt = wait_for_ocr_pattern(win_id, 'Ready', timeout=25)
print('OCR output after merge:\n', txt.strip())
capture_screen(win_id, '/tmp/verify_step2_merged.png')

# Step 8: LIST lines 10 to 50
print('Step 8: Executing: list 10-50')
send_text('list 10-50')
send_enter()
time.sleep(3)
txt = ocr_screen(win_id)
print('OCR output after list 10-50:\n', txt.strip())
capture_screen(win_id, '/tmp/verify_step3_listed.png')

# Step 9: RUN Eliza
print('Step 9: Executing: run')
send_text('run')
send_enter()
print('Waiting for Eliza greeting banner...')
time.sleep(5)
txt = ocr_screen(win_id)
print('OCR output after run:\n', txt.strip())
capture_screen(win_id, '/tmp/verify_step4_running.png')

# Step 10: Interactive dialog with Eliza
print('Step 10: Interacting with Eliza: typing "HELLO"')
send_text('HELLO')
send_enter()
time.sleep(4)
txt = ocr_screen(win_id)
print('OCR output after greeting Eliza:\n', txt.strip())
capture_screen(win_id, '/tmp/verify_step5_dialog.png')

print('All verification steps completed!')
emu.terminate()
