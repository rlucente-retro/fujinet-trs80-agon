import subprocess
import time
import os
import ctypes
import ctypes.util

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
    time.sleep(0.15)

def capture(win_id, path):
    if win_id:
        subprocess.run(['screencapture', '-x', '-l', str(win_id), path])
    else:
        subprocess.run(['screencapture', '-x', path])

subprocess.run(['pkill', '-9', '-f', 'fab-agon-emulator'])
time.sleep(1)

emu_bin = '/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/fab-agon-emulator'
sdcard_img = '/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/sdcard.img'
emu = subprocess.Popen(
    [emu_bin, '--uart1-device', '/tmp/agon-uart1', '--uart1-baud', '0', '--sdcard-img', sdcard_img],
    cwd='/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64',
    stdout=open('/tmp/emu_run.log', 'w'), stderr=subprocess.STDOUT
)

for _ in range(20):
    time.sleep(0.5)
    win_id = find_emulator_window()
    if win_id: break

print('Waiting for TRSDOS boot prompt...')
time.sleep(8)
capture(win_id, '/tmp/step1_trsdos_boot.png')

print('Selecting drive 0...')
activate_emulator()
subprocess.run(['osascript', '-e', '''
tell application "System Events"
    key code 29
    key code 36
end tell
'''])
time.sleep(6)
capture(win_id, '/tmp/step2_date_prompt.png')

print('Pressing enter for date/time...')
activate_emulator()
subprocess.run(['osascript', '-e', 'tell application "System Events" to key code 36'])
time.sleep(4)
capture(win_id, '/tmp/step3_trsdos_ready.png')

print('Starting basic...')
activate_emulator()
subprocess.run(['osascript', '-e', '''
tell application "System Events"
    keystroke "basic" & (character id 13)
end tell
'''])
time.sleep(18)
capture(win_id, '/tmp/step4_basic_ready.png')

print('Typing merge "eliza/bas:0"...')
activate_emulator()
subprocess.run(['osascript', '-e', '''
tell application "System Events"
    keystroke "merge " & (character id 34) & "eliza/bas:"
    delay 0.1
    key code 29
    delay 0.1
    keystroke (character id 34) & (character id 13)
end tell
'''])
time.sleep(10)
capture(win_id, '/tmp/step5_merge_done.png')

print('Typing list...')
activate_emulator()
subprocess.run(['osascript', '-e', '''
tell application "System Events"
    keystroke "list" & (character id 13)
end tell
'''])
time.sleep(5)
capture(win_id, '/tmp/step6_list_done.png')

print('Typing run...')
activate_emulator()
subprocess.run(['osascript', '-e', '''
tell application "System Events"
    keystroke "run" & (character id 13)
end tell
'''])
time.sleep(5)
capture(win_id, '/tmp/step7_run_done.png')

subprocess.run(['pkill', '-9', '-f', 'fab-agon-emulator'])
print('Interactive test finished successfully.')
