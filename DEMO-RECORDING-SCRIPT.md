# FujiNet + TRS-80 on Agon Light 2 — Screen-Recording Demo Script

This is a manual follow-along shot list and narration guide for screen recording every major capability delivered across **Phases 1–4**:

- Native Quark/Console8 MOS disk management (`*trs80`)
- Visual pre-boot manager & lowcore mailbox staging (`FNBOOT`)
- Network booting LS-DOS 6.3 over FujiNet DriveWire (`OP_READEX`)
- In-DOS FujiNet utilities (`OP_FUJI`: `FNSTATUS`, `FNHOSTS`, `FNDEVS`)
- Dynamic Local MicroSD mounting (`SDMOUNT`, `DRVMAP`)
- Hybrid cross-tier Read/Write (`COPY` from FujiNet UART1 to Local MicroSD SPI0 `CMD24` RMW)
- Model 4 Disk BASIC persistence on Local SD + hardware Write-Protect enforcement

---

## Pre-Recording Checklist (Clean Slate)

1. **(Recommended) Rebuild a fresh `sdcard.img`** so `testdisk1.dsk` has no leftover files from prior rehearsals and `/fnboot.ini` does not yet exist:
   ```bash
   /Users/richardlucente/development/git/trs-os-zds/create_sdcard_img.sh \
     /Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/sdcard \
     /Users/richardlucente/development/git/fab-agon-emulator-v1.2.6-macos-arm64/sdcard.img
   ```
2. **Terminal 1 — Start FujiNet-PC (BoIP server on port 65504):**
   ```bash
   cd ~/development/git/fujinet-firmware/build/dist && ./fujinet -c fnconfig_phase1.ini
   ```
3. **Terminal 2 — Start `fab-agon-emulator`:**
   ```bash
   ~/development/git/agon-net-utils/tools/run_emulator.sh
   ```
   Wait until `autoexec.txt` finishes `openstream 127.0.0.1 65504` and stops at the MOS `*` prompt.
4. **Start your screen recorder**, focus the `fab-agon-emulator` window, and step through the commands below at your own pace.

---

## Shot-by-Shot Demo Script (24 Steps)

### Part 1: Quark / Console8 MOS Unified Disk Suite (`*trs80`)

| Step | Command to Enter | What Happens On Screen | Talking Point / Narration Cue |
| :---: | :--- | :--- | :--- |
| **1** | `trs80 dir /disks/base/testdisk1.dsk` | Lists `SDTEST1` directory (`HELLO/CMD`) directly from FAT32. | "`*trs80` is a Toolshed-style MOS utility that inspects and edits LS-DOS disk images directly on the Agon's FAT32 SD card—no PC required." |
| **2** | `trs80 free /disks/base/testdisk1.dsk` | Displays `40x2x18` geometry, allocated vs. free granules, and KiB free. | "It auto-detects `DiskDISK`, `JV1`, `JV3`, and `HDV` containers and reports exact LS-DOS granule capacity." |
| **3** | `trs80 check /disks/base/testdisk1.dsk` | Validates Sector 0 DCT, Track 17 GAT, HIT hashes, and extents (`0 errors`). | "The built-in structural auditor checks every GAT bit, HIT hash, and file extent for filesystem integrity." |
| **4** | `trs80 map /disks -o /disks/demo_map.txt` | Recursively scans `/disks`, prints relative paths + 32-bit LBAs + contiguity. | "`trs80 map` recursively resolves physical 32-bit SD card LBAs so TRSDOS can mount local images directly over hardware SPI0." |

### Part 2: `FNBOOT` Visual Pre-Boot Manager & Mailbox Staging

| Step | Command to Enter | What Happens On Screen | Talking Point / Narration Cue |
| :---: | :--- | :--- | :--- |
| **5** | `fnboot` | Full-screen menu showing FujiNet `ONLINE` (IP/Host/FW), `/disks/sdmap.txt`, and `:0` on DriveWire. | "`FNBOOT` queries FujiNet directly over polled UART1 without dropping the `openstream` link, and auto-discovers `sdmap.txt`." |
| **6** | `1` -> `S` -> `2` + Enter | Assigns Drive `:1` to `Local SD` `RW` `disks/base/testdisk1.dsk`. | "Here we pre-mount a writable Local MicroSD work disk on logical Drive `:1`." |
| **7** | `2` -> `S` -> `3` + Enter -> `2` -> `T` | Assigns Drive `:2` to `Local SD` `RO` `disks/games/testdisk2.dsk`. | "And we mount a second Local SD disk on Drive `:2` toggled to Read-Only to protect it." |
| **8** | `S` | Saves `/fnboot.ini` and confirms in the status bar. | "Saving writes `/fnboot.ini`, allowing instant non-interactive quick boots via `fnboot -b`." |
| **9** | `B` | Stages `/fnboot.tmp` mailbox, launches `OSboot -2f`, boots LS-DOS 6.3 over FujiNet `:0`. | "`FNBOOT` patches the lowcore SRAM mailbox directly into `TRSDOS.bin` and hands off to `OSboot` to boot LS-DOS 6.3 over DriveWire." |

### Part 3: Native LS-DOS 6.3 FujiNet Utilities (`OP_FUJI` over UART1)

| Step | Command to Enter | What Happens On Screen | Talking Point / Narration Cue |
| :---: | :--- | :--- | :--- |
| **10** | `DRVMAP` | Shows `:0` = DriveWire (`Slot 0`), `:1` = Local SD (`RW`), `:2` = Local SD (`RO`). | "`DRVMAP` shows our hybrid multi-tier storage layout—both local SD drives are already online from `FNBOOT`'s mailbox." |
| **11** | `FNSTATUS` | Displays FujiNet firmware, hostname, SSID, IP, gateway, netmask, DNS, MAC. | "All `FN*.CMD` utilities are authentic `0x2600` LS-DOS load modules speaking `OP_FUJI` (`0xE2`) over UART1." |
| **12** | `FNHOSTS` | Lists all 8 configured FujiNet host slots (SD + TNFS). | "`FNHOSTS` queries the 256-byte host slot table (`0xF4`) directly from the FujiNet device." |
| **13** | `FNDEVS` | Lists virtual device slots `0`–`3` and mounted disk image paths. | "`FNDEVS` inspects the active virtual device slots (`0xF2`) showing `/boot_volume.dsk` mounted on Slot 0." |
| **14** | `DIR :0` | Displays files on `root360` (`boot_volume.dsk`) over FujiNet DriveWire. | "Standard LS-DOS commands like `DIR :0` transparently stream 256-byte sectors via DriveWire `OP_READEX`." |

### Part 4: Dynamic In-DOS Local MicroSD Mounting (`SDMOUNT`)

| Step | Command to Enter | What Happens On Screen | Talking Point / Narration Cue |
| :---: | :--- | :--- | :--- |
| **15** | `SDMOUNT :3 (PATH=games/testdisk2.dsk)` | Resolves `games/testdisk2.dsk` from `/disks/sdmap.txt` over SPI0, mounts `:3`. | "Without leaving LS-DOS, `SDMOUNT` reads `sdmap.txt` directly over hardware SPI0 using `FNBOOT`'s mailbox and hot-mounts Drive `:3`." |
| **16** | `DIR :3` | Lists `SDTEST2` directly from Local MicroSD Drive `:3`. | "Drive `:3` is immediately live with synchronized DCT geometry." |

### Part 5: Hybrid Cross-Tier Read/Write (`UART1` -> `SPI0 CMD24` RMW)

| Step | Command to Enter | What Happens On Screen | Talking Point / Narration Cue |
| :---: | :--- | :--- | :--- |
| **17** | `COPY FNSTATUS/CMD:0 TO :1` | Copies `FNSTATUS/CMD` from FujiNet `:0` to Local MicroSD `:1`. | "Now for hybrid cross-tier I/O: standard LS-DOS `COPY` reads sectors over UART1 from FujiNet and writes 512-byte Read-Modify-Write blocks to Local SD via SPI0 `CMD24`." |
| **18** | `DIR :1` | Shows `FNSTATUS/CMD` now alongside `HELLO/CMD` on `:1`. | "The file is committed directly into the FAT32-backed `.dsk` image on the Agon's microSD card." |
| **19** | `FNSTATUS:1` | Executes `FNSTATUS/CMD` straight from Local SD `:1`. | "And it executes cleanly right off Local MicroSD `:1`, querying FujiNet live." |

### Part 6: Model 4 Disk BASIC Persistence & Write-Protect Enforcement

| Step | Command to Enter | What Happens On Screen | Talking Point / Narration Cue |
| :---: | :--- | :--- | :--- |
| **20** | `BASIC` | Loads Model 4 Disk BASIC to `Ready`. | "Let's verify interactive BASIC file creation and write protection." |
| **21** | `10 PRINT "FUJINET + AGON SD WRITE OK!"` + `SAVE "SDTEST/BAS:1"` | Enters and saves a BASIC program onto Local MicroSD `:1`. | "Saving a BASIC program updates the directory, HIT, GAT, and data sectors on `:1` via SPI0 `CMD24` RMW." |
| **22** | `NEW` -> `LOAD "SDTEST/BAS:1"` -> `RUN` | Clears RAM, reloads from `:1`, prints `FUJINET + AGON SD WRITE OK!`. | "Reloading after `NEW` proves the file persisted cleanly to Local MicroSD." |
| **23** | `SAVE "BLOCKED/BAS:2"` | Fails immediately with `Disk write protected` (Error 15). | "Trying to save to our Read-Only Drive `:2` is blocked by the driver's DCT write-protect check with zero SD writes." |
| **24** | `SYSTEM` | Returns cleanly to LS-DOS `DOS Ready`. | "Clean exit back to LS-DOS Ready." |

### Optional Finale (Manual Reboot to MOS)
Press **`Cmd+R`** (or restart emulator) back to the MOS `*` prompt and run:
```text
trs80 dir /disks/base/testdisk1.dsk
trs80 check /disks/base/testdisk1.dsk
```
to show that `FNSTATUS/CMD` and `SDTEST/BAS` persisted across reboot with **0 filesystem errors**!
