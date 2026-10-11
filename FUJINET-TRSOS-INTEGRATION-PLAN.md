# Architectural Plan: FujiNet Firmware Integration for TRS-OS on Agon Light 2

**Document Version:** 1.3.0  
**Target Architecture:** Zilog eZ80F92 (Olimex Agon Light 2)  
**Target Operating System:** TRS-OS (TRSDOS 6.3.1 / LS-DOS 6.3 adapted for eZ80)  
**Boot Drive:** Drive `:0` (Minimal RAM disk embedded in `TRSDOS.bin`, loaded via `OSboot.bin`)  
**Network Mass Storage:** Drives `:1` through `:7` (Dynamically mounted from `fujinet-firmware`)  
**Host / Coprocessor:** `fujinet-firmware` (DriveWire Bus over Serial / BoIP)  
**Toolchain:** Zilog ZDS II (`ez80asm.exe` via Wine) matching `trs-os-zds`  
**Physical Transports:**  
- **Transport A (Direct Serial Link):** Olimex MOD-USB-RS232 (or direct RS-232/UART) on Agon UEXT; connection is inherently active at hardware reset (no `openstream` or `closestream` needed).  
- **Transport B (Wi-Fi TCP/IP Streaming):** Olimex MOD-WIFI-ESP8266 on Agon UEXT; requires `openstream` in MOS to enter transparent passthrough mode (`CIPMODE=1`) prior to boot, and `closestream` upon exit.  

---

## Table of Contents

1. [Executive Summary & Objectives](#1-executive-summary--objectives)
2. [Codebase Analysis & Architectural Overview](#2-codebase-analysis--architectural-overview)
   - [2.1 `agon-net-utils`: MOS Network Bootstrap](#21-agon-net-utils-mos-network-bootstrap)
   - [2.2 `trs-net-tcp`: Existing Host Daemon & Protocol](#22-trs-net-tcp-existing-host-daemon--protocol)
   - [2.3 `trs-os-zds`: Agon Light 2 Operating System Core](#23-trs-os-zds-agon-light-2-operating-system-core)
   - [2.4 `fujinet-firmware`: Multi-Protocol Peripheral Server](#24-fujinet-firmware-multi-protocol-peripheral-server)
   - [2.5 `nitros9-main`: Reference FujiNet Client Implementations](#25-nitros9-main-reference-fujinet-client-implementations)
3. [Protocol Validation & Verification](#3-protocol-validation--verification)
   - [3.1 Validation of Direct Serial Protocol Support](#31-validation-of-direct-serial-protocol-support)
   - [3.2 Verification of Server Silence Upon Connection](#32-verification-of-server-silence-upon-connection)
   - [3.3 Boot Lifecycle & Handover: Direct Serial Link vs. Wi-Fi Streaming](#33-boot-lifecycle--handover-direct-serial-link-vs-wi-fi-streaming)
4. [Universal Disk Image Support: DiskDISK, JV1, and HDV](#4-universal-disk-image-support-diskdisk-jv1-and-hdv)
   - [4.1 Image Formats & Geometry Comparison](#41-image-formats--geometry-comparison)
   - [4.2 Automated Header Detection & Offset Translation](#42-automated-header-detection--offset-translation)
   - [4.3 Geometry Derivation: GAT Sector vs. Header Parameters](#43-geometry-derivation-gat-sector-vs-header-parameters)
   - [4.4 High-Capacity HDV Hard Disk Support (24-Bit LSNs)](#44-high-capacity-hdv-hard-disk-support-24-bit-lsns)
5. [Wire Protocol Specifications](#5-wire-protocol-specifications)
   - [5.1 DriveWire Block Storage Protocol (Read/Write/Print)](#51-drivewire-block-storage-protocol-readwriteprint)
   - [5.2 16-Bit Big-Endian Checksum Specification](#52-16-bit-big-endian-checksum-specification)
   - [5.3 FujiNet Configuration Transaction Protocol (`OP_FUJI`)](#53-fujinet-configuration-transaction-protocol-op_fuji)
6. [TRS-OS Drive Architecture: Drive :0 Boot & Drives :1–:7 Network Mounts](#6-trs-os-drive-architecture-drive-0-boot--drives-17-network-mounts)
   - [6.1 Minimal Boot Architecture for `TRSDOS.bin` (Drive `:0`)](#61-minimal-boot-architecture-for-trsdosbin-drive-0)
   - [6.2 De-coupling from Drive `:6`: Network Mounting across Drives `:1` through `:7`](#62-de-coupling-from-drive-6-network-mounting-across-drives-1-through-7)
   - [6.3 Device Control Table (DCT) Dynamic Management](#63-device-control-table-dct-dynamic-management)
   - [6.4 Storage Driver Overhaul (`driver-FDCDVR.S`)](#64-storage-driver-overhaul-driver-fdcdvrs)
   - [6.5 Real-Time Clock & Line Printer Drivers](#65-real-time-clock--line-printer-drivers)
7. [Native TRSDOS FujiNet Utilities Specification](#7-native-trsdos-fujinet-utilities-specification)
   - [7.1 Architecture & Peripheral Scope (TRSDOS Transport Decoupling)](#71-architecture--peripheral-scope-trsdos-transport-decoupling)
   - [7.2 Shared Client Library (`FUJILIB.LIB` / `fujilib.s`)](#72-shared-client-library-fujiliblib--fujilibs)
   - [7.3 Detailed Specification of the 9 Command Utilities](#73-detailed-specification-of-the-9-command-utilities)
   - [7.4 Command Line Parser & LS-DOS System Interface](#74-command-line-parser--ls-dos-system-interface)
8. [Build Pipeline with ZDS II (`ez80asm.exe` via Wine)](#8-build-pipeline-with-zds-ii-ez80asmexe-via-wine)
   - [8.1 Toolchain Configuration](#81-toolchain-configuration)
   - [8.2 TRSDOS `.CMD` Load Module Generation](#82-trsdos-cmd-load-module-generation)
   - [8.3 Automated Build Script & Packaging Pipeline](#83-automated-build-script--packaging-pipeline)
9. [Technical Challenges & Risk Mitigations](#9-technical-challenges--risk-mitigations)
   - [9.1 Lack of Hardware Flow Control on Agon UEXT](#91-lack-of-hardware-flow-control-on-agon-uext)
   - [9.2 UART FIFO Overrun Protection & Interrupt Isolation](#92-uart-fifo-overrun-protection--interrupt-isolation)
   - [9.3 Wi-Fi Latency, Timeouts, and Packet Fragmentation](#93-wi-fi-latency-timeouts-and-packet-fragmentation)
10. [Phased Implementation Roadmap](#10-phased-implementation-roadmap)
11. [Verification & Test Matrix](#11-verification--test-matrix)
12. [Project Reference Matrix & Upstream Sources](#12-project-reference-matrix--upstream-sources)

---

## 1. Executive Summary & Objectives

The primary objective of this project is to evolve the network mass storage and peripheral infrastructure of **TRS-OS** (Daniel Paul Martin's port of TRSDOS 6.3.1 / LS-DOS 6.3 for the Zilog eZ80 on the Olimex Agon Light 2) by replacing the legacy, bespoke `trs-net-tcp` server protocol with standard **FujiNet** protocols provided by **`fujinet-firmware`**.

### Core Goals:
1. **Adopt Industry-Standard Protocols:** Replace the legacy ASCII text protocol (`<00012\n`, `@ping\n`, `@bind\n`) with the high-performance binary **DriveWire 3/4** block storage protocol and the **`OP_FUJI` (0xE2)** transaction protocol.
2. **Dual Physical Transport Support (Direct Serial & Wi-Fi Streaming):**
   * **Direct Serial Link (e.g., Olimex MOD-USB-RS232):** Direct point-to-point hardware connection between the Agon Light 2 UEXT connector (UART1) and FujiNet hardware or a host PC running FujiNet-PC (via USB CDC-ACM or RS-232). Because the physical serial link is inherently present and active upon power-on and hardware reset, no connection handshake is required. Agon Quark MOS directly boots TRS-OS using `*OSboot -2f TRSDOS.bin` without needing `openstream` or `closestream`.
   * **Wi-Fi TCP/IP Streaming (e.g., Olimex MOD-WIFI-ESP8266):** Retains the proven `openstream` $\to$ `OSboot.bin` $\to$ `TRSDOS.bin` workflow where Agon Quark MOS uses AT commands via `openstream` to connect the ESP8266 to FujiNet's BoIP server (TCP port 65504) and enter transparent UART passthrough mode (`CIPMODE=1`), boots TRS-OS, and leaves the transparent stream intact. `closestream` is used if returning to MOS to restore command mode (`AT+CIPMODE=0`).
3. **Drive Role Separation:**
   * **Drive `:0` (Boot Drive):** Dedicated to the primary system volume embedded within `TRSDOS.bin`, containing the minimal core OS kernel and FujiNet command tools.
   * **Drives `:1` through `:7` (Network Mounts):** Freely available for mounting remote TNFS shares or local SD card disk images from FujiNet, removing the historical restriction to Drive `:6`.
4. **Universal Disk Image Support:** Provide robust, transparent support for **DiskDISK**, **JV1**, and high-capacity **HDV** disk images.
5. **Native TRSDOS Utilities via Wine Toolchain:** Implement native TRSDOS/LS-DOS 6.3 command utilities (`.CMD`) functionally equivalent to the NitrOS-9 FujiNet utilities, compiled using the existing ZDS II `ez80asm.exe` pipeline under Wine.

---

## 2. Codebase Analysis & Architectural Overview

```
+-------------------------------------------------------------------------------+
|                             AGON LIGHT 2 (CLIENT)                             |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  |       TRS-OS Operating System (TRSDOS 6.3.1 / LS-DOS 6.3 on eZ80)       |  |
|  |                                                                         |  |
|  | +-------------------------------+     +-------------------------------+ |  |
|  | |     BOOT DRIVE: Drive :0      |     |    DriveWire Block Driver     | |  |
|  | | Minimal Boot RAM Disk (:0)    |     |        driver-FDCDVR.S        | |  |
|  | | SYS0..SYS6 + FN*.CMD Tools    |     |   (Network Drives :1 to :7)   | |  |
|  | +-------------------------------+     +---------------+---------------+ |  |
|  | | Native FujiNet Utilities      |                     | OP_READ/WRITE   |  |
|  | | (FNSTATUS, FNMOUNT, etc.)     |                     | (0x52 / 0x57)   |  |
|  | +---------------+---------------+                     | 24b LSN, CRC    |  |
|  |                 |                                     |                 |  |
|  |                 | OP_FUJI (0xE2)                      |                 |  |
|  |                 +------------------+------------------+                 |  |
|  |                                    |                                    |  |
|  |                                    v                                    |  |
|  | +---------------------------------------------------------------------+ |  |
|  | |  driver-UART1.S: 115200 8-N-1 Polled I/O (No RTS/CTS Handshake)     | |  |
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
|     OP_READ/WRITE |                                       | OP_FUJI (0xE2)    |
|                   v                                       v                   |
|   +-------------------------------+       +-------------------------------+   |
|   |         drivewireDisk         |       |         drivewireFuji         |   |
|   |   (Universal Media Handler)   |       |    (FujiNet Config Engine)    |   |
|   |  - JV1 Disks (Flat 256B)      |       |  - 8 Host Slots (TNFS / SD)   |   |
|   |  - DiskDISK (+256B Header)    |       |  - 8 Device Slots (:1 to :7)  |   |
|   |  - HDV Hard Disk (+256B)      |       |  - Device Path Configuration  |   |
|   +-------------------------------+       +-------------------------------+   |
+-------------------------------------------------------------------------------+
```

### 2.1 `agon-net-utils`: MOS Network Bootstrap (Wi-Fi Only)
Located at `~/development/git/agon-net-utils`, this repository provides C utilities for Quark MOS on the eZ80 specifically for Wi-Fi streaming operation:
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

### 2.2 `trs-net-tcp`: Existing Host Daemon & Protocol
Located at `~/development/git/trs-net-tcp`, this repository provides `trs-netd.py`:
* **Wire Protocol:** ASCII line-oriented protocol with `\n` line delimiters:
  * Read Sector: `<00012\n` $\to$ Returns 256 bytes raw data + 4 bytes big-endian checksum (`sum % 256`).
  * Write Sector: `>00012\n` + 256B payload + 4B checksum $\to$ Returns 4B calculated checksum.
  * Spool Character: `#<ascii_code>\n`.
  * Control Commands: `@ping\n` $\to$ `@pong\n`, `@bind\n` $\to$ `@bound\n` + 256B DiskDISK header.
* **Characteristics:** Pure request-response; completely silent upon connection until client transmits. Bound to a single disk file (`Volumes/sys720k.dsk`) at daemon launch.

### 2.3 `trs-os-zds`: Agon Light 2 Operating System Core
Located at `~/development/git/trs-os-zds`, this repository contains the source code for TRSDOS 6.3.1 / LS-DOS 6.3 adapted for the eZ80:
* **Hardware Adaptation (`patches/0001-uart1-streaming.patch`):**
  * Agon Light 2 uses Port C Mode 7 (`PC0` = TXD1, `PC1` = RXD1) on the eZ80F92.
  * 18.432 MHz crystal yields BRG divisor 10 for 115,200 baud with 0% error.
  * UEXT header lacks RTS/CTS lines; driver runs in **pure polled I/O mode** with `UART1_IER` cleared to zero to avoid triggering `STRAY.handler` freezes.
* **Disk Driver (`driver-FDCDVR.S`):**
  * Masks interrupts (`DI`/`EI`) during multi-byte transfers to prevent timer tasker FIFO overruns.

### 2.4 `fujinet-firmware`: Multi-Protocol Peripheral Server
Located at `~/development/git/fujinet-firmware`:
* **DriveWire Bus (`BUILD_COCO`):**
  * Implemented in `lib/bus/drivewire/drivewire.cpp` and `lib/device/drivewire/`.
  * Operates over physical UART (`_serial`), USB CDC-ACM, or Becker port / BoIP (`_becker` over TCP port 65504).
  * Implements `OP_READ` (0x52), `OP_WRITE` (0x57), `OP_PRINT` (0x50), `OP_TIME` (0x23), and `OP_FUJI` (0xE2).
* **FujiNet Config Engine (`fujiDevice`):**
  * Manages 8 Host Slots (servers/shares) and 8 Device Slots (virtual drives).
  * Interfaced via three-phase `OP_FUJI` transactions: execute command $\to$ query error $\to$ read response.
* **FujiNet-PC:**
  * Compiles natively on macOS/Linux/Windows (`build/dist/fujinet`).
  * Running `build/dist/fujinet -V` confirms `Target: COCO`.

### 2.5 `nitros9-main`: Reference FujiNet Client Implementations
Located at `~/development/git/nitros9-main/level1/cmds/fn*.as`:
* Nine reference utilities demonstrate interaction with the FujiNet firmware:
  * `fnstatus.as`: Queries `FUJI$GetAdapterCfg` (0xE8); prints IP, SSID, MAC, version.
  * `fnlisthosts.as`: Queries `FUJI$ReadHostSlots` (0xF4); lists host slots 0–7.
  * `fngethost.as`: Queries host slot by ID.
  * `fnsethost.as`: Modifies host slot using `FUJI$WriteHostSlots` (0xF3).
  * `fnmount.as`: Issues `FUJI$MountHost` (0xF9) to mount a TNFS server.
  * `fnlistdevs.as`: Queries `FUJI$ReadDevSlots` (0xF2); lists disk device slots 0–7.
  * `fngetdevfile.as`: Queries `FUJI$GetDevicePath` (0xDA); retrieves mounted image path.
  * `fnsetdevfile.as`: Issues `FUJI$SetDevicePath` (0xE2); configures disk image assignment.
  * `fnmountimg.as`: Issues `FUJI$MountImage` (0xF8); mounts image in device slot.

---

## 3. Protocol Validation & Verification

### 3.1 Validation of Direct Serial Protocol Support

**Finding: FULLY SUPPORTED.**

FujiNet firmware incorporates an architectural I/O abstraction layer (`IOChannel`) in `lib/hardware/`:
1. **Physical UART (`UARTChannel`):** Directly interfaces with ESP32 / ESP32-S3 hardware UARTs at 115,200 baud (or configured baud).
2. **USB Serial (`ACMChannel`):** Interfaces with USB CDC-ACM virtual COM ports.
3. **Bus-over-IP (`BoIPChannel`):** Interfaces with raw TCP/IP sockets (`lib/hardware/BoIPChannel.cpp`).

All DriveWire opcodes (`op_read`, `op_write`, `op_fuji`, `op_clock`, `op_print`) execute through `_port->read()` and `_port->write()`. The protocol logic is 100% transport-agnostic and functions identically whether over a physical RS-232 / UART cable or across a transparent TCP stream.

#### Direct Hardware Serial Operation (Olimex MOD-USB-RS232)
When connecting an Agon Light 2 to a host PC running FujiNet-PC (or a standalone FujiNet hardware unit) using the **Olimex MOD-USB-RS232** module (or a compatible RS-232 level converter) on the UEXT header:
* The eZ80 communicates via UART1 (`PC0` = TXD1, `PC1` = RXD1) at 115,200 baud 8-N-1.
* The physical serial circuit is **inherently present and active upon hardware reset**.
* There is no modem negotiation, no AT command set, and no Wi-Fi association delay.
* FujiNet-PC attaches directly to the host TTY/COM port (e.g., `/dev/cu.usbserial*` or `COM3`) via `UARTChannel`/`ACMChannel`.
* Crucially, **`openstream` and `closestream` are not used in this configuration**.

### 3.2 Verification of Server Silence Upon Connection

**Finding: CONFIRMED 100% SILENT.**

Inspection of `lib/hardware/BoIPChannel.cpp` confirms:
1. When a client establishes a TCP connection, `BoIPChannel::accept_connection()` executes:
   * Sets socket options (`SO_KEEPALIVE`, `TCP_NODELAY`, non-blocking).
   * Emits debug diagnostics to the local server console via `Debug_printf`.
   * **Transmits zero bytes over the network socket.**
2. In `systemBus::service()` in `lib/bus/drivewire/drivewire.cpp`:
   ```cpp
   if (available())
       _drivewire_process_cmd();
   ```
   If no data is present in the receive buffer, `service()` takes no action and transmits nothing.

**Conclusion:** FujiNet is a strict slave/responder. After a streaming TCP connection or serial link is opened, the server remains completely silent until the client sends a command byte.

### 3.3 Boot Lifecycle & Handover: Direct Serial Link vs. Wi-Fi Streaming

The boot lifecycle differs based on the physical transport used:

#### Workflow A: Direct Physical Serial Link (Olimex MOD-USB-RS232)
Because the serial connection is inherently established by the physical wiring, no pre-boot stream utilities are needed. The user directly boots TRS-OS from the MOS prompt:

```
[ Agon Quark MOS ]
  |
  +---> OSboot -2f TRSDOS.bin
  |     - Direct launch: loads TRSDOS.bin into eZ80 RAM
  |     - Transfers execution directly to TRS-OS kernel entry point
  |     - (No openstream or connection setup required!)
  v
[ TRS-OS / TRSDOS 6.3.1 ]
  |
  +---> finish_init (SYS0_IPL):
  |     - Re-asserts Port C Mode 7 (PC0=TXD1, PC1=RXD1)
  |     - Configures BRG divisor = 10 (115200 baud @ 18.432 MHz)
  |     - Zeroes UART1_IER to guarantee pure polled I/O
  |     - Flushes UART1 hardware FIFOs (out0 (UART1_FCTL), 07h)
  |
  +---> TRS-OS boots into Drive :0 (the minimal boot RAM disk embedded in TRSDOS.bin)
  +---> User dynamically mounts remote/local images (JV1, DiskDISK, HDV) onto Drives :1 through :7
```

#### Workflow B: Wi-Fi TCP/IP Streaming (Olimex MOD-WIFI-ESP8266)
When using the Wi-Fi module, the ESP8266 boots in AT command mode. The `openstream` utility from `agon-net-utils` must be executed first to connect to FujiNet and switch the ESP8266 into transparent UART passthrough mode (`CIPMODE=1`):

```
[ Agon Quark MOS ]
  |
  +---> openstream <fujinet_host> 65504
  |     - UART1 communicates with Olimex MOD-WIFI-ESP8266 AT firmware
  |     - Sends AT+CIPSTART="TCP","<fujinet_host>",65504
  |     - Enters transparent streaming mode (AT+CIPMODE=1 & AT+CIPSEND)
  |     - FujiNet accepts TCP socket and remains COMPLETELY SILENT
  |     - openstream flushes residual chars, detaches MOS ISR (mos_uclose), exits
  |
  +---> OSboot -2f TRSDOS.bin
  |     - Loads TRSDOS.bin into eZ80 RAM
  |     - Transfers execution to TRS-OS entry point while preserving UART1 stream
  v
[ TRS-OS / TRSDOS 6.3.1 ]
  |
  +---> finish_init (SYS0_IPL):
  |     - Re-asserts Port C Mode 7 (PC0=TXD1, PC1=RXD1)
  |     - Configures BRG divisor = 10 (115200 baud @ 18.432 MHz)
  |     - Zeroes UART1_IER to guarantee pure polled I/O
  |     - Flushes UART1 hardware FIFOs (out0 (UART1_FCTL), 07h)
  |
  +---> TRS-OS boots into Drive :0 (the minimal boot RAM disk embedded in TRSDOS.bin)
  +---> User dynamically mounts remote images (JV1, DiskDISK, HDV) onto Drives :1 through :7
  |
  +---> (Optional Exit to MOS: closestream issues '+++' to restore AT command mode)
```

---

## 4. Universal Disk Image Support: DiskDISK, JV1, and HDV

To ensure maximum compatibility with the rich ecosystem of TRS-80 / Model 4 software, the integration natively supports **DiskDISK**, **JV1**, and high-capacity **HDV** formats.

### 4.1 Image Formats & Geometry Comparison

| Format | Header Size | Sector Size | Typical Capacities | File Offset Formula | Magic / Identifier |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **JV1** | 0 bytes | 256 bytes | 87.5 KB – 720 KB | `Offset = LSN * 256` | Flat file, no header |
| **DiskDISK** | 256 bytes | 256 bytes | 87.7 KB – 720.2 KB | `Offset = 256 + (LSN * 256)` | First 8 bytes = `DiskDISK` |
| **HDV** | 256 bytes (standard) | 256 bytes | 5 MB – 32 MB | `Offset = 256 + (LSN * 256)` | Extension `.hdv` or HDV header |

### 4.2 Automated Header Detection & Offset Translation

In the DriveWire model, the client operating system requests sectors strictly by **Logical Sector Number (LSN)**:
$$\text{Client Request} = \text{Drive } D, \text{ LSN } L$$

The physical file offset is resolved on the FujiNet server. We extend FujiNet's `MediaTypeDSK` (or a dedicated `mediaTypeTRSDOS`) to perform automatic header classification at image mount time:

```cpp
// Offset computation in FujiNet Media Handler
uint32_t MediaTypeDSK::_block_to_offset(uint32_t blockNum)
{
    return _image_header_offset + (blockNum * MEDIA_BLOCK_SIZE);
}
```

#### Identification Rules:
1. **Magic Signature Check:** Read the first 256 bytes of the image file. If bytes 0..7 match ASCII `'DiskDISK'`, set `_image_header_offset = 256`.
2. **HDV Signature / Extension Check:** If the filename ends with `.hdv` or contains an HDV geometry descriptor, set `_image_header_offset = 256` (or `0` if raw unheadered HDV).
3. **Default / JV1:** If neither signature matches, set `_image_header_offset = 0` (flat raw 256-byte sector layout).

**Architectural Benefit:** TRS-OS drivers remain completely agnostic to the underlying container format. Reading or writing sector $L$ always requests LSN $L$; the FujiNet server handles the container offset transparently.

### 4.3 Geometry Derivation: GAT Sector vs. Header Parameters

TRSDOS and LS-DOS 6.3 manage disk allocation via the **Granule Allocation Table (GAT)** located on **Track 17, Sector 0** (LSN 170 for standard single-density, or LSN 306 for double-density):
* The GAT sector encodes:
  * Number of tracks and cylinders.
  * Sectors per granule and granules per track.
  * Directory cylinder index.
  * Volume label and password hashes.
* For **JV1** images, TRS-OS inspects the GAT sector during mounting to configure the drive's Device Control Table (DCT) parameters dynamically.
* For **DiskDISK** images, the 256-byte header explicitly duplicates the 7 DCT parameters (`DCT+3` through `DCT+9`), which can be directly loaded into the DCT during mount.

### 4.4 High-Capacity HDV Hard Disk Support (24-Bit LSNs)

Hard disk images (.HDV) in LS-DOS 6.3 enable virtual partitions and massive storage volumes (5 MB to 32 MB):
* **DriveWire Addressing:** DriveWire uses **24-bit LSNs** on the wire (`LSN[23..16]`, `LSN[15..8]`, `LSN[7..0]`), enabling addressing of up to $2^{24} = 16,777,216$ sectors ($\mathbf{4\text{ GB}}$).
* **HDV Capacity:** A 32 MB HDV disk contains only 131,072 sectors ($0\text{x}020000$), easily fitting within DriveWire's 24-bit addressing envelope.
* **Driver Routing:** When an HDV image is mounted, TRSDOS configures the drive's DCT with hard drive characteristics (e.g. 512 cylinders, 4 heads, 32 sectors/track). Sector requests pass directly to `driver-FDCDVR.S`, which emits the 24-bit LSN over UART1.

---

## 5. Wire Protocol Specifications

### 5.1 DriveWire Block Storage Protocol (Read/Write/Print)

DriveWire 3/4 uses compact binary framing:

#### Sector Read (`OP_READ = 0x52`)
```
Request (Client -> FujiNet): 5 bytes
+---------------+----------------+----------------+----------------+----------------+
|  0x52 (OP_RD) | Drive# (0-255) |  LSN [23..16]  |  LSN [15..8]   |   LSN [7..0]   |
+---------------+----------------+----------------+----------------+----------------+

Response (FujiNet -> Client): 259 bytes
+---------------+----------------+----------------+---------------------------------+
|  Status (1B)  | Checksum [15..8| Checksum [7..0]|       256-Byte Sector Data      |
|  (0x00 = OK)  |     (MSB)      |     (LSB)      |                                 |
+---------------+----------------+----------------+---------------------------------+
```

#### Sector Write (`OP_WRITE = 0x57`)
```
Request (Client -> FujiNet): 263 bytes
+---------------+----------------+----------------+----------------+----------------+
|  0x57 (OP_WR) | Drive# (0-255) |  LSN [23..16]  |  LSN [15..8]   |   LSN [7..0]   |
+---------------+----------------+----------------+----------------+----------------+
|                             256-Byte Sector Data                                  |
+-------------------------------------------------+----------------+----------------+
|                                                 | Checksum [15..8| Checksum [7..0]|
+-------------------------------------------------+----------------+----------------+

Response (FujiNet -> Client): 1 byte
+---------------+
|  Status (1B)  |  (0x00 = OK, 0xF3 [243] = CRC Error)
+---------------+
```

#### Printer Output (`OP_PRINT = 0x50` & `OP_PRINTFLUSH = 0x46`)
* **Spool Character:** `[0x50] [Char_Byte]` (2 bytes, unbuffered response).
* **Flush Buffer:** `[0x46]` (1 byte, triggers spool file commit/render).

#### Real-Time Clock Query (`OP_TIME = 0x23`)
* **Request:** `[0x23]` (1 byte).
* **Response:** 6 bytes representing host timestamp: Year (since 1900), Month (1-12), Day (1-31), Hour (0-23), Minute (0-59), Second (0-59).

### 5.2 16-Bit Big-Endian Checksum Specification

DriveWire verifies data integrity using an unsigned 16-bit summation across all 256 sector bytes:

$$\text{Checksum} = \left( \sum_{i=0}^{255} \text{byte}_i \right) \pmod{65536}$$

* **Endianness:** Transmitted and received in **big-endian** format (`Checksum[15..8]` followed by `Checksum[7..0]`).
* On the little-endian eZ80/Z80 architecture, byte order must be reversed when storing or loading 16-bit register pairs (`D` = MSB, `E` = LSB).

### 5.3 FujiNet Configuration Transaction Protocol (`OP_FUJI`)

All configuration commands follow a structured three-phase transaction model:

```
Phase 1: Execute Command
Client -> Server: [ 0xE2 (OP_FUJI) ] [ CMD_BYTE ] [ Parameters... ]

Phase 2: Check Status / Error
Client -> Server: [ 0xE2 (OP_FUJI) ] [ 0x02 (FUJI$GetError) ]
Server -> Client: [ Status (1B) ]  (0x01 = FUJI$E.OK, 0x90 = FUJI$E.Err)

Phase 3: Fetch Response Data (if applicable)
Client -> Server: [ 0xE2 (OP_FUJI) ] [ 0x01 (FUJI$GetResponse) ]
Server -> Client: [ N-Byte Response Payload... ]
```

---

## 6. TRS-OS Drive Architecture: Drive :0 Boot & Drives :1–:7 Network Mounts

### 6.1 Minimal Boot Architecture for `TRSDOS.bin` (Drive `:0`)

In TRSDOS / LS-DOS 6.3, **Drive `:0` is the permanent system boot drive**. 

`TRSDOS.bin` contains the OS boot code and Slice 1 Volume 0 embedded directly in eZ80 RAM. When `OSboot.bin TRSDOS.bin` runs, `ipl-BIOS.s` mounts this embedded RAM volume as **Drive `:0`**.

In this new architecture:
* **Lean Boot Disk (Drive `:0`):** `TRSDOS.bin` embeds only the minimal core system files required to reach the `TRSDOS Ready` prompt:
  * `SYS0/SYS` through `SYS6/SYS` (OS kernel, I/O drivers, memory manager, command interpreter).
  * The native FujiNet utility suite: `FNSTATUS.CMD`, `FNMOUNT.CMD`, `FNLISTHOSTS.CMD`, `FNSETHOST.CMD`, `FNLISTDEVS.CMD`, `FNGETDEV.CMD`, `FNSETDEV.CMD`, `FNMOUNTIMG.CMD`.
  * The updated DriveWire disk driver (`driver-FDCDVR.S`).
* **Instant Start:** Booting from the minimal embedded image takes less than 1 second.
* **Secondary Volume Mounting:** Once booted into TRSDOS, the system prompt is immediately available, and the user can mount any external disks, games, development suites, or large HDV hard drives onto **Drives `:1` through `:7`**.

### 6.2 De-coupling from Drive `:6`: Network Mounting across Drives `:1` through `:7`

Network disk storage is no longer confined to Drive `:6`:
* **Drives `:1` through `:7`** are fully allocatable to FujiNet remote disk images.
* If `OSboot` was invoked with a local disk image (`OSboot TRSDOS.bin work.dsk`), that local image is mounted in RAM as Drive `:1`, leaving **Drives `:2` through `:7`** for FujiNet network mounts.
* If no local disk was specified, all seven drives (**`:1` through `:7`**) are available for FujiNet network storage.
* **Slot Mapping:** TRSDOS Drive `:D` maps to FujiNet Device Slot `S`:
  $$\text{FujiNet Device Slot } S = D - 1 \quad (\text{for } D \in [1..7])$$
  *(Or explicitly configurable via `FNSETDEV` / `FNMOUNTIMG`).*

### 6.3 Device Control Table (DCT) Dynamic Management

When a disk image is mounted to Drive `:D` (`D` = 1..7) via `FNMOUNTIMG`:
1. The utility queries the image type (JV1, DiskDISK, HDV) and geometry.
2. The target drive's DCT (`DCT1$` through `DCT7$`) in low memory is dynamically initialized:
   * Sets driver vector to `FDCBGN`.
   * Sets cylinder count, sectors/track, heads, and directory track based on image metadata.
   * Marks the drive online and ready.

### 6.4 Storage Driver Overhaul (`driver-FDCDVR.S`)

The sector read routine in `driver-FDCDVR.S` is updated to support all dynamic network drives (`:1` through `:7`):

```assembly
; =============================================================================
; Universal DriveWire Sector Read Routine for driver-FDCDVR.S
; =============================================================================
rd_drivewire:
        xor     a
        ld      (rd_retries), a         ; Reset retry counter

rd_dw_retry:
        ld      a, 07h                  ; Flush UART1 RX/TX FIFOs
        out0    (UART1_FCTL), a

        ; Prepare 5-byte DriveWire OP_READ header
        ld      a, 52h                  ; OP_READ (0x52)
        ld      (dw_hdr), a
        ld      a, (LDRV$)              ; Logical drive (1 to 7)
        dec     a                       ; Map Drive :1..:7 -> DW Slot 0..6
        ld      (dw_hdr+1), a           ; Target slot index
        xor     a
        ld      (dw_hdr+2), a           ; LSN [23..16] (support 24-bit addressing)
        ld      hl, (fdc_no_sum)        ; LSN from calcsec
        ld      a, h
        ld      (dw_hdr+3), a           ; LSN [15..8]
        ld      a, l
        ld      (dw_hdr+4), a           ; LSN [7..0]

        di                              ; Mask interrupts during streaming I/O
        UART1.put.bytes 5, dw_hdr, fdc_rd_timeout

        ; Receive 1-byte Status
        UART1.get.byte  fdc_rd_timeout
        or      a                       ; Check if status == 0
        jr      nz, fdc_rd_status_err

        ; Receive 2-byte Checksum (Big-Endian)
        UART1.get.bytes 2, dw_chk, fdc_rd_timeout

        ; Receive 256-byte Sector Data directly into destination buffer
        pop     hl                      ; Destination buffer address
        push    hl                      ; Keep copy on stack
        UART1.get.bytes 0, hl, fdc_rd_timeout
        ei                              ; Re-enable interrupts

        ; Compute 16-bit checksum across buffer
        pop     hl
        push    hl
        call    calc_dw_checksum        ; Returns computed checksum in DE
        
        ; Compare computed checksum (DE) with received checksum (dw_chk)
        ld      a, (dw_chk)             ; Received MSB
        cp      d
        jr      nz, fdc_rd_chk_err
        ld      a, (dw_chk+1)           ; Received LSB
        cp      e
        jr      nz, fdc_rd_chk_err

        ; Success: clean up stack and return
        pop     hl                      ; Discard saved buffer
        pop     de                      ; Restore track/sector
        pop     bc                      ; Restore entry BC
        pop     hl                      ; Restore entry HL
        xor     a                       ; Z set = success
        ret
```

### 6.5 Real-Time Clock & Line Printer Drivers
* **Printer (`driver-PRDVR.S`):** Replace `#<byte>\n` string framing with `OP_PRINT` (`0x50` + character) and issue `OP_PRINTFLUSH` (`0x46`) upon document close or form feed.
* **Clock (`drIver-TOD.S`):** Query FujiNet via `OP_TIME` (`0x23`) during IPL to synchronize the eZ80 system clock with network NTP time.

---

## 7. Native TRSDOS FujiNet Utilities Specification

### 7.1 Architecture & Peripheral Scope (TRSDOS Transport Decoupling)

A central architectural principle of this integration is that **TRSDOS has no networking stack and is completely unaware of TCP/IP or the physical link layer**.

1. **Transport Decoupling & OS Transparency:**
   * Whether the Agon Light 2 connects via **Direct Serial (MOD-USB-RS232)** or **Wi-Fi Streaming (MOD-WIFI-ESP8266)**, TRSDOS experiences the link identically: as a raw, polled serial UART on `UART1` (`PC0`/`PC1`) at 115,200 baud 8-N-1.
   * In the Wi-Fi configuration, Agon Quark MOS handles the entire AT command negotiation (`openstream`) before TRSDOS is ever launched.
   * TRSDOS has no socket abstractions, no TCP/IP stack, and no knowledge of network interfaces.

2. **Scope of FujiNet Utilities:**
   * The `FN*.CMD` command utilities communicate over standard serial `OP_FUJI` (0xE2) transactions to interrogate and configure the **remote FujiNet peripheral device**, not the local Agon hardware.
   * **Network Status (`FNSTATUS`):** Queries FujiNet's `FUJI$GetAdapterCfg` (0xE8). The resulting IP address, Wi-Fi SSID, Gateway, and MAC address describe the **FujiNet peripheral's own connection to the local LAN / Internet** (used by FujiNet to fetch disk images from remote TNFS servers or serve its web configuration interface), **not** an Agon Light 2 network interface.
   * **Direct Serial / FujiNet-PC Operation:** When connecting via direct serial to a PC running FujiNet-PC, `FNSTATUS` displays the host PC's IP configuration and indicates `Direct Serial / Wired` (or N/A) for Wi-Fi SSID.
   * **Host Slot Management (`FNLISTHOSTS`, `FNMOUNT`, etc.):** Tells the **FujiNet server** to open connections to remote TNFS file servers or local SD cards over its own network stack.
   * **Device Slot Mapping (`FNSETDEV`, `FNMOUNTIMG`):** Binds disk images hosted on FujiNet to virtual DriveWire devices and links them to TRSDOS drives `:1` through `:7`.

### 7.2 Shared Client Library (`FUJILIB.LIB` / `fujilib.s`)

To eliminate code duplication across all utilities, implement a reusable Z80/eZ80 FujiNet client library (`fujilib.s`):

```
+-------------------------------------------------------------------------------+
|                                  FUJILIB API                                  |
+-------------------+-----------------------------------------------------------+
| fn_open           | Verify UART1 stream readiness and flush hardware FIFOs.   |
| fn_ready          | Poll OP_FUJI + FUJI$Ready (0x00); return Z if ready.      |
| fn_cmd            | Transmit OP_FUJI + command opcode + parameters.           |
| fn_get_error      | Query OP_FUJI + FUJI$GetError (0x02); return A = status.  |
| fn_read_response  | Query OP_FUJI + FUJI$GetResponse (0x01); read N bytes.    |
| fn_close          | Finalize transaction, re-enable interrupts, flush state.  |
+-------------------+-----------------------------------------------------------+
```

### 7.3 Detailed Specification of the 9 Command Utilities

All utilities will be assembled as native TRSDOS load modules (`.CMD` files) executable from the `TRSDOS Ready` prompt:

```
                      TRSDOS Native FujiNet Utility Suite
┌─────────────┬─────────────────────────────────────────────────────────────────┐
│ Command     │ Purpose & Usage                                                 │
├─────────────┼─────────────────────────────────────────────────────────────────┤
│ FNSTATUS    │ Display FujiNet peripheral firmware, IP, MAC & network status   │
│ FNLISTHOSTS │ Display table of FujiNet's 8 configured host slots (0–7)        │
│ FNGETHOST   │ Query and display hostname for a specific FujiNet host slot     │
│ FNSETHOST   │ Configure remote hostname or TNFS URL for a FujiNet host slot   │
│ FNMOUNT     │ Mount a host slot (connect FujiNet to remote TNFS server or SD) │
│ FNLISTDEVS  │ List FujiNet's 8 virtual device slots, mounted files & modes    │
│ FNGETDEV    │ Display full pathname of disk image mounted in a device slot    │
│ FNSETDEV    │ Assign a disk image file on a host slot to a device slot        │
│ FNMOUNTIMG  │ Mount FujiNet device slot disk image to TRSDOS drive (:1–:7)   │
└─────────────┴─────────────────────────────────────────────────────────────────┘
```

#### 1. `FNSTATUS.CMD`
* **Syntax:** `FNSTATUS`
* **Protocol Flow:** Issues `OP_FUJI` + `FUJI$GetAdapterCfg` (0xE8); confirms via `FUJI$GetError`; reads 140 bytes of `AdapterConfig` via `FUJI$GetResponse`.
* **Output Format (Hardware FujiNet Peripheral with Wi-Fi):**
  ```text
  FujiNet Peripheral Status:
    Firmware Version : 1.6-49ca88a0d (COCO)
    Hostname         : fujinet-agon
    Connection Type  : Wi-Fi (Hardware Peripheral)
    Wi-Fi SSID       : RetroNet-5G
    IP Address       : 192.168.1.150
    Gateway          : 192.168.1.1
    Netmask          : 255.255.255.0
    DNS Server       : 192.168.1.1
    MAC Address      : A4:CF:12:8B:3E:91
  ```
* **Output Format (Direct Serial Link to Host PC running FujiNet-PC):**
  ```text
  FujiNet Peripheral Status:
    Firmware Version : 1.6-49ca88a0d (COCO)
    Hostname         : retropc
    Connection Type  : Direct Serial Link (Host PC)
    Wi-Fi SSID       : N/A (Direct Serial / Wired Host)
    IP Address       : 192.168.1.85
    Gateway          : 192.168.1.1
    Netmask          : 255.255.255.0
    DNS Server       : 192.168.1.1
    MAC Address      : 02:42:AC:11:00:02
  ```

#### 2. `FNLISTHOSTS.CMD`
* **Syntax:** `FNLISTHOSTS`
* **Protocol Flow:** Issues `OP_FUJI` + `FUJI$ReadHostSlots` (0xF4); reads 256 bytes (8 slots $\times$ 32 bytes).

#### 3. `FNGETHOST.CMD`
* **Syntax:** `FNGETHOST <slot>` (`slot` = 0..7)
* **Protocol Flow:** Queries host slots on FujiNet and displays entry for the specified slot.

#### 4. `FNSETHOST.CMD`
* **Syntax:** `FNSETHOST <slot> <hostname>`
* **Protocol Flow:** Reads existing 256 bytes via `FUJI$ReadHostSlots` (0xF4); updates slot record; commits back via `FUJI$WriteHostSlots` (0xF3).

#### 5. `FNMOUNT.CMD`
* **Syntax:** `FNMOUNT <slot>`
* **Protocol Flow:** Issues `OP_FUJI` + `FUJI$MountHost` (0xF9) + `[slot (1B)]`; verifies status via `FUJI$GetError`. Commands FujiNet to connect to the remote TNFS server or local SD card.

#### 6. `FNLISTDEVS.CMD`
* **Syntax:** `FNLISTDEVS`
* **Protocol Flow:** Issues `OP_FUJI` + `FUJI$ReadDevSlots` (0xF2); reads 304 bytes (8 slots $\times$ 38 bytes: 1B HostSlot, 1B Mode, 36B Filename).
* **Output Format:**
  ```text
  FujiNet Device Slots:
    Slot 0 (Drive :1): [HS:0] [Mode:RW] trs80_games.dsk
    Slot 1 (Drive :2): [HS:0] [Mode:RO] sys720k.dsk
    Slot 2 (Drive :3): [HS:1] [Mode:RW] work_disk.hdv
    Slot 3 (Drive :4): <empty>
    ...
  ```

#### 7. `FNGETDEV.CMD`
* **Syntax:** `FNGETDEV <slot>`
* **Protocol Flow:** Issues `OP_FUJI` + `FUJI$GetDevicePath` (0xDA) + `[slot (1B)]`; reads 256 bytes.

#### 8. `FNSETDEV.CMD`
* **Syntax:** `FNSETDEV <dev_slot> <host_slot> <mode> <path>`
  * `mode`: `1` = Read-Only, `2` = Read/Write.
* **Protocol Flow:** Issues `OP_FUJI` + `FUJI$SetDevicePath` (0xE2) + parameters.

#### 9. `FNMOUNTIMG.CMD`
* **Syntax:** `FNMOUNTIMG <dev_slot> [mode] [drive]`
  * `mode` (optional): `1` = Read-Only, `2` = Read/Write (default: 1).
  * `drive` (optional): Target TRSDOS drive (`1` through `7`, defaults to `dev_slot + 1`).
* **Protocol Flow:** Issues `OP_FUJI` + `FUJI$MountImage` (0xF8) + parameters; verifies status; initializes DCT table for target drive (`:1` through `:7`).

### 7.4 Command Line Parser & LS-DOS System Interface
TRSDOS command utilities interface with LS-DOS via standard system SVCs:
* `@PARAM` (SVC 34): Parses command line tokens and flags.
* `@DSPLY` (SVC 10): Outputs strings to the console.
* `@PRINT` (SVC 14): Prints formatted diagnostic messages.
* `@EXIT` (SVC 22): Returns to TRSDOS Ready with exit code.

---

## 8. Build Pipeline with ZDS II (`ez80asm.exe` via Wine)

To maintain 100% toolchain parity with `trs-os-zds`, all utilities and driver patches will be assembled using **Zilog ZDS II (`ez80asm.exe`)** executed via Wine on macOS.

### 8.1 Toolchain Configuration
The build system invokes `ez80asm.exe` with standard ZDS II assembly flags:
```makefile
WINE     ?= wine64
ZDS_DIR  ?= $(HOME)/.wine/drive_c/Zilog/ZDSII_eZ80Acclaim!_5.3.5
AS       := $(WINE) "$(ZDS_DIR)/bin/ez80asm.exe"
ASFLAGS  := -define:_EZ80ACCLAIM!=1 \
            -include:"$(INCLUDES)" \
            -list -name -quiet -NOsdiopt -warn -debug -NOigcase -cpu:eZ80F91
```

### 8.2 TRSDOS `.CMD` Load Module Generation
TRSDOS / LS-DOS 6.3 executable command files (`.CMD`) utilize a structured record format:
* **Record Type 01 (Load Block Header):** Specifies load address in RAM and block byte count.
* **Record Type 02 (Data Block):** Program instructions and data payload.
* **Record Type 06 (Entry Point Record):** Specifies program execution start address (e.g. `2600h`).

### 8.3 Automated Build Script & Packaging Pipeline
We provide a dedicated Python packager (`tools/bin_to_cmd.py`) that converts the flat binary output from `ez80asm` into standard TRSDOS `.CMD` load files, enabling a clean `make` workflow:
```makefile
%.cmd: %.s $(INCLUDES)
	$(AS) $(ASFLAGS) $< -out:$*.obj
	$(LD) ... -out:$*.bin
	python3 tools/bin_to_cmd.py --load 2600 --entry 2600 $*.bin $@
```

---

## 9. Technical Challenges & Risk Mitigations

### 9.1 Lack of Hardware Flow Control on Agon UEXT
* **Challenge:** Agon Light 2 UEXT brings out only `PC0` (TXD1) and `PC1` (RXD1) without RTS or CTS handshake lines.
* **Mitigation:**
  * Strict polled operation in software.
  * Transmit routines poll only the `THRE` bit in `UART1_LSR`.
  * Multi-byte data streaming is paced by the protocol's lock-step request-response structure.

### 9.2 UART FIFO Overrun Protection & Interrupt Isolation
* **Challenge:** At 115,200 baud, bytes arrive every 86.8 µs. The eZ80 hardware FIFO holds only 16 bytes.
* **Mitigation:**
  * Zero `UART1_IER` (`xor a; out0 (UART1_IER), a`) so incoming bytes never trigger stray CPU interrupt vectors.
  * Wrap all multi-byte streaming reads in `DI` and `EI` blocks.
  * Flush FIFOs (`out0 (UART1_FCTL), 07h`) prior to every request to purge residual bytes.

### 9.3 Wi-Fi Latency, Timeouts, and Packet Fragmentation
* **Challenge:** Wi-Fi transport across the ESP8266 or Internet TNFS connections introduces latencies of 50–250 ms.
* **Mitigation:** Expand driver timeout countdown counters to 2.5–3.0 seconds; `BufferedStreamReader` in FujiNet handles server-side chunk coalescing.

---

## 10. Phased Implementation Roadmap

```
+-------------------------------------------------------------------------------+
| PHASE 1: Wire Protocol Verification & FujiNet Host Setup                      |
| - Verify fujinet-firmware (BUILD_COCO) serial port interface and BoIP listener.|
| - Test direct hardware serial connection (MOD-USB-RS232) without openstream.   |
| - Test agon-net-utils openstream connection to FujiNet BoIP (for Wi-Fi stream). |
| - Validate zero-transmission silence on connection using test scripts.        |
| - Test image detection with JV1, DiskDISK, and HDV sample files.              |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 2: Storage Driver Integration (driver-FDCDVR.S)                         |
| - Implement DriveWire OP_READ (0x52) and OP_WRITE (0x57) binary framing.      |
| - Implement 16-bit big-endian checksum assembly calculation.                 |
| - Patch and build TRSDOS.bin in trs-os-zds; verify sector read/write.         |
| - Extend driver to route network I/O across Drives :1 through :7.             |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 3: FujiNet Client Library (fujilib.s) & Base Utility                    |
| - Implement core fujilib.s transaction routines (OP_FUJI 0xE2).               |
| - Set up ez80asm Wine build rule and bin_to_cmd packager.                     |
| - Build and test FNSTATUS.CMD; verify AdapterConfig parsing and display.      |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 4: Host & Device Slot Management Utilities                              |
| - Implement FNLISTHOSTS, FNGETHOST, FNSETHOST, and FNMOUNT.                   |
| - Implement FNLISTDEVS, FNGETDEV, FNSETDEV, and FNMOUNTIMG.                   |
| - Validate dynamic TNFS mounting and image switching to Drives :1–:7.         |
+-------------------------------------------------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 5: Minimal Boot Image Packaging & Multi-Volume Validation               |
| - Create minimal boot RAM disk image containing only essential OS + utilities.|
| - Validate dynamic mounting of JV1, DiskDISK, and HDV images on drives :1-:7. |
| - Integrate OP_TIME (0x23) for automatic RTC synchronization.                 |
| - Update driver-PRDVR.S for DriveWire printer spooling.                       |
+-------------------------------------------------------------------------------+
```

---

## 11. Verification & Test Matrix

| Test ID | Objective | Test Procedure | Acceptance Criteria |
| :--- | :--- | :--- | :--- |
| **TC-01** | Server Silence Verification | Connect via `nc` or test script to port 65504; monitor for 10 seconds. | Zero bytes received from server prior to client command. |
| **TC-02a** | Direct Serial Boot Handover | Connect MOD-USB-RS232; execute `OSboot TRSDOS.bin` directly from MOS. | TRSDOS boots to prompt immediately; UART1 communicates over direct serial. |
| **TC-02b** | MOS Wi-Fi Stream Handover | Connect MOD-WIFI-ESP8266; run `openstream <host> 65504` $\to$ `OSboot TRSDOS.bin`. | TRSDOS boots to prompt without UART1 desynchronization or freeze. |
| **TC-03** | DriveWire Checksum Integrity | Execute test script computing 16-bit sums against known 256B test vectors. | Assembly calculation matches expected sum $\sum b_i \pmod{65536}$ exactly. |
| **TC-04** | Sector Read Verification | TRSDOS reads sector 0 from Drive `:1` mounted to test image. | 256 bytes read correctly; status byte 0x00; no CRC errors. |
| **TC-05** | Sector Write Verification | TRSDOS writes known sector pattern; re-reads and compares. | Write verified; server commits data; status byte 0x00. |
| **TC-06** | `FNSTATUS` Verification | Run `FNSTATUS` from TRSDOS prompt. | Accurately prints firmware version, IP, SSID, and MAC. |
| **TC-07** | Host Slot Configuration | Run `FNSETHOST 0 tnfs.fujinet.online` $\to$ `FNLISTHOSTS`. | Slot 0 accurately updated and persisted. |
| **TC-08** | Dynamic Image Mounting | Mount TNFS image via `FNSETDEV` + `FNMOUNTIMG` $\to$ `DIR :1`. | Directory listing accurately displays files on remote virtual disk. |
| **TC-09** | HDV Hard Disk Mounting | Mount 15 MB HDV image to Drive `:4`; run directory listing and file I/O. | Reads and writes sectors beyond 65,536 cleanly via 24-bit LSNs. |
| **TC-10** | Minimal Boot Validation | Boot minimal `TRSDOS.bin` with only base OS and utilities as Drive `:0`. | Boots cleanly in under 1 second; prompt active; Drives `:1`–`:7` mountable. |

---

## 12. Project Reference Matrix & Upstream Sources

This integration synthesizes architecture, codebases, and protocols across five primary repositories:

### 12.1 Primary Repository Directory Paths

| Project / Component | Local Workspace Path | Upstream Repository / Reference | Role in Integration |
| :--- | :--- | :--- | :--- |
| **`agon-net-utils`** | `~/development/git/agon-net-utils` | [rlucente-retro/agon-net-utils](https://github.com/rlucente-retro/agon-net-utils) | Agon Quark MOS utilities (`openstream`, `sendstream`, `closestream`) managing ESP8266 transparent streaming mode (`CIPMODE=1`) for Wi-Fi; not required for direct serial links. |
| **`trs-net-tcp`** | `~/development/git/trs-net-tcp` | [rlucente-retro/trs-net-tcp](https://github.com/rlucente-retro/trs-net-tcp) | Legacy Python TCP/serial disk server (`trs-netd.py`) and protocol evaluation documentation being superseded. |
| **`trs-os-zds`** | `~/development/git/trs-os-zds` | [rlucente-retro/trs-os-zds](https://github.com/rlucente-retro/trs-os-zds) | Core operating system source tree (`TRSDOS_7`), Agon Light 2 patchset, and ZDS II Wine build automation for `TRSDOS.bin`. |
| **`fujinet-firmware`** | `~/development/git/fujinet-firmware` | [FujiNetWIFI/fujinet-firmware](https://github.com/FujiNetWIFI/fujinet-firmware) | Target server framework providing DriveWire (`BUILD_COCO`) block I/O, `OP_FUJI` transactions, and BoIP/serial transports. |
| **`nitros9-main`** | `~/development/git/nitros9-main` | [nitros-9/nitros9](https://github.com/nitros-9/nitros9) | Reference implementations for the 9 FujiNet client utilities targeting DriveWire on NitrOS-9. |
| **`OSboot` Loader** | N/A (Utility) | [sijnstra/agon-projects/OSboot](https://github.com/sijnstra/agon-projects/tree/main/OSboot) | MOS bootstrap loader for launching `TRSDOS.bin` while maintaining UART1 streaming continuity. |

### 12.2 NitrOS-9 FujiNet Reference Source Files

The TRSDOS utility suite (`FN*.CMD`) is modeled directly on the following reference commands written in 6809 assembly code from `nitros9-main`:

1. `~/development/git/nitros9-main/level1/cmds/fnstatus.as` — Query and format adapter status and Wi-Fi connection state.
2. `~/development/git/nitros9-main/level1/cmds/fnlisthosts.as` — Enumerate all 8 FujiNet host slots.
3. `~/development/git/nitros9-main/level1/cmds/fngethost.as` — Display host assignment for an individual slot.
4. `~/development/git/nitros9-main/level1/cmds/fnsethost.as` — Configure hostname/TNFS address for a host slot.
5. `~/development/git/nitros9-main/level1/cmds/fnmount.as` — Mount a host slot connection.
6. `~/development/git/nitros9-main/level1/cmds/fnlistdevs.as` — Enumerate all 8 virtual device (disk) slots.
7. `~/development/git/nitros9-main/level1/cmds/fngetdevfile.as` — Query mounted disk image path for a device slot.
8. `~/development/git/nitros9-main/level1/cmds/fnsetdevfile.as` — Assign disk image path and access mode to a device slot.
9. `~/development/git/nitros9-main/level1/cmds/fnmountimg.as` — Commit/mount disk image for active read/write I/O.
10. `~/development/git/nitros9-main/lib/fuji.as` — Shared DriveWire `OP_FUJI` transaction protocol engine (`NOpen`, `FBReady`, `FBCmd`, `FBErr`, `FBRead`).

