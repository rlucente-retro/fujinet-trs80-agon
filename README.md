# fujinet-trsos

**FujiNet Network Mass Storage and Utility Integration for TRS-OS on Agon Light 2**

This project provides the architecture, driver specifications, and native utility suite to integrate the **[FujiNet](https://fujinet.online)** peripheral framework with **TRS-OS** (TRSDOS 6.3.1 / LS-DOS 6.3 adapted for the Zilog eZ80 on the Olimex Agon Light 2).

It replaces the legacy, custom serial protocol from [`trs-net-tcp`](https://github.com/rlucente-retro/trs-net-tcp) with the standard binary **DriveWire 3/4** protocol and **FujiNet (`OP_FUJI`)** transaction architecture, enabling dynamic mounting of local images and remote TNFS shares across virtual disk drives.

---

## Key Features & Drive Architecture

* **Staged Drive Roles & Storage Architecture:**
  * **Drive `:0` (Minimal Bootstrap RAM Drive):** Dedicated minimal in-memory system volume (`0x010200` in RAM) containing the essential OS kernel overlays (`SYS0/SYS`..`SYS6/SYS`) and FujiNet command tools (`FN*.CMD`), booting in under 1 second to bootstrap TRS-OS and mount network drives.
  * **Drives `:1` through `:4` (Stage 1 Stock Firmware Baseline):** Stock CoCo `fujinet-firmware` limits DriveWire to 4 slots (`MAX_DWDISK_DEVICES = 4`). In Stage 1, Drives `:1` through `:4` map directly to FujiNet Device Slots 0 through 3 (corresponding to Drives 1 through 4 in the FujiNet Web UI via `slot = LDRV - 1`). Drives `:5`–`:7` remain reserved.
  * **Drives `:1` through `:7` (Stage 2 Firmware Expansion):** TRSDOS/LS-DOS 6.3 natively supports 8 drives (`:0` through `:7`). Recompiling `fujinet-firmware` with `MAX_DWDISK_DEVICES 8` in Stage 2 unlocks all seven network drives (`:1` through `:7`, mapping to Slots 0–6) simultaneously.
* **Universal Disk Image Support:**
  * **JV1:** Flat 256-byte sector layout ($LSN \times 256$), with geometry derived dynamically (e.g. Directory Cylinder 17).
  * **DiskDISK:** 256-byte header containing authentic DCT metadata; the driver accounts for the 1-sector (+256B) header offset via dynamic format detection.
  * **HDV Hard Disks:** High-capacity images. While DriveWire wire frames support 24-bit LSNs (4 GB), the standard TRS-OS floppy driver architecture (`driver-FDCDVR.S`) addresses up to 4 MiB ($256 \text{ cylinders} \times 64 \text{ sectors/cyl}$); volumes exceeding 4 MiB require a dedicated hard disk driver.
* **Staged Integrity & Safety-First Deployment:**
  * **Stage 1 (Verified Read-Only Network Drives):** Initial deployment enforces write-protection (`WP`) at both the guest driver and server mount levels across Drives `:1`–`:4`. *Workflow Note:* Because Drive `:0` is minimal RAM and network drives are read-only, user file creation/saving is deferred until Stage 2.
  * **Stage 2 (Server Firmware Patches & Writable Storage):** Auditing of the upstream COCO `fujinet-firmware` revealed that write checksum validation is currently disabled, image bounds checks are missing in `mediaTypeDSK.cpp`, and flush errors are not propagated. Full writable storage and 7-drive expansion are enabled after applying server-side integrity patches and passing negative regression suites.
* **ZDS II / Wine Build Pipeline & Authentic CMD Packaging:**
  * Compiles using Zilog's official `ez80asm.exe` under Wine (matching `trs-os-zds`).
  * Emits authentic TRSDOS `.CMD` binaries with proper load blocks (Record Type 01: `01 <len> <addr_lo> <addr_hi> <payload>`) and transfer addresses (Record Type 02: `02 02 <entry_lo> <entry_hi>`).
* **Dual Physical Transports:**
  * **Direct Serial Link (Olimex MOD-USB-RS232):** Inherent, always-on point-to-point hardware connection on UEXT (`PC0`/`PC1`); bypasses `openstream` and boots directly from MOS.
  * **Wi-Fi TCP/IP Streaming (Olimex MOD-WIFI-ESP8266):** Uses `openstream` $\to$ `OSboot.bin` $\to$ `TRSDOS.bin` to place the Wi-Fi module in transparent passthrough mode (`CIPMODE=1`).

---

## Documentation

* **Detailed Architectural Plan:** [`FUJINET-TRSOS-INTEGRATION-PLAN.md`](FUJINET-TRSOS-INTEGRATION-PLAN.md) — Comprehensive technical specification covering wire protocols, assembly driver modifications, disk format detection, the 9 native TRSDOS utilities, hardware constraints, and phased implementation milestones.
* **Boot Chain & OS Architecture Guide:** [`TRSOS-BOOT-ARCHITECTURE.md`](TRSOS-BOOT-ARCHITECTURE.md) — Comprehensive explanation of the layered execution model: Quark MOS $\to$ OSboot $\to$ TRS-OS (eZ80 HAL/BIOS) $\to$ TRSDOS/LS-DOS 6.3.1, including driver preservation and hardware emulation.
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
| +-----------------|---------------------------------------|-----------------+ |
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

---

## Operating Lifecycle

The system supports two physical transports:

### Option A: Direct Serial Link (e.g., Olimex MOD-USB-RS232)
* **Inherent Hardware Link:** When using the MOD-USB-RS232 module (or compatible RS-232 adapter) connected to a host PC running FujiNet-PC or a hardware FujiNet unit, the physical connection is inherently active upon power-on and hardware reset.
* **Direct Boot via MOS:** No connection handshake or pre-boot streaming utilities (`openstream` / `closestream`) are required. Agon Quark MOS directly launches TRS-OS:
  ```text
  *OSboot -2f TRSDOS.bin
  ```
* **Explicit UART Reinitialization:** TRS-OS explicitly configures Port C Mode 7, BRG divisor 10 (115,200 baud), LCTL `0x03` (8-N-1), zeroes `UART1_IER` for polled mode, and flushes FIFOs (`UART1_FCTL = 0x07`), ensuring clean handover from MOS.

### Option B: Wi-Fi TCP/IP Streaming (e.g., Olimex MOD-WIFI-ESP8266)
1. **Pre-Boot Handshake (`openstream`):**
   * Before booting TRS-OS, Quark MOS runs `openstream <fujinet_host> 65504` (from `agon-net-utils`) to connect the ESP8266 coprocessor to FujiNet's BoIP server (`port 65504`) and enter transparent UART-to-TCP passthrough mode (`CIPMODE=1`).
   * `fujinet-firmware` accepts the connection and remains **completely silent**, transmitting zero bytes until TRS-OS sends its first command.
2. **Boot TRS-OS (`OSboot.bin`):**
   * MOS executes `*OSboot -2f TRSDOS.bin`, transferring execution to TRS-OS while preserving the active transparent stream on UART1.
3. **Session Teardown (`closestream`):**
   * If exiting back to MOS, `closestream` sends `+++` with guard times (>20 ms) and `AT+CIPCLOSE` to restore AT command mode.

### Active TRS-OS Operation (Both Transports)
* `TRSDOS.bin` boots with Drive `:0` in RAM (embedding bootstrap Volume 0 at `0x010200`), presenting the `TRSDOS Ready` prompt to bootstrap and mount network storage.
* **Stage 1 Drive Mapping:** Drives `:1` through `:4` are configured as read-only network storage drives mapped 1:1 to FujiNet Device Slots 0 through 3 (matching FujiNet Web UI Drives 1 through 4 via `slot = LDRV - 1`). Non-persistent RAM drive `:1` and legacy `:6` special-casing are completely eliminated. Drives `:5`–`:7` are unlocked in Stage 2.
* Disk read I/O occurs via binary DriveWire `OP_READEX` (`0xD2`) commands:
  1. Client sends `0xD2 <slot> <LSN_24>` (5 bytes).
  2. Server returns 256 bytes raw sector data.
  3. Client calculates and transmits 16-bit big-endian checksum (2 bytes).
  4. Server returns 1-byte status (`0x00` = Success, `0xF3` = Checksum error).
* Writes are rejected with write-protection (`WP`) during Stage 1 until server integrity patches are applied.

---

## Native TRSDOS FujiNet Utilities

This suite ports the functionality of the reference NitrOS-9 FujiNet utilities ([nitros9 level1/cmds/fn*.as](https://github.com/nitros-9/nitros9/tree/main/level1/cmds)) to native TRSDOS `.CMD` binaries executable directly from the `TRSDOS Ready` prompt.

> [!NOTE]
> **Transport Transparency, TNFS Paths & TRSDOS Independence:**  
> TRSDOS is **completely unaware** of the physical connection type or whether an intermediate TCP/IP bridge is in use. To TRSDOS, UART1 (`PC0`/`PC1`) is strictly a local 115,200 baud serial interface. When Wi-Fi is used with the MOD-WIFI-ESP8266, Agon Quark MOS handles the AT command connection (`openstream`) before TRSDOS boots; when a direct serial link (MOD-USB-RS232) is used, communication is purely hardware serial. TRSDOS runs no TCP/IP stack.  
> 
> Consequently, all `FN*.CMD` utilities communicate over standard serial `OP_FUJI` (0xE2) transactions to configure the **remote FujiNet peripheral device**:
> - Any network properties reported by `FNSTATUS` (IP address, Wi-Fi SSID, MAC address) belong to the **FujiNet device itself** (its own network interface used to reach TNFS servers or its web config) or the host PC running FujiNet-PC, **not** to the Agon Light 2 or TRSDOS.
> - When running FujiNet-PC over a direct serial cable without Wi-Fi, `FNSTATUS` displays the host PC's IP and indicates `Direct Serial / Wired` (or N/A) for Wi-Fi SSID.
> - Remote host and mounting utilities (`FNHOSTS`, `FNMOUNT`, `FNSETDEV`, `FNMNTIMG`) instruct the **FujiNet server** to fetch disk images over its own network or local storage and map them to virtual DriveWire device slots.
> - **Case-Sensitivity on TNFS:** Because TRSDOS command line parsing automatically coerces unquoted arguments to UPPERCASE, pathnames on remote case-sensitive TNFS servers must be enclosed in quotes (e.g. `FNSETDEV 0 0 1 "/coco/disks/game.dsk"`).
> - **Dynamic DCT Synchronization:** In addition to issuing `0xF8` to FujiNet, `FNMNTIMG` reads Sector 0 and the directory GAT of the newly attached image to dynamically configure the target drive's DCT (`DCTn$+3`..`DCTn$+9`) and enable its vector (`JP FDCDVR`).

All utilities conform to the LS-DOS 6.x constraint of **$\le 8$-character basenames**:

| Utility | Syntax | Purpose |
| :--- | :--- | :--- |
| `FNSTATUS` | `FNSTATUS` | Queries FujiNet adapter configuration (`0xE8`), displaying firmware version, IP address, MAC, and external network connection state (140-byte record). |
| `FNHOSTS` | `FNHOSTS` | Lists all 8 configured host slots (0–7) stored on the FujiNet device (remote TNFS servers, internet URLs, or local SD card). |
| `FNGETH` | `FNGETH <slot>` | Displays the hostname/URL assigned to a specific FujiNet host slot (0–7). |
| `FNSETH` | `FNSETH <slot> <hostname>` | Configures or updates the remote hostname or TNFS URL for a given FujiNet host slot (wrap URL in quotes if case-sensitive). |
| `FNMOUNT` | `FNMOUNT <slot>` | Commands the FujiNet device to mount a host slot (connect to remote TNFS server or local storage). |
| `FNDEVS` | `FNDEVS` | Queries virtual device slots (152B for 4 slots in Stage 1; 304B for 8 slots in Stage 2), listing mounted filenames, access modes, and drive mappings. |
| `FNGETDEV` | `FNGETDEV <slot>` | Displays the full pathname of the disk image currently mounted in a FujiNet device slot (0–3 in Stage 1; 0–6 in Stage 2). |
| `FNSETDEV` | `FNSETDEV <dev_slot> <host_slot> <mode> <path>` | Assigns disk image on a host slot to a device slot (wrap `<path>` in quotes to preserve case). |
| `FNMNTIMG` | `FNMNTIMG <dev_slot> [mode]` | Mounts the disk image in the specified device slot for active I/O and synchronizes guest DCT geometry. |

---

## Related Repositories & Reference Codebases

* **`agon-net-utils`** ([rlucente-retro/agon-net-utils](https://github.com/rlucente-retro/agon-net-utils)): Agon Quark MOS network utilities (`openstream`, `sendstream`, `closestream`).
* **`trs-net-tcp`** ([rlucente-retro/trs-net-tcp](https://github.com/rlucente-retro/trs-net-tcp)): Previous Python-based TCP/serial disk daemon for TRS-OS.
* **`trs-os-zds`** ([rlucente-retro/trs-os-zds](https://github.com/rlucente-retro/trs-os-zds)): TRS-OS build automation and Agon Light 2 patchset.
* **`fujinet-firmware`** ([FujiNetWIFI/fujinet-firmware](https://github.com/FujiNetWIFI/fujinet-firmware)): Core multi-platform FujiNet firmware (`BUILD_COCO` DriveWire target).
* **`nitros9`** ([nitros-9/nitros9](https://github.com/nitros-9/nitros9)): Reference 6809 assembly FujiNet utilities for NitrOS-9 ([`level1/cmds/fn*.as`](https://github.com/nitros-9/nitros9/tree/main/level1/cmds)).
* **`OSboot`** ([sijnstra/agon-projects/OSboot](https://github.com/sijnstra/agon-projects/tree/main/OSboot)): MOS bootstrap loader for launching `TRSDOS.bin`.
