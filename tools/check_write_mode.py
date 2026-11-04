import subprocess
import time
import os

subprocess.run(['pkill', '-9', '-f', 'fab-agon-emulator'])
subprocess.run(['pkill', '-9', '-f', 'esp8266_sim.py'])
subprocess.run(['pkill', '-9', '-f', 'fujinet'])
time.sleep(1)

# Enable write on Host1 in fnconfig_phase1.ini temporarily?
# Wait! fnconfig_phase1.ini has mode=r!
# With mode=r, writes to DSK are rejected by FujiNet!
# Let's check fnconfig_phase1.ini: mode=r!
