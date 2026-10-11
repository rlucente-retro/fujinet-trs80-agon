# Architectural Plan: FujiNet Firmware Integration for TRS-OS on Agon Light 2

**Document Version:** 1.5.0  
**Target Architecture:** Zilog eZ80F92 (Olimex Agon Light 2)  
**Target Operating System:** TRS-OS (TRSDOS 6.3.1 / LS-DOS 6.3 adapted for eZ80)  
**Architectural Baseline:**  
- **Boot Chain:** Agon Quark MOS $\to$ `OSboot.bin` $\to$ `TRS-OS` (eZ80 HAL/BIOS) $\to$ `TRSDOS / LS-DOS 6.3.1` (Z80 mode)  
- **Local Storage:** Drive `:0` (Local Boot SD / RAM) and Drive `:1` (Local Data SD) preserved untouched.  
- **Network Mass Storage:** Drive `:6` assigned as the primary Network Data Drive mapped to FujiNet Device Slot 0.  
- **Multi-Drive Expansion:** Drives `:2` through `:5` configurable for network mounting across FujiNet's 4 virtual device slots (Slots 0–3).  
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
2. **Build Upon Established Architecture:** Preserve the proven execution chain (`OSboot` $\to$ `TRS-OS` HAL $\to$ `TRSDOS/LS-DOS 6.3.1`), video/console VDP translation, keyboard routines, 60 Hz timer tasker, and RAM slice virtual floppy drivers for Drives `:0` and `:1`. Replace *only* the legacy line-oriented ASCII networking routines in `driver-FDCDVR.S` and the IPL handshake with binary DriveWire `OP_READEX` (`0xD2`) / `OP_WRITE` (`0x57`) and `OP_FUJI` (`0xE2`), ensuring seamless coexistence between local and network storage.
3. **Dual Physical Transport Support (Direct Serial & Wi-Fi Streaming):**
   * **Direct Serial Link (e.g., Olimex MOD-USB-RS232):** Direct point-to-point hardware connection between the Agon Light 2 UEXT connector (UART1) and FujiNet hardware or a host PC running FujiNet-PC. Because the physical serial link is inherently present and active upon power-on and hardware reset, no connection handshake is required. Agon Quark MOS directly boots TRS-OS using `*OSboot -2f TRSDOS.bin` without needing `openstream` or `closestream`.
   * **Wi-Fi TCP/IP Streaming (e.g., Olimex MOD-WIFI-ESP8266):** Retains the proven `openstream` $\to$ `OSboot.bin` $\to$ `TRSDOS.bin` workflow where Agon Quark MOS uses AT commands via `openstream` to connect the ESP8266 to FujiNet's BoIP server (TCP port 65504) and enter transparent UART passthrough mode (`CIPMODE=1`), boots TRS-OS, and leaves the transparent stream intact. `closestream` is used if returning to MOS to restore command mode (`AT+CIPMODE=0`).
4. **Safety-First, Staged Storage Deployment:**
   * **Stage 1 (Production Baseline):** Preserve existing local SD card storage on Drive `:0` (boot drive) and Drive `:1` (local data drive). Assign **Drive `:6`** as the primary network mass storage volume mapped to FujiNet Device Slot 0, operating in strict **Read-Only** mode to guarantee zero data corruption on host media.
   * **Stage 2 (Writable Storage & Server Fixes):** Address confirmed upstream firmware flaws in `BUILD_COCO` (disabled write checksum validation, unbounded DSK writes, and ignored flush errors) via server patches and negative regression qualification before enabling writes.
5. **Capacity & Device Realities:**
   * Support FujiNet `BUILD_COCO`'s architectural allocation of **4 Virtual Device Slots (Slots 0–3)** and **8 Host Slots (Slots 0–7)**.
   * Map additional TRSDOS drives (Drives `:2` through `:5`) to FujiNet slots 1–3 as required.
6. **Native TRSDOS Utilities via Wine Toolchain:** Implement native TRSDOS/LS-DOS 6.3 command utilities (`.CMD`) functionally equivalent to the NitrOS-9 FujiNet utilities, complying with TRSDOS's **8-character basename limit**, authentic load-record formatting, and guest SVC interfaces.

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
| **Local RAM Drives (`:0`, `:1`)** | Physical WD1773 Floppy Disk Controller. | In-memory RAM disk slices (`0x010000` / `0x045300`) managed by `ramdrv$`. | **Preserved Untouched.** Local boot volume (`:0`) and user volume (`:1`) remain fully functional. |
| **System Calls (`SVC`)** | `RST 28h` (RST 40 decimal) vector through Page 1 table. | Authentic Roy Soltoff / Logical Systems LS-DOS 6.3.1 SVC dispatch table. | **Preserved Untouched.** Full LS-DOS 6.3.1 API compatibility maintained. |
| **File System Manager** | `SYS4/SYS` (Directory parsing, FCBs, file extents, GAT/HIT). | Exact, original binary code from LS-DOS 6.3.1 executed directly by eZ80 CPU. | **Preserved Untouched.** All file parsing and extent management remain authentic. |
| **UART1 Warm Boot Setup** | Serial communications interface. | Configured during `finish_init` in `SYS0_IPL/ipl-eZ80_CPU.S`. | **Adapted / Preserved.** Solidifies `0001-uart1-streaming.patch` (Port C Mode 7, BRG=10, 8-N-1, IER=0, FCTL=07h). |
| **Network Disk Driver** | WD1773 FDC or bespoke network controller. | Legacy line-based ASCII protocol (`<XXXXX`, `>XXXXX`) in `driver-FDCDVR.S` for Drive `:6`. | **Replaced.** Overhauled to binary DriveWire `OP_READEX` (`0xD2`) / `OP_WRITE` (`0x57`) for Drive `:6` & `:2`–`:5`. |
| **IPL Network Startup** | Initial boot handshake. | Legacy `@ping` / `@bind` exchange in `ipl-BIOS.s` / `ipl_net_*.s`. | **Replaced.** Uses `OP_FUJI` Ready query (`0xE2 0x00`) or defers mounting to native `FN*.CMD` tools. |
| **Line Printer (`*PR`)** | Centronics parallel printer port at I/O port `0xF8`. | Intercepted in `driver-PRDVR.S`. | **Adapted.** Redirects character stream to UART1 `OP_PRINT` (`0x50`) to FujiNet. |

```
+-------------------------------------------------------------------------------+
|                             AGON LIGHT 2 (CLIENT)                             |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  |       TRS-OS Operating System (TRSDOS 6.3.1 / LS-DOS 6.3 on eZ80)       |  |
|  |                                                                         |  |
|  | +-------------------------------+     +-------------------------------+ |  |
|  | |     LOCAL STORAGE DRIVES      |     |    DriveWire Block Driver     | |  |
|  | | Drive :0 (Local Boot SD/RAM)  |     |        driver-FDCDVR.S        | |  |
|  | | Drive :1 (Local Data SD)      |     |   Drive :6 (Network Drive)    | |  |
|  | | SYS0..SYS6 + FN*.CMD Tools    |     |   (Optional Drives :2 to :5)  | |  |
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
1. **Quark MOS Execution:**
   * The user runs `*OSboot -2f TRSDOS.bin`.
   * `OSboot` loads `TRSDOS.bin` into eZ80 RAM at `0x000000`, sets terminal font/colors via internal UART0, writes the platform signature `3` into `UART0_SPR`, and jumps to `0x000000` (`__init`).
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
4. **BIOS & DOS Initialization:**
   * `ipl-BIOS.s` configures eZ80 Timer 1 for 60 Hz interrupts and mounts RAM Slice 0 as Drive `:0`.
   * `ipl-DOS.S` enables interrupts (`EI`) to start the 60 Hz task scheduler, calls `zICNFG`, and enters `zCMNDI` in `SYS6/SYS` to present the `TRSDOS Ready` prompt.

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
* When mounting a **DiskDISK** or **Reed HDV** file that contains a 256-byte container header, raw LSN 0 returns the header, shifting all subsequent sectors by one position.
* **Client Translation Strategy:** To ensure immediate compatibility with unpatched servers, the TRS-OS network driver supports an automatic sector offset adjustment (+1 sector / +256 bytes) when mounting DiskDISK containers, or the images can be stored as raw headerless `.dsk` payload files.

### 4.3 Geometry Derivation & Non-Universal GAT Placement

TRSDOS / LS-DOS 6.3 validates disk geometry dynamically:
* **GAT Location is Derived:** The Granule Allocation Table is **not universally located at Track 1**. Admitted JV1 images place the GAT on **Track 17, Sector 0**. DiskDISK specifies GAT and DCT parameters directly in header bytes `0xF9`–`0xFF`.
* The TRS-OS driver validates sector 0 byte 2 against the directory cylinder index, ensuring authentic DOS metadata parsing before mounting.

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

---

## 6. TRS-OS Drive Architecture & Storage Management

### 6.1 Local Storage Coexistence & Drive Allocation Policy

To ensure high reliability, TRS-OS preserves local storage alongside network drives:
* **Drive `:0` (Boot Drive):** Dedicated to the local SD card system boot image. Contains kernel overlays and core command tools.
* **Drive `:1` (Local Data Drive):** Dedicated to local SD card storage, providing immediate local storage even without network connectivity.
* **Drive `:6` (Primary Network Drive):** Mapped to FujiNet Device Slot 0. This matches the established `net_volume` routing in `driver-FDCDVR.S` and has been verified by emulator execution.
* **Drives `:2` through `:5` (Expansion Network Drives):** Dynamically configurable for network mounts across FujiNet's remaining device slots (Slots 1–3).

### 6.2 Device Control Table (DCT) Management & Write-Protection

Each logical drive in TRS-OS is governed by a 10-byte Device Control Table entry (`DCT0$` through `DCT7$`):
* `DCT+0`, `DCT+1`: Driver vector (points to `FDCBGN`).
* `DCT+3`: Drive status flags. **Bit 7 is set to 1 to enforce Write Protection** on network drives during Stage 1.
* `DCT+4`: Cylinder count and head configuration.
* `DCT+7`: Sectors per track mask.

### 6.3 Storage Driver Routing & DriveWire Read Integration (`driver-FDCDVR.S`)

The storage driver `driver-FDCDVR.S` is designed to build upon the existing Daniel Paul Martin RAM slice driver, routing local drives `:0` and `:1` to memory while directing Drive `:6` (and expansion drives) to DriveWire:

```assembly
; =============================================================================
; Storage Driver Routing Dispatch in driver-FDCDVR.S
; Preserves existing RAM slice driver while routing network drives to DriveWire
; =============================================================================
FDCDVR:
        ld      a, (LDRV$)              ; Check requested logical drive
        cp      0                       ; Drive :0 (Local Boot RAM/SD)?
        jr      z, call_ramdrv          ; Route to existing in-memory RAM driver
        cp      1                       ; Drive :1 (Local Data RAM/SD)?
        jr      z, call_ramdrv          ; Route to existing in-memory RAM driver
        cp      6                       ; Drive :6 (FujiNet Network Drive)?
        jr      z, call_drivewire       ; Route to binary DriveWire routines
        ; (Optionally test drives :2-:5 for expansion network slots)
        ret                             ; Unsupported drive

call_ramdrv:
        jp      ramdrv$                 ; Existing Daniel Paul Martin RAM driver (UNTOUCHED)

call_drivewire:
        ; Check operation: read, write, initialize
        ld      a, b                    ; Function in register B
        cp      9                       ; Sector Read?
        jr      z, rd_drivewire
        cp      10                      ; Sector Write?
        jr      z, wr_drivewire
        xor     a                       ; Return OK for init/restore
        ret

; =============================================================================
; Validated DriveWire Sector Read Routine (OP_READEX = 0xD2)
; =============================================================================
rd_drivewire:
        ; 1. Prepare 5-byte OP_READEX Request Header
        ld      a, 0D2h                 ; OP_READEX (0xD2)
        ld      (dw_hdr), a
        ld      a, (LDRV$)              ; Logical drive (e.g. 6)
        call    map_drive_to_slot       ; Maps Drive :6 -> Slot 0
        ld      (dw_hdr+1), a
        xor     a
        ld      (dw_hdr+2), a           ; LSN [23..16]
        ld      hl, (fdc_no_sum)        ; LSN from track/sector calculation
        ld      a, h
        ld      (dw_hdr+3), a           ; LSN [15..8]
        ld      a, l
        ld      (dw_hdr+4), a           ; LSN [7..0]

        ; 2. Transmit Header
        UART1.put.bytes 5, dw_hdr, fdc_rd_timeout
        jr      nz, fdc_io_error

        ; 3. Receive 256-Byte Sector Data into Staging Buffer
        ld      hl, dw_sector_buf
        UART1.get.bytes 256, hl, fdc_rd_timeout
        jr      nz, fdc_io_error

        ; 4. Calculate 16-Bit Big-Endian Summation
        call    calc_dw_checksum        ; Returns sum in DE (D=MSB, E=LSB)

        ; 5. Transmit 2-Byte Big-Endian Checksum to Server
        ld      a, d
        ld      (dw_chk), a
        ld      a, e
        ld      (dw_chk+1), a
        UART1.put.bytes 2, dw_chk, fdc_rd_timeout
        jr      nz, fdc_io_error

        ; 6. Receive 1-Byte Status Byte
        UART1.get.byte  fdc_rd_timeout
        jr      nz, fdc_io_error
        or      a                       ; 0x00 = OK
        jr      nz, fdc_status_error

        ; 7. Copy Staged Sector Data to Target User Buffer
        ld      hl, dw_sector_buf
        ld      de, (user_buffer_ptr)
        ld      bc, 256
        ldir
        xor     a                       ; Z set = success
        ret
```

### 6.4 Real-Time Clock & Line Printer Drivers
* **Printer (`driver-PRDVR.S`):** Sends `OP_PRINT` (`0x50` + character) without expecting a wire ACK. Document finalization is handled via FujiNet's web spooler interface.
* **Clock (`driver-TOD.S`):** Issues `OP_TIME` (`0x23`), receiving 6 bytes (Year since 1900, Month, Day, Hour, Minute, Second) to initialize the system time-of-day clock.

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
| `FNSETH` | `FNSETH <slot> <host>` | `0xF3` (256B) | Configures remote hostname or TNFS URL for a host slot. |
| `FNMOUNT` | `FNMOUNT <slot>` | `0xF9` | Mounts a host slot (connects FujiNet to TNFS server or SD card). |
| `FNDEVS` | `FNDEVS` | `0xF2` (152B) | Lists FujiNet's 4 virtual device slots (0–3), files, and modes. |
| `FNGETDEV` | `FNGETDEV <slot>` | `0xDA` (256B) | Displays pathname of disk image mounted in device slot (0–3). |
| `FNSETDEV` | `FNSETDEV <s> <h> <m> <p>`| `0xE2` (261B) | Assigns disk image on host slot to device slot. |
| `FNMNTIMG` | `FNMNTIMG <slot> [mode]` | `0xF8` | Commits disk image mount for active I/O. |

### 7.3 Detailed Specification of the 9 Command Utilities

#### 1. `FNSTATUS.CMD`
* **Syntax:** `FNSTATUS`
* **Payload:** Issues `OP_FUJI` + `0xE8`; confirms via `GetError` (`0x01`); reads 140 bytes of `AdapterConfig` via `GetResponse`.
* **Field Offsets:** SSID (0..32, 33B), Hostname (33..96, 64B), IP (97..100, 4B), Gateway (101..104, 4B), Netmask (105..108, 4B), DNS (109..112, 4B), MAC (113..118, 6B), BSSID (119..124, 6B), Firmware (125..139, 15B).

#### 2. `FNHOSTS.CMD`
* **Syntax:** `FNHOSTS`
* **Payload:** Issues `OP_FUJI` + `0xF4`; reads 256 bytes (8 slots $\times$ 32 bytes). Displays slot index and hostname string.

#### 3. `FNGETH.CMD`
* **Syntax:** `FNGETH <slot>` (`slot` = 0..7)
* **Payload:** Reads 256-byte host table via `0xF4` and extracts the 32-byte record for the specified slot.

#### 4. `FNSETH.CMD`
* **Syntax:** `FNSETH <slot> <hostname>`
* **Payload:** Reads table (`0xF4`), updates slot with max 31 characters + NUL, writes back via `0xF3` (258 bytes total on wire).

#### 5. `FNMOUNT.CMD`
* **Syntax:** `FNMOUNT <slot>`
* **Payload:** Issues `OP_FUJI` + `0xF9` + `<slot>`. Verifies return status via `GetError`.

#### 6. `FNDEVS.CMD`
* **Syntax:** `FNDEVS`
* **Payload:** Issues `OP_FUJI` + `0xF2`; reads **152 bytes** (4 device slots $\times$ 38 bytes: 1B HostSlot, 1B Mode, 36B Filename). Displays slots 0–3.

#### 7. `FNGETDEV.CMD`
* **Syntax:** `FNGETDEV <slot>` (`slot` = 0..3)
* **Payload:** Issues `OP_FUJI` + `0xDA` + `<slot>`; reads 256-byte path string.

#### 8. `FNSETDEV.CMD`
* **Syntax:** `FNSETDEV <dev_slot> <host_slot> <mode> <path>`
* **Payload:** Issues `OP_FUJI` + `0xE2` + `<dev_slot>` + `<host_slot>` + `<mode>` + `<path[256]>` (261 wire bytes).

#### 9. `FNMNTIMG.CMD`
* **Syntax:** `FNMNTIMG <dev_slot> [mode]`
* **Payload:** Issues `OP_FUJI` + `0xF8` + `<dev_slot>` + `<mode>`. Verifies status via `GetError`.

### 7.4 LS-DOS System Interface & System SVC Conventions

TRSDOS command utilities interface with LS-DOS via standard system SVCs dispatched through `RST 28h` (decimal `RST 40`):
* **`@PARAM` (SVC 17 / 11h):** Parses command line parameters against a defined parameter table. *(Note: SVC 34 is `zDODIR`, not parameter parsing).*
* **`@DSPLY` (SVC 10 / 0Ah):** Emits strings to the console. Strings **must be terminated by Carriage Return (`0x0D`, emitted) or ETX (`0x03`, not emitted)**. Strings are NOT NUL-terminated!
* **`@PRINT` (SVC 14 / 0Eh):** Directs output to the printer DCB.
* **`@EXIT` (SVC 22 / 16h):** Returns to TRSDOS Ready with return code in HL.

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

At 115,200 baud (8-N-1):
* 1 byte requires $86.8\ \mu\text{s}$.
* A complete 264-byte sector transaction (`OP_READEX` 5B + 256B data + 2B sum + 1B status) requires **22.92 ms minimum wire time**.
* Maximum theoretical sector throughput is **10.9 KiB/s**, before host I/O, network latency, and turnarounds.
* At 230,400 baud, minimum wire time drops to 11.46 ms (payload ceiling ~21.8 KiB/s).

### 9.2 UART FIFO Overrun Protection & Interrupt Isolation

* The eZ80 hardware FIFO holds only 16 bytes, representing **1.39 ms of buffer time** at 115,200 baud.
* Masking interrupts (`DI`) for >22.9 ms suppresses 60 Hz timer tasker ticks. Driver implementations must use tight polling loops and re-enable interrupts cleanly between sector bursts to maintain system responsiveness.
* `UART1_IER` is held at `0x00` to prevent stray interrupt handler crashes.

### 9.3 ESP8266 `+++` Control Escape Timing & Binary Transparency

The ESP8266 AT firmware requires precise silence intervals for the `+++` escape sequence:
* Greater than 20 ms silence before the first `+`.
* Less than 20 ms between each of the three `+` characters.
* Greater than 20 ms silence after the third `+`.
* At least 1.0 second pause before issuing the next AT command (`AT+CIPCLOSE`).
* Arbitrary binary sector payloads that accidentally contain `+++` will not trigger escape mode as long as they are transmitted in continuous bursts without matching the guard pauses.

---

## 10. Phased Implementation Roadmap

```
+-------------------------------------------------------------------------------+
| STAGE 1: Baseline Software Proof & Emulator Verification                      |
| - Verify read-only Drive :6 mapped to FujiNet Slot 0 in Fab Agon emulator.    |
| - Test directory listing (DIR :6) and file loading (LOAD "ELIZA/BAS:6").      |
| - Verify write rejection (R_PROTECTED) and local SD (:0/:1) coexistence.      |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| STAGE 2: Maintained Read-Only Storage Driver Integration                      |
| - Port verified client.c and uart.src into maintained driver-FDCDVR.S.        |
| - Integrate explicit eZ80 UART1 initialization in finish_init (SYS0_IPL).     |
| - Build and verify TRSDOS.bin via Wine/ZDS II and native AgonDev.             |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| STAGE 3: Utility ABI & FNSTATUS.CMD Implementation                            |
| - Build bin_to_cmd.py packager emitting authentic Record Type 01/02 formats.  |
| - Implement FNSTATUS.CMD using SVC 17 (@PARAM) and SVC 10 (@DSPLY).           |
| - Validate 140-byte AdapterConfig parsing and bounded display.                |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| STAGE 4: Host & Device Slot Configuration Utilities                           |
| - Implement FNHOSTS, FNGETH, FNSETH, and FNMOUNT (8 host slots, 256 bytes).   |
| - Implement FNDEVS, FNGETDEV, FNSETDEV, and FNMNTIMG (4 device slots, 152B).  |
| - Test TNFS server mounting and virtual disk attachment.                      |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| STAGE 5: Physical Transport Qualification (Bench Testing)                     |
| - Qualify Direct Serial Link (MOD-USB-RS232) on Agon Light 2 hardware.        |
| - Qualify Wi-Fi TCP/IP Streaming (MOD-WIFI-ESP8266) with openstream/closestream.|
| - Measure actual physical sector transfer latency and verify error recovery.  |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| STAGE 6: FujiNet Server Firmware Patches & Writable Storage                   |
| - Patch BUILD_COCO drivewire.cpp (big-endian checksum verification).          |
| - Patch mediaTypeDSK.cpp (image boundary enforcement and flush verification). |
| - Qualify writable storage with negative corruption tests and hash checks.    |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| STAGE 7: Safe Media Change Lifecycle & Multi-Drive Expansion                  |
| - Implement cache invalidation, open-file quiescence, and safe remount.       |
| - Expand device mapping across Drives :2 through :5.                          |
| - Integrate OP_TIME (0x23) RTC synchronization and OP_PRINT printer spooling. |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| STAGE 8: Integrated Packaging & High-Capacity Volume Architecture             |
| - Package complete distribution (.CMD utilities, patched TRSDOS.bin).        |
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
| **TC-04** | `OP_READEX` Verification | Read known sector from Drive `:6` via `0xD2` opcode. | 256 bytes read, client checksum sent, status byte `0x00` returned. |
| **TC-05** | Write Rejection (Stage 1) | Attempt write to Drive `:6` (`SAVE "TEST:6"`). | Immediate `R_PROTECTED` (error 15); host image remains unmodified. |
| **TC-06** | `FNSTATUS` Verification | Run `FNSTATUS` from TRSDOS Ready prompt. | Correctly parses and prints 140-byte adapter configuration. |
| **TC-07** | Host Table Management | Run `FNHOSTS` and `FNSETH 0 tnfs.fujinet.online`. | Displays 8 slots (256 bytes); updates and persists slot 0. |
| **TC-08** | Device Table Query | Run `FNDEVS` from TRSDOS Ready prompt. | Correctly queries 152 bytes; displays 4 device slots (0–3). |
| **TC-09** | Image Mount & Directory | Mount disk image to slot 0 $\to$ run `DIR :6`. | Directory listing displays files on mounted virtual disk. |
| **TC-10** | Local Storage Coexistence | Run `DIR :0` and write to `:1` while `:6` is mounted. | Local SD drives operate normally; independent of network status. |

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
