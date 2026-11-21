# Phase 4 Implementation Explanation: Pre-Boot Visual Manager, Unified MOS Disk Suite, In-DOS Utilities, and Writable Local MicroSD Driver

This document provides a comprehensive technical explanation of all architectural decisions, hardware SPI0 & UART1 protocols, lowcore SRAM mailbox contracts, utility implementations, bugs discovered and resolved, and live qualification results completed for **Phase 4: Pre-Boot & In-DOS Utility Suites** of the FujiNet TRS-80 integration for the Olimex Agon Light 2.

---

## 1. Executive Summary & Phase 4 Deliverables

Phase 4 completes the end-to-end user experience across both **Quark MOS / Console8 MOS** (pre-boot eZ80 24-bit ADL mode) and **TRSDOS 6.3.1 / LS-DOS 6.3** (runtime Z80 mode), unlocking all three storage operating modes (**Pure Network**, **Pure Local MicroSD**, and **Hybrid Cross-Tier Read/Write**) with zero host-PC dependency.

### Core Deliverables Achieved in Phase 4

1. **Native In-DOS FujiNet Utility Suite (`FN*.CMD`):**
   - Implemented 9 authentic LS-DOS 6.3 `.CMD` utilities (`<= 8`-character basenames, origin `0x2600`) sharing [include/fuji_lib.inc](file:///Users/richardlucente/development/git/fujinet-trs80-agon/include/fuji_lib.inc):
     - `FNSTATUS` ([FNSTATUS.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/fnstatus/FNSTATUS.S)) — queries 140-byte `AdapterConfig` (`0xE8`).
     - `FNHOSTS` ([FNHOSTS.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/fnhosts/FNHOSTS.S)), `FNGETH` ([FNGETH.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/fngeth/FNGETH.S)), `FNSETH` ([FNSETH.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/fnseth/FNSETH.S)), `FNMOUNT` ([FNMOUNT.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/fnmount/FNMOUNT.S)) — full 8-slot host table management (`0xF4`, `0xF3`, `0xF9`) with double-quoted case preservation.
     - `FNDEVS` ([FNDEVS.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/fndevs/FNDEVS.S)), `FNGETDEV` ([FNGETDEV.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/fngetdev/FNGETDEV.S)), `FNSETDEV` ([FNSETDEV.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/fnsetdev/FNSETDEV.S)), `FNMNTIMG` ([FNMNTIMG.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/fnmntimg/FNMNTIMG.S)) — virtual device slot inspection, case-preserving assignment (`0xE2`), and live DriveWire Sector 0 / DCT geometry synchronization (`0xF8`).

2. **Native In-DOS Local MicroSD & Drive Mapping Utilities (`SDMOUNT`, `DRVMAP`, `SETMAP`):**
   - Implemented shared SPI0 raw block reader and `sdmap.txt` stream parser in [include/sd_lib.inc](file:///Users/richardlucente/development/git/fujinet-trs80-agon/include/sd_lib.inc).
   - `SDMOUNT` ([SDMOUNT.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/sdmount/SDMOUNT.S)): authentic LS-DOS parenthesized syntax (`SDMOUNT :d (PATH=...,RO|RW)`, `SDMOUNT :d (INDEX=n)`, `SDMOUNT :d (LBA=hex)`, `SDMOUNT :d (OFF)`, or interactive menu), with live SPI0 Sector 0 probing for Misosys `"DiskDISK"` headers (`dw_offset = 1`) vs raw JV1 (`dw_offset = 0`) and DCT geometry synchronization.
   - `DRVMAP` ([DRVMAP.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/drvmap/DRVMAP.S)): inspects `` `DCT0$` ``..`` `DCT7$` ``, `drv_target_table`, and `sddvr_base_lba`, resolving active Local SD LBAs back to relative paths via `sddvr_map_lba` (`0xF428`).
   - `SETMAP` ([SETMAP.S](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/setmap/SETMAP.S)): diagnostic helper to inspect or manually seed `sddvr_map_lba` / `sddvr_map_len` without `FNBOOT`.

3. **Unified Toolshed-Style MOS Disk Utility Suite (`/bin/trs80.bin`):**
   - Implemented in C ([src/mos/trs80/](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/mos/trs80/main.c)) and compiled with `agondev` (`ez80-none-elf-clang`, 52,461 bytes, eZ80 24-bit ADL @ `0x040000`).
   - Supports `DiskDISK`, `JV1`, `JV3`, `Matthew Reed HDV`, and `Flat HDV` containers across 10 subcommands (`dir`, `copy`, `delete`, `rename`, `format`, `check`, `free`, `dump`, `attr`, `map`).
   - Verified with 100% binary parity against the Python reference tooling via [tools/test_trs80_mos.py](file:///Users/richardlucente/development/git/fujinet-trs80-agon/tools/test_trs80_mos.py) (5/5 tests passing).

4. **Pre-Boot Visual Storage Manager & Launcher (`/bin/fnboot.bin`) + Kernel IPL Mailbox:**
   - Implemented in C ([src/mos/fnboot/](file:///Users/richardlucente/development/git/fujinet-trs80-agon/src/mos/fnboot/main.c)), compiled with `agondev` (28,822 bytes @ `0x040000`).
   - Direct polled UART1 FujiNet client (`IO(UART1_*)` via `<ez80f92.h>`) preserving the active `openstream` TCP link across MOS-to-TRSDOS handover.
   - Automatic `sdmap.txt` discovery (`-m` -> `MAPFILE=` in `/fnboot.ini` -> `./sdmap.txt` -> `/disks/sdmap.txt` -> `/trs80/sdmap.txt` -> `/sdmap.txt`), live recursive SD rescan (`[R]`), `/fnboot.ini` persistence (`[S]`), non-interactive quick boot (`fnboot -b`), and lowcore mailbox staging into `/fnboot.tmp` launched via `OSboot`.
   - Kernel IPL enhancement ([0006-fnboot-mailbox-ipl.patch](file:///Users/richardlucente/development/git/trs-os-zds/patches/0006-fnboot-mailbox-ipl.patch) in `trs-os-zds`): supports Pure Local MicroSD boot when `drv_target_table[0] == 0` and preserves pre-mounted Drive `:1` when `FNBOOT` sets the Alien controller bit (`bit 4` of `` `DCT1$` `` + 4).
   - Verified via automated host mailbox tests in [tools/test_fnboot.py](file:///Users/richardlucente/development/git/fujinet-trs80-agon/tools/test_fnboot.py) (2/2 tests passing).

5. **Writable Local MicroSD Driver (`CMD17` + `CMD24` 512-Byte Read-Modify-Write):**
   - Implemented in [driver-SDDVR.s](file:///Users/richardlucente/development/git/trs-os-zds/TRSDOS_7/SYSRES/drivers/driver-SDDVR.s) and [driver-FDCDVR.s](file:///Users/richardlucente/development/git/trs-os-zds/TRSDOS_7/SYSRES/drivers/driver-FDCDVR.s) ([0007-local-sd-write-rmw.patch](file:///Users/richardlucente/development/git/trs-os-zds/patches/0007-local-sd-write-rmw.patch)).
   - Enables full in-place sector writes (`@WRSEC` / `@WRSSC`) to updateable Local MicroSD `.dsk` images with DCT write-protect enforcement (`bit 7` of `(IY+3)` -> Error 15 `R_PROTECTED`), verified live with hybrid `COPY FNSTATUS/CMD:0 TO :1`, BASIC `SAVE`/`LOAD`/`RUN`, reboot persistence, and 0-error `*trs80 check` structural audits.

---

## 2. Lowcore SRAM Memory Map & Locked Mailbox ABI

To guarantee binary compatibility across `TRSDOS.bin`, `FNBOOT`, `SDMOUNT`, `DRVMAP`, `SETMAP`, and `FNMNTIMG`, all lowcore routing and mailbox tables reside at fixed, verified addresses in Bank 0 (`0x0000`–`0xFFFF`):

| Address Range | Symbol | Size | Purpose |
| :--- | :--- | :--- | :--- |
| `0x0470`–`0x04BF` | `` `DCT0$` ``..`` `DCT7$` `` | 80 bytes (8 * 10B) | LS-DOS Drive Control Tables (`+0` = `0xC3` active / `0xC9` offline; `+3` bit 7 = WP; `+4` bit 4 = Alien pre-mounted flag). |
| `0x0E58` | `FDCDVR` | Entry point | Central storage driver router (`driver-FDCDVR.s`). |
| `0x0E76`–`0x0E7D` | `drv_target_table` | 8 bytes | Per-drive storage backend (`0` = Local SD, `1` = DriveWire, `2` = MEMDISK). |
| `0x0F78`–`0x0F7F` | `dw_offset` | 8 bytes | Per-drive container sector offset (`1` = Misosys DiskDISK +256B header, `0` = raw JV1). |
| `0xF1FF` | `` `COREEND$` `` (`` `zHIGH$` ``) | Boundary | Top of LS-DOS user TPA (lowered from `0xF3FF` by 512 bytes to protect RMW buffer and write driver). |
| `0xF200`–`0xF2FF` | `sd_rmw_buf` | 256 bytes | Sibling sub-sector buffer for 512-byte SPI0 Read-Modify-Write. |
| `0xF300`–`0xF3D6` | `sddvr_write_sector` | 215 bytes | SPI0 `CMD17`+`CMD24` RMW write driver (`41` bytes headroom before `0xF400`). |
| `0xF400` | `sddvr_inited` | 1 byte | SPI0 hardware initialization flag (`0` = uninitialized, `1` = initialized). |
| `0xF408`–`0xF427` | `sddvr_base_lba` | 32 bytes (8 * 4B LE) | 32-bit physical SD card starting LBA for logical drives `:0`..`:7`. |
| `0xF428`–`0xF42B` | `sddvr_map_lba` | 4 bytes LE | 32-bit physical SD card starting LBA of active `sdmap.txt`. |
| `0xF42C`–`0xF42D` | `sddvr_map_len` | 2 bytes LE | Byte length of active `sdmap.txt`. |
| `0xF448`–`0xF5B4` | `sddvr_init` / `sddvr_read_sector` | 365 bytes | SPI0 init, shared command/transfer helpers, and `CMD17` block reader (`64` bytes headroom before `0xF5F5`). |
| `0xF600`–`0xFFFF` | `INTERRUPT.handler` / Video | 2,560 bytes | eZ80 Mode 2 interrupt vector table, Timer 1 ISR, reboot trampoline, and 1,920-byte video RAM. |

Keeping `DOWRIT` in [driver-FDCDVR.s](file:///Users/richardlucente/development/git/trs-os-zds/TRSDOS_7/SYSRES/drivers/driver-FDCDVR.s) at its exact 20-byte footprint preserved `dw_offset` at `0x0F78` with zero symbol drift across the entire kernel.

---

## 3. Writable Local MicroSD Architecture (`CMD17` + `CMD24` RMW)

### 3.1 256-Byte Sub-Sector Mapping & 256-Byte RMW Buffer Optimization

Physical SDHC/SDXC cards address 512-byte blocks, whereas LS-DOS 6.3 operates on 256-byte logical sectors:

- `Effective LSN = LSN + dw_offset[drive]`
- `SD Block LBA  = sddvr_base_lba[drive] + (Effective LSN / 2)`
- `Sub-sector    = Effective LSN & 1` (`0` = lower 256 bytes, `1` = upper 256 bytes)

Rather than allocating a full 512-byte scratch buffer in precious Bank 0 high memory, `sddvr_write_sector` leverages the caller's existing 256-byte source buffer `(HL)` and buffers only the **sibling** 256-byte half in `sd_rmw_buf` (`0xF200`–`0xF2FF`):

1. **Write-Protect Check:** Tests `bit 7, (IY+3)` in the active DCT. If set, restores stack and returns `A = 15` (`R_PROTECTED`, `NZ`) with zero SPI bus activity.
2. **Phase 1 (`CMD17` Sibling Read):** Reads the target 512-byte SD block:
   - If writing sub-sector `0` (lower half): discards lower 256 bytes, reads upper 256 bytes into `sd_rmw_buf`.
   - If writing sub-sector `1` (upper half): reads lower 256 bytes into `sd_rmw_buf`, discards upper 256 bytes.
   - Discards 2-byte CRC and deasserts `/CS`.
3. **Phase 2 (`CMD24` Merged Write):** Reuses `sd_cmd_buf+1..4` (holding the identical 32-bit big-endian LBA):
   - Sends `CMD24` (`0x58`), waits for `R1 = 0x00`, sends 1 idle `0xFF` byte (`Nwr >= 1`), sends Data Start Token `0xFE`.
   - Streams 512 bytes in order: `[caller (HL), sd_rmw_buf]` for sub-sector `0`, or `[sd_rmw_buf, caller (HL)]` for sub-sector `1`.
   - Sends 2 dummy CRC bytes (`0xFF`, `0xFF`), validates Data Response Token (`(resp & 0x1F) == 0x05`), polls MISO until flash busy clears (`0xFF`), and deasserts `/CS`.
4. **LS-DOS Return Contract:** Restores `DE` (track/sector), `BC`, and `HL`, leaving `IX` and `IY` untouched throughout, and returns `A = 0` (`Z` set) with `EI`. This satisfies LS-DOS `zDIRWR`, `zGATWR`, and `zHITWR`, which immediately follow `CALL zWRSSC` with `CALL Z, zVRSEC` (`VERIFY`) expecting `A = 6` on Track 17.

### 3.2 Dual Compatibility: `fab-agon-emulator` vs Real Olimex Agon Light 2 Hardware

Empirical testing against `fab-agon-emulator`'s `agon-ez80-emulator/src/spi_sdcard.rs` uncovered two subtle SPI timing differences compared to physical SDHC cards, both handled cleanly in `driver-SDDVR.s` and `include/sd_lib.inc`:

| Protocol Event | Real SDHC Hardware (`MISO`) | `fab-agon-emulator` (`spi_sdcard.rs`) | Driver Handling |
| :--- | :--- | :--- | :--- |
| **`CMD17` / `CMD24` `R1` Response** | Returns `0xFF` on byte 6 (CRC); returns `R1 = 0x00` on subsequent `0xFF` kick (`Ncr`). | Immediately queues and returns `R1 = 0x00` on byte 6 (CRC). | Check `bit 7, a` right after byte 6 before entering the `spi_kick` poll loop. |
| **`CMD24` Data Response Token (`0x05` / `0xE5`)** | Returns `0xFF` on data byte 512 and both CRC bytes; returns `0xE5` on the kick after CRC byte 2. | Writes block on data byte 512 and queues `[0x05, 0x01]`, returning `0x05` on data byte 512 and `0x01` on CRC byte 1. | Capture MISO from data byte 512 into `C`, drain both CRC bytes (flushing `0x01`), then inspect `C`, CRC byte 2, or post-CRC kicks for `(resp & 0x1F) == 0x05`. |

---

## 4. Quark MOS / Console8 MOS Pre-Boot Tooling (`*trs80` & `FNBOOT`)

### 4.1 Console8 MOS `/bin/` ADL Memory Architecture

Console8 MOS searches both `/mos/` (linked at `0x0B0000`, strict 32 KiB limit) and `/bin/` (linked at `0x040000`, 448 KiB contiguous ADL RAM). Linking both `trs80.bin` (52,461 bytes) and `fnboot.bin` (28,822 bytes) at `0x040000` (`RAM_SIZE=0x070000`) and installing them in `/bin/` allows direct star-command invocation (`trs80 ...`, `fnboot ...`) from any directory while leaving `0x0B0000` completely free for `OSboot.bin` handover.

### 4.2 Unified `*trs80` Disk & File Utility Suite (`src/mos/trs80/`)

Modeled on Toolshed's comma syntax (`<image.dsk>,<guest_file>`), `*trs80` encapsulates:
- **Container Abstraction (`disk.c`):** Automatic detection and sector translation for `DiskDISK` (`+256B` header + DCT), `JV1` (raw linear), `JV3` (`0x2200` sector descriptor array), `Matthew Reed HDV` (`+256B` header), and `Flat HDV`.
- **Authentic LS-DOS 6.3 Engine (`lsdos.c`):** Exact Track 17 GAT/HIT/FPDE/FXDE parsing, 8-bit rotating HIT hash generation, cross-cylinder multi-granule extent traversal, active FPDE bit `0x10` validation, bidirectional file copy with `-l` CR/LF conversion, 100% contiguous FAT32 image formatting (`format`), and full structural filesystem auditing (`check`).
- **Recursive FAT32 LBA Mapper (`map.c`):** Uses MOS `mos_getfil()` / `FATFS` (`database + (sclust - 2) * csize`) with contiguity verification across cluster chains to emit hierarchical `sdmap.txt` catalogs (e.g. `disks/base/testdisk1.dsk,0x00001FEA`).

### 4.3 `FNBOOT` Pre-Boot Visual Manager & Mailbox Stager (`src/mos/fnboot/`)

- **Direct Polled UART1 FujiNet Client (`uart_fuji.c`):** Uses official AgonDev `<ez80f92.h>` `IO(port)` (`address_space(3)`) instructions rather than `mos_uopen`/`mos_uclose` so the active ESP8266 / BoIP `openstream` TCP bridge is never torn down. Matches the exact `OP_FUJI` (`0xE2`) framing from [include/fuji_lib.inc](file:///Users/richardlucente/development/git/fujinet-trs80-agon/include/fuji_lib.inc).
- **Mailbox Stager & `OSboot` Handover (`boot.c`):** Loads `/TRSDOS.bin` (66,048 bytes), probes each assigned Local SD image's first 256 bytes for `"DiskDISK"` headers and DCT parameters, populates `sddvr_map_lba`, `sddvr_map_len`, `drv_target_table`, `dw_offset`, `sddvr_base_lba`, and `` `DCT0$` ``..`` `DCT7$` `` directly in the staged binary, writes `/fnboot.tmp`, and hands off to `OSboot -<color>[v][f] /fnboot.tmp`.
- **IPL Coordination ([0006-fnboot-mailbox-ipl.patch](file:///Users/richardlucente/development/git/trs-os-zds/patches/0006-fnboot-mailbox-ipl.patch)):** On boot, `ipl-BIOS.s` inspects `drv_target_table[0]`: when `0`, it reads raw LSN 0 of Drive `:0` via `FDCDVR`/`SDDVR` for 100% offline Pure Local MicroSD booting; it also checks `bit 4` of `` `DCT1$` `` + 4 so pre-mounted Drive `:1` survives IPL intact while preserving legacy `OSboot TRSDOS.bin` behavior.

---

## 5. Notable Bugs Discovered & Resolved During Phase 4

1. **LS-DOS `IX` Preservation Across Disk Driver Calls (`driver-SDDVR.s`):** LS-DOS 6.3 retains the active FCB pointer in `IX` across `FDCDVR` calls (`IOFUNC` does not save `IX`). Refactoring `driver-SDDVR.s` to avoid `IX` eliminated `"Record number out of range"` and system lockups when executing `.CMD` files from Local SD.
2. **SPI0 Register & `sd_lib.inc` Fixes (`equ.inc`, `sd_lib.inc`, `SETMAP.S`):** Corrected `SPI_SR` (`0xBB`, not `0xBD`), fixed `case_cmp` clobbering the `Z` flag via `pop af` after `cp c`, preserved `BC`/`DE` across `map_get_char` in `map_get_line`, and added `dw_offset` translation in `driver-SDDVR.s` for DiskDISK images.
3. **`ez80-none-elf-clang` I/O Legalization (`uart_fuji.c`):** Replaced inline `in`/`out` assembly (which triggered an LLVM GlobalISel `G_ANYEXT` backend crash) with official `<ez80f92.h>` `IO(port)` `address_space(3)` accessors.
4. **`OP_FUJI` `GET_RESPONSE` Wire Framing (`uart_fuji.c`):** Aligned `fuji_get_response` to the exact 2-byte `[0xE2, 0x01]` command sequence used by `fuji_lib.inc` (removing stray length bytes and intermediate `POLL` calls).
5. **`DOWRIT` Register Clobbering (`driver-FDCDVR.s`):** `get_drv_target` previously ran before saving `DE`/`HL` and before `calcsec`, clobbering track/sector and buffer pointers on write calls. Fixed inside the exact 20-byte `DOWRIT` footprint.
6. **`CMD24` Byte-512 Response Timing (`driver-SDDVR.s`):** Captured the MISO byte on data byte 512 and drained CRC byte 1 (`0x01`) so both `fab-agon-emulator` and physical SD cards pass Data Response Token verification cleanly.

---

## 6. Verification & Qualification Results

### 6.1 Automated Host & Cross-Build Unit Tests (`make test`)
All 4 automated suites (**17/17 tests**) pass with zero warnings:
- [test_bin_to_cmd.py](file:///Users/richardlucente/development/git/fujinet-trs80-agon/tools/test_bin_to_cmd.py): **5/5 OK**
- [test_includes.py](file:///Users/richardlucente/development/git/fujinet-trs80-agon/tools/test_includes.py): **5/5 OK**
- [test_trs80_mos.py](file:///Users/richardlucente/development/git/fujinet-trs80-agon/tools/test_trs80_mos.py): **5/5 OK** (`DiskDISK`, `JV1`, `JV3`, `Reed HDV`, extraction parity, format/inject/clone/rename/attr/delete, recursive `map`)
- [test_fnboot.py](file:///Users/richardlucente/development/git/fujinet-trs80-agon/tools/test_fnboot.py): **2/2 OK** (hybrid mailbox staging + Pure Local SD Drive `:0` boot staging)

### 6.2 Live Emulation Qualification Matrix

| Test ID | Verification Executed Live in `fab-agon-emulator` | Result |
| :--- | :--- | :--- |
| **TC-07** | `FNSTATUS` from Drive `:0` and copied onto Local SD `:1` (`FNSTATUS:1`). | **PASS** |
| **TC-08** | `FNHOSTS`, `FNGETH`, `FNSETH` (quoted lowercase), and `FNMOUNT`. | **PASS** |
| **TC-09** | `FNDEVS` and `FNGETDEV` querying 4 Stage 1 virtual slots (152 bytes). | **PASS** |
| **TC-10** | `FNSETDEV` with quoted mixed/lowercase path verified case-preserved on FujiNet. | **PASS** |
| **TC-11** | `FNMNTIMG` live DCT synchronization and immediate `DIR` access. | **PASS** |
| **TC-17** | `SDMOUNT`, `DRVMAP`, `FNBOOT` pre-mount, Pure Local SD boot (`:0`), BASIC `SAVE`/`LOAD`/`RUN` on RW `:1`, `Disk write protected` rejection on RO `:2`, reboot persistence, and `trs80 check` reporting 0 errors. | **PASS** |
| **TC-18** | Live MOS execution of `trs80 dir`, `copy`, `format`, `check`, `free`, `dump`, `rename`, `attr`, `delete`, and `map` on Agon FAT32. | **PASS** |
| **TC-19** | Hybrid cross-tier `COPY FNSTATUS/CMD:0 TO :1` (FujiNet UART1 -> Local SD SPI0 `CMD24` RMW), verified via `DIR :1`, live `FNSTATUS:1` execution, reboot persistence, and `trs80 check` 0-error audit. | **PASS** |
