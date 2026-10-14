# Architectural Plan: FujiNet Firmware Integration for TRS-OS on Agon Light 2

**Document Version:** 1.5.0  
**Target Architecture:** Zilog eZ80F92 (Olimex Agon Light 2)  
**Target Operating System:** TRS-OS (TRSDOS 6.3.1 / LS-DOS 6.3 adapted for eZ80)  
**Architectural Baseline:**  
- **Boot Chain:** Agon Quark MOS $\to$ `OSboot.bin` $\to$ `TRS-OS` (eZ80 HAL/BIOS) $\to$ `TRSDOS / LS-DOS 6.3.1` (Z80 mode)  
- **Monolithic System Image:** `TRSDOS.bin` is a 491,520-byte (480 KiB) monolithic binary containing the 64 KB eZ80 kernel and 512-byte slice tables (`0x000000`–`0x0101FF`), followed by the embedded bootstrap Volume 0 DiskDISK image (`0x010200`–`0x077FFF`).
- **Minimal Bootstrap Storage:** Drive `:0` (Minimal Bootstrap RAM Drive) mounts from Slice 1 Volume 0 in RAM, containing the essential OS kernel overlays (`SYS0/SYS`..`SYS6/SYS`) and FujiNet command tools (`FN*.CMD`) to bootstrap and mount network storage. (Non-persistent secondary RAM disk `:1` is eliminated).
- **Stage 1 Network Mass Storage:** Drives `:1` through `:4` mapped directly to FujiNet Device Slots 0 through 3 on stock CoCo `fujinet-firmware` (`MAX_DWDISK_DEVICES = 4`), operating in strict read-only mode (`bit 7, DCT+3 = 1`). Drives `:5` through `:7` remain reserved.
- **Stage 2 Firmware & Drive Expansion:** Recompiling `fujinet-firmware` with `MAX_DWDISK_DEVICES 8` unlocks all seven network drives (`:1` through `:7`, mapping to Slots 0–6 via `slot = LDRV - 1`, matching FujiNet Web UI Drives 1–7) and enables validated write support.
**Host / Coprocessor:** `fujinet-firmware` (`BUILD_COCO` DriveWire Bus over Serial / BoIP)  
**Toolchain:** Zilog ZDS II (`ez80asm.exe` via Wine) matching `trs-os-zds` (with verified native AgonDev parity)  
**Physical Transports:**  
- **Transport A (Direct Serial Link):** Olimex MOD-USB-RS232 on Agon UEXT (`PC0`/`PC1`); point-to-point hardware connection inherently active at reset (bypasses `openstream` and `closestream`).  
- **Transport B (Wi-Fi TCP/IP Streaming):** Olimex MOD-WIFI-ESP8266 on Agon UEXT; requires `openstream` in MOS to enter transparent passthrough mode (`CIPMODE=1`) prior to boot, and `closestream` (`+++`) upon exit.  

---

## Table of Contents

1. [Executive Summary & Objectives](#1-executive-summary--objectives)
2. [Codebase Analysis & Architectural Overview](#2-codebase-analysis--architectural-overview)
   - [2.1 Architectural Hierarchy: OSboot, TRS-OS HAL, and TRSDOS/LS-DOS 6.3.1](#21-architectural-hierarchy-osboot-trs-os-hal-and-trsdosls-dos-631)
   - [2.2 Driver & Subsystem Disposition Matrix](#22-driver--subsystem-disposition-matrix)
   - [2.3 `agon-net-utils`: MOS Network Bootstrap (Wi-Fi Only)](#23-agon-net-utils-mos-network-bootstrap-wi-fi-only)
   - [2.4 `trs-net-tcp`: Existing Host Daemon & Protocol](#24-trs-net-tcp-existing-host-daemon--protocol)
   - [2.5 `trs-os-zds`: Agon Light 2 Operating System Core](#25-trs-os-zds-agon-light-2-operating-system-core)
   - [2.6 `fujinet-firmware`: Multi-Protocol Peripheral Server](#26-fujinet-firmware-multi-protocol-peripheral-server)
   - [2.7 `nitros9`: Reference FujiNet Client Implementations](#27-nitros9-reference-fujinet-client-implementations)
3. [Protocol Validation & Hardware Handover](#3-protocol-validation--hardware-handover)
   - [3.1 Validation of Direct Serial Protocol Support](#31-validation-of-direct-serial-protocol-support)
   - [3.2 Verification of Server Silence & Shared Transaction State](#32-verification-of-server-silence--shared-transaction-state)
   - [3.3 Boot Lifecycle, Execution Flow & Explicit UART1 Reinitialization](#33-boot-lifecycle-execution-flow--explicit-uart1-reinitialization)
4. [Universal Disk Image Support: JV1, DiskDISK, and HDV](#4-universal-disk-image-support-jv1-diskdisk-and-hdv)
   - [4.1 Image Formats & Container Comparison](#41-image-formats--container-comparison)
   - [4.2 Header Offset Handling & Container Translation](#42-header-offset-handling--container-translation)
   - [4.3 Geometry Derivation & Non-Universal GAT Placement](#43-geometry-derivation--non-universal-gat-placement)
   - [4.4 HDV Hard Disk Support vs. Floppy Driver Addressing Bounds](#44-hdv-hard-disk-support-vs-floppy-driver-addressing-bounds)
5. [Wire Protocol Specifications](#5-wire-protocol-specifications)
   - [5.1 Corrected DriveWire Block Storage Protocol (`OP_READEX` & `OP_WRITE`)](#51-corrected-drivewire-block-storage-protocol-op_readex--op_write)
   - [5.2 16-Bit Big-Endian Checksum Specification & Test Vectors](#52-16-bit-big-endian-checksum-specification--test-vectors)
   - [5.3 Audit of Upstream Server Write Defects & Safety Mitigation](#53-audit-of-upstream-server-write-defects--safety-mitigation)
   - [5.4 FujiNet Configuration Transaction Protocol (`OP_FUJI`)](#54-fujinet-configuration-transaction-protocol-op_fuji)
6. [TRS-OS Drive Architecture & Storage Management](#6-trs-os-drive-architecture--storage-management)
   - [6.1 Local Storage Coexistence & Drive Allocation Policy](#61-local-storage-coexistence--drive-allocation-policy)
   - [6.2 Device Control Table (DCT) Management & Write-Protection](#62-device-control-table-dct-management--write-protection)
   - [6.3 Storage Driver Routing & DriveWire Read Integration (`driver-FDCDVR.S`)](#63-storage-driver-routing--drivewire-read-integration-driver-fdcdvrs)
   - [6.4 Real-Time Clock & Line Printer Drivers](#64-real-time-clock--line-printer-drivers)
7. [Native TRSDOS FujiNet Utilities Specification](#7-native-trsdos-fujinet-utilities-specification)
   - [7.1 Architecture & Peripheral Scope (TRSDOS Transport Decoupling)](#71-architecture--peripheral-scope-trsdos-transport-decoupling)
   - [7.2 Eight-Character Basename Compliance & Command Summary](#72-eight-character-basename-compliance--command-summary)
   - [7.3 Detailed Specification of the 9 Command Utilities](#73-detailed-specification-of-the-9-command-utilities)
   - [7.4 LS-DOS System Interface & System SVC Conventions](#74-ls-dos-system-interface--system-svc-conventions)
8. [Build Pipeline with ZDS II & Authentic CMD Packaging](#8-build-pipeline-with-zds-ii--authentic-cmd-packaging)
   - [8.1 Toolchain Configuration & AgonDev Parity](#81-toolchain-configuration--agondev-parity)
   - [8.2 Authentic TRSDOS `.CMD` Load Module Generation](#82-authentic-trsdos-cmd-load-module-generation)
   - [8.3 Automated Build Script & Packaging Pipeline](#83-automated-build-script--packaging-pipeline)
9. [Technical Challenges, Timing & Risk Mitigations](#9-technical-challenges-timing--risk-mitigations)
   - [9.1 Physical Wire Timing & Bandwidth Ceilings](#91-physical-wire-timing--bandwidth-ceilings)
   - [9.2 UART FIFO Overrun Protection & Interrupt Isolation](#92-uart-fifo-overrun-protection--interrupt-isolation)
   - [9.3 ESP8266 `+++` Control Escape Timing & Binary Transparency](#93-esp8266--control-escape-timing--binary-transparency)
10. [Phased Implementation Roadmap](#10-phased-implementation-roadmap)
11. [Verification & Test Matrix](#11-verification--test-matrix)
12. [Project Reference Matrix & Upstream Sources](#12-project-reference-matrix--upstream-sources)

---

## 1. Executive Summary & Objectives

The primary objective of this project is to evolve the network mass storage and peripheral infrastructure of **TRS-OS** (Daniel Paul Martin's port of TRSDOS 6.3.1 / LS-DOS 6.3 for the Zilog eZ80 on the Olimex Agon Light 2) by replacing the legacy, bespoke `trs-net-tcp` server protocol with standard **FujiNet** protocols provided by **`fujinet-firmware`**.

### Core Goals:
1. **Adopt Industry-Standard Protocols with Validated Wire Handshakes:** Replace the legacy ASCII text protocol (`<00012\n`, `@ping\n`, `@bind\n`) with the high-performance binary **DriveWire 3/4** protocol (`OP_READEX = 0xD2`, `OP_WRITE = 0x57`) and the **`OP_FUJI` (0xE2)** transaction protocol.
2. **Build Upon Established Architecture:** Preserve the proven execution chain (`OSboot` $\to$ `TRS-OS` HAL $\to$ `TRSDOS/LS-DOS 6.3.1`), video/console VDP translation, keyboard routines, 60 Hz timer tasker, and minimal RAM slice virtual floppy driver for Drive `:0`. Replace the legacy line-oriented ASCII networking routines in `driver-FDCDVR.S` and the IPL handshake with binary DriveWire `OP_READEX` (`0xD2`) / `OP_WRITE` (`0x57`) and `OP_FUJI` (`0xE2`), eliminating the non-persistent RAM data disk `:1` in favor of persistent FujiNet network storage.
3. **Dual Physical Transport Support (Direct Serial & Wi-Fi Streaming):**
   * **Direct Serial Link (e.g., Olimex MOD-USB-RS232):** Direct point-to-point hardware connection between the Agon Light 2 UEXT connector (UART1) and FujiNet hardware or a host PC running FujiNet-PC. Because the physical serial link is inherently present and active upon power-on and hardware reset, no connection handshake is required. Agon Quark MOS directly boots TRS-OS using `*OSboot -2f TRSDOS.bin` without needing `openstream` or `closestream`.
   * **Wi-Fi TCP/IP Streaming (e.g., Olimex MOD-WIFI-ESP8266):** Retains the proven `openstream` $\to$ `OSboot.bin` $\to$ `TRSDOS.bin` workflow where Agon Quark MOS uses AT commands via `openstream` to connect the ESP8266 to FujiNet's BoIP server (TCP port 65504) and enter transparent UART passthrough mode (`CIPMODE=1`), boots TRS-OS, and leaves the transparent stream intact. `closestream` is used if returning to MOS to restore command mode (`AT+CIPMODE=0`).
4. **Safety-First, Staged Storage Deployment:**
   * **Stage 1 (Production Baseline):** Preserve minimal Drive `:0` in RAM (embedding bootstrap Volume 0 at `0x010200`) with just enough system overlays (`SYS0`..`SYS6`) and `FN*.CMD` utilities to bootstrap. Assign **Drives `:1` through `:4`** as persistent network mass storage volumes mapped to FujiNet Device Slots 0–3, operating in strict **Read-Only** mode (`WP`) on stock CoCo firmware (`MAX_DWDISK_DEVICES = 4`) to guarantee zero data corruption on host media. *Workflow Note:* File writing and saving are disabled in Stage 1 and unlocked in Stage 2.
   * **Stage 2 (Writable Storage & Server Fixes):** Address confirmed upstream firmware flaws in `BUILD_COCO` (disabled write checksum validation, unbounded DSK writes, and ignored flush errors) via server patches, recompile firmware with `MAX_DWDISK_DEVICES 8`, and pass negative regression qualifications before enabling writes.
5. **Multi-Drive FujiNet Network Expansion:**
   * In Stage 1: Support 4 virtual device slots (Slots 0–3 mapped to Drives `:1`–`:4`).
   * In Stage 2: Expand to **8 Virtual Device Slots (Slots 0–7)** and **8 Host Slots (Slots 0–7)** in FujiNet firmware. Map all seven TRSDOS network drives (`:1` through `:7`) directly to FujiNet Device Slots 0 through 6 via `slot = LDRV - 1` (corresponding to Drives 1 through 7 in the FujiNet Web UI). Dynamically enable guest DCTs (`JP FDCDVR`) and derive geometry on mount.
6. **Native TRSDOS Utilities via Wine Toolchain:** Implement native TRSDOS/LS-DOS 6.3 command utilities (`.CMD`) functionally equivalent to the NitrOS-9 FujiNet utilities, complying with TRSDOS's **8-character basename limit**, authentic load-record formatting, quoted string support for case-sensitive TNFS paths, and guest SVC interfaces.

---

## 2. Codebase Analysis & Architectural Overview

### 2.1 Architectural Hierarchy: OSboot, TRS-OS HAL, and TRSDOS/LS-DOS 6.3.1

The execution environment is structured into three cooperating layers, as documented in [`TRSOS-BOOT-ARCHITECTURE.md`](TRSOS-BOOT-ARCHITECTURE.md):

```
+-------------------------------------------------------------------------------+
| 1. OSboot (Shawn Sijnstra)                                                    |
|    - Quark MOS utility running in eZ80 Z80 mode.                              |
|    - Configures VDP video/font, loads TRSDOS.bin into RAM, sets platform tag. |
+-------------------------------------------------------------------------------+
                                        |
                                        v Jumps to 0x000000 (__init)
+-------------------------------------------------------------------------------+
| 2. TRS-OS (Daniel Paul Martin)                                                |
|    - eZ80 Hardware Abstraction Layer (HAL), BIOS, and Initial Program Loader. |
|    - Handles CPU registers, eZ80 Timers, UART0 (VDP), UART1 (UEXT), and RAM.   |
|    - Emulates Model 4 hardware (video RAM, keyboard matrix, floppy controller)|
+-------------------------------------------------------------------------------+
                                        |
                                        v Hands off to DOSINIT -> zCMNDI
+-------------------------------------------------------------------------------+
| 3. TRSDOS / LS-DOS 6.3.1 (Logical Systems Inc. / Tandy)                       |
|    - Authentic 1980s TRS-80 Model 4 disk operating system.                    |
|    - Runs in Z80 compatibility mode (ADL = 0) with a standard 64 KB map.      |
|    - Unaware of eZ80/Agon hardware; issues standard Model 4 SVCs and I/O.     |
+-------------------------------------------------------------------------------+
```

1. **`OSboot` (`OSboot.bin`)**: Shawn Sijnstra's MOS bootstrap loader utility. It executes from Quark MOS, programs the ESP32 Video Display Processor (VDP) with text modes and custom fonts, loads the monolithic `TRSDOS.bin` image into memory, flags the platform type (`UART0_SPR = 3`), and transfers control to the operating system.
2. **`TRS-OS` (`TRSDOS_7`)**: Daniel Paul Martin's modern port and hardware abstraction layer (HAL) for the Zilog eZ80 processor. It provides the BIOS, CPU initializers, video terminal translation, interrupt tables, timer tasks, and virtual floppy drivers (`driver-FDCDVR.S`) that simulate a TRS-80 Model 4 environment on modern eZ80 microcontrollers.
3. **`TRSDOS / LS-DOS 6.3.1`**: The authentic, historical disk operating system originally created by Logical Systems Inc. (LSI) as LS-DOS 6.3 and licensed by Tandy Corporation as TRSDOS 6.3.1 for the TRS-80 Model 4. It contains the system overlays (`SYS0/SYS` through `SYS6/SYS`), supervisor calls (SVCs), file management routines, and the command line interpreter (`TRSDOS Ready`).

### 2.2 Driver & Subsystem Disposition Matrix

To ensure stability and backward compatibility, the FujiNet integration builds directly upon the existing codebase, replacing only the components strictly required for network storage:

| Subsystem / Driver | Real Model 4 Hardware | Agon Light 2 Implementation under `TRS-OS` | Integration Strategy with FujiNet |
| :--- | :--- | :--- | :--- |
| **Video Display (`*DO`)** | Memory-mapped video at `0xF800`–`0xFFFF` (80×24 / 64×16). | Intercepted in `videoRAM.s`; characters converted to terminal stream over UART0 to ESP32 VDP. | **Preserved Untouched.** Reuses existing VDP translation and authentic TRS-80 font support. |
| **Keyboard Input (`*KI`)** | Memory-mapped matrix scan ports (`0x3801`–`0x3880`). | Intercepted in `SVC09-keyin.s`; reads ASCII keys received from VDP over UART0. | **Preserved Untouched.** Reuses existing keyboard handler. |
| **Real-Time Clock** | Hardware heartbeat interrupt (30/60 Hz). | eZ80 Timer 1 (`TMR1`) programmed to fire periodic 60 Hz interrupts; updates `TIME$` and `DATE$`. | **Preserved Untouched.** Maintains 60 Hz task scheduler; optionally updated via `OP_TIME` (`0x23`). |
| **Local RAM Drives** | Physical WD1773 Floppy Disk Controller. | Minimal in-memory RAM disk slice (`0x010200`) for Drive `:0` managed by `ramdrv$`. Non-persistent RAM data disk (`:1` at `0x045300`) eliminated. | **Minimal Bootstrap Baseline.** Drive `:0` contains only kernel (`SYS0`..`SYS6`) and `FN*.CMD` tools to mount persistent network drives. |
| **Device Control Tables (`DCT`)**| 10-byte tables (`DCT0$`..`DCT7$`) defining driver vectors and media geometry. | `DCT0$` and `DCT1$` contain `JP FDCDVR`; `DCT2$`–`DCT7$` contain `RET` (`0xC9`) stubs with dummy geometry. | **Dynamic Enablement.** `FNMNTIMG` enables `DCTn$` by writing `JP FDCDVR` (`0xC3`) and populates derived geometry upon mount. |
| **System Calls (`SVC`)** | `RST 28h` (RST 40 decimal) vector through Page 1 table. | Authentic Roy Soltoff / Logical Systems LS-DOS 6.3.1 SVC dispatch table. | **Preserved Untouched.** Full LS-DOS 6.3.1 API compatibility maintained. |
| **File System Manager** | `SYS4/SYS` (Directory parsing, FCBs, file extents, GAT/HIT). | Exact, original binary code from LS-DOS 6.3.1 executed directly by eZ80 CPU. | **Preserved Untouched.** All file parsing and extent management remain authentic. |
| **UART1 Warm Boot Setup** | Serial communications interface. | Configured during `finish_init` in `SYS0_IPL/ipl-eZ80_CPU.S`. | **Adapted / Preserved.** Solidifies `0001-uart1-streaming.patch` (Port C Mode 7, BRG=10, 8-N-1, IER=0, FCTL=07h). |
| **Network Disk Driver** | WD1773 FDC or bespoke network controller. | Legacy line-based ASCII protocol (`<XXXXX`, `>XXXXX`) in `driver-FDCDVR.S` for Drive `:6`. | **Replaced.** Overhauled to binary DriveWire `OP_READEX` (`0xD2`) / `OP_WRITE` (`0x57`) for Drives `:1`–`:4` (Stage 1) and `:1`–`:7` (Stage 2) via `funtable$`. |
| **IPL Network Startup** | Initial boot handshake. | Legacy `@ping` / `@bind` exchange in `ipl-BIOS.s` / `ipl_net_*.s`. | **Replaced.** `ipl-BIOS.s` refactored to neutralize Volume 1 RAM scan and `CKDRIVE 1` halt; pre-configures `DCT1$` for DriveWire. |
| **Line Printer (`*PR`)** | Centronics parallel printer port at I/O port `0xF8`. | Intercepted in `driver-PRDVR.S`. | **Adapted.** Redirects character stream to UART1 `OP_PRINT` (`0x50`) to FujiNet. |

```
+-------------------------------------------------------------------------------+
|                             AGON LIGHT 2 (CLIENT)                             |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  |       TRS-OS Operating System (TRSDOS 6.3.1 / LS-DOS 6.3 on eZ80)       |  |
|  |                                                                         |  |
|  | +-------------------------------+     +-------------------------------+ |  |
|  | |      BOOTSTRAP RAM DRIVE      |     |    DriveWire Block Driver     | |  |
|  | | Drive :0 (Minimal Boot RAM)   |     |        driver-FDCDVR.S        | |  |
|  | | SYS0..SYS6 + FN*.CMD Tools    |     | Drives :1..:7 (Net Slots 0-6) | |  |
|  | | (Bootstrap & Mount Engine)    |     | (1:1 with FujiNet UI 1..7)    | |  |
|  | +-------------------------------+     +---------------+---------------+ |  |
|  | | Native FujiNet Utilities      |                     | OP_READEX/WR    |  |
|  | | (FNSTATUS, FNMOUNT, etc.)     |                     | (0xD2 / 0x57)   |  |
|  | | Basenames <= 8 Characters     |                     | 24b LSN, CRC16  |  |
|  | +---------------+---------------+                     |                 |  |
|  |                 |                                     |                 |  |
|  |                 | OP_FUJI (0xE2)                      |                 |  |
|  |                 +------------------+------------------+                 |  |
|  |                                    |                                    |  |
|  |                                    v                                    |  |
|  | +---------------------------------------------------------------------+ |  |
|  | |  driver-UART1.S: Explicit 115200 8-N-1 Polled I/O (IER=0, FCTL=07h)  | |  |
|  | +----------------------------------+----------------------------------+ |  |
|  +------------------------------------|------------------------------------+  |
|                                       | UART1 (PC0/PC1 on UEXT Connector)     |
|                   +-------------------+-------------------+                   |
|                   |                                       |                   |
|                   v Option A: Direct Serial               v Option B: Wi-Fi   |
|   +-------------------------------+       +-------------------------------+   |
|   |     Olimex MOD-USB-RS232      |       |    Olimex MOD-WIFI-ESP8266    |   |
|   | (Direct Hardware Serial Link) |       |    (Wi-Fi TCP Passthrough)    |   |
|   |   * Always-on physical link   |       |    * Pre-boot: openstream     |   |
|   |   * Direct: OSboot TRSDOS.bin |       |    * Transparent CIPMODE=1    |   |
|   +---------------+---------------+       +---------------+---------------+   |
+-------------------|---------------------------------------|-------------------+
                    |                                       |                    
                    | Physical USB CDC-ACM / RS-232         | Wi-Fi (TCP:65504)  
                    v                                       v                    
+-------------------------------------------------------------------------------+
|                       FUJINET HOST (SERVER / PERIPHERAL)                      |
|                                                                               |
|   +---------------+---------------------------------------+---------------+   |
|   |               |                                       |               |   |
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
|   |  - DiskDISK (+256B Offset)    |       |  - 4 Device Slots (Slots 0-3) |   |
|   |  - HDV Hard Disk (4 MiB max)  |       |  - Device Path Configuration  |   |
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
> In contrast, when using a **direct physical serial link** (such as the Olimex MOD-USB-RS232 adapter plugged into UEXT, connecting over USB CDC-ACM or RS-232 to a host running FujiNet-PC or a hardware FujiNet unit), the communication channel is **inherently present and active upon hardware reset**. No AT initialization, socket negotiation, `openstream`, or `closestream` is necessary; Quark MOS directly executes `*OSboot -2f TRSDOS.bin`.

### 2.4 `trs-net-tcp`: Existing Host Daemon & Protocol
Provided by [rlucente-retro/trs-net-tcp](https://github.com/rlucente-retro/trs-net-tcp), this repository provides `trs-netd.py`:
* **Wire Protocol:** ASCII line-oriented protocol with `\n` line delimiters:
  * Read Sector: `<00012\n` $\to$ Returns 256 bytes raw data + 4 bytes big-endian checksum (`sum % 256`).
  * Write Sector: `>00012\n` + 256B payload + 4B checksum $\to$ Returns 4B calculated checksum.
  * Spool Character: `#<ascii_code>\n`.
  * Control Commands: `@ping\n` $\to$ `@pong\n`, `@bind\n` $\to$ `@bound\n` + 256B DiskDISK header.
* **Characteristics:** Pure request-response; completely silent upon connection until client transmits. Bound to a single disk file (`Volumes/sys720k.dsk`) at daemon launch.

### 2.5 `trs-os-zds`: Agon Light 2 Operating System Core
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
  * Interfaced via three-phase `OP_FUJI` transactions: execute command $\to$ query error $\to$ read response.
  * **Shared Transaction Buffer:** Server maintains a single response buffer across client interactions. Responses survive TCP reconnects until drained.
* **FujiNet-PC:**
  * Compiles natively on macOS/Linux/Windows (`build/dist/fujinet`).
  * Running `build/dist/fujinet -V` confirms `Target: COCO`.

### 2.7 `nitros9`: Reference FujiNet Client Implementations
Provided by [nitros-9/nitros9](https://github.com/nitros-9/nitros9) (`level1/cmds/fn*.as`):
* Reference utilities demonstrate interaction with the FujiNet firmware:
  * `fnstatus.as`: Queries `FUJI$GetAdapterCfg` (0xE8); prints IP, SSID, MAC, version.
  * `fnlisthosts.as`: Queries `FUJI$ReadHostSlots` (0xF4); lists host slots 0–7 (256 bytes).
  * `fnlistdevs.as`: Queries `FUJI$ReadDevSlots` (0xF2). Note: reference assumed 8 device slots (304 bytes), but COCO target server specifically returns 4 slots (152 bytes).
  * `fngetdevfile.as`, `fnsetdevfile.as`, `fnmountimg.as`: Configure and mount disk images.
* **Wire Protocol Distinction:** The 2-byte expected-response-length prefix seen in NitrOS-9's `lib/fuji.as` is internal to OS-9's `scdwv` SetStat driver interface. It is consumed locally by OS-9 and **must not be transmitted on the wire**.

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
2. **Shared Response Buffer Caveat:** The FujiNet configuration transaction engine retains unread response data across TCP reconnections. If a previous session terminated mid-transaction, the new connection will receive the stale buffered payload. The TRS-OS client must strictly serialize transactions, consume expected response payloads completely, and enforce a clean session initialization.

### 3.3 Boot Lifecycle, Execution Flow & Explicit UART1 Reinitialization

For a comprehensive explanation of the entire three-stage execution chain, memory maps, and driver emulation, see [`TRSOS-BOOT-ARCHITECTURE.md`](TRSOS-BOOT-ARCHITECTURE.md).

#### The Complete Handover Flow:
1. **Quark MOS Execution & Hardware Remapping:**
   * The user runs `*OSboot -2f TRSDOS.bin`.
   * `OSboot` reads monolithic `TRSDOS.bin` (480 KiB: 64 KB kernel + slice tables at `0x000000`–`0x0101FF`, followed by bootstrap Volume 0 at `0x010200`) into high external SRAM staging memory.
   * `OSboot` executes from eZ80 internal on-chip SRAM (`0xFFE000` / `RAM_ADDR_U`) to reprogram Chip Selects (`CS0`, `CS1`, `RAM_CTL`), remapping external SRAM down to base `0x000000` and displacing Flash ROM.
   * It copies the binary to address `0x000000`, sets terminal font/colors via internal UART0, writes the platform signature `3` into `UART0_SPR`, and jumps to `0x000000` (`__init`).
2. **`TRS-OS` HAL Startup (`ipl-eZ80_CPU.S`):**
   * Enters at `__init` with interrupts disabled (`DI`).
   * Reads `UART0_SPR`. Detecting `3` $\implies$ branches to `NOINITIAL` (warm boot), preserving MOS memory layouts and VDP terminal settings.
3. **Preserving the Streaming Connection across the Boundary:**
   * When `openstream` finished in MOS via `mos_uclose()`, MOS zeroed the eZ80 CPU's UART1 control registers (`LCTL = 0`, `FCTL = 0`, `IER = 0`) to detach its own interrupt handlers.
   * **Crucially, the external physical link (MOD-USB-RS232 or ESP8266 TCP socket) remains 100% open and active.** The ESP8266 does not disconnect or leave passthrough mode.
   * In `finish_init`, `TRS-OS` executes the explicit hardware reinitialization established in `patches/0001-uart1-streaming.patch`:
     - Re-asserts Port C Mode 7 (`PC0` = TXD1, `PC1` = RXD1).
     - Loads BRG divisor 10 ($18.432\text{ MHz} / (16 \times 115,200)$).
     - Sets Line Control to 8-N-1 (`UART1_LCTL = 0x03`).
     - Zeroes `UART1_IER` (`0x00`) to enforce pure polled I/O and prevent stray interrupt lockups.
     - Enables and flushes hardware FIFOs (`UART1_FCTL = 0x07`).
   * As soon as these local registers are set, the eZ80 UART1 pins immediately resume communication with the external FujiNet peripheral without dropping a single byte!
4. **BIOS & DOS Initialization (`ipl-BIOS.s` Refactoring):**
   * `ipl-BIOS.s` configures eZ80 Timer 1 for 60 Hz interrupts and mounts minimal RAM Slice 1 Volume 0 (`0x010200`) as Drive `:0`.
   * The legacy Volume 1 RAM scan and `CKDRIVE 1, mounterr1` failure trap are neutralized; `DCT1$` is pre-configured with `JP FDCDVR` (`0xC3`) and write-protected for DriveWire network operation.
   * `ipl-DOS.S` enables interrupts (`EI`) to start the 60 Hz task scheduler, calls `zICNFG`, and enters `zCMNDI` in `SYS6/SYS` to present the `TRSDOS Ready` prompt ready to mount persistent network drives (`:1`–`:4` in Stage 1; `:1`–`:7` in Stage 2).

```assembly
; =============================================================================
; Explicit eZ80 UART1 Hardware Initialization for TRS-OS (finish_init)
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
| **JV1** | 0 bytes | 256 bytes | Floppy ($LSN \le 16384$) | $LSN \times 256$ | Flat raw sectors; directory cylinder typically Track 17. |
| **DiskDISK** | 256 bytes | 256 bytes | Floppy ($LSN \le 16384$) | $256 + (LSN \times 256)$ | Authentic 256B header; bytes F9h–FFh match 7 DCT parameters. |
| **Reed HDV** | 256 bytes | 256 bytes | Hard Disk ($LSN > 16384$) | $256 + (LSN \times 256)$ | Signature `56 CB`h; geometry fields in header; requires hard disk driver. |

### 4.2 Header Offset Handling & Container Translation

FujiNet's unpatched `mediaTypeDSK.cpp` treats disk files as raw flat byte streams (`offset = LSN * 256`).
* When mounting a **DiskDISK** or **Reed HDV** file that contains a 256-byte container header, raw LSN 0 returns the header, shifting all subsequent payload sectors by one position.
* **Concrete Client Translation Architecture:**
  1. The TRS-OS storage driver maintains a per-drive offset table in RAM: `dw_offset` (8 bytes, one per logical drive `:0`..`:7`).
  2. When `FNMNTIMG` mounts a disk image into a FujiNet slot, it reads raw LSN 0 via `OP_READEX`.
  3. If the first 8 bytes match ASCII `'DiskDISK'`, `FNMNTIMG` sets `dw_offset[slot] = 1`.
  4. If LSN 0 does not match `'DiskDISK'`, the image is treated as a flat raw JV1 image, and `FNMNTIMG` sets `dw_offset[slot] = 0`.
  5. During active sector I/O in `driver-FDCDVR.S`, the driver adds `dw_offset[slot]` to the calculated `fdc_no_sum` before constructing the 5-byte `OP_READEX` / `OP_WRITE` request header:
     $$\text{Wire LSN} = \text{fdc\_no\_sum} + \text{dw\_offset}[\text{slot}]$$
  This guarantees 100% transparent support for both flat JV1 and authentic DiskDISK containers without requiring server-side container parsing.

### 4.3 Dynamic Geometry Derivation & DCT Synchronization

Because DriveWire does not transmit disk geometry metadata during mounting, TRSDOS must derive and configure the guest `DCT` (`DCTn$`) dynamically upon image attachment:
* **DiskDISK Metadata Extraction:** If `'DiskDISK'` magic is detected, bytes `0xF9`–`0xFF` (or offset `volbuf+11` in the header) contain the authentic 7-byte DCT parameter block. `FNMNTIMG` directly copies these 7 bytes into `DCTn$+3` through `DCTn$+9`.
* **JV1 Dynamic Geometry Derivation:** If the image is flat JV1:
  1. `FNMNTIMG` reads Sector 0 (at wire LSN 0).
  2. Byte 2 of Sector 0 contains the Directory Cylinder index (typically Track 17 / `11h`).
  3. `FNMNTIMG` reads Sector 0 of the directory cylinder to fetch the Granule Allocation Table (GAT).
  4. Byte `0xCB` of the GAT confirms the formatting DOS version (`0x60` for LS-DOS 6.x).
  5. Byte `0xCC` contains cylinders in excess of 35 ($\text{Max Cylinders} = 35 + \text{byte}_{CC}$).
  6. Byte `0xCD` contains media configuration flags:
     - Bit 7: System/Data disk indicator.
     - Bit 6: Density (`0` = Single Density / 10 sectors/track, `1` = Double Density / 18 sectors/track).
     - Bit 5: Sides (`0` = Single-sided / 1 head, `1` = Double-sided / 2 heads).
  7. `FNMNTIMG` writes the derived parameters into `DCTn$+3`..`DCTn$+9`.
* **DCT Vector Activation:** Authentic LS-DOS initial state leaves `DCT2$` through `DCT7$` configured as inactive `RET` stubs (`0xC9`). After populating the geometry, `FNMNTIMG` writes opcode `0xC3` (`JP FDCDVR`) to `DCTn$`, activating the drive in the running OS.

### 4.4 HDV Hard Disk Support vs. Floppy Driver Addressing Bounds

While the DriveWire protocol packet format carries **24-bit LSNs** capable of addressing up to 4 GB ($2^{24} \times 256$ bytes):
* **Floppy Driver Geometric Limit:** The standard TRS-OS floppy driver (`driver-FDCDVR.S`) calculates sectors using an 8-bit cylinder register (`D`), sectors per track from DCT (`DCT+7 & 1Fh + 1`), and a 16-bit accumulator `fdc_no_sum`. This caps addressing through the floppy driver at $256 \times 64 = 16,384 \text{ sectors} = \mathbf{4\text{ MiB}}$.
* **High-Capacity HDV Architecture:** True 5 MB to 32 MB HDV support requires a dedicated hard disk driver architecture (e.g. `RHDVR` / partition driver) rather than routing wide LSNs through the floppy disk driver. Stage 1 focuses on standard floppy geometries ($\le 4\text{ MiB}$).

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

$$\text{Checksum} = \left( \sum_{i=0}^{255} \text{byte}_i \right) \pmod{65536}$$

**Deterministic Test Vectors:**
* 256 bytes of `0x00` $\implies$ Checksum = `0x0000`
* 256 bytes of `0xFF` $\implies$ Checksum = `0xFF00`
* Sequential bytes `0x00` through `0xFF` $\implies$ Checksum = `0x7F80`
* 256 bytes of `0x5A` $\implies$ Checksum = `0x5A00`

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
* **Session Response Drainage:** The firmware configuration engine maintains a persistent response buffer (`_transaction_response`). If a previous session aborted mid-transaction, stale unread bytes remain in the buffer until drained. The client utility startup sequence issues a single `0xE2 0x01` query to drain any residual data before initiating new transactions.

---

## 6. TRS-OS Drive Architecture & Storage Management

### 6.1 Storage Allocation Policy & Multi-Drive Network Mapping

To ensure high reliability, persistence, and maximum capacity, TRS-OS isolates transient bootstrapping from persistent network storage:
* **Drive `:0` (Minimal Bootstrap RAM Drive):** Dedicated to the minimal in-memory system volume (`0x010200` in RAM). Contains only the essential kernel overlays (`SYS0/SYS`..`SYS6/SYS`) and FujiNet utilities (`FN*.CMD`). Its sole purpose is to boot TRS-OS in under 1 second and provide the command engine to mount persistent network volumes.
* **Elimination of Non-Persistent RAM Data Disk (`:1`):** Previously, an optional secondary disk was loaded into high RAM (`0x045300`) as Drive `:1`. Because all data written to an eZ80 RAM disk is lost upon reboot or power cycle, this non-persistent drive is removed.
* **Stage 1 Drive Allocation (Stock Firmware Baseline):**
  * Stock `fujinet-firmware` (`BUILD_COCO`) sets `MAX_DWDISK_DEVICES = 4` in `drivewireFuji.h`.
  * Drives `:1` through `:4` map directly to FujiNet Device Slots 0 through 3 (corresponding to Drives 1 through 4 in the FujiNet Web UI via `slot = LDRV - 1`):
    * **Drive `:1`** $\longleftrightarrow$ **FujiNet Device Slot 0** (Primary persistent network drive; FujiNet Web UI "Drive 1").
    * **Drive `:2`** $\longleftrightarrow$ **FujiNet Device Slot 1** (FujiNet Web UI "Drive 2").
    * **Drive `:3`** $\longleftrightarrow$ **FujiNet Device Slot 2** (FujiNet Web UI "Drive 3").
    * **Drive `:4`** $\longleftrightarrow$ **FujiNet Device Slot 3** (FujiNet Web UI "Drive 4").
  * Drives `:5`, `:6`, `:7` remain reserved until Stage 2.
  * *Workflow Constraint in Stage 1:* Because network drives are strictly read-only and Drive `:0` is a minimal RAM drive, user file saving and modification are deferred until Stage 2 writable storage is qualified.
* **Stage 2 Drive Expansion (Patched Firmware):**
  * Recompiling firmware with `MAX_DWDISK_DEVICES 8` expands device slot arrays to 8 (`MAX_DISK_DEVICES = MAX_MOUNT_SLOTS = 8`).
  * Unlocks Drives `:5`, `:6`, and `:7` (mapping to FujiNet Slots 4, 5, 6 / Web UI Drives 5, 6, 7).
  * Direct 1:1 mapping formula across all network drives: $\text{FujiNet Wire Slot} = \text{LDRV} - 1$. Legacy `:6` special-casing is completely eliminated.

### 6.2 Device Control Table (DCT) Management & Write-Protection

Each logical drive in TRS-OS is governed by a 10-byte Device Control Table entry (`DCT0$` through `DCT7$`):
* `DCT+0`, `DCT+1`, `DCT+2`: Driver jump vector (`JP FDCDVR`, `C3 xx xx`h).
  * In authentic LS-DOS (`DCT.S`), `DCT0$` and `DCT1$` are initialized with `JP FDCDVR`.
  * `DCT2$` through `DCT7$` are initialized with `RET` (`0xC9`), followed by `dw FDCDVR`. Because LS-DOS calls the DCT vector directly, any drive containing `RET` returns immediately without invoking `FDCDVR`!
  * **Dynamic Activation:** When `FNMNTIMG` attaches an image to Drive `:2`..`:7`, it writes opcode `0xC3` (`JP FDCDVR`) to byte 0 of `DCTn$`, activating the drive vector in the running operating system.
* `DCT+3`: Drive status flags. **Bit 7 is set to 1 to enforce Write Protection** on network drives during Stage 1.
* `DCT+4`: Cylinder count and head configuration (Bit 5 set = 2 heads / double-sided).
* `DCT+6`: Maximum cylinders ($N - 1$).
* `DCT+7`: Sectors per track configuration.
* `DCT+9`: Directory cylinder index (typically Track 17 / `11h`). Dynamically populated by `FNMNTIMG` during mounting.

### 6.3 Storage Driver Routing & DriveWire Read Integration (`driver-FDCDVR.S`)

The storage driver `driver-FDCDVR.S` builds upon the existing Daniel Paul Martin RAM slice driver, routing minimal bootstrap Drive `:0` to memory while directing persistent Drives `:1` through `:7` to DriveWire. 

#### Critical Driver Lifecycle & Calling Convention Requirements

A comprehensive audit of `driver-FDCDVR.S` in `trs-os-zds` reveals strict LS-DOS driver invariants that must be upheld:
1. **Entry Point & Stack Frame:** LS-DOS calls `FDCDVR` (`JR FDCBGN`). `FDCBGN` disables interrupts (`DI`), stores cylinder, logical drive `(LDRV$)`, sector `(secreq$)`, and physical drive `(PDRV$)`. It pushes registers `push hl`, `push bc`, `push hl`, retrieves the function vector from `funtable$`, restores one `hl`, leaves `bc` and `hl` on the stack, and jumps to the function via `vecvec$`.
2. **Drive Select (`SLCT` / Function 1):** Function 1 is called by LS-DOS prior to disk operations. It **must** return with register `A` having bit 1 set (`00000010b` / `0x02`), signaling an active index pulse from the virtual controller. Returning `A = 0` causes LS-DOS to fail immediately with "Drive Not Ready".
3. **Function Table Indexing:** Primitive functions are dispatched via `funtable$`:
   * Function 0: `TSTBSY` (NOP / Test Busy — restores `bc`, `hl`, clears A, enables interrupts, `RET`)
   * Function 1: `SLCT` (Drive Select — sets bit 1 in A, restores registers, `EI`, `RET`)
   * Functions 2..7: `TSTBSY` / `RESTOR` / `STPIN` / `SEEKTRK`
   * Function 9: `RDIN` (Read Sector)
   * Function 10: `VERIFY` (Verify Sector Readable)
   * Functions 13 & 14: `DOWRIT` (Write Sector / Write System Directory)
4. **Directory Cylinder Signal (`rd_finish`):** After reading a sector, LS-DOS requires comparing the track just read against the directory cylinder stored in `(IY+9)`. If the sector read was on the directory cylinder, the driver **must return `A = 6` with Non-Zero (NZ) set**. Returning `Z` (success) on a directory track read causes LS-DOS directory parsing to fail! If not on the directory cylinder, it returns via `TSTBSY` with `A = 0` (Z set).
5. **No Fatal `ramdrv$` Jump:** `ramdrv$` in `driver-FDCDVR.S` is a 16-byte data buffer holding volume memory pointers (`ramdrv$ blkb 16,0`), *not executable code*. Drive `:0` dispatch routes to `not_net_rd` (the memory block move routine), not a jump to `ramdrv$`.
6. **Container Offset Addition (`dw_offset`):** For DiskDISK formatted images (which prepend a 256-byte metadata header), sector 0 of the disk resides at LSN 1 on the wire. The driver indexes into an 8-byte `dw_offset` table by drive slot and adds the offset to the calculated LSN.

```assembly
; =============================================================================
; Storage Driver Routing in driver-FDCDVR.S (Function 9: RDIN)
; Preserves minimal bootstrap RAM driver (:0) while routing Drives :1-:7 to DriveWire
; =============================================================================
RDIN:
        push    de                      ; Save D=track, E=sector for rd_finish
        push    hl                      ; Save 16-bit user destination buffer pointer

        call    calcsec                 ; Calculate linear sector number (LSN) -> (fdc_no_sum)
                                        ; and calculate RAM pointer -> (PADDRESS)

        ld      a, (LDRV$)              ; Get requested logical drive (0..7)
        or      a                       ; Drive :0 (Minimal Bootstrap RAM)?
        jp      z, not_net_rd           ; Route Drive :0 to existing memory LDIR.L block copy

        ; Check valid network drive range
        ; Stage 1 supports Drives :1-:4 (Slots 0-3); Stage 2 supports :1-:7 (Slots 0-6)
        cp      5                       ; Stage 1 boundary check (< 5 = Drives 1..4)
        jr      nc, fdc_rd_err          ; Unsupported or unmounted drive -> abort with error

        ; ---------------------------------------------------------------------
        ; Validated DriveWire Sector Read Routine (OP_READEX = 0xD2)
        ; ---------------------------------------------------------------------
rd_drivewire:
        ; 1. Calculate FujiNet Device Slot & Apply Container Offset
        ld      a, (LDRV$)              ; Drive 1..4 (or 1..7 in Stage 2)
        dec     a                       ; Maps Drive :1 -> Slot 0, Drive :2 -> Slot 1, etc.
        ld      (dw_slot), a            ; Store slot index for request header

        ; Retrieve container offset (0 for raw JV1, 1 for DiskDISK header)
        ld      e, a
        ld      d, 0
        ld      hl, dw_offset           ; Offset lookup table (1 byte per slot)
        add     hl, de
        ld      e, (hl)                 ; E = offset (0 or 1)
        ld      hl, (fdc_no_sum)        ; HL = base calculated LSN
        add     hl, de                  ; HL = physical LSN on server

        ; 2. Prepare 5-byte OP_READEX Request Header
        ld      a, 0D2h                 ; OP_READEX opcode (0xD2)
        ld      (dw_hdr+0), a
        ld      a, (dw_slot)            ; Virtual device slot (0..6)
        ld      (dw_hdr+1), a
        xor     a
        ld      (dw_hdr+2), a           ; LSN [23..16] (always 0 for floppy geometries)
        ld      a, h
        ld      (dw_hdr+3), a           ; LSN [15..8] (MSB)
        ld      a, l
        ld      (dw_hdr+4), a           ; LSN [7..0] (LSB)

        ; 3. Transmit 5-Byte Request Header over UART1
        UART1.put.bytes 5, dw_hdr, fdc_rd_err

        ; 4. Receive 256-Byte Sector Data Directly into Target User Buffer
        pop     hl                      ; Recover user destination buffer pointer
        push    hl                      ; Re-save for checksum calculation
        UART1.get.bytes 0, hl, fdc_rd_err ; Length 0 = 256 bytes in eZ80 macro

        ; 5. Calculate 16-Bit Big-Endian Unsigned Summation
        pop     hl                      ; Buffer start pointer
        call    calc_dw_checksum        ; Computes sum: D=MSB, E=LSB
        ld      (dw_chk+0), d           ; Store big-endian MSB
        ld      (dw_chk+1), e           ; Store big-endian LSB

        ; 6. Transmit 2-Byte Checksum to Server
        UART1.put.bytes 2, dw_chk, fdc_rd_err

        ; 7. Receive 1-Byte Status from Server (0x00 = Success)
        UART1.get.bytes 1, dw_status, fdc_rd_err
        ld      a, (dw_status)
        or      a                       ; 0x00 = OK
        jr      nz, fdc_rd_err          ; Non-zero indicates server CRC/IO error

        ; 8. Clean Exit via Authentic LS-DOS Directory Check
        jp      rd_finish               ; Checks (IY+9), sets A=6 if directory, restores stack

fdc_rd_err:
        pop     hl                      ; Clean up saved user buffer pointer
        pop     de                      ; Clean up saved track/sector
        pop     bc                      ; Restore entry BC
        pop     hl                      ; Restore entry HL
        ld      a, 8                    ; Error 8 = Drive Read Error / Parity
        or      a                       ; NZ flag indicates error
        ei                              ; Re-enable interrupts
        ret

; =============================================================================
; Write Protection Enforcement in driver-FDCDVR.S (Functions 13 & 14: DOWRIT)
; =============================================================================
DOWRIT:
        ; Check if network drive in Stage 1 (Read-Only)
        ld      a, (LDRV$)
        or      a                       ; Drive :0 (RAM drive)?
        jr      z, fdc_ram_write        ; Allow write if local RAM slice

        ; Network Drives :1-:7 are strictly Write-Protected in Stage 1
        pop     bc                      ; Clean up stack frame
        pop     hl
        ld      a, 15                   ; LS-DOS Error 15: "Write Protected Disk"
        or      a                       ; NZ = error
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

  **Year Modulo-100 Normalization:** TRSDOS / LS-DOS maintains system date in `DATE$` (`MM/DD/YY`) and expects a 2-digit decimal year ($00$–$99$). Because FujiNet's `drivewire.cpp` transmits POSIX `tm_year` directly ($126$ for 2026), `driver-TOD.S` must normalize the incoming year modulo-100:
  ```assembly
  ; Convert tm_year (years since 1900, e.g. 126) to 2-digit decimal year (26)
  ld      a, (time_buf + 0)       ; A = tm_year
  cp      100                     ; Year >= 2000?
  jr      c, yr_ok
  sub     100                     ; Subtract 100 (e.g. 126 - 100 = 26)
yr_ok:
  ld      (sys_year), a           ; Store normalized 2-digit year in LS-DOS date cell
  ```
  This guarantees seamless Y2K+ compatibility and initializes TRSDOS system time on every boot.

---

## 7. Native TRSDOS FujiNet Utilities Specification

### 7.1 Architecture & Peripheral Scope (TRSDOS Transport Decoupling)

TRSDOS operates completely independently of network topology:
* All `FN*.CMD` utilities communicate over standard serial `OP_FUJI` (0xE2) transactions to interrogate and configure the **remote FujiNet peripheral device**, not the local Agon hardware.
* **Network Status (`FNSTATUS`):** Queries FujiNet's `FUJI$GetAdapterCfg` (0xE8), receiving 140 bytes describing the FujiNet peripheral's network adapter. Note that when running FujiNet-PC, dummy Wi-Fi metadata contains synthetic SSIDs and MACs.

### 7.2 Eight-Character Basename Compliance & Command Summary

TRSDOS / LS-DOS 6.x enforces an **8-character maximum filespec basename**. All utilities comply with this limit:

| Utility | Syntax | Opcode / Function | Description |
| :--- | :--- | :--- | :--- |
| `FNSTATUS` | `FNSTATUS` | `0xE8` (140B) | Displays FujiNet firmware, IP, MAC, gateway, and connection state. |
| `FNHOSTS` | `FNHOSTS` | `0xF4` (256B) | Lists all 8 configured FujiNet host slots (0–7). |
| `FNGETH` | `FNGETH <slot>` | `0xF4` | Displays hostname for a specific host slot (0–7). |
| `FNSETH` | `FNSETH <slot> "<host>"` | `0xF3` (256B) | Configures remote hostname or TNFS URL for a host slot. |
| `FNMOUNT` | `FNMOUNT <slot>` | `0xF9` | Mounts a host slot (connects FujiNet to TNFS server or SD card). |
| `FNDEVS` | `FNDEVS` | `0xF2` (152B / 304B) | Lists virtual device slots (0–3 in Stage 1; 0–7 in Stage 2), files, and modes. |
| `FNGETDEV` | `FNGETDEV <slot>` | `0xDA` (256B) | Displays pathname of disk image mounted in device slot (0–3 or 0–7). |
| `FNSETDEV` | `FNSETDEV <s> <h> <m> "<p>"`| `0xE2` (261B) | Assigns disk image on host slot to device slot (quotes preserve path case). |
| `FNMNTIMG` | `FNMNTIMG <slot> [mode]` | `0xF8` | Commits image mount, queries GAT, and synchronizes live DCT geometry. |

### 7.3 Detailed Specification of the 9 Command Utilities

#### 1. `FNSTATUS.CMD`
* **Syntax:** `FNSTATUS`
* **Payload:** Issues `OP_FUJI` (`0xE2`) + `0xE8` (`FUJI$GetAdapterCfg`); verifies handshake via `GetError` (`0x01`); reads 140 bytes of `AdapterConfig` struct via `GetResponse`.
* **Field Offsets:** SSID (0..32, 33B), Hostname (33..96, 64B), IP (97..100, 4B), Gateway (101..104, 4B), Netmask (105..108, 4B), DNS (109..112, 4B), MAC (113..118, 6B), BSSID (119..124, 6B), Firmware (125..139, 15B).
* **Display Format:** Bounded fields rendered cleanly to the TRSDOS display via `@DSPLY`. On FujiNet-PC, synthetic metadata is formatted transparently.

#### 2. `FNHOSTS.CMD`
* **Syntax:** `FNHOSTS`
* **Payload:** Issues `OP_FUJI` + `0xF4` (`FUJI$GetHostSlots`); reads 256 bytes (8 slots $\times$ 32 bytes). Displays slot index (0–7) and hostname string for each non-empty entry.

#### 3. `FNGETH.CMD`
* **Syntax:** `FNGETH <slot>` (`slot` = 0..7)
* **Payload:** Reads 256-byte host table via `0xF4` and extracts the 32-byte record corresponding to the specified slot. Displays hostname or `(empty)`.

#### 4. `FNSETH.CMD`
* **Syntax:** `FNSETH <slot> "<hostname>"`
* **Payload:** Reads full 256-byte table (`0xF4`), updates targeted 32-byte slot with maximum 31 characters + NUL terminator, and writes back complete table via `0xF3` (`FUJI$SetHostSlots`, 258 bytes total on wire).
* **Quoting Requirement:** Enclosing `<hostname>` in double quotes prevents the TRSDOS CLI scanner from uppercasing domain names or URL paths.

#### 5. `FNMOUNT.CMD`
* **Syntax:** `FNMOUNT <slot>` (`slot` = 0..7)
* **Payload:** Issues `OP_FUJI` + `0xF9` (`FUJI$MountHost`) + `<slot>`. Verifies return status via `GetError` (`0x01`). Mounts remote TNFS export or SD card filesystem on the FujiNet device.

#### 6. `FNDEVS.CMD`
* **Syntax:** `FNDEVS`
* **Payload:** Issues `OP_FUJI` + `0xF2` (`FUJI$GetDevSlots`).
  * **Stage 1 (Stock Firmware):** Reads **152 bytes** (4 device slots $\times$ 38 bytes: 1B HostSlot, 1B Mode, 36B Filename). Displays slots 0–3 (corresponding to TRSDOS Drives `:1` through `:4`).
  * **Stage 2 (Patched Firmware):** Reads **304 bytes** (8 device slots $\times$ 38 bytes). Displays slots 0–6 (TRSDOS Drives `:1` through `:7`).

#### 7. `FNGETDEV.CMD`
* **Syntax:** `FNGETDEV <slot>` (`slot` = 0..3 in Stage 1; 0..7 in Stage 2)
* **Payload:** Issues `OP_FUJI` + `0xDA` (`FUJI$GetDevFile`) + `<slot>`; reads 256-byte null-terminated full path string.

#### 8. `FNSETDEV.CMD`
* **Syntax:** `FNSETDEV <dev_slot> <host_slot> <mode> "<path>"`
* **Payload:** Issues `OP_FUJI` + `0xE2` (`FUJI$SetDevFile`) + `<dev_slot>` + `<host_slot>` + `<mode>` + `<path[256]>` (261 wire bytes total).
* **Critical Case-Sensitivity Handling:** Remote Linux TNFS file servers enforce strict case-sensitivity. The TRSDOS / LS-DOS command line interpreter automatically uppercases all unquoted tokens. To prevent paths like `/disks/trs80/game.dsk` from mutating into `/DISKS/TRS80/GAME.DSK` (causing file not found errors), the `<path>` argument **must be enclosed in double quotes**. `FNSETDEV` strips the enclosing quotes while preserving exact string casing.

#### 9. `FNMNTIMG.CMD`
* **Syntax:** `FNMNTIMG <dev_slot> [mode]`
* **Payload & Lifecycle:** Issues `OP_FUJI` + `0xF8` (`FUJI$MountImg`) + `<dev_slot>` + `<mode>` (1 = Read-Only, 2 = Read-Write; defaults to 1 in Stage 1). Verifies status via `GetError`.
* **Live DCT Geometry Synchronization:** Following a successful mount on the FujiNet server, `FNMNTIMG` automatically synchronizes the TRSDOS Device Control Table (`DCT`) for the corresponding logical drive (`LDRV = dev_slot + 1`):
  1. Issues a read of Sector 0 (LSN 0 for JV1, LSN 1 for DiskDISK) via `FDCDVR` to extract disk geometry from the Granule Allocation Table (GAT).
  2. Updates `DCT+4` (heads/sides), `DCT+6` (cylinders - 1), `DCT+7` (sectors per track), and `DCT+9` (directory cylinder, typically track 17 / `11h`).
  3. Sets bit 7 of `DCT+3` to enforce Write Protection during Stage 1.
  4. Dynamically activates the drive vector by writing opcode `0xC3` (`JP FDCDVR`) to `DCT+0`, enabling immediate drive access without requiring a system reboot!

### 7.4 Peripheral Communication Architecture (Direct Polling Mode)

All 9 `FN*.CMD` utilities communicate directly with the remote FujiNet hardware via the Agon Light 2 eZ80 UART1 interface (`0xD0`–`0xD5`):
* **Bypass MOS/Quark OS Layer:** Because TRSDOS replaces the MOS environment, utilities communicate using direct eZ80 `IN0`/`OUT0` hardware polling loops.
* **Interrupt Masking:** Atomic transaction sequences (e.g. `OP_FUJI` command, checksum transfer, response drain) are bracketed by `DI` and `EI` instructions to prevent background timer interrupts from interrupting wire exchanges.
* **Hardware FIFO Optimization:** The 16-byte eZ80 UART1 FIFO is actively drained using tight polling routines, ensuring zero byte loss even at 115,200 baud.

### 7.5 LS-DOS System Interface & SVC Calling Conventions

TRSDOS command utilities interface with LS-DOS via standard system SVCs dispatched through `RST 28h` (decimal `RST 40`):
* **`@PARAM` (SVC 17 / 11h):** Parses command line arguments passed in the system command line buffer (`HL`).
  * `@PARAM` parameter tables define expected positional arguments, numeric ranges (e.g. slots 0–7), and string buffers.
  * For string parameters (such as TNFS hostnames and image paths), `@PARAM` preserves quoted string literals exactly as entered, allowing lowercase directory traversal on Linux TNFS servers.
* **`@DSPLY` (SVC 10 / 0Ah):** Emits strings pointed to by register `HL` to the video console. Strings **must be terminated by Carriage Return (`0x0D`, emitted with newline) or ETX (`0x03`, terminating without newline)**. Strings are *not* null-terminated!
* **`@PRINT` (SVC 14 / 0Eh):** Directs character or string output to the printer DCB.
* **`@ERROR` (SVC 26 / 1Ah):** Formats and outputs standard LS-DOS error message strings corresponding to error codes in register `A`.
* **`@EXIT` (SVC 22 / 16h):** Cleanly terminates the utility program and returns control to the TRSDOS Ready prompt. Register `HL` contains the exit status code (0 = success).

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
* 1 byte requires $86.8\ \mu\text{s}$ of physical wire time.
* A complete 264-byte sector transaction (`OP_READEX` 5B header + 256B payload + 2B checksum + 1B status) requires **22.92 ms minimum wire time**.
* Theoretical maximum sector read throughput is **10.9 KiB/s**, before host I/O, network latency, and server turnarounds.
* At 230,400 baud, minimum wire time drops to **11.46 ms** (payload ceiling ~21.8 KiB/s).

### 9.2 UART FIFO Overrun Protection & 60 Hz Timer Clock Drift

* **FIFO Constraints:** The eZ80 hardware UART1 FIFO holds only 16 bytes, representing **1.39 ms of buffer time** at 115,200 baud (0.69 ms at 230,400 baud). If byte polling is interrupted or delayed by $\ge 1.39\text{ ms}$, a FIFO Overrun Error (OE) occurs, corrupting sector data.
* **60 Hz Timer Tick Suppression & Clock Drift:** In TRS-OS on Agon Light, Timer 1 generates a periodic 60 Hz interrupt (interval of $16.67\text{ ms}$) used for system time-of-day clock tracking (`TIME$` / RTC) and operating system task scheduling.
  * Because a single 264-byte `OP_READEX` transaction takes $\ge 22.92\text{ ms}$, holding interrupts disabled (`DI`) continuously throughout the entire sector transfer will cause at least one 60 Hz timer interrupt tick to be dropped on *every* sector read!
  * Over multi-sector file operations (such as loading a 16 KiB program = 64 sectors $\approx 1.47\text{ s}$ of wire time), holding `DI` continuously causes the system clock to lose more than 60 timer ticks (over 1 second of clock drift per file loaded).
* **Mitigation Architecture:**
  1. **Surgical Interrupt Bracketing:** Mask interrupts (`DI`) only during time-sensitive burst byte transfers (the 5-byte header, the 256-byte payload burst, the 2-byte checksum, and the 1-byte status). Re-enable interrupts (`EI`) between phases—specifically during checksum computation and inter-packet turnarounds.
  2. **Periodic RTC Resynchronization:** During idle periods or command utility executions, query FujiNet's `OP_TIME` (`0x23`) to resynchronize the TRS-OS system time-of-day clock against the network time base.
  3. **Interrupt Isolation:** `UART1_IER` is held at `0x00` during polled disk I/O to prevent stray UART interrupt handlers from corrupting registers.

### 9.3 ESP8266 `+++` Control Escape Timing & Binary Transparency

When using MOD-WIFI-ESP8266 in transparent streaming mode (`CIPMODE=1`), escaping back to AT command mode requires the standard Hayes-compatible escape sequence `+++`:
* The ESP8266 AT firmware enforces precise guard intervals:
  * Greater than 20 ms of line silence before the first `+`.
  * Less than 20 ms between each of the three `+` characters.
  * Greater than 20 ms of line silence after the third `+`.
  * At least 1.0 second pause before issuing the subsequent AT command (`AT+CIPCLOSE`).
* **Binary Transparency:** Any arbitrary binary sector data containing the sequence `+++` (`0x2B 0x2B 0x2B`) will **not** trigger escape mode because disk sectors are streamed continuously at full baud rate without matching the required pre- and post-guard silence intervals.
* **Handover State Retention:** When Quark MOS launches `TRSDOS.bin` via `OSboot`, `OSboot` preserves the existing UART1 baud rate and connection state. If Wi-Fi streaming was opened via `openstream`, the TCP socket remains transparently connected throughout TRSDOS execution.

---

## 10. Phased Implementation Roadmap

```
=================================================================================
STAGE 1: Stock Firmware Baseline & Read-Only Storage Proof (Drives :1-:4 / Slots 0-3)
=================================================================================

+-------------------------------------------------------------------------------+
| PHASE 1: Minimal Bootstrap & Emulator Proof                                  |
| - Verify minimal bootstrap Drive :0 in RAM booting into TRSDOS Ready.         |
| - Verify read-only Drive :1 mapped to FujiNet Slot 0 in Fab Agon emulator.    |
| - Test directory listing (DIR :1) and file loading (LOAD "ELIZA/BAS:1").      |
| - Verify write rejection (R_PROTECTED / Error 15) on network drives.          |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 2: Core Storage Driver Integration (trs-os-zds)                         |
| - Port verified client.c and uart.src into maintained driver-FDCDVR.S.        |
| - Integrate funtable$ dispatch (Function 9 RDIN, Functions 13/14 DOWRIT).     |
| - Implement dw_offset lookup table for JV1 vs DiskDISK (+256B) header handling.|
| - Integrate rd_finish directory track (IY+9) check and stack frame cleanup.   |
| - Add explicit UART1 initialization in finish_init (SYS0_IPL).                |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 3: Build Pipeline & Authentic CMD Packager                              |
| - Build and verify TRSDOS.bin via Wine/ZDS II and native AgonDev.             |
| - Build bin_to_cmd.py packager emitting authentic Record Type 01/02 formats.  |
| - Establish automated Makefile targets for OS image and utility binaries.     |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 4: Stage 1 Native TRSDOS Utilities (FN*.CMD)                            |
| - Implement FNSTATUS.CMD (140-byte AdapterConfig parsing via SVC 10 @DSPLY).  |
| - Implement FNHOSTS, FNGETH, FNSETH, FNMOUNT (8 host slots, 256 bytes).       |
| - Implement FNDEVS (4 device slots, 152 bytes) and FNGETDEV (slots 0-3).      |
| - Implement FNSETDEV with double-quoted case-preserving paths for TNFS.       |
| - Implement FNMNTIMG with dynamic DCT geometry extraction and vector enable.  |
| - Implement direct UART1 polled I/O and SVC 17 (@PARAM) argument parsing.     |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 5: Physical Hardware Bench Qualification                                |
| - Qualify Direct Serial Link (MOD-USB-RS232) on Agon Light 2 hardware.        |
| - Qualify Wi-Fi TCP/IP Streaming (MOD-WIFI-ESP8266) with openstream/closestream.|
| - Verify OSboot loader handover and clock stability during heavy sector I/O.  |
+-------------------------------------------------------------------------------+

=================================================================================
STAGE 2: Patched Firmware Expansion & Writable Storage (Drives :1-:7 / Slots 0-6)
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
| - Expand device mapping across Drives :1 through :7 (FujiNet Slots 0–6).      |
| - Expand FNDEVS to query 8 device slots (304 bytes).                          |
| - Qualify OP_WRITEEX (0xD3) with SHA-256 pre/post write image hashes.         |
| - Implement cache invalidation and safe remount lifecycles.                   |
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
| **TC-02a** | Direct Serial Handover | Connect MOD-USB-RS232; boot via `OSboot TRSDOS.bin`. | Boots cleanly to TRSDOS Ready; UART1 functions in polled mode. |
| **TC-02b** | Wi-Fi Handover | Connect MOD-WIFI-ESP8266; run `openstream` $\to$ `OSboot`. | Stream preserved; TRSDOS boots to prompt without freezing. |
| **TC-03** | Checksum Vectors | Compute checksum across known 256B test vectors. | Matches big-endian unsigned summation $\sum b_i \pmod{65536}$ exactly. |
| **TC-04** | `OP_READEX` Verification | Read known sector from Drive `:1` via `0xD2` opcode. | 256 bytes read, client checksum sent, status byte `0x00` returned. |
| **TC-05** | Write Rejection (Stage 1) | Attempt write to Drive `:1` (`SAVE "TEST:1"`). | Immediate `R_PROTECTED` (error 15); host image remains unmodified. |
| **TC-06** | Container Offset Verification | Read Sector 0 from raw JV1 vs DiskDISK image. | Correctly adds +0 for JV1 (LSN 0) and +1 for DiskDISK (LSN 1); returns valid GAT. |
| **TC-07** | `FNSTATUS` Verification | Run `FNSTATUS` from TRSDOS Ready prompt. | Correctly parses and prints 140-byte adapter configuration via `@DSPLY`. |
| **TC-08** | Host Table Management | Run `FNHOSTS` and `FNSETH 0 "tnfs.fujinet.online"`. | Displays 8 slots (256 bytes); updates and persists slot 0 without case distortion. |
| **TC-09** | Device Table Query | Run `FNDEVS` from TRSDOS Ready prompt. | Correctly queries device slot table (152B for 4 slots in Stage 1; 304B for 8 slots in Stage 2). |
| **TC-10** | Path Quoting Case Preservation | Run `FNSETDEV 0 0 1 "/disks/trs80/game.dsk"`. | Server mounts exact path without uppercase conversion (`/disks/...`, not `/DISKS/...`). |
| **TC-11** | Live DCT Geometry Synchronization | Run `FNMNTIMG 0 1` on unmounted Drive `:2`. | Sector 0 read; DCT geometry populated; byte 0 changed to `0xC3`; `DIR :2` succeeds. |
| **TC-12** | Directory & File Execution | Run `DIR :1` and load executable (`RUN "TEST/CMD:1"`). | Directory displays files; executable loads and runs cleanly. |
| **TC-13** | Timer 1 Clock Stability | Perform multi-sector file load ($\ge 64$ sectors). | System time-of-day clock does not drift by $>1$ tick during transfer. |
| **TC-14** | Stage 2 7-Drive Multi-Mount | Mount unique disks to slots 0–6; access Drives `:1`..`:7`. | All 7 drives respond independently without data crosstalk or buffer pollution. |
| **TC-15** | Stage 2 Writable Storage | Write 10 sectors via `SAVE`; verify on server. | Host image SHA-256 matches written data; no trailing image corruption. |
| **TC-16** | RTC Normalization (`OP_TIME`) | Run system clock init via `driver-TOD.S`. | Year $126$ normalized to $26$; `DATE$` renders valid `MM/DD/26` date string. |

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
