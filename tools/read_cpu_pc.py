import subprocess
import time
import os

subprocess.run(['pkill', '-9', '-f', 'fab-agon-emulator'])
subprocess.run(['pkill', '-9', '-f', 'esp8266_sim.py'])
subprocess.run(['pkill', '-9', '-f', 'fujinet'])
time.sleep(1)

fn = subprocess.Popen(
    ['./fujinet', '-c', 'fnconfig_phase1.ini'],
    cwd='/Users/richardlucente/development/git/fujinet-firmware/build/dist',
    stdout=open('/tmp/fn_pc.log', 'w'), stderr=subprocess.STDOUT
)
time.sleep(2)

sim = subprocess.Popen(
    ['python3', 'esp8266_sim.py'],
    cwd='/Users/richardlucente/development/git/agon-net-utils/tools',
    stdout=open('/tmp/sim_pc.log', 'w'), stderr=subprocess.STDOUT
)
time.sleep(2)

emu_bin = '/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/fab-agon-emulator'
sdcard_img = '/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/sdcard.img'
emu = subprocess.Popen(
    [emu_bin, '--uart1-device', '/tmp/agon-uart1', '--uart1-baud', '0', '--sdcard-img', sdcard_img],
    cwd='/Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64',
    stdout=open('/tmp/emu_pc.log', 'w'), stderr=subprocess.STDOUT
)

def activate():
    subprocess.run([
        'osascript', '-e',
        'tell application "System Events" to set frontmost of (first process whose name contains "fab-agon-emulator") to true'
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.1)

def send_char(ch):
    activate()
    if ch == '"':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to keystroke (character id 34)'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == ':':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to keystroke ":"'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == '0':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to key code 29'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif ch == '\n':
        subprocess.run(['osascript', '-e', 'tell application "System Events" to key code 36'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        subprocess.run(['osascript', '-e', f'tell application "System Events" to keystroke "{ch}"'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.08)

def send_string(s):
    for ch in s: send_char(ch)

print('Booting...')
time.sleep(8)
send_string('0\n')
time.sleep(6)
send_string('\n')
time.sleep(4)

print('Starting basic...')
send_string('basic\n')
time.sleep(18)

print('Sending merge command...')
send_string('merge "eliza/bas:0"\n')
time.sleep(5)

print(f'Attaching lldb to PID {emu.pid}...')
lldb_script = '''
thread select 2
frame select 4
register read sp x20 x22 x28
memory read -count 16 -format x -size 1 $sp+0x90
memory read -count 16 -format x -size 1 $sp+0xa0
'''
lldb_res = subprocess.run([
    'lldb', '-p', str(emu.pid), '--batch',
    '-o', 'thread select 2',
    '-o', 'frame select 4',
    '-o', 'register read sp x20 x22 x28',
    '-o', 'memory read -count 32 -format x -size 1 $sp+0x90'
], capture_output=True, text=True)

with open('/tmp/lldb_pc_out.txt', 'w') as f:
    f.write(lldb_res.stdout + '\n=== STDERR ===\n' + lldb_res.stderr)

print('LLDB done. Terminating.')
subprocess.run(['pkill', '-9', '-f', 'fab-agon-emulator'])
sim.terminate()
fn.terminate()
