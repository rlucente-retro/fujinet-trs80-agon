# Architectural Plan: FujiNet Firmware Integration for TRS-80 on Agon Light 2

**Document Version:** 1.7.0  
**Target Architecture:** Zilog eZ80F92 (Olimex Agon Light 2)  
**Target Operating System:** TRS-80 Model 4 Environments (TRSDOS 6.3.1 / LS-DOS 6.3 and CP/M on eZ80)  
**Architectural Baseline:**  
- **Boot Chain:** Agon Quark MOS -> `FNBOOT` / `OSboot.bin` -> `TRS-80 HAL/BIOS` -> `TRSDOS / LS-DOS 6.3.1` (Z80 mode)  
- **Lean System Image:** `TRSDOS.bin` is a lean **66,048-byte (~64 KiB)** binary containing only the eZ80 kernel, HAL, and slice tables (`0x000000`–`0x0101FF`). The legacy 425 KB embedded RAM disk is completely eliminated, freeing **~448 KiB of external SRAM** for sector caches, RAM disks, or user workspaces.
- **Pure FujiNet Client & Network Booting:** Drive `:0` boots standard LS-DOS 6.3.1 system disk images directly from FujiNet Device Slot 0 over DriveWire (`OP_READEX` = `0xD2`), providing 100% persistent storage for system configurations and files.
- **Local SD Card Disk Support:** Updateable (Read/Write) `.dsk` disk images hosted on the Agon Light 2's on-board microSD card (FAT32 filesystem) are mounted directly via eZ80 hardware SPI0 block I/O (`driver-SDDVR.S`).
- **Three Flexible Operating Modes:**
  1. *Mode 1 (Pure Network Boot / Diskless Workstation):* Drives `:0` through `:7` are hosted remotely on FujiNet / TNFS.
  2. *Mode 2 (Pure Local Boot / Standalone Workstation):* Drives `:0` through `:3` are mounted from local `.dsk` files on the Agon microSD card via SPI0.
  3. *Mode 3 (Hybrid Operation):* Mix local and network drives (e.g. Drive `:0` booted over TNFS, Drive `:1` mapped to a local updateable SD image) with seamless cross-tier copying (`COPY`, `BACKUP`).
- **Pre-Boot & In-DOS Tool Suites:**
  - *Quark MOS Pre-Boot Manager (`FNBOOT`):* Interactive visual browser to configure FujiNet host/device slots and local SD card images prior to OS launch.
  - *MOS File Interchange Tools:* Native utilities (`*TRSDIR`, `*TRSEXT`, `*TRSADD`, `*TRSDEL`, `*TRSNEW`) bridging the Agon FAT32 filesystem and TRSDOS `.dsk` images.
  - *Native TRSDOS Utilities:* Full <= 8-character suite (`FNSTATUS`, `FNHOSTS`, `FNMOUNT`, `FNDEVS`, `FNSETDEV`, `FNMNTIMG`, `DRVMAP`, `SDMOUNT`).
- **Host / Coprocessor:** `fujinet-firmware` (`BUILD_COCO` DriveWire Bus over Serial / BoIP)  
- **Toolchain:** Zilog ZDS II (`ez80asm.exe` via Wine) matching `trs-os-zds` (with verified native AgonDev parity)  
- **Physical Transports:**  
  - *Transport A (Direct Serial Link):* Olimex MOD-USB-RS232 on Agon UEXT (`PC0`/`PC1`); point-to-point hardware connection inherently active at reset (bypasses `openstream` and `closestream`).  
  - *Transport B (Wi-Fi TCP/IP Streaming):* Olimex MOD-WIFI-ESP8266 on Agon UEXT; requires `openstream` in MOS to enter transparent passthrough mode (`CIPMODE=1`) prior to boot, and `closestream` (`+++`) upon exit.  

---

## Table of Contents

1. [Executive Summary & Objectives](#1-executive-summary--objectives)
2. [Codebase Analysis & Architectural Overview](#2-codebase-analysis--architectural-overview)
   - [2.1 Architectural Hierarchy: FNBOOT/OSboot, Agon eZ80 HAL, and TRSDOS/LS-DOS 6.3.1](#21-architectural-hierarchy-fnbootosboot-agon-ez80-hal-and-trsdosls-dos-631)
   - [2.2 Driver & Subsystem Disposition Matrix](#22-driver--subsystem-disposition-matrix)
   - [2.3 `agon-net-utils`: MOS Network Bootstrap (Wi-Fi Only)](#23-agon-net-utils-mos-network-bootstrap-wi-fi-only)
   - [2.4 `trs-net-tcp`: Existing Host Daemon & Protocol](#24-trs-net-tcp-existing-host-daemon--protocol)
   - [2.5 `trs-os-zds`: Agon Light 2 Kernel Build Tree](#25-trs-os-zds-agon-light-2-kernel-build-tree)
   - [2.6 `fujinet-firmware`: Multi-Protocol Peripheral Server](#26-fujinet-firmware-multi-protocol-peripheral-server)
   - [2.7 `nitros9`: Reference FujiNet Client Implementations](#27-nitros9-reference-fujinet-client-implementations)
   - [2.8 How TRS-80 Operating Systems Run on the eZ80 Agon Light](#28-how-trs-80-operating-systems-run-on-the-ez80-agon-light)
3. [Protocol Validation & Hardware Handover](#3-protocol-validation--hardware-handover)
   - [3.1 Validation of Direct Serial Protocol Support](#31-validation-of-direct-serial-protocol-support)
   - [3.2 Verification of Server Silence & Shared Transaction State](#32-verification-of-server-silence--shared-transaction-state)
   - [3.3 Boot Lifecycle, Execution Flow & Explicit UART1 Reinitialization](#33-boot-lifecycle-execution-flow--explicit-uart1-reinitialization)
4. [Universal Disk Image Support: JV1, DiskDISK, and HDV](#4-universal-disk-image-support-jv1-diskdisk-and-hdv)
   - [4.1 Image Formats & Container Comparison](#41-image-formats--container-comparison)
   - [4.2 Header Offset Handling & Container Translation](#42-header-offset-handling--container-translation)
   - [4.3 Dynamic Geometry Derivation & DCT Synchronization](#43-dynamic-geometry-derivation--dct-synchronization)
   - [4.4 HDV Hard Disk Support vs. Floppy Driver Addressing Bounds](#44-hdv-hard-disk-support-vs-floppy-driver-addressing-bounds)
5. [Wire Protocol Specifications](#5-wire-protocol-specifications)
   - [5.1 Corrected DriveWire Block Storage Protocol (`OP_READEX` & `OP_WRITE`)](#51-corrected-drivewire-block-storage-protocol-op_readex--op_write)
   - [5.2 16-Bit Big-Endian Checksum Specification & Test Vectors](#52-16-bit-big-endian-checksum-specification--test-vectors)
   - [5.3 Audit of Upstream Server Write Defects & Safety Mitigation](#53-audit-of-upstream-server-write-defects--safety-mitigation)
   - [5.4 FujiNet Configuration Transaction Protocol (`OP_FUJI`)](#54-fujinet-configuration-transaction-protocol-op_fuji)
6. [TRSDOS Storage Architecture & Drive Management](#6-trsdos-storage-architecture--drive-management)
   - [6.1 Multi-Tier Storage Allocation Policy & The Three Operating Modes](#61-multi-tier-storage-allocation-policy--the-three-operating-modes)
   - [6.2 Device Control Table (DCT) Management & Dynamic Vector Enablement](#62-device-control-table-dct-management--dynamic-vector-enablement)
   - [6.3 Storage Driver Routing: DriveWire (`driver-DWDVR.S`) & Local SD (`driver-SDDVR.S`)](#63-storage-driver-routing-drivewire-driver-dwdvrs--local-sd-driver-sddvrs)
   - [6.4 Real-Time Clock & Line Printer Drivers](#64-real-time-clock--line-printer-drivers)
7. [Native TRSDOS & Quark MOS Utilities Specification](#7-native-trsdos--quark-mos-utilities-specification)
   - [7.1 Architecture & Peripheral Scope (TRSDOS Transport Decoupling)](#71-architecture--peripheral-scope-trsdos-transport-decoupling)
   - [7.2 Pre-Boot Quark MOS Utility Suite (`FNBOOT` & MOS File Interchange Tools)](#72-pre-boot-quark-mos-utility-suite-fnboot--mos-file-interchange-tools)
   - [7.3 Eight-Character Basename Compliance & Command Summary](#73-eight-character-basename-compliance--command-summary)
   - [7.4 Detailed Specification of the 11 Command Utilities](#74-detailed-specification-of-the-11-command-utilities)
   - [7.5 LS-DOS System Interface & System SVC Conventions](#75-ls-dos-system-interface--system-svc-conventions)
8. [Build Pipeline with ZDS II & Authentic CMD Packaging](#8-build-pipeline-with-zds-ii--authentic-cmd-packaging)
   - [8.1 Toolchain Configuration & AgonDev Parity](#81-toolchain-configuration--agondev-parity)
   - [8.2 Authentic TRSDOS `.CMD` Load Module Generation](#82-authentic-trsdos-cmd-load-module-generation)
   - [8.3 Automated Build Script & Packaging Pipeline](#83-automated-build-script--packaging-pipeline)
9. [Technical Challenges, Timing & Risk Mitigations](#9-technical-challenges-timing--risk-mitigations)
   - [9.1 Physical Wire Timing & Bandwidth Ceilings](#91-physical-wire-timing--bandwidth-ceilings)
   - [9.2 UART FIFO Overrun Protection & 60 Hz Timer Clock Drift](#92-uart-fifo-overrun-protection--60-hz-timer-clock-drift)
   - [9.3 ESP8266 `+++` Control Escape Timing & Binary Transparency](#93-esp8266--control-escape-timing--binary-transparency)
10. [Phased Implementation Roadmap](#10-phased-implementation-roadmap)
11. [Verification & Test Matrix](#11-verification--test-matrix)
12. [Project Reference Matrix & Upstream Sources](#12-project-reference-matrix--upstream-sources)

---

## 1. Executive Summary & Objectives

The primary objective of this project is to provide a complete network mass storage and peripheral ecosystem for **TRSDOS 6.3.1 / LS-DOS 6.3** running on the Olimex Agon Light 2 (Zilog eZ80). Originating from Daniel Paul Martin's initial eZ80 port, this effort eliminates the legacy embedded RAM-disk and bespoke `trs-net-tcp` protocol, replacing them with standard **FujiNet** protocols provided by **`fujinet-firmware`** alongside direct hardware SPI0 local SD card storage.

### Core Goals:
1. **Adopt Industry-Standard Protocols with Validated Wire Handshakes:** Replace the legacy ASCII text protocol (`<00012\n`, `@ping\n`, `@bind\n`) with the high-performance binary **DriveWire 3/4** protocol (`OP_READEX = 0xD2`, `OP_WRITE = 0x57`) and the **`OP_FUJI` (0xE2)** transaction protocol.
2. **Pure FujiNet Client & Lean Network Booting:** Eliminate the 425 KB embedded RAM disk image and monolithic 480 KiB binary. Drive `:0` boots standard LS-DOS 6.3.1 system disk images directly from FujiNet Device Slot 0 over DriveWire (`OP_READEX`), shrinking `TRSDOS.bin` to a lean **~64 KiB** kernel/HAL and leaving **~448 KiB of SRAM completely free**.
3. **Local Agon SD Card Disk Coexistence:** Provide direct eZ80 SPI0 block drivers (`driver-SDDVR.S`) to mount, read, and write `.dsk` disk images residing on the Agon Light 2's on-board microSD card (FAT32 filesystem), providing self-contained, offline, updateable storage.
4. **Three Flexible Operating Modes:**
   * *Mode 1 (Pure Network Boot):* Diskless cloud workstation where Drives `:0` through `:7` are hosted remotely on FujiNet / TNFS.
   * *Mode 2 (Pure Local Boot):* Standalone offline workstation where Drives `:0` through `:3` are mounted from local `.dsk` files on the Agon microSD card via SPI0.
   * *Mode 3 (Hybrid Operation):* Mix local and network drives (e.g. Drive `:0` booted over TNFS, Drive `:1` mapped to a local updateable SD image) with seamless cross-tier copying (`COPY`, `BACKUP`).
5. **Pre-Boot & In-DOS Tool Suites:**
   * Provide **`FNBOOT`** in Quark MOS: an interactive visual launcher to browse TNFS repositories, mount local/remote images, and boot.
   * Provide native MOS file tools (`*TRSDIR`, `*TRSEXT`, `*TRSADD`, `*TRSDEL`, `*TRSNEW`) to transfer files between the Agon FAT32 filesystem and TRSDOS `.dsk` disk images without a PC.
   * Provide a complete suite of native TRSDOS in-DOS utilities (`FN*.CMD`, `DRVMAP.CMD`, `SDMOUNT.CMD`).
6. **Dual Physical Transport Support (Direct Serial & Wi-Fi Streaming):**
   * *Direct Serial Link (Olimex MOD-USB-RS232):* Point-to-point hardware connection inherently active at reset; boots directly from MOS via `*FNBOOT` or `*OSboot TRSDOS.bin`.
   * *Wi-Fi TCP/IP Streaming (Olimex MOD-WIFI-ESP8266):* Retains `openstream` -> `FNBOOT` -> `TRSDOS.bin` workflow where MOS uses AT commands to enter transparent passthrough (`CIPMODE=1`), preserving the active socket across the OS boundary.
7. **Safety-First, Staged Storage Deployment:**
   * *Stage 1 (Production Baseline):* Drives `:1`–`:4` (and network `:0`) operate in verified Read-Only mode (`WP`) on stock CoCo firmware (`MAX_DWDISK_DEVICES = 4`) to protect host media while upstream firmware write routines are audited.
   * *Stage 2 (Patched Firmware Expansion):* Firmware recompiled with `MAX_DWDISK_DEVICES 8` unlocks all seven network drives (`:1` through `:7`), with validated write support enabled.

---

## 2. Codebase Analysis & Architectural Overview

### 2.1 Architectural Hierarchy: FNBOOT/OSboot, Agon eZ80 HAL, and TRSDOS/LS-DOS 6.3.1

The execution environment is structured into three cooperating software layers:

```
+-------------------------------------------------------------------------------+
| 1. FNBOOT / OSboot (Agon Quark MOS Environment)                               |
|    - Quark MOS visual launcher & pre-boot manager (eZ80 Z80 mode).            |
|    - Configures VDP video/font, manages FujiNet slots & local SD images.      |
|    - Stages 64 KB TRSDOS.bin into SRAM, remaps memory, sets platform tag.     |
+-------------------------------------------------------------------------------+
                                        |
                                        v Jumps to 0x000000 (__init)
+-------------------------------------------------------------------------------+
| 2. Agon eZ80 HAL & BIOS (Kernel & Hardware Drivers)                           |
|    - eZ80 Hardware Abstraction Layer (HAL), BIOS, and Initial Program Loader. |
|    - Handles CPU registers, timers, UART0 (VDP), UART1 (UEXT), SPI0 (SD).     |
|    - Emulates Model 4 hardware (video RAM, keyboard, floppy controller).      |
|    - Routes :0..:7 to DriveWire (driver-DWDVR) or Local SD (driver-SDDVR).    |
+-------------------------------------------------------------------------------+
                                        |
                                        v Hands off to DOSINIT -> zCMNDI
+-------------------------------------------------------------------------------+
| 3. TRSDOS / LS-DOS 6.3.1 (Logical Systems Inc. / Tandy)                       |
|    - Authentic 1980s TRS-80 Model 4 disk operating system.                    |
|    - Runs in Z80 compatibility mode (ADL = 0) with a standard 64 KB map.      |
|    - 100% authentic LSI code for SVCs, SYS0..SYS6 overlays, FMS, and CLI.     |
|    - Unaware of eZ80/Agon hardware; issues standard Model 4 sector I/O.       |
+-------------------------------------------------------------------------------+
```

1. **`FNBOOT` / `OSboot`**: Agon Quark MOS bootstrap utilities. `FNBOOT` provides an interactive pre-boot visual browser to inspect FujiNet status, navigate remote TNFS archives, select local Agon SD images, configure drives `:0`–`:7`, program the ESP32 VDP with TRS-80 font/colors, load the lean `TRSDOS.bin` image, and transfer control to the OS. `OSboot` provides a fast direct-boot path.
2. **Agon eZ80 HAL & BIOS**: The hardware abstraction layer (HAL) and BIOS for the Zilog eZ80 processor (originating from the `TRSDOS_7` port). It provides the CPU initializers, video terminal translation, interrupt tables, 60 Hz timer tasks, and virtual block storage drivers (`driver-FDCDVR.S`) that route sector requests across DriveWire (`driver-DWDVR.S`) and local SD (`driver-SDDVR.S`).
3. **`TRSDOS / LS-DOS 6.3.1`**: The authentic, historical disk operating system originally created by Logical Systems Inc. (LSI) as LS-DOS 6.3 and licensed by Tandy Corporation as TRSDOS 6.3.1 for the TRS-80 Model 4. It contains the system overlays (`SYS0/SYS` through `SYS6/SYS`), supervisor calls (SVCs), file management routines (`SYS4`), and the command line interpreter (`SYS6` `zCMNDI`).

### 2.2 Driver & Subsystem Disposition Matrix

| Subsystem / Driver | Real Model 4 Hardware | Agon Light 2 Implementation (eZ80 HAL) | Integration Strategy with FujiNet |
| :--- | :--- | :--- | :--- |
| **Video Display (`*DO`)** | Memory-mapped video at `0xF800`–`0xFFFF` (80×24 / 64×16). | Intercepted in `videoRAM.s`; characters converted to terminal stream over UART0 to ESP32 VDP. | **Preserved Untouched.** Reuses existing VDP translation and authentic TRS-80 font support. |
| **Keyboard Input (`*KI`)** | Memory-mapped matrix scan ports (`0x3801`–`0x3880`). | Intercepted in `SVC09-keyin.s`; reads ASCII keys received from VDP over UART0. | **Preserved Untouched.** Reuses existing keyboard handler. |
| **Real-Time Clock** | Hardware heartbeat interrupt (30/60 Hz). | eZ80 Timer 1 (`TMR1`) programmed to fire periodic 60 Hz interrupts; updates `TIME$` and `DATE$`. | **Preserved / Enhanced.** Maintains 60 Hz task scheduler; synchronized via `OP_TIME` (`0x23`). |
| **Drive `:0` (Boot Drive)** | Physical Floppy Drive `:0`. | Legacy in-memory RAM disk slice (`0x010200`) eliminated. | **Network Booted.** Drive `:0` boots standard system disks over DriveWire from FujiNet Slot 0 (or Local SD). |
| **Local SD Card Drives**| Physical Floppy Disk Controller. | Direct eZ80 SPI0 block I/O (`PB1`–`PB4`) reading/writing `.dsk` files on the Agon microSD card. | **Integrated (`driver-SDDVR.S`).** Updateable read/write local storage; 512B SD block = 2x 256B TRSDOS sectors. |
| **Device Control Tables (`DCT`)**| 10-byte tables (`DCT0$`..`DCT7$`) defining driver vectors and media geometry. | Initialized dynamically based on pre-boot configuration or runtime mount. | **Dynamic Enablement.** `DCTn$` populated with `JP FDCDVR` (`0xC3`) and derived geometry upon mount. |
| **System Calls (`SVC`)** | `RST 28h` (RST 40 decimal) vector through Page 1 table. | Authentic Roy Soltoff / Logical Systems LS-DOS 6.3.1 SVC dispatch table. | **Preserved Untouched.** Full LS-DOS 6.3.1 API compatibility maintained. |
| **File System Manager** | `SYS4/SYS` (Directory parsing, FCBs, file extents, GAT/HIT). | Exact, original binary code from LS-DOS 6.3.1 executed directly by eZ80 CPU. | **Preserved Untouched.** All file parsing and extent management remain authentic. |
| **UART1 Warm Boot Setup** | Serial communications interface. | Configured during `finish_init` in `SYS0_IPL/ipl-eZ80_CPU.S`. | **Adapted / Preserved.** Solidifies `0001-uart1-streaming.patch` (Port C Mode 7, BRG=10, 8-N-1, IER=0, FCTL=07h). |
| **Network Disk Driver** | WD1773 FDC or bespoke network controller. | Legacy line-based ASCII protocol (`<XXXXX`, `>XXXXX`) in `driver-FDCDVR.S` for Drive `:6`. | **Replaced.** Overhauled to binary DriveWire `OP_READEX` (`0xD2`) / `OP_WRITE` (`0x57`) for Drives `:0`–`:7` via `driver-DWDVR.S`. |
| **Line Printer (`*PR`)** | Centronics parallel printer port at I/O port `0xF8`. | Intercepted in `driver-PRDVR.S`. | **Adapted.** Redirects character stream to UART1 `OP_PRINT` (`0x50`) to FujiNet. |

```
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

### 2.3 `agon-net-utils`: MOS Network Bootstrap (Wi-Fi Only)
Provided by [rlucente-retro/agon-net-utils](https://github.com/rlucente-retro/agon-net-utils), this repository contains C utilities for Quark MOS on the eZ80 specifically for Wi-Fi streaming operation:
* **`openstream.c`:** Sends AT commands over UART1 to initialize the Olimex MOD-WIFI-ESP8266 module:
  1. `ATE0`: Disables local echo.
  2. `AT+CIPMUX=0`: Sets single-connection mode.
  3. `AT+CIPMODE=1`: Configures transparent UART passthrough mode.
  4. `AT+CIPSTART="TCP","<host>",<port>`: Establishes a TCP connection to the host.
  5. `AT+CIPSEND`: Initiates transmission; waits for the `>` prompt.
  6. Upon receiving `>`, calls `mos_uclose()` to detach MOS interrupt handlers and returns cleanly to MOS with the connection preserved.
* **`sendstream.c`:** Sends a test string over the active transparent stream and verifies the response.
* **`closestream.c`:** Employs the standard Hayes `+++` escape sequence with a 1.25s silence guard window to exit passthrough mode, followed by `AT+CIPCLOSE` and `AT+CIPMODE=0`.

> [!IMPORTANT]
> **Transport Scope of `openstream` and `closestream`:**  
> The `openstream` and `closestream` utilities are strictly required **only** when operating across the Olimex MOD-WIFI-ESP8266 module, where the Wi-Fi coprocessor powers up in AT command mode and must be explicitly instructed to establish a socket and enter transparent UART passthrough (`CIPMODE=1`).  
> In contrast, when using a **direct physical serial link** (such as the Olimex MOD-USB-RS232 adapter plugged into UEXT, connecting over USB CDC-ACM or RS-232 to a host running FujiNet-PC or a hardware FujiNet unit), the communication channel is **inherently present and active upon hardware reset**. No AT initialization, socket negotiation, `openstream`, or `closestream` is necessary; Quark MOS directly executes `*FNBOOT` or `*OSboot -2f TRSDOS.bin`.

### 2.4 `trs-net-tcp`: Existing Host Daemon & Protocol
Provided by [rlucente-retro/trs-net-tcp](https://github.com/rlucente-retro/trs-net-tcp), this repository provides `trs-netd.py`:
* **Wire Protocol:** ASCII line-oriented protocol with `\n` line delimiters:
  * Read Sector: `<00012\n` -> Returns 256 bytes raw data + 4 bytes big-endian checksum (`sum % 256`).
  * Write Sector: `>00012\n` + 256B payload + 4B checksum -> Returns 4B calculated checksum.
  * Spool Character: `#<ascii_code>\n`.
  * Control Commands: `@ping\n` -> `@pong\n`, `@bind\n` -> `@bound\n` + 256B DiskDISK header.
* **Characteristics:** Pure request-response; completely silent upon connection until client transmits. Bound to a single disk file (`Volumes/sys720k.dsk`) at daemon launch.

### 2.5 `trs-os-zds`: Agon Light 2 Kernel Build Tree
Provided by [rlucente-retro/trs-os-zds](https://github.com/rlucente-retro/trs-os-zds), this repository contains the source code for TRSDOS 6.3.1 / LS-DOS 6.3 adapted for the eZ80:
* **Hardware Adaptation (`patches/0001-uart1-streaming.patch`):**
  * Agon Light 2 uses Port C Mode 7 (`PC0` = TXD1, `PC1` = RXD1) on the eZ80F92.
  * 18.432 MHz crystal yields BRG divisor 10 for 115,200 baud with 0% error.
  * UEXT header lacks RTS/CTS lines; driver runs in **pure polled I/O mode** with `UART1_IER` cleared to zero to avoid triggering `STRAY.handler` freezes.
* **Disk Driver (`driver-FDCDVR.S`):**
  * Masks interrupts (`DI`/`EI`) during multi-byte transfers to prevent timer tasker FIFO overruns.

### 2.6 `fujinet-firmware`: Multi-Protocol Peripheral Server
Provided by [FujiNetWIFI/fujinet-firmware](https://github.com/FujiNetWIFI/fujinet-firmware):
* **DriveWire Bus (`BUILD_COCO`):**
  * Implemented in `lib/bus/drivewire/drivewire.cpp` and `lib/device/drivewire/`.
  * Operates over physical UART (`_serial`), USB CDC-ACM, or Becker port / BoIP (`_becker` over TCP port 65504).
  * **Device Slots:** Pinned `BUILD_COCO` allocates **4 virtual disk slots** (`MAX_DWDISK_DEVICES = 4` in `drivewireFuji.h`), responding with 152 bytes for `FUJI$ReadDevSlots` (0xF2).
  * Implements `OP_READEX` (0xD2), `OP_WRITE` (0x57), `OP_PRINT` (0x50), `OP_TIME` (0x23), and `OP_FUJI` (0xE2).
* **FujiNet Config Engine (`fujiDevice`):**
  * Manages 8 Host Slots (0–7) and 4 Device Slots (0–3).
  * Interfaced via three-phase `OP_FUJI` transactions: execute command -> query error -> read response.
  * **Shared Transaction Buffer:** Server maintains a single response buffer across client interactions. Responses survive TCP reconnects until drained.
* **FujiNet-PC:**
  * Compiles natively on macOS/Linux/Windows (`build/dist/fujinet`).
  * Running `build/dist/fujinet -V` confirms `Target: COCO`.

### 2.7 `nitros9`: Reference FujiNet Client Implementations
Provided by [nitros-9/nitros9](https://github.com/nitros-9/nitros9) (`level1/cmds/fn*.as`):
* Reference utilities demonstrate interaction with the FujiNet firmware:
  * `fnstatus.as`: Queries `FUJI$GetAdapterCfg` (0xE8); prints IP, SSID, MAC, version.
  * `fnlisthosts.as`: Queries `FUJI$ReadHostSlots` (0xF4); lists host slots 0–7 (256 bytes).
  * `fnlistdevs.as`: Queries `FUJI$ReadDevSlots` (0xF2).
  * `fngetdevfile.as`, `fnsetdevfile.as`, `fnmountimg.as`: Configure and mount disk images.
* **Wire Protocol Distinction:** The 2-byte expected-response-length prefix seen in NitrOS-9's `lib/fuji.as` is internal to OS-9's `scdwv` SetStat driver interface. It is consumed locally by OS-9 and **must not be transmitted on the wire**.

### 2.8 How TRS-80 Operating Systems Run on the eZ80 Agon Light

The ability of vintage TRS-80 Model 4 operating systems (TRSDOS 6.3.1, LS-DOS 6.3, Montezuma CP/M 2.2) to execute seamlessly on a modern eZ80 microcontroller is made possible by several architectural factors:

1. **eZ80 Z80-Compatibility Mode (`ADL = 0`):**
   * The Zilog eZ80 processor powers up with full backward compatibility with the Z80 instruction set.
   * In Z80 mode (`ADL = 0`), the CPU uses 16-bit addresses and 8-bit/16-bit registers identical to an original Z80.
   * Instructions execute with authentic Z80 semantics, but at roughly 3× the throughput per clock cycle due to pipelining. At 18.432 MHz, the Agon Light behaves like an effective **~54 MHz TRS-80 Model 4**.

2. **Clean Operating System Abstraction (The Power of LS-DOS):**
   * Unlike systems where programs poked directly into floppy controller chips, LS-DOS was built with a clean supervisor architecture:
     * File I/O is accessed through `@OPEN`, `@READ`, `@WRITE`, `@CLOSE`.
     * Console I/O is accessed through `@KEYIN` and `@DSPLY`.
     * Disk drivers are accessed through standardized Device Control Tables (`DCT`).
   * Because the Agon HAL replaces low-level hardware drivers while leaving supervisor tables (`Pages 0–3` and `DCTs`) in their exact historical memory locations, the authentic LS-DOS kernel runs completely unaware that physical floppy drives have been replaced by DriveWire network packets and SPI0 flash blocks.

3. **Software Compatibility:**
   * Any well-behaved TRS-80 Model 4 program that uses standard LS-DOS SVC calls or accesses the screen through standard DOS drivers runs natively on the Agon Light without modification or recompilation.

---

## 3. Protocol Validation & Hardware Handover

### 3.1 Validation of Direct Serial Protocol Support

**Finding: FULLY SUPPORTED.**

FujiNet firmware incorporates an architectural I/O abstraction layer (`IOChannel`) in `lib/hardware/`:
1. **Physical UART (`UARTChannel`):** Directly interfaces with hardware UARTs at 115,200 baud.
2. **USB Serial (`ACMChannel`):** Interfaces with USB CDC-ACM virtual COM ports.
3. **Bus-over-IP (`BoIPChannel`):** Interfaces with raw TCP/IP sockets (`lib/hardware/BoIPChannel.cpp`).

All DriveWire opcodes execute through `_port->read()` and `_port->write()`. The protocol logic is transport-agnostic and functions identically whether over a physical RS-232 / UART cable or across a transparent TCP stream.

### 3.2 Verification of Server Silence & Shared Transaction State

**Finding: CONFIRMED SILENT UPON CONNECTION; SHARED RESPONSE BUFFER REQUIRES BOUNDED DRAINING.**

Empirical validation on host loopback demonstrates:
1. **Zero Unsolicited Bytes:** When a connection is established, the server configures socket options and logs diagnostics locally, but **transmits zero bytes** over the network or serial socket. Ten seconds of continuous idle observation confirmed 100% silence until the client issued `E2 00` (Ready).
2. **Shared Response Buffer Caveat:** The FujiNet configuration transaction engine retains unread response data across TCP reconnections. If a previous session terminated mid-transaction, the new connection will receive the stale buffered payload. The TRSDOS client must strictly serialize transactions, consume expected response payloads completely, and enforce a clean session initialization.

### 3.3 Boot Lifecycle, Execution Flow & Explicit UART1 Reinitialization

#### The Complete Handover Flow:
1. **Quark MOS Execution & Pre-Boot Selection (`FNBOOT` / `OSboot`):**
   * The user runs `*FNBOOT` (or `*OSboot -2f TRSDOS.bin`).
   * `FNBOOT` queries FujiNet over UART1, presents an interactive VDP visual browser, mounts remote disk images to device slots, maps local SD `.dsk` files, and reads lean `TRSDOS.bin` (~64 KiB: kernel and slice tables `0x000000`–`0x0101FF`) into high external SRAM staging memory.
   * `FNBOOT` executes from eZ80 internal on-chip SRAM (`0xFFE000` / `RAM_ADDR_U`) to reprogram Chip Selects (`CS0`, `CS1`, `RAM_CTL`), remapping external SRAM down to base `0x000000` and displacing Flash ROM.
   * It copies the 64 KB kernel to address `0x000000`, sets terminal font/colors via internal UART0, writes the platform signature `3` into `UART0_SPR`, and jumps to `0x000000` (`__init`).
2. **Agon eZ80 HAL Startup (`ipl-eZ80_CPU.S`):**
   * Enters at `__init` with interrupts disabled (`DI`).
   * Reads `UART0_SPR`. Detecting `3` => branches to `NOINITIAL` (warm boot), preserving MOS memory layouts and VDP terminal settings.
3. **Preserving the Streaming Connection across the Boundary:**
   * When `openstream` finished in MOS via `mos_uclose()`, MOS zeroed UART1 control registers.
   * In `finish_init`, the HAL executes explicit hardware reinitialization:
     - Re-asserts Port C Mode 7 (`PC0` = TXD1, `PC1` = RXD1).
     - Loads BRG divisor 10 (18.432 MHz / (16 * 115,200)).
     - Sets Line Control to 8-N-1 (`UART1_LCTL = 0x03`).
     - Zeroes `UART1_IER` (`0x00`) to enforce pure polled I/O and prevent stray interrupt lockups.
     - Enables and flushes hardware FIFOs (`UART1_FCTL = 0x07`).
   * As soon as these local registers are set, UART1 immediately resumes communication with the external FujiNet peripheral without dropping a single byte!
4. **BIOS & Network Storage Mounting (`ipl-BIOS.s`):**
   * Configures eZ80 Timer 1 for 60 Hz interrupts.
   * Neutralizes legacy RAM volume scanning and mounts Drive `:0` directly over DriveWire from FujiNet Slot 0 (or Local SD).
   * Reads Sector 0 and Directory GAT over the wire to dynamically configure `DCT0$`.
   * Enters authentic `DOSINIT`.
5. **Authentic TRSDOS / LS-DOS 6.3.1 Handover (`ipl-DOS.S`):**
   * LS-DOS reads Track 0, Sector 2 from Drive `:0` over DriveWire to check `AUTO` command strings and `SYSGEN` settings.
   * Enables interrupts (`EI`) to start the 60 Hz task scheduler, calls `zICNFG`, and loads system overlays (`SYS0` through `SYS6`).
   * Enters `zCMNDI` in `SYS6/SYS` to present the `TRSDOS Ready` prompt.

```assembly
; =============================================================================
; Explicit eZ80 UART1 Hardware Initialization (finish_init)
; Solidifies the verified patch from patches/0001-uart1-streaming.patch
; =============================================================================
fn_uart_init:
        ; 1. Configure Port C Mode 7 for UART1 pins (PC0=TXD1, PC1=RXD1)
        in0     a, (09Fh)               ; PC_DDR
        or      03h
        out0    (09Fh), a
        in0     a, (0A0h)               ; PC_ALT1
        and     0FCh
        out0    (0A0h), a
        in0     a, (0A1h)               ; PC_ALT2
        or      03h
        out0    (0A1h), a

        ; 2. Enable Divisor Latch (DLAB = 1) in LCTL
        ld      a, 80h
        out0    (0D3h), a               ; UART1_LCTL

        ; 3. Set Baud Rate Divisor = 10 (115,200 baud @ 18.432 MHz)
        ld      a, 10                   ; Divisor LSB
        out0    (0D0h), a               ; UART1_BRG_L
        xor     a                       ; Divisor MSB
        out0    (0D1h), a               ; UART1_BRG_H

        ; 4. Set Line Control: 8 data bits, 1 stop bit, no parity (DLAB = 0)
        ld      a, 03h
        out0    (0D3h), a               ; UART1_LCTL

        ; 5. Disable all UART interrupts (pure polled I/O mode)
        xor     a
        out0    (0D1h), a               ; UART1_IER
        out0    (0D4h), a               ; UART1_MCTL

        ; 6. Enable and Flush Hardware FIFOs
        ld      a, 07h                  ; FIFO enable, clear RX and TX FIFOs
        out0    (0D2h), a               ; UART1_FCTL
        ret
```

---

## 4. Universal Disk Image Support: JV1, DiskDISK, and HDV

### 4.1 Image Formats & Container Comparison

| Format | Header Size | Sector Size | Addressing | Offset Formula | Characteristics |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **JV1** | 0 bytes | 256 bytes | Floppy (LSN <= 16384) | LSN * 256 | Flat raw sectors; directory cylinder typically Track 17. |
| **DiskDISK** | 256 bytes | 256 bytes | Floppy (LSN <= 16384) | 256 + (LSN * 256) | Authentic 256B header; bytes F9h–FFh match 7 DCT parameters. |
| **Reed HDV** | 256 bytes | 256 bytes | Hard Disk (LSN > 16384) | 256 + (LSN * 256) | Signature `56 CB`h; geometry fields in header; requires hard disk driver. |

### 4.2 Header Offset Handling & Container Translation

FujiNet's unpatched `mediaTypeDSK.cpp` treats disk files as raw flat byte streams (`offset = LSN * 256`).
* When mounting a **DiskDISK** or **Reed HDV** file that contains a 256-byte container header, raw LSN 0 returns the header, shifting all subsequent payload sectors by one position.
* **Concrete Client Translation Architecture:**
  1. The storage driver router maintains a per-drive offset table in RAM: `dw_offset` (8 bytes, one per logical drive `:0`..`:7`).
  2. When an image is mounted, the client reads raw LSN 0 via `OP_READEX`.
  3. If the first 8 bytes match ASCII `'DiskDISK'`, it sets `dw_offset[slot] = 1`.
  4. If LSN 0 does not match `'DiskDISK'`, the image is treated as a flat raw JV1 image, setting `dw_offset[slot] = 0`.
  5. During active sector I/O in `driver-FDCDVR.S`, the driver adds `dw_offset[slot]` to the calculated `fdc_no_sum` before constructing the 5-byte `OP_READEX` / `OP_WRITE` request header:
     **Wire LSN = fdc_no_sum + dw_offset[slot]**
  This guarantees 100% transparent support for both flat JV1 and authentic DiskDISK containers without requiring server-side container parsing.

### 4.3 Dynamic Geometry Derivation & DCT Synchronization

Because DriveWire does not transmit disk geometry metadata during mounting, TRSDOS derives and configures the guest `DCT` (`DCTn$`) dynamically:
* **DiskDISK Metadata Extraction:** If `'DiskDISK'` magic is detected, bytes `0xF9`–`0xFF` contain the authentic 7-byte DCT parameter block. The mount utility directly copies these 7 bytes into `DCTn$+3` through `DCTn$+9`.
* **JV1 Dynamic Geometry Derivation:** If the image is flat JV1:
  1. Reads Sector 0 (at wire LSN 0).
  2. Byte 2 of Sector 0 contains the Directory Cylinder index (typically Track 17 / `11h`).
  3. Reads Sector 0 of the directory cylinder to fetch the Granule Allocation Table (GAT).
  4. Byte `0xCB` of the GAT confirms the formatting DOS version (`0x60` for LS-DOS 6.x).
  5. Byte `0xCC` contains cylinders in excess of 35 (`Max Cylinders = 35 + byte_CC`).
  6. Byte `0xCD` contains media configuration flags (Bit 7: System/Data, Bit 6: Density, Bit 5: Sides).
  7. Writes the derived parameters into `DCTn$+3`..`DCTn$+9`.
* **DCT Vector Activation:** Authentic LS-DOS initial state leaves `DCT2$` through `DCT7$` configured as inactive `RET` stubs (`0xC9`). After populating the geometry, the mount routine writes opcode `0xC3` (`JP FDCDVR`) to `DCTn$`, activating the drive vector in the running OS.

### 4.4 HDV Hard Disk Support vs. Floppy Driver Addressing Bounds

While the DriveWire protocol packet format carries **24-bit LSNs** capable of addressing up to 4 GB (2^24 * 256 bytes):
* **Floppy Driver Geometric Limit:** The standard TRSDOS floppy driver (`driver-FDCDVR.S`) calculates sectors using an 8-bit cylinder register (`D`), sectors per track from DCT (`DCT+7 & 1Fh + 1`), and a 16-bit accumulator `fdc_no_sum`. This caps addressing through the floppy driver at 256 * 64 = 16,384 sectors = **4 MiB**.
* **High-Capacity HDV Architecture:** True 5 MB to 32 MB HDV support requires a dedicated hard disk driver architecture (e.g. `RHDVR` / partition driver) rather than routing wide LSNs through the floppy disk driver. Stage 1 focuses on standard floppy geometries (<= 4 MiB).

---

## 5. Wire Protocol Specifications

### 5.1 Corrected DriveWire Block Storage Protocol (`OP_READEX` & `OP_WRITE`)

#### Sector Read (`OP_READEX = 0xD2`)
In `BUILD_COCO` of `fujinet-firmware`, the server command dispatcher only routes `OP_READEX` (`0xD2`) to `op_readex()`. Opcode `0x52` is not dispatched.

```
Client -> Server:  [ 0xD2 ] [ Slot (1B) ] [ LSN_23..16 ] [ LSN_15..8 ] [ LSN_7..0 ]   (5 bytes)
Server -> Client:  [ 256-Byte Sector Data ]                                             (256 bytes)
Client -> Server:  [ Checksum_MSB (1B) ] [ Checksum_LSB (1B) ]                          (2 bytes)
Server -> Client:  [ Status (1B) ]                                                      (1 byte)
```

**Status Return Codes:**
* `0x00`: Success.
* `0xF3`: Checksum mismatch (client checksum disagrees with server calculation).
* `0xF4`: Read failure.
* `0xF6`: Inactive device / empty slot.
* `0xD3`: Media status error.

> [!IMPORTANT]
> **Complete Exchange Drainage:** In error conditions (e.g., inactive slot `0xF6` or read error), the server transmits 256 dummy zero bytes and still expects the 2-byte checksum before emitting the status byte. The client **must complete the full 264-byte exchange** even on errors to maintain protocol lockstep.

#### Sector Write (`OP_WRITE = 0x57`)
```
Client -> Server:  [ 0x57 ] [ Slot (1B) ] [ LSN_23..16 ] [ LSN_15..8 ] [ LSN_7..0 ]   (5 bytes)
                   [ 256-Byte Sector Data ]                                             (256 bytes)
                   [ Checksum_MSB (1B) ] [ Checksum_LSB (1B) ]                          (2 bytes)
Server -> Client:  [ Status (1B) ]                                                      (1 byte)
```

### 5.2 16-Bit Big-Endian Checksum Specification & Test Vectors

The checksum is the unsigned 16-bit summation of all 256 payload bytes, transmitted **big-endian** (MSB first, LSB second):

**Checksum = sum(byte_0 .. byte_255) % 65536**

**Deterministic Test Vectors:**
* 256 bytes of `0x00` => Checksum = `0x0000`
* 256 bytes of `0xFF` => Checksum = `0xFF00`
* Sequential bytes `0x00` through `0xFF` => Checksum = `0x7F80`
* 256 bytes of `0x5A` => Checksum = `0x5A00`

### 5.3 Audit of Upstream Server Write Defects & Safety Mitigation

Detailed source audit and host probing of `fujinet-firmware` (`BUILD_COCO`) revealed three critical server defects:
1. **Unchecked Write Checksum:** In `drivewire.cpp::op_write()`, checksum verification is commented out, and dead parsing code expects little-endian order. The server returns status `0x00` and commits corrupted data even when sent an invalid checksum.
2. **Missing Image Bounds Check:** In `mediaTypeDSK.cpp::write()`, seeks to `LSN * 256` without verifying `LSN < block_count`. Out-of-range writes expand the host file on disk.
3. **Ignored Flush Errors:** `fnio::fflush()` return values are logged but not checked; `fsync` is disabled.

**Safety Mitigation Policy:**
* **Stage 1 (Production Baseline):** Network storage operates in **Strict Read-Only Mode**. Guest writes are intercepted and rejected immediately with `R_PROTECTED` (LS-DOS error 15). Server slots are mounted with Mode 1 (RO).
* **Stage 2 (Writable Storage):** Upstream server fixes are applied and qualified against bad-checksum and out-of-bounds negative test suites before writable storage is approved.

### 5.4 FujiNet Configuration Transaction Protocol (`OP_FUJI`)

Configuration commands follow a structured three-phase transaction model:

```
Phase 1: Execute Command
Client -> Server:  [ 0xE2 (OP_FUJI) ] [ CMD_BYTE ] [ Parameters... ]

Phase 2: Check Status / Error
Client -> Server:  [ 0xE2 (OP_FUJI) ] [ 0x02 (FUJI$GetError) ]
Server -> Client:  [ Status (1B) ]  (0x01 = Success, 0x90 = General Error)

Phase 3: Fetch Response Data (if applicable)
Client -> Server:  [ 0xE2 (OP_FUJI) ] [ 0x01 (FUJI$GetResponse) ]
Server -> Client:  [ Response Payload Bytes... ]
```

* **Success Status Code:** `OP_FUJI` uses **`0x01`** for success (`FUJI$E.OK`), unlike DriveWire sector I/O which uses **`0x00`**.
* **Ready Query:** `0xE2 0x00` returns `0x01` to confirm active command processing.
* **No Wire Length Prefix:** Do not transmit NitrOS-9's 2-byte expected length prefix on the wire.
* **Session Response Drainage:** The firmware configuration engine maintains a persistent response buffer (`_transaction_response`). The client utility startup sequence issues a single `0xE2 0x01` query to drain any residual data before initiating new transactions.

---

## 6. TRSDOS Storage Architecture & Drive Management

### 6.1 Multi-Tier Storage Allocation Policy & The Three Operating Modes

The integration replaces the artificial in-memory RAM disk with a flexible multi-tier storage architecture supporting three distinct operating modes:

#### The Three Operating Modes:
1. **Mode 1: Pure Network Boot (Diskless Cloud Client):**
   * Drives `:0` through `:7` are hosted remotely on FujiNet / TNFS.
   * Logical Drive `:0` <-> FujiNet Device Slot 0 (Network Boot System Volume).
   * Drives `:1` through `:7` <-> FujiNet Device Slots 1 through 7.
   * The Agon's local microSD card acts strictly as a "Boot ROM" holding MOS and the lean 64 KB `TRSDOS.bin` kernel.
2. **Mode 2: Pure Local Boot (Standalone / Offline Workstation):**
   * Drives `:0` through `:3` are mounted from local `.dsk` files on the Agon Light 2's on-board microSD card via direct eZ80 SPI0 block I/O (`driver-SDDVR.S`).
   * Operates completely offline with zero external network hardware or dongles attached.
   * Full real-time read/write commitment to local flash storage.
3. **Mode 3: Hybrid Operation (Cross-Tier Interoperability):**
   * Combines local SD card storage and remote FujiNet storage simultaneously (e.g. Drive `:0` booted over TNFS, Drive `:1` mapped to a local updateable SD image).
   * Authentic TRSDOS commands work seamlessly across tiers:
     - `COPY FILE:0 TO :1` => Reads from remote TNFS cloud via DriveWire and writes directly to local Agon microSD flash via SPI0!
     - `BACKUP :0 TO :1` => Clones an entire remote network disk image onto local SD card storage.

### 6.2 Device Control Table (DCT) Management & Dynamic Vector Enablement

Each logical drive in TRSDOS / LS-DOS is governed by a 10-byte Device Control Table entry (`DCT0$` through `DCT7$`):
* `DCT+0`, `DCT+1`, `DCT+2`: Driver jump vector (`JP FDCDVR`, `C3 xx xx`h).
  * `DCT0$` and `DCT1$` are initialized with `JP FDCDVR`.
  * `DCT2$` through `DCT7$` are initialized with `RET` (`0xC9`), followed by `dw FDCDVR`.
  * **Dynamic Activation:** When an image is mounted to Drive `:2`..`:7`, the mount utility writes opcode `0xC3` (`JP FDCDVR`) to byte 0 of `DCTn$`, activating the drive vector in the running operating system.
* `DCT+3`: Drive status flags. **Bit 7 is set to 1 to enforce Write Protection** on network drives during Stage 1.
* `DCT+4`: Cylinder count and head configuration (Bit 5 set = 2 heads / double-sided).
* `DCT+6`: Maximum cylinders (N - 1).
* `DCT+7`: Sectors per track configuration.
* `DCT+9`: Directory cylinder index (typically Track 17 / `11h`). Dynamically populated upon mount.

### 6.3 Storage Driver Routing: DriveWire (`driver-DWDVR.S`) & Local SD (`driver-SDDVR.S`)

The storage driver `driver-FDCDVR.S` functions as an I/O router, dispatching sector requests across two underlying drivers based on the drive target table:

```
                      +----------------------------------+
                      |         TRSDOS / LS-DOS          |
                      |        File System (SYS4)        |
                      +-----------------+----------------+
                                        |
                      +-----------------v----------------+
                      |       I/O Router (FDCDVR)        |
                      +--------+----------------+--------+
                               |                |
             Local SD Target   |                | FujiNet Slot Target
        (e.g. Drives :0, :1)   |                | (e.g. Drives :2 through :7)
                               v                v
                     +------------------+     +------------------+
                     |  driver-SDDVR.S  |     |  driver-DWDVR.S  |
                     | (eZ80 SPI0 I/O)  |     | (UART1 DriveWire)|
                     +--------+---------+     +--------+---------+
                              |                        |
                              v                        v
                     +------------------+     +------------------+
                     | Agon MicroSD     |     | FujiNet Hardware |
                     | (Local FAT32)    |     | (TNFS / Cloud)   |
                     +------------------+     +------------------+
```

#### 1. Local SD Block Driver Architecture (`driver-SDDVR.S`)
* Interfaces directly with the Agon Light 2's on-chip eZ80 SPI0 registers (`SPI_CTL`, `SPI_SR`, `SPI_RBR`, `SPI_TBR`) and Port B GPIOs (`PB1`=MOSI, `PB2`=MISO, `PB3`=SCK, `PB4`=`/CS`).
* **Block Translation:** SD card sectors are 512 bytes; TRSDOS sectors are 256 bytes (1 SD Block = 2 TRSDOS Sectors).
  **SD LBA = BaseLBA + floor(LSN / 2), Sub-sector = LSN % 2**
* Reads issue `CMD17` (READ_SINGLE_BLOCK) and copy the targeted 256-byte half into the user buffer.
* Writes issue `CMD17` into a sector buffer, modify the targeted 256-byte half, and commit via `CMD24` (WRITE_SINGLE_BLOCK).

#### 2. DriveWire Block Driver Architecture (`driver-DWDVR.S`)
* Natural 1:1 mapping across network drives: `FujiNet Wire Slot = LDRV`.
* Formulates 5-byte `OP_READEX` (`0xD2`) headers, streams 256-byte payloads, calculates 16-bit big-endian checksums, and evaluates 1-byte status codes.

```assembly
; =============================================================================
; Storage Driver Routing in driver-FDCDVR.S (Function 9: RDIN)
; Dispatches sector requests to Local SD (driver-SDDVR) or DriveWire (driver-DWDVR)
; =============================================================================
RDIN:
        push    de                      ; Save D=track, E=sector for rd_finish
        push    hl                      ; Save 16-bit user destination buffer pointer

        call    calcsec                 ; Calculate linear sector number (LSN) -> (fdc_no_sum)

        ld      a, (LDRV$)              ; Get requested logical drive (0..7)
        ld      e, a
        ld      d, 0
        ld      hl, drv_target_table    ; Table: 0 = Local SD, 1 = DriveWire Net
        add     hl, de
        ld      a, (hl)
        or      a                       ; Local SD card drive?
        jp      z, rd_local_sd          ; Route to driver-SDDVR.S (SPI0 block read)

        ; ---------------------------------------------------------------------
        ; DriveWire Sector Read Routine (OP_READEX = 0xD2)
        ; ---------------------------------------------------------------------
rd_drivewire:
        ld      a, (LDRV$)              ; Drive 0..7
        ld      (dw_slot), a            ; Direct 1:1 slot mapping (Drive :0 = Slot 0)

        ; Retrieve container offset (0 for raw JV1, 1 for DiskDISK header)
        ld      e, a
        ld      d, 0
        ld      hl, dw_offset           ; Offset lookup table (1 byte per slot)
        add     hl, de
        ld      e, (hl)                 ; E = offset (0 or 1)
        ld      hl, (fdc_no_sum)        ; HL = base calculated LSN
        add     hl, de                  ; HL = physical LSN on server

        ; Prepare 5-byte OP_READEX Request Header
        ld      a, 0D2h                 ; OP_READEX opcode (0xD2)
        ld      (dw_hdr+0), a
        ld      a, (dw_slot)            ; Virtual device slot (0..7)
        ld      (dw_hdr+1), a
        xor     a
        ld      (dw_hdr+2), a           ; LSN [23..16]
        ld      a, h
        ld      (dw_hdr+3), a           ; LSN [15..8] (MSB)
        ld      a, l
        ld      (dw_hdr+4), a           ; LSN [7..0] (LSB)

        ; Transmit 5-Byte Request Header over UART1
        UART1.put.bytes 5, dw_hdr, fdc_rd_err

        ; Receive 256-Byte Sector Data Directly into Target User Buffer
        pop     hl                      ; Recover user destination buffer pointer
        push    hl                      ; Re-save for checksum calculation
        UART1.get.bytes 0, hl, fdc_rd_err ; Length 0 = 256 bytes in eZ80 macro

        ; Calculate 16-Bit Big-Endian Unsigned Summation
        pop     hl                      ; Buffer start pointer
        call    calc_dw_checksum        ; Computes sum: D=MSB, E=LSB
        ld      (dw_chk+0), d           ; Store big-endian MSB
        ld      (dw_chk+1), e           ; Store big-endian LSB

        ; Transmit 2-Byte Checksum to Server
        UART1.put.bytes 2, dw_chk, fdc_rd_err

        ; Receive 1-Byte Status from Server (0x00 = Success)
        UART1.get.bytes 1, dw_status, fdc_rd_err
        ld      a, (dw_status)
        or      a                       ; 0x00 = OK
        jr      nz, fdc_rd_err

        ; Clean Exit via Authentic LS-DOS Directory Check
        jp      rd_finish               ; Checks (IY+9), sets A=6 if directory, restores stack

fdc_rd_err:
        pop     hl                      ; Clean up saved user buffer pointer
        pop     de                      ; Clean up saved track/sector
        pop     bc                      ; Restore entry BC
        pop     hl                      ; Restore entry HL
        ld      a, 8                    ; Error 8 = Drive Read Error
        or      a                       ; NZ indicates error
        ei
        ret
```

### 6.4 Real-Time Clock & Line Printer Drivers

* **Printer (`driver-PRDVR.S`):** Sends `OP_PRINT` (`0x50` + character) via UART1 without expecting a wire ACK. Characters are streamed directly to FujiNet's print subsystem; document finalization/flushing is managed via FujiNet's web spooler interface or timeout.
* **Clock (`driver-TOD.S`):** Issues `OP_TIME` (`0x23`), receiving 6 raw bytes from FujiNet's DriveWire server:
  1. `Year`: `now->tm_year` (sent as 1-byte integer: years since 1900, e.g. 126 for year 2026).
  2. `Month`: `now->tm_mon + 1` (1–12).
  3. `Day`: `now->tm_mday` (1–31).
  4. `Hour`: `now->tm_hour` (0–23).
  5. `Minute`: `now->tm_min` (0–59).
  6. `Second`: `now->tm_sec` (0–59).

  **Year Modulo-100 Normalization:** `driver-TOD.S` normalizes POSIX `tm_year` (126 -> 26) before storing in LS-DOS `DATE$` cells, guaranteeing seamless Y2K+ compatibility.

---

## 7. Native TRSDOS & Quark MOS Utilities Specification

### 7.1 Architecture & Peripheral Scope (TRSDOS Transport Decoupling)

TRSDOS operates completely independently of network topology:
* All `FN*.CMD` utilities communicate over standard serial `OP_FUJI` (0xE2) transactions to interrogate and configure the **remote FujiNet peripheral device**, not the local Agon hardware.
* Network properties reported by `FNSTATUS` belong to the **FujiNet device itself** (its own network interface used to reach TNFS servers or its web config) or the host PC running FujiNet-PC.

### 7.2 Pre-Boot Quark MOS Utility Suite (`FNBOOT` & MOS File Interchange Tools)

Executed from the Quark MOS `*` prompt before launching TRSDOS:

1. **`FNBOOT` (Visual Storage Manager & Launcher):**
   * Full-screen interactive MOS application.
   * Scans FujiNet adapter status and enumerates TNFS host slots.
   * Browses remote TNFS directories and local Agon SD card `.dsk` files.
   * Assigns disk images to TRSDOS Logical Drives `:0` through `:7`.
   * Sets VDP font/colors, stages lean 64 KB `TRSDOS.bin`, remaps SRAM, and boots.
2. **`*TRSDIR <image.dsk>`:**
   * Reads Directory Cylinder (Track 17) of `<image.dsk>` on the FAT32 SD card.
   * Formats and prints file directory, extensions, sizes, and granule allocation.
3. **`*TRSEXT <image.dsk> <trsdos_file> [fat_file]`:**
   * Extracts a file from inside `<image.dsk>` to a standalone file on the Agon FAT32 filesystem (with optional CRLF translation).
4. **`*TRSADD <image.dsk> <fat_file> [trsdos_file]`:**
   * Injects a FAT32 file into `<image.dsk>`, updating the GAT (allocation bitmap), HIT (hash table), and FXDE (directory record).
5. **`*TRSDEL <image.dsk> <trsdos_file>`:**
   * Deletes a file inside `<image.dsk>` and frees its granules in the GAT.
6. **`*TRSNEW <image.dsk> [cyls] [sides] [density]`:**
   * Formats a brand new, empty LS-DOS 6.3 disk image directly on the FAT32 card.

### 7.3 Eight-Character Basename Compliance & Command Summary

Conforming to the LS-DOS 6.x constraint of **<= 8-character basenames**:

| Utility | Syntax | Opcode / Target | Description |
| :--- | :--- | :--- | :--- |
| `FNSTATUS` | `FNSTATUS` | `0xE8` (140B) | Displays FujiNet firmware, IP, MAC, gateway, and connection state. |
| `FNHOSTS` | `FNHOSTS` | `0xF4` (256B) | Lists all 8 configured FujiNet host slots (0–7). |
| `FNGETH` | `FNGETH <slot>` | `0xF4` | Displays hostname for a specific host slot (0–7). |
| `FNSETH` | `FNSETH <slot> "<host>"` | `0xF3` (256B) | Configures remote hostname or TNFS URL for a host slot. |
| `FNMOUNT` | `FNMOUNT <slot>` | `0xF9` | Mounts a host slot (connects FujiNet to TNFS server or SD card). |
| `FNDEVS` | `FNDEVS` | `0xF2` (152B / 304B) | Lists virtual device slots (0–3 in Stage 1; 0–7 in Stage 2), files, and modes. |
| `FNGETDEV` | `FNGETDEV <slot>` | `0xDA` (256B) | Displays pathname of disk image mounted in device slot (0–7). |
| `FNSETDEV` | `FNSETDEV <s> <h> <m> "<p>"`| `0xE2` (261B) | Assigns disk image on host slot to device slot (quotes preserve path case). |
| `FNMNTIMG` | `FNMNTIMG <slot> [mode]` | `0xF8` | Commits image mount, queries GAT, and synchronizes live DCT geometry. |
| `DRVMAP` | `DRVMAP` | Local / Guest DCT | Displays unified mapping of all 8 logical drives (Local SD vs FujiNet). |
| `SDMOUNT` | `SDMOUNT <drv> "<path>"` | Local SPI0 Mailbox | Mounts or swaps a local Agon SD card `.dsk` image into a logical drive. |

### 7.4 Detailed Specification of the 11 Command Utilities

#### 1. `FNSTATUS.CMD`
* **Syntax:** `FNSTATUS`
* **Payload:** Issues `OP_FUJI` (`0xE2`) + `0xE8` (`FUJI$GetAdapterCfg`); verifies handshake via `GetError` (`0x01`); reads 140 bytes of `AdapterConfig` struct via `GetResponse`.
* **Field Offsets:** SSID (0..32, 33B), Hostname (33..96, 64B), IP (97..100, 4B), Gateway (101..104, 4B), Netmask (105..108, 4B), DNS (109..112, 4B), MAC (113..118, 6B), BSSID (119..124, 6B), Firmware (125..139, 15B).

#### 2. `FNHOSTS.CMD`
* **Syntax:** `FNHOSTS`
* **Payload:** Issues `OP_FUJI` + `0xF4` (`FUJI$GetHostSlots`); reads 256 bytes (8 slots * 32 bytes). Displays slot index (0–7) and hostname string for each non-empty entry.

#### 3. `FNGETH.CMD`
* **Syntax:** `FNGETH <slot>` (`slot` = 0..7)
* **Payload:** Reads 256-byte host table via `0xF4` and extracts the 32-byte record corresponding to the specified slot. Displays hostname or `(empty)`.

#### 4. `FNSETH.CMD`
* **Syntax:** `FNSETH <slot> "<hostname>"`
* **Payload:** Reads full 256-byte table (`0xF4`), updates targeted 32-byte slot with maximum 31 characters + NUL terminator, and writes back complete table via `0xF3` (`FUJI$SetHostSlots`, 258 bytes total on wire). Enclosing `<hostname>` in double quotes prevents the TRSDOS CLI scanner from uppercasing domain names or URL paths.

#### 5. `FNMOUNT.CMD`
* **Syntax:** `FNMOUNT <slot>` (`slot` = 0..7)
* **Payload:** Issues `OP_FUJI` + `0xF9` (`FUJI$MountHost`) + `<slot>`. Verifies return status via `GetError` (`0x01`). Mounts remote TNFS export or SD card filesystem on the FujiNet device.

#### 6. `FNDEVS.CMD`
* **Syntax:** `FNDEVS`
* **Payload:** Issues `OP_FUJI` + `0xF2` (`FUJI$GetDevSlots`). Reads 152 bytes (4 device slots * 38 bytes: 1B HostSlot, 1B Mode, 36B Filename) in Stage 1, or 304 bytes (8 device slots) in Stage 2. Displays virtual device slot assignments.

#### 7. `FNGETDEV.CMD`
* **Syntax:** `FNGETDEV <slot>` (`slot` = 0..7)
* **Payload:** Issues `OP_FUJI` + `0xDA` (`FUJI$GetDevFile`) + `<slot>`; reads 256-byte null-terminated full path string.

#### 8. `FNSETDEV.CMD`
* **Syntax:** `FNSETDEV <dev_slot> <host_slot> <mode> "<path>"`
* **Payload:** Issues `OP_FUJI` + `0xE2` (`FUJI$SetDevFile`) + `<dev_slot>` + `<host_slot>` + `<mode>` + `<path[256]>` (261 wire bytes total). Double quotes preserve case-sensitive paths on Linux TNFS servers.

#### 9. `FNMNTIMG.CMD`
* **Syntax:** `FNMNTIMG <slot> [mode]`
* **Payload & Lifecycle:** Issues `OP_FUJI` + `0xF8` (`FUJI$MountImg`) + `<slot>` + `<mode>`. Reads Sector 0 and directory GAT over DriveWire, synchronizes target drive's DCT (`DCTn$+3`..`DCTn$+9`), and writes opcode `0xC3` (`JP FDCDVR`) to `DCTn$`.

#### 10. `DRVMAP.CMD`
* **Syntax:** `DRVMAP`
* **Payload:** Inspects all 8 logical drive DCTs (`DCT0$`..`DCT7$`) and the storage target table. Displays whether each drive is mapped to **Local SD** (`sd:/path/image.dsk`) or **FujiNet** (Slot number, access mode, geometry).

#### 11. `SDMOUNT.CMD`
* **Syntax:** `SDMOUNT <drv> "<path>"`
* **Payload:** Updates the local SPI0 driver table for logical drive `<drv>`, sets starting LBA for `<path>` on the Agon microSD card, derives geometry from Sector 0 / GAT, and activates the drive vector.

### 7.5 LS-DOS System Interface & SVC Calling Conventions

TRSDOS command utilities interface with LS-DOS via standard system SVCs dispatched through `RST 28h` (decimal `RST 40`):
* **`@PARAM` (SVC 17 / 11h):** Parses command line arguments passed in the system command line buffer (`HL`). Preserves quoted string literals exactly as entered.
* **`@DSPLY` (SVC 10 / 0Ah):** Emits strings pointed to by register `HL` to the video console. Strings must be terminated by Carriage Return (`0x0D`, with newline) or ETX (`0x03`, without newline). Strings are *not* null-terminated!
* **`@PRINT` (SVC 14 / 0Eh):** Directs character or string output to the printer DCB.
* **`@ERROR` (SVC 26 / 1Ah):** Formats and outputs standard LS-DOS error message strings corresponding to error codes in register `A`.
* **`@EXIT` (SVC 22 / 16h):** Cleanly terminates the utility program and returns control to the TRSDOS Ready prompt (HL = exit status).

---

## 8. Build Pipeline with ZDS II & Authentic CMD Packaging

### 8.1 Toolchain Configuration & AgonDev Parity

The build system leverages Zilog ZDS II (`ez80asm.exe`) executed under Wine on macOS, with proven byte-for-byte binary parity verified against native AgonDev on Linux:
```makefile
WINE     ?= wine64
ZDS_DIR  ?= $(HOME)/.wine/drive_c/Zilog/ZDSII_eZ80Acclaim!_5.3.5
AS       := $(WINE) "$(ZDS_DIR)/bin/ez80asm.exe"
ASFLAGS  := -define:_EZ80ACCLAIM!=1 \
            -include:"$(INCLUDES)" \
            -list -name -quiet -NOsdiopt -warn -debug -NOigcase -cpu:eZ80F91
```

### 8.2 Authentic TRSDOS `.CMD` Load Module Generation

TRSDOS / LS-DOS 6.3 executable command files (`/CMD`) require strict load module records:
* **Record Type 01 (Load Block):**
  `01 <length> <addr_lo> <addr_hi> <payload...>`
  where `<length>` equals `(payload_length + 2) & 0xFF`.
* **Record Type 02 (Transfer / Entry Address):**
  `02 02 <entry_lo> <entry_hi>`
  terminates loading and executes program at `<entry_address>` (e.g. `2600h`).
* **Record Type 06 (PDS Header):** Historical module header; skipped by standard TRSDOS loader.

### 8.3 Automated Build Script & Packaging Pipeline

A dedicated Python packaging tool (`tools/bin_to_cmd.py`) processes linked binary outputs and formats authentic `.CMD` files:
```makefile
%.cmd: %.s $(INCLUDES)
	$(AS) $(ASFLAGS) $< -out:$*.obj
	$(LD) ... -out:$*.bin
	python3 tools/bin_to_cmd.py --load 2600 --entry 2600 $*.bin $@
```

---

## 9. Technical Challenges, Timing & Risk Mitigations

### 9.1 Physical Wire Timing & Bandwidth Ceilings

At 115,200 baud (8 data bits, 1 stop bit, no parity = 10 bits per character frame):
* 1 byte requires 86.8 µs of physical wire time.
* A complete 264-byte sector transaction (`OP_READEX` 5B header + 256B payload + 2B checksum + 1B status) requires **22.92 ms minimum wire time**.
* Theoretical maximum sector read throughput is **10.9 KiB/s**, before host I/O, network latency, and server turnarounds.
* At 230,400 baud, minimum wire time drops to **11.46 ms** (payload ceiling ~21.8 KiB/s).

### 9.2 UART FIFO Overrun Protection & 60 Hz Timer Clock Drift

* **FIFO Constraints:** The eZ80 hardware UART1 FIFO holds only 16 bytes, representing **1.39 ms of buffer time** at 115,200 baud. If byte polling is interrupted or delayed by >= 1.39 ms, a FIFO Overrun Error (OE) occurs.
* **60 Hz Timer Tick Suppression & Clock Drift:** Timer 1 generates a periodic 60 Hz interrupt (16.67 ms). Because a single 264-byte `OP_READEX` transaction takes >= 22.92 ms, holding interrupts disabled (`DI`) continuously throughout the sector transfer drops timer ticks.
* **Mitigation Architecture:**
  1. **Surgical Interrupt Bracketing:** Mask interrupts (`DI`) only during burst transfers (5B header, 256B payload, 2B checksum, 1B status). Re-enable interrupts (`EI`) during checksum computation and inter-packet turnarounds.
  2. **Periodic RTC Resynchronization:** Query FujiNet's `OP_TIME` (`0x23`) during idle periods to resynchronize the TRSDOS system time-of-day clock against network time.
  3. **Interrupt Isolation:** `UART1_IER` is held at `0x00` during polled disk I/O to prevent stray UART interrupt handlers from corrupting registers.

### 9.3 ESP8266 `+++` Control Escape Timing & Binary Transparency

When using MOD-WIFI-ESP8266 in transparent streaming mode (`CIPMODE=1`), escaping back to AT command mode requires the standard Hayes-compatible escape sequence `+++`:
* Requires > 20 ms silence before first `+`, < 20 ms between each `+`, and > 20 ms silence after the third `+`.
* **Binary Transparency:** Any arbitrary binary sector data containing the sequence `+++` (`0x2B 0x2B 0x2B`) will **not** trigger escape mode because disk sectors stream continuously without matching silence guard intervals.

---

## 10. Phased Implementation Roadmap

```
=================================================================================
STAGE 1: Stock Firmware Baseline & Read-Only Storage Proof (Drives :0-:4 / Slots 0-3)
=================================================================================

+-------------------------------------------------------------------------------+
| PHASE 1: Lean Kernel & Network Boot Proof                                     |
| - Build lean ~64 KB TRSDOS.bin (eliminating embedded 425 KB RAM disk).        |
| - Verify pure network boot of Drive :0 from FujiNet Slot 0 in emulator/bench. |
| - Test directory listing (DIR :0) and file loading (LOAD "ELIZA/BAS:0").      |
| - Verify write rejection (R_PROTECTED / Error 15) on network drives.          |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 2: Core Storage Driver Routing (trs-os-zds)                             |
| - Implement router in driver-FDCDVR.S dispatching to DWDVR and SDDVR.         |
| - Implement driver-DWDVR.S (OP_READEX 0xD2, dw_offset table, big-endian sum). |
| - Implement driver-SDDVR.S (eZ80 SPI0 block I/O for local Agon microSD .dsk). |
| - Add explicit UART1 initialization in finish_init (SYS0_IPL).                |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 3: Build Pipeline & Authentic CMD Packager                              |
| - Build and verify lean TRSDOS.bin via Wine/ZDS II and native AgonDev.        |
| - Build bin_to_cmd.py packager emitting authentic Record Type 01/02 formats.  |
| - Establish automated Makefile targets for OS image and utility binaries.     |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 4: Pre-Boot & In-DOS Utility Suites                                     |
| - Implement FNBOOT visual manager in Quark MOS.                               |
| - Implement MOS file interchange tools (*TRSDIR, *TRSEXT, *TRSADD, *TRSNEW).  |
| - Implement FN*.CMD suite: FNSTATUS, FNHOSTS, FNMOUNT, FNDEVS, FNSETDEV, etc. |
| - Implement DRVMAP.CMD (unified drive mapping) and SDMOUNT.CMD.               |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 5: Physical Hardware Bench Qualification                                |
| - Qualify Direct Serial Link (MOD-USB-RS232) on Agon Light 2 hardware.        |
| - Qualify Wi-Fi TCP/IP streaming (MOD-WIFI-ESP8266) with openstream.          |
| - Verify 3 Operating Modes: Pure Network, Pure Local, and Hybrid operation.   |
| - Verify clock stability during heavy multi-sector transfers.                 |
+-------------------------------------------------------------------------------+

=================================================================================
STAGE 2: Patched Firmware Expansion & Writable Storage (Drives :0-:7 / Slots 0-7)
=================================================================================

+-------------------------------------------------------------------------------+
| PHASE 6: FujiNet Server Firmware Patches                                      |
| - Patch BUILD_COCO drivewire.cpp (MAX_DWDISK_DEVICES = 8, big-endian sums).   |
| - Patch mediaTypeDSK.cpp (image boundary enforcement and flush verification). |
| - Verify patched server against negative corruption test suites.              |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 7: Multi-Drive Expansion & Writable Storage Qualification               |
| - Expand device mapping across Drives :0 through :7 (FujiNet Slots 0–7).      |
| - Expand FNDEVS to query 8 device slots (304 bytes).                          |
| - Qualify OP_WRITE (0x57) with SHA-256 pre/post write image hashes.           |
| - Enable full read/write support on network drives.                           |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 8: Peripheral Drivers & High-Capacity Architecture                      |
| - Implement driver-TOD.S with OP_TIME (0x23) year modulo-100 normalization.   |
| - Implement driver-PRDVR.S with OP_PRINT (0x50) line printer spooling.        |
| - Design dedicated hard disk driver architecture for 5–32 MB HDV volumes.     |
+-------------------------------------------------------------------------------+
```

---

## 11. Verification & Test Matrix

| Test ID | Objective | Test Procedure | Acceptance Criteria |
| :--- | :--- | :--- | :--- |
| **TC-01** | Server Silence Verification | Connect to port 65504 on loopback; monitor for 10 seconds. | Zero bytes received prior to first client command `0xE2 0x00`. |
| **TC-02a** | Direct Serial Handover | Connect MOD-USB-RS232; boot via `FNBOOT` or `OSboot`. | Boots cleanly to TRSDOS Ready; UART1 functions in polled mode. |
| **TC-02b** | Wi-Fi Handover | Connect MOD-WIFI-ESP8266; run `openstream` -> `FNBOOT`. | Stream preserved; TRSDOS boots to prompt without freezing. |
| **TC-02c** | Pure Network Boot of Drive `:0`| Boot `TRSDOS.bin` with no local disk; mount `:0` on Net Slot 0. | Reads Sector 0, GAT, and `SYS0`..`SYS6` from FujiNet; prompt appears. |
| **TC-03** | Checksum Vectors | Compute checksum across known 256B test vectors. | Matches big-endian unsigned summation sum(b_i) % 65536 exactly. |
| **TC-04** | `OP_READEX` Verification | Read known sector from Drive `:0` via `0xD2` opcode. | 256 bytes read, client checksum sent, status byte `0x00` returned. |
| **TC-05** | Write Rejection (Stage 1) | Attempt write to network drive (`SAVE "TEST:0"`). | Immediate `R_PROTECTED` (error 15); host image remains unmodified. |
| **TC-06** | Container Offset Verification | Read Sector 0 from raw JV1 vs DiskDISK image. | Correctly adds +0 for JV1 (LSN 0) and +1 for DiskDISK (LSN 1); returns valid GAT. |
| **TC-07** | `FNSTATUS` Verification | Run `FNSTATUS` from TRSDOS Ready prompt. | Correctly parses and prints 140-byte adapter configuration via `@DSPLY`. |
| **TC-08** | Host Table Management | Run `FNHOSTS` and `FNSETH 0 "tnfs.fujinet.online"`. | Displays 8 slots (256 bytes); updates and persists slot 0 without case distortion. |
| **TC-09** | Device Table Query | Run `FNDEVS` from TRSDOS Ready prompt. | Correctly queries device slot table (152B for 4 slots in Stage 1; 304B for 8 slots in Stage 2). |
| **TC-10** | Path Quoting Case Preservation | Run `FNSETDEV 0 0 1 "/disks/trs80/game.dsk"`. | Server mounts exact path without uppercase conversion (`/disks/...`, not `/DISKS/...`). |
| **TC-11** | Live DCT Geometry Synchronization | Run `FNMNTIMG 1 1` on unmounted Drive `:1`. | Sector 0 read; DCT geometry populated; byte 0 changed to `0xC3`; `DIR :1` succeeds. |
| **TC-12** | Directory & File Execution | Run `DIR :0` and load executable (`RUN "TEST/CMD:0"`). | Directory displays files; executable loads and runs cleanly. |
| **TC-13** | Timer 1 Clock Stability | Perform multi-sector file load (>= 64 sectors). | System time-of-day clock does not drift by > 1 tick during transfer. |
| **TC-14** | Stage 2 7-Drive Multi-Mount | Mount unique disks to slots 0–6; access Drives `:1`..`:7`. | All 7 drives respond independently without data crosstalk or buffer pollution. |
| **TC-15** | Stage 2 Writable Storage | Write 10 sectors via `SAVE`; verify on server. | Host image SHA-256 matches written data; no trailing image corruption. |
| **TC-16** | RTC Normalization (`OP_TIME`) | Run system clock init via `driver-TOD.S`. | Year 126 normalized to 26; `DATE$` renders valid `MM/DD/26` date string. |
| **TC-17** | Local Agon SD Mounting | Mount `WORK.DSK` from Agon microSD via `SDMOUNT 1 "WORK.DSK"`. | Reads sectors via eZ80 SPI0; `DIR :1` lists files; writes commit to flash. |
| **TC-18** | MOS File Tools Verification | In MOS, run `*TRSDIR`, `*TRSEXT`, `*TRSADD` on test image. | Files extracted and added match binary SHA-256 hashes. |
| **TC-19** | Hybrid Cross-Tier Copy | In TRSDOS, execute `COPY FILE/CMD:0 TO :1` (Net to Local SD). | File streams across UART1, commits to SPI0 flash; executes cleanly from `:1`. |

---

## 12. Project Reference Matrix & Upstream Sources

### 12.1 Primary Repositories

| Project / Component | Upstream GitHub Repository | Role in Integration |
| :--- | :--- | :--- |
| **`agon-net-utils`** | [rlucente-retro/agon-net-utils](https://github.com/rlucente-retro/agon-net-utils) | Agon Quark MOS utilities (`openstream`, `sendstream`, `closestream`) managing ESP8266 transparent streaming mode (`CIPMODE=1`) for Wi-Fi; not required for direct serial links. |
| **`trs-net-tcp`** | [rlucente-retro/trs-net-tcp](https://github.com/rlucente-retro/trs-net-tcp) | Legacy Python TCP/serial disk server (`trs-netd.py`) and protocol evaluation documentation being superseded. |
| **`trs-os-zds`** | [rlucente-retro/trs-os-zds](https://github.com/rlucente-retro/trs-os-zds) | Core operating system source tree (`TRSDOS_7`), Agon Light 2 patchset, and ZDS II Wine build automation for `TRSDOS.bin`. |
| **`fujinet-firmware`** | [FujiNetWIFI/fujinet-firmware](https://github.com/FujiNetWIFI/fujinet-firmware) | Target server framework providing DriveWire (`BUILD_COCO`) block I/O, `OP_FUJI` transactions, and BoIP/serial transports. |
| **`nitros9`** | [nitros-9/nitros9](https://github.com/nitros-9/nitros9) | Reference implementations for the 9 FujiNet client utilities targeting DriveWire on NitrOS-9. |
| **`OSboot` Loader** | [sijnstra/agon-projects/OSboot](https://github.com/sijnstra/agon-projects/tree/main/OSboot) | MOS bootstrap loader for launching `TRSDOS.bin` while maintaining UART1 streaming continuity. |

### 12.2 NitrOS-9 FujiNet Reference Source Files

The TRSDOS utility suite (`FN*.CMD`) is modeled directly on the following reference commands written in 6809 assembly code from [nitros-9/nitros9](https://github.com/nitros-9/nitros9):

1. [`level1/cmds/fnstatus.as`](https://github.com/nitros-9/nitros9/blob/main/level1/cmds/fnstatus.as) — Query and format adapter status and Wi-Fi connection state.
2. [`level1/cmds/fnlisthosts.as`](https://github.com/nitros-9/nitros9/blob/main/level1/cmds/fnlisthosts.as) — Enumerate all 8 FujiNet host slots.
3. [`level1/cmds/fngethost.as`](https://github.com/nitros-9/nitros9/blob/main/level1/cmds/fngethost.as) — Display host assignment for an individual slot.
4. [`level1/cmds/fnsethost.as`](https://github.com/nitros-9/nitros9/blob/main/level1/cmds/fnsethost.as) — Configure hostname/TNFS address for a host slot.
5. [`level1/cmds/fnmount.as`](https://github.com/nitros-9/nitros9/blob/main/level1/cmds/fnmount.as) — Mount a host slot connection.
6. [`level1/cmds/fnlistdevs.as`](https://github.com/nitros-9/nitros9/blob/main/level1/cmds/fnlistdevs.as) — Enumerate virtual device (disk) slots.
7. [`level1/cmds/fngetdevfile.as`](https://github.com/nitros-9/nitros9/blob/main/level1/cmds/fngetdevfile.as) — Query mounted disk image path for a device slot.
8. [`level1/cmds/fnsetdevfile.as`](https://github.com/nitros-9/nitros9/blob/main/level1/cmds/fnsetdevfile.as) — Assign disk image path and access mode to a device slot.
9. [`level1/cmds/fnmountimg.as`](https://github.com/nitros-9/nitros9/blob/main/level1/cmds/fnmountimg.as) — Commit/mount disk image for active read/write I/O.
10. [`lib/fuji.as`](https://github.com/nitros-9/nitros9/blob/main/lib/fuji.as) — Shared DriveWire `OP_FUJI` transaction protocol engine (`NOpen`, `FBReady`, `FBCmd`, `FBErr`, `FBRead`).
