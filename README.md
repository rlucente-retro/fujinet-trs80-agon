# fujinet-trsos

**FujiNet Network Mass Storage and Utility Integration for TRS-OS on Agon Light 2**

This project provides the architecture, driver specifications, and native utility suite to integrate the **[FujiNet](https://fujinet.online)** peripheral framework with **TRS-OS** (TRSDOS 6.3.1 / LS-DOS 6.3 adapted for the Zilog eZ80 on the Olimex Agon Light 2).

It replaces the legacy, custom serial protocol from [`trs-net-tcp`](https://github.com/rlucente-retro/trs-net-tcp) with the standard binary **DriveWire** protocol and **FujiNet (`OP_FUJI`)** transaction architecture, enabling dynamic mounting of local images and remote TNFS shares across multiple virtual drives.

---

## Key Features & Drive Architecture

* **Drive Roles:**
  * **Drive `:0` (Boot Drive):** Dedicated boot system volume embedded in `TRSDOS.bin` and launched via `OSboot.bin`, containing the minimal core OS kernel and FujiNet utilities (`FN*.CMD`).
  * **Drives `:1` through `:7` (Network Mounts):** Dynamically mounted from remote TNFS servers or local SD card images via FujiNet commands (`FNSETDEV`, `FNMOUNTIMG`).
* **Universal Disk Image Support:** Fully supports **DiskDISK** (+256B header), **JV1** (flat 256B sectors), and high-capacity **HDV** hard disk files (5 MB to 32 MB) through automated header detection and 24-bit LSN addressing.
* **Lean Boot Architecture:** `TRSDOS.bin` embeds only a minimal bootable OS image (`SYS0/SYS`..`SYS6/SYS`) and the FujiNet command suite, booting instantly into `TRSDOS Ready`.
* **ZDS II / Wine Build Pipeline:** Leverages Zilog's official `ez80asm.exe` assembler via Apple's Wine layer (matching `trs-os-zds`), generating authentic TRSDOS `.CMD` load files.
* **Dual Physical Transports:**
  * **Direct Serial Link (Olimex MOD-USB-RS232):** Inherent, always-on point-to-point hardware connection; bypasses `openstream` and boots directly from MOS.
  * **Wi-Fi TCP/IP Streaming (Olimex MOD-WIFI-ESP8266):** Uses `openstream` $\to$ `OSboot.bin` $\to$ `TRSDOS.bin` to place the Wi-Fi module in transparent passthrough mode (`CIPMODE=1`).

---

## Documentation

* **Detailed Architectural Plan:** [`FUJINET-TRSOS-INTEGRATION-PLAN.md`](FUJINET-TRSOS-INTEGRATION-PLAN.md) — Comprehensive technical specification covering wire protocols, assembly driver modifications, disk format detection, the 9 native TRSDOS utilities, hardware constraints, and phased implementation milestones.
* **Agon OSboot Bootstrap Loader:** [Shawn Sijnstra's OSboot](https://github.com/sijnstra/agon-projects/tree/main/OSboot) — MOS bootstrap utility for booting `TRSDOS.bin` while preserving UART1 streaming connections.

---

## System Architecture

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

---

## Operating Lifecycle

The system supports two physical transports:

### Option A: Direct Serial Link (e.g., Olimex MOD-USB-RS232)
* **Inherent Hardware Link:** When using the MOD-USB-RS232 module (or compatible RS-232 adapter) connected to a host PC running FujiNet-PC or a hardware FujiNet unit, the physical connection is inherently active upon power-on and hardware reset.
* **Direct Boot via MOS:** No connection handshake or pre-boot streaming utilities (`openstream` / `closestream`) are required. Agon Quark MOS directly launches TRS-OS:
  ```text
  *OSboot -2f TRSDOS.bin
  ```
* **Zero Latency:** TRS-OS initializes UART1 (polled mode) and immediately accesses DriveWire virtual disks and FujiNet utilities over the physical serial line.

### Option B: Wi-Fi TCP/IP Streaming (e.g., Olimex MOD-WIFI-ESP8266)
1. **Pre-Boot Handshake (`openstream`):**
   * Before booting TRS-OS, Quark MOS runs `openstream <fujinet_host> 65504` (from `agon-net-utils`) to connect the ESP8266 coprocessor to FujiNet's BoIP server (`port 65504`) and enter transparent UART-to-TCP passthrough mode (`CIPMODE=1`).
   * `fujinet-firmware` accepts the connection and remains **completely silent**, transmitting zero bytes until TRS-OS sends its first command.
2. **Boot TRS-OS (`OSboot.bin`):**
   * MOS executes `*OSboot -2f TRSDOS.bin`, transferring execution to TRS-OS while preserving the active transparent stream on UART1.
3. **Session Teardown (`closestream`):**
   * If exiting back to MOS, `closestream` sends `+++` with guard times and `AT+CIPCLOSE` to restore AT command mode.

### Active TRS-OS Operation (Both Transports)
* `TRSDOS.bin` initializes the embedded RAM drive as **Drive `:0`** and presents the `TRSDOS Ready` prompt.
* User mounts disk images onto **Drives `:1` through `:7`** using native TRSDOS FujiNet utilities (`FNSTATUS`, `FNMOUNT`, `FNLISTDEVS`, `FNSETDEV`, `FNMOUNTIMG`).
* Disk I/O to Drives `:1`–`:7` occurs via binary DriveWire `OP_READ` (0x52) and `OP_WRITE` (0x57) commands with 16-bit big-endian checksums.

---

## Native TRSDOS FujiNet Utilities

This suite ports the functionality of the reference NitrOS-9 FujiNet utilities ([nitros9 level1/cmds/fn*.as](https://github.com/nitros-9/nitros9/tree/main/level1/cmds)) to native TRSDOS `.CMD` binaries executable directly from the `TRSDOS Ready` prompt.

> [!NOTE]
> **Transport Transparency & TRSDOS Independence:**  
> TRSDOS is **completely unaware** of the physical connection type or whether an intermediate TCP/IP bridge is in use. To TRSDOS, UART1 (`PC0`/`PC1`) is strictly a local 115,200 baud serial interface. When Wi-Fi is used with the MOD-WIFI-ESP8266, Agon Quark MOS handles the AT command connection (`openstream`) before TRSDOS boots; when a direct serial link (MOD-USB-RS232) is used, communication is purely hardware serial. TRSDOS runs no TCP/IP stack.  
> 
> Consequently, all `FN*.CMD` utilities communicate over standard serial `OP_FUJI` (0xE2) transactions to configure the **remote FujiNet peripheral device**:
> - Any network properties reported by `FNSTATUS` (IP address, Wi-Fi SSID, MAC address) belong to the **FujiNet device itself** (its own network interface used to reach TNFS servers or its web config) or the host PC running FujiNet-PC, **not** to the Agon Light 2 or TRSDOS.
> - When running FujiNet-PC over a direct serial cable without Wi-Fi, `FNSTATUS` displays the host PC's IP and indicates `Direct Serial / Wired` (or N/A) for Wi-Fi SSID.
> - Remote host and mounting utilities (`FNLISTHOSTS`, `FNMOUNT`, `FNSETDEV`, `FNMOUNTIMG`) instruct the **FujiNet server** to fetch disk images over its own network or local storage and map them to virtual DriveWire drives `:1` through `:7`.

| Utility | Syntax | Purpose |
| :--- | :--- | :--- |
| `FNSTATUS` | `FNSTATUS` | Queries the FujiNet peripheral's adapter configuration, displaying its firmware version, IP address, MAC, and external Wi-Fi/network connection state. |
| `FNLISTHOSTS` | `FNLISTHOSTS` | Lists all 8 configured host slots (0–7) stored on the FujiNet device (remote TNFS servers, internet URLs, or local SD card). |
| `FNGETHOST` | `FNGETHOST <slot>` | Displays the hostname/URL assigned to a specific FujiNet host slot. |
| `FNSETHOST` | `FNSETHOST <slot> <hostname>` | Configures or updates the remote hostname or TNFS URL for a given FujiNet host slot. |
| `FNMOUNT` | `FNMOUNT <slot>` | Commands the FujiNet device to mount a host slot (connect to remote TNFS server or local storage). |
| `FNLISTDEVS` | `FNLISTDEVS` | Queries FujiNet's 8 virtual device slots, listing mounted image filenames, access modes (RO/RW), and drive mappings. |
| `FNGETDEV` | `FNGETDEV <slot>` | Displays the full pathname of the disk image currently mounted in a FujiNet device slot. |
| `FNSETDEV` | `FNSETDEV <dev_slot> <host_slot> <mode> <path>` | Directs FujiNet to assign a specific disk image on a host slot to a device slot. |
| `FNMOUNTIMG` | `FNMOUNTIMG <dev_slot> [mode] [drive]` | Mounts the disk image in the specified FujiNet device slot to target TRSDOS Drive `:1` through `:7`. |

---

## Related Repositories & Reference Codebases

* **`agon-net-utils`** ([rlucente-retro/agon-net-utils](https://github.com/rlucente-retro/agon-net-utils)): Agon Quark MOS network utilities (`openstream`, `sendstream`, `closestream`).
* **`trs-net-tcp`** ([rlucente-retro/trs-net-tcp](https://github.com/rlucente-retro/trs-net-tcp)): Previous Python-based TCP/serial disk daemon for TRS-OS.
* **`trs-os-zds`** ([rlucente-retro/trs-os-zds](https://github.com/rlucente-retro/trs-os-zds)): TRS-OS build automation and Agon Light 2 patchset.
* **`fujinet-firmware`** ([FujiNetWIFI/fujinet-firmware](https://github.com/FujiNetWIFI/fujinet-firmware)): Core multi-platform FujiNet firmware.
* **`nitros9`** ([nitros-9/nitros9](https://github.com/nitros-9/nitros9)): Reference 6809 assembly FujiNet utilities for NitrOS-9 (`level1/cmds/fn*.as`).
* **`OSboot`** ([sijnstra/agon-projects/OSboot](https://github.com/sijnstra/agon-projects/tree/main/OSboot)): MOS bootstrap loader for launching `TRSDOS.bin`.
