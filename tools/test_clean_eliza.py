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
    time.sleep(0.1)

def send_char(ch):
    activate_emulator()
    if ch == '"':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to keystroke (character id 34)'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == ':':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to keystroke ":"'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == '0':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to key code 29'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == '1':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to key code 18'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == '5':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to key code 23'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == '-':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to key code 27'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == ' ':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to key code 49'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == '\n':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to key code 36'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        subprocess.run(['osascript', '-e', f'tell application "System Events" to keystroke "{ch}"'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.08)

def send_string(s):
    for ch in s:
        send_char(ch)

def capture(win_id, path):
    if win_id:
        subprocess.run(['screencapture', '-x', '-l', str(win_id), path])
    else:
        subprocess.run(['screencapture', '-x', path])

subprocess.run(['pkill', '-9', '-f', 'fab-agon-emulator'])
subprocess.run(['pkill', '-9', '-f', 'esp8266_sim.py'])
subprocess.run(['pkill', '-9', '-f', 'fujinet'])
time.sleep(1)

fn_log_file = '/tmp/fn_clean_test.log'
fn_log = open(fn_log_file, 'w')
fn = subprocess.Popen(
    ['./fujinet', '-c', 'fnconfig_phase1.ini'],
    cwd='/Users/richardlucente/development/git/fujinet-firmware/build/dist',
    stdout=fn_log, stderr=subprocess.STDOUT
)
time.sleep(2)

sim_log = open('/tmp/sim_clean_test.log', 'w')
sim = subprocess.Popen(
    ['python3', 'esp8266_sim.py'],
    cwd='/Users/richardlucente/development/git/agon-net-utils/tools',
    stdout=sim_log, stderr=subprocess.STDOUT
)
time.sleep(2)

emu_bin = '/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/fab-agon-emulator'
sdcard_img = '/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/sdcard.img'
emu_log = open('/tmp/emu_clean_test.log', 'w')
emu = subprocess.Popen(
    [emu_bin, '--uart1-device', '/tmp/agon-uart1', '--uart1-baud', '0', '--sdcard-img', sdcard_img],
    cwd='/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64',
    stdout=emu_log, stderr=subprocess.STDOUT
)

for _ in range(20):
    time.sleep(0.5)
    win_id = find_emulator_window()
    if win_id: break

print('Booting...')
time.sleep(8)
send_string('0\n')
time.sleep(6)
send_string('\n')
time.sleep(4)

print('Starting basic...')
send_string('basic\n')
time.sleep(18)
capture(win_id, '/tmp/clean_basic_ready.png')

print('Testing merge "eliza/bas:0"...')
send_string('merge "eliza/bas:0"\n')
time.sleep(10)
capture(win_id, '/tmp/clean_merge_result.png')

print('Testing list 10-50...')
send_string('list 10-50\n')
time.sleep(5)
capture(win_id, '/tmp/clean_list_result.png')

print('Testing list...')
send_string('list\n')
time.sleep(8)
capture(win_id, '/tmp/clean_list_all.png')

print('Testing run...')
send_string('run\n')
time.sleep(6)
capture(win_id, '/tmp/clean_run.png')

subprocess.run(['pkill', '-9', '-f', 'fab-agon-emulator'])
sim.terminate()
fn.terminate()
print('Test script finished successfully.')
