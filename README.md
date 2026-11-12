# fujinet-trs80-agon

**FujiNet Network Mass Storage and Utility Integration for TRS-80 on Agon Light 2**

This project provides the architecture, driver specifications, and native utility suite to integrate the **[FujiNet](https://fujinet.online)** peripheral framework with the **TRS-80 Model 4 environment** (TRSDOS 6.3.1 / LS-DOS 6.3 adapted for the Zilog eZ80 on the Olimex Agon Light 2).

It replaces the legacy, custom serial protocol from [`trs-net-tcp`](https://github.com/rlucente-retro/trs-net-tcp) with the standard binary **DriveWire 3/4** protocol and **FujiNet (`OP_FUJI`)** transaction architecture, enabling dynamic mounting of local images and remote TNFS shares across virtual disk drives.

---

## Key Features & Multi-Tier Storage Architecture

* **Pure FujiNet Client & Network Booting:**
  * **Network-Booted Drive `:0`:** Replaces the legacy embedded in-memory RAM disk with direct DriveWire booting. Drive `:0` boots standard LS-DOS 6.3.1 system disk images directly from FujiNet Device Slot 0 (via remote TNFS shares, internet archives, or the FujiNet peripheral's SD card).
  * **Lean System Image:** Eliminating the embedded 425 KB RAM disk image shrinks `TRSDOS.bin` from 480 KiB down to a lean **~64 KiB** kernel/HAL binary (`0x000000`–`0x00FFFF`), leaving **~448 KiB of external SRAM free** for high-speed sector caches, RAM disks, or user workspaces.
  * **100% Persistent System Storage:** System configuration changes, `AUTO` startup scripts, and newly saved files persist directly to the mounted disk image on the FujiNet host.
* **Updateable Local Agon SD Card Disk Support:**
  * **Direct SPI0 Hardware Driver (`driver-SDDVR.S`):** Mounts `.dsk` disk images residing on the Agon Light 2's on-board microSD card (FAT32 filesystem) directly to TRSDOS drives via hardware SPI0 block I/O.
  * **Updateable Read/Write Media:** Real-time, in-place sector read/write commitments (1 SD block = 2 TRSDOS sectors) without requiring external dongles or networking.
* **Historical Expanded Memory & Authentic `MEMDISK` (128K – 512K):**
  * **Harnessing Freed SRAM:** Emulates historical Model 4 expanded memory architectures (Tandy 128K Bank 1 and Dave Huffman / Anitek SuperMEM 512K) using the Agon's ~448 KiB of freed external SRAM (Banks 1–7: `0x010000`–`0x07FFFF`).
  * **Authentic High-Speed `MEMDISK`:** Direct 24-bit eZ80 memory block transfers (`LDIR.L`) achieve 256-byte sector copy times of **27.7 µs** (512 cycles @ 18.432 MHz), providing an ultra-fast in-memory RAM disk fully compatible with LS-DOS and CP/M.
  * **Authentic `@BANK` Supervisor Call (`SVC 102`):** Implements historical 32 KB window switching (`0x8000`–`0xFFFF`) in the HAL, enabling full backward compatibility for expanded memory applications like *LeScript*.
* **Three Flexible Operating Modes:**
  1. **Mode 1: Pure Network Boot (Diskless Cloud Client):** Drives `:0` through `:7` are hosted remotely on FujiNet / TNFS. The Agon's local microSD card acts strictly as a "Boot ROM" holding MOS and the 64 KB `TRSDOS.bin` kernel.
  2. **Mode 2: Pure Local Boot (Standalone / Offline Workstation):** Drives `:0` through `:3` (or up to `:7`) are mounted from local `.dsk` files on the Agon microSD card via SPI0. Runs 100% self-contained with no network or dongles required.
  3. **Mode 3: Hybrid Operation (Cross-Tier Interoperability):** Mix local SD, remote network, and high SRAM drives (e.g. Drive `:0` booted over TNFS, Drive `:1` mapped to a local updateable SD image, Drive `:4` mapped to high SRAM `MEMDISK`). Standard TRSDOS commands operate transparently across tiers (e.g. `COPY FILE:0 TO :1` downloads a cloud file directly to the local SD card; `BACKUP :0 TO :1` clones a remote network disk to local flash).
* **Pre-Boot & In-DOS Tool Suites:**
  * **Pre-Boot Visual Manager (`FNBOOT` in Quark MOS):** Interactive MOS visual launcher to query FujiNet status, browse remote TNFS directories and local SD images, assign drives `:0`–`:7`, and launch TRSDOS / LS-DOS 6.3.1.
  * **MOS FAT32 <-> TRSDOS Interchange Tools:** Native MOS utilities (`*TRSDIR`, `*TRSEXT`, `*TRSADD`, `*TRSDEL`, `*TRSNEW`) to inspect, extract, inject, and create TRSDOS disk images directly on the Agon FAT32 filesystem without needing a PC.
  * **Native TRSDOS In-DOS Utilities:** Full suite of <= 8-character LS-DOS commands (`FNSTATUS`, `FNHOSTS`, `FNMOUNT`, `FNDEVS`, `FNSETDEV`, `FNMNTIMG`, `DRVMAP`, `SDMOUNT`, `MEMDISK`) for inspecting and hot-swapping drives from the `TRSDOS Ready` prompt.
* **Universal Disk Image Support:**
  * **JV1:** Flat 256-byte sector layout (LSN * 256), with geometry derived dynamically (Directory Cylinder 17 / GAT).
  * **DiskDISK:** 256-byte header containing authentic DCT metadata; the driver accounts for the 1-sector (+256B) header offset via dynamic format detection (`dw_offset`).
  * **HDV Hard Disks:** High-capacity images. Volumes <= 4 MiB supported through the floppy driver; larger volumes supported via dedicated hard disk partitions.
* **Staged Integrity & Safety-First Deployment:**
  * **Stage 1 (Stock Firmware Baseline):** Up to 4 virtual network disk slots (FujiNet Device Slots 0–3, mapped to Drives `:0`–`:3` in network mode, or secondary drives in hybrid mode) operate in verified Read-Only mode (`WP`) on stock firmware (`MAX_DWDISK_DEVICES = 4`) to protect host media while upstream firmware write routines are audited.
  * **Stage 2 (Patched Firmware Expansion):** Recompiled firmware with `MAX_DWDISK_DEVICES = 8` unlocks all 8 device slots (Slots 0–7, supporting all 8 TRSDOS drives `:0` through `:7`), with validated write support enabled.
* **Dual Physical Transports:**
  * **Direct Serial Link (Olimex MOD-USB-RS232):** Inherent, always-on point-to-point hardware connection on UEXT (`PC0`/`PC1`); bypasses `openstream` and boots directly from MOS.
  * **Wi-Fi TCP/IP Streaming (Olimex MOD-WIFI-ESP8266):** Uses `openstream` -> `OSboot.bin` -> `TRSDOS.bin` to place the Wi-Fi module in transparent passthrough mode (`CIPMODE=1`).

---

## Documentation

* **Phase 1 Implementation & Technical Explanation:** [`PHASE1-IMPLEMENTATION-EXPLANATION.md`](PHASE1-IMPLEMENTATION-EXPLANATION.md) — Comprehensive technical explanation of Phase 1 deliverables, including lean kernel architecture (~64 KB), DriveWire `OP_READEX` network IPL, VDP cursor suppression, display loop hang resolution, and authentic Model 4 Break & Ctrl-C handling.
* **Phase 2 Implementation & Technical Explanation:** [`PHASE2-IMPLEMENTATION-EXPLANATION.md`](PHASE2-IMPLEMENTATION-EXPLANATION.md) — Comprehensive technical explanation of Phase 2 deliverables, including the multi-tier storage router in `driver-FDCDVR.S`, modular DriveWire driver `driver-DWDVR.s`, local microSD SPI0 block driver `driver-SDDVR.s`, memory map budgeting, and empirical verification.
* **Comprehensive Architectural & Integration Specification:** [`FUJINET-TRS80-INTEGRATION-PLAN.md`](FUJINET-TRS80-INTEGRATION-PLAN.md) — Authoritative technical specification covering the three-layer execution model, boot chain, wire protocols, assembly driver modifications, local SD card block drivers, the native TRSDOS/MOS utility suites, hardware constraints, and phased implementation milestones.
* **Disk & Hard Drive Image Format Analysis:** [`DISK-IMAGE-FORMAT-ANALYSIS.md`](DISK-IMAGE-FORMAT-ANALYSIS.md) — Binary format specifications and addressing mechanics for JV1, DiskDISK, JV3, DMK, and Reed HDV images, detailing DriveWire compatibility and geometry extraction.
* **Agon OSboot Bootstrap Loader:** [Shawn Sijnstra's OSboot](https://github.com/sijnstra/agon-projects/tree/main/OSboot) — MOS bootstrap utility for booting `TRSDOS.bin` while preserving UART1 streaming connections.

---

## System Architecture

```text
+-------------------------------------------------------------------------------+
|                             AGON LIGHT 2 (CLIENT)                             |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  |      TRS-80 Model 4 Environment (TRSDOS 6.3.1 / LS-DOS 6.3 on eZ80)     |  |
|  |                                                                         |  |
|  |  +-------------------------------------------------------------------+  |  |
|  |  |         Storage Driver Router (driver-FDCDVR.S)                   |  |  |
|  |  |         Unified Logical Drives :0 through :7                      |  |  |
|  |  +--------------------+-------------------------+--------------------+  |  |
|  |                       |                         |                       |  |
|  |                       v                         v                       |  |
|  |       +-------------------------------+   +---------------------------+ |  |
|  |       |   Local SD Block Driver       |   |  DriveWire Block Driver   | |  |
|  |       |       driver-SDDVR.S          |   |      driver-DWDVR.S       | |  |
|  |       |   (Direct eZ80 SPI0 I/O)      |   |  (UART1 DriveWire Bus)    | |  |
|  |       |  Local Updateable .DSK Files  |   | Drives :0..:7 (Net Slots) | |  |
|  |       +---------------+---------------+   +-------------+-------------+ |  |
|  |                       |                                 | OP_READEX/WR  |  |
|  |  +--------------------+----------+                      | (0xD2 / 0x57) |  |
|  |  | Native In-DOS Utilities       |                      | 24b LSN, CRC16|  |
|  |  | (FN*.CMD, DRVMAP, SDMOUNT)    |                      |               |  |
|  |  | Basenames <= 8 Characters     |                      |               |  |
|  |  +----------------+--------------+                      |               |  |
|  |                   |                                     |               |  |
|  |                   | OP_FUJI (0xE2)                      |               |  |
|  |                   +------------------+------------------+               |  |
|  |                                      |                                  |  |
|  |                                      v                                  |  |
|  |  +-------------------------------------------------------------------+  |  |
|  |  |    driver-UART1.S: Explicit 115200 8-N-1 I/O (IER=0, FCTL=07h)    |  |  |
|  |  +-----------------------------------+-------------------------------+  |  |
|  +--------------------------------------|----------------------------------+  |
|              | eZ80 SPI0                | UART1 (PC0/PC1 on UEXT Connector)   |
|              v (PB1-PB4)                |                                     |
|     +------------------+   +------------+------------+                        |
|     | Agon MicroSD     |   |                         |                        |
|     | (Local FAT32     |   v Option A: Direct Serial v Option B: Wi-Fi        |
|     |  .DSK Files)     |   +-----------------------+ +----------------------+ |
|     |                  |   | Olimex MOD-USB-RS232  | | Olimex MOD-WIFI-ESP  | |
|     | * Local boot/data|   | * Always-on link      | | * MOS: openstream    | |
|     | * Updateable RW  |   | * Direct MOS: FNBOOT  | | * CIPMODE=1 stream   | |
|     +------------------+   +-----------+-----------+ +----------+-----------+ |
+----------------------------------------|------------------------|-------------+
                                         |                        |              
                                         | USB CDC-ACM / RS-232   | Wi-Fi (TCP)  
                                         v                        v              
+-------------------------------------------------------------------------------+
|                       FUJINET HOST (SERVER / PERIPHERAL)                      |
|                                                                               |
|   +---------------+---------------------------------------+---------------+   |
|   |  UARTChannel / ACMChannel (Serial)    OR    BoIPChannel (TCP:65504)   |   |
|   +---------------+-------------------+-------------------+---------------+   |
|                                       |                                       |
|                                       v                                       |
|   +-----------------------------------------------------------------------+   |
|   |                      BUILD_COCO / DriveWire Bus                       |   |
|   |                   (lib/bus/drivewire/drivewire.cpp)                   |   |
|   +---------------+---------------------------------------+---------------+   |
|                   |                                       |                   |
|     OP_READEX/WR  |                                       | OP_FUJI (0xE2)    |
|                   v                                       v                   |
|   +-------------------------------+       +-------------------------------+   |
|   |         drivewireDisk         |       |         drivewireFuji         |   |
|   |   (Universal Media Handler)   |       |    (FujiNet Config Engine)    |   |
|   |  - JV1 Disks (Flat 256B)      |       |  - 8 Host Slots (TNFS / SD)   |   |
|   |  - DiskDISK (+256B Offset)    |       |  - 8 Device Slots (Slots 0-7) |   |
|   |  - HDV Hard Disk (4 MiB max)  |       |  - Remote Directory Browser   |   |
|   +-------------------------------+       +-------------------------------+   |
+-------------------------------------------------------------------------------+
```

---

## Operating Lifecycle

The system supports two physical transports:

### Option A: Direct Serial Link (e.g., Olimex MOD-USB-RS232)
* **Inherent Hardware Link:** Connected to a host PC running FujiNet-PC or a hardware FujiNet unit, the connection is inherently active upon reset.
* **Direct Boot via MOS:** Quark MOS launches the pre-boot manager or OS directly:
  ```text
  *FNBOOT
  ```
  or for direct fast boot:
  ```text
  *OSboot -2f TRSDOS.bin
  ```

### Option B: Wi-Fi TCP/IP Streaming (e.g., Olimex MOD-WIFI-ESP8266)
1. **Pre-Boot Handshake (`openstream`):**
   * Before booting TRSDOS, Quark MOS runs `openstream <fujinet_host> 65504` to connect the ESP8266 to FujiNet's BoIP server (`port 65504`) and enter transparent UART passthrough mode (`CIPMODE=1`).
   * `fujinet-firmware` accepts the connection and remains **completely silent**, transmitting zero bytes until the client sends its first command.
2. **Boot TRSDOS (`FNBOOT` / `OSboot`):**
   * Transmits execution to `TRSDOS.bin`, preserving the active transparent stream on UART1.
3. **Session Teardown (`closestream`):**
   * If exiting back to MOS, `closestream` sends `+++` with guard times (>20 ms) and `AT+CIPCLOSE` to restore AT command mode.

---

## Pre-Boot and In-DOS Utilities

### Pre-Boot Visual Manager & MOS File Interchange
* **`FNBOOT`:** Interactive full-screen MOS application that queries FujiNet adapter status, enumerates TNFS host slots and local Agon SD images, assigns disk images to Drives `:0` through `:7`, and launches `TRSDOS.bin`.
* **`*TRSDIR <image.dsk>`:** Inspects the Directory Cylinder (Track 17) of a local `.dsk` image from Quark MOS, listing files, sizes, and extents.
* **`*TRSEXT <image.dsk> <trsdos_file> [fat_file]`:** Extracts a file from inside a TRSDOS disk image to the native FAT32 filesystem.
* **`*TRSADD <image.dsk> <fat_file> [trsdos_file]`:** Injects a FAT32 file into a TRSDOS disk image, updating GAT, HIT, and FXDE records.
* **`*TRSNEW <image.dsk> [cyls] [sides] [density]`:** Formats a fresh, authentic LS-DOS 6.3 disk image file directly on the Agon FAT32 card.

### Native TRSDOS Runtime Command Utilities
Conforming to the LS-DOS 6.x constraint of **<= 8-character basenames**:

| Utility | Syntax | Purpose |
| :--- | :--- | :--- |
| `FNSTATUS` | `FNSTATUS` | Displays FujiNet firmware version, IP address, MAC, and external network state. |
| `FNHOSTS` | `FNHOSTS` | Lists all 8 configured host slots (0–7) stored on the FujiNet device. |
| `FNGETH` | `FNGETH <slot>` | Displays the hostname/URL assigned to a specific FujiNet host slot (0–7). |
| `FNSETH` | `FNSETH <slot> "<hostname>"` | Configures remote hostname or TNFS URL for a given FujiNet host slot. |
| `FNMOUNT` | `FNMOUNT <slot>` | Commands FujiNet to mount a host slot (connect to remote TNFS server or SD card). |
| `FNDEVS` | `FNDEVS` | Queries virtual device slots (0–3 in Stage 1; 0–7 in Stage 2), listing mounted images. |
| `FNGETDEV` | `FNGETDEV <slot>` | Displays the pathname of the disk image mounted in a FujiNet device slot. |
| `FNSETDEV` | `FNSETDEV <s> <h> <m> "<p>"` | Assigns disk image on a host slot to a device slot (quotes preserve case). |
| `FNMNTIMG` | `FNMNTIMG <slot> [mode]` | Mounts the disk image for active I/O and synchronizes guest DCT geometry. |
| `DRVMAP` | `DRVMAP` | Displays unified drive mapping across all 8 logical drives (Local SD vs FujiNet). |
| `SDMOUNT` | `SDMOUNT <drv> "<path>"` | Mounts or swaps a local Agon SD card `.dsk` image into a logical drive. |

---

## Shared Assembly Includes (`include/`)

Standardized include files for developing authentic LS-DOS 6.x `.CMD` command utilities and drivers on the Olimex Agon Light 2:

* **[`include/svc.inc`](include/svc.inc):** TRSDOS 6.3 / LS-DOS 6.3 Supervisor Call (SVC) equates (`SVC_IPL` through `SVC_CKBRKC`) and dual-syntax invocation macros (`_DSPLY` / `zDSPLY`, `_KEY` / `zKEY`, `_EXIT` / `zEXIT`, `_ABORT`, `_ERROR`, `_PARAM`, `_FSPEC`, `_BANK`, `_CLS`, etc.) supporting ZDS II and AgonDev assemblers.
* **[`include/equ.inc`](include/equ.inc):** Lowcore memory map addresses (`INBUF$`, `DCT0$`–`DCT7$`, `LDRV$`, `TIME$`, `DATE$`, `BUR$`, `BAR$`, `LBANK$`), Agon storage router dispatch table (`drv_target_table`, `sddvr_base_lba`), ASCII control codes, standard LS-DOS error codes (`LSERR_NONE` through `LSERR_SVC`), and eZ80 UART1 hardware registers.
* **[`include/fujinet.inc`](include/fujinet.inc):** DriveWire and FujiNet bus protocol opcodes (`OP_FUJI`, `OP_READEX`, `OP_WRITE`, `OP_TIME`), transaction commands (`FUJI_CMD_POLL`, `FUJI_CMD_GET_ADAPTER_CFG`, `FUJI_CMD_GET_DEV_SLOTS`, `FUJI_CMD_GET_HOST_SLOTS`, `FUJI_CMD_MOUNT_IMAGE`, etc.), data structure field offsets (140-byte `AdapterConfig`, 256-byte Host Slot table, 38-byte Device Slot entries), and transaction status codes (`FUJI_STAT_OK = 0x01`).

---

## Development & Verification Tools (`tools/`)

The repository includes a standalone Python diagnostic and verification suite for testing and inspecting TRSDOS/LS-DOS disk images and network booting:

* **[`tools/audit_filesystem.py`](tools/audit_filesystem.py):** Comprehensive filesystem auditor that parses the Track 17 Directory Cylinder, validates Granule Allocation Tables (GAT), Hash Index Tables (HIT), and File Extended Directory Entries (FXDE), and reports disk geometry and file extents.
* **[`tools/verify_disk_image.py`](tools/verify_disk_image.py):** Validates raw JV1 and Misosys DiskDISK container structures, confirming physical-to-logical sector mappings, authentic DCT headers, and checksum validity.
* **[`tools/verify_all_files.py`](tools/verify_all_files.py):** Traverses all directory entries on a disk image to verify end-of-file (EOF) bounds, sector allocations, and directory record integrity.
* **[`tools/bin_to_cmd.py`](tools/bin_to_cmd.py):** Authentic TRSDOS / LS-DOS 6.x executable command packager converting flat binary outputs into Load Module Format (`/CMD`) with Record Type 01 (load blocks up to 256 bytes) and Record Type 02 (execution entry points).
* **[`tools/test_bin_to_cmd.py`](tools/test_bin_to_cmd.py):** Unit test and round-trip verification suite for `bin_to_cmd.py`.
* **[`tools/test_includes.py`](tools/test_includes.py):** Parser and consistency test suite validating all equates, memory addresses, and data structures across `svc.inc`, `equ.inc`, and `fujinet.inc`.

---

## Related Repositories & Reference Codebases

* **`trs-os-zds`** ([rlucente-retro/trs-os-zds](https://github.com/rlucente-retro/trs-os-zds), branch `fujinet-dw-boot`): Core operating system source tree (`TRSDOS_7`), Agon Light 2 patchset, and ZDS II Wine build automation for `TRSDOS.bin`.
* **`fujinet-firmware`** ([rlucente-retro/fujinet-firmware](https://github.com/rlucente-retro/fujinet-firmware), branch `fix/boip-iowait-timeout`): Fork of [FujiNetWIFI/fujinet-firmware](https://github.com/FujiNetWIFI/fujinet-firmware) providing DriveWire (`BUILD_COCO`) block I/O, `OP_FUJI` transactions, and optimized BoIP socket timeouts.
* **`agon-net-utils`** ([rlucente-retro/agon-net-utils](https://github.com/rlucente-retro/agon-net-utils)): Agon Quark MOS network utilities (`openstream`, `sendstream`, `closestream`).
* **`trs-net-tcp`** ([rlucente-retro/trs-net-tcp](https://github.com/rlucente-retro/trs-net-tcp)): Previous Python-based TCP/serial disk daemon for the initial Agon TRSDOS port.
* **`nitros9`** ([nitros-9/nitros9](https://github.com/nitros-9/nitros9)): Reference 6809 assembly FujiNet utilities for NitrOS-9 ([`level1/cmds/fn*.as`](https://github.com/nitros-9/nitros9/tree/main/level1/cmds)).
* **`OSboot`** ([sijnstra/agon-projects/OSboot](https://github.com/sijnstra/agon-projects/tree/main/OSboot)): MOS bootstrap loader for launching `TRSDOS.bin`.
