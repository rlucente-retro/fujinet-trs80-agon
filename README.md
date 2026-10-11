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
* **Preserved Agon Bootchain:** Keeps the proven `openstream` $\to$ `OSboot.bin` $\to$ `TRSDOS.bin` boot sequence using the Olimex MOD-WIFI-ESP8266 module on UART1.

---

## Documentation

* **Detailed Architectural Plan:** [`FUJINET-TRSOS-INTEGRATION-PLAN.md`](FUJINET-TRSOS-INTEGRATION-PLAN.md) — Comprehensive technical specification covering wire protocols, assembly driver modifications, disk format detection, the 9 native TRSDOS utilities, hardware constraints, and phased implementation milestones.
* **Agon OSboot Bootstrap Loader:** [Shawn Sijnstra's OSboot](https://github.com/sijnstra/agon-projects/tree/main/OSboot) — MOS bootstrap utility for booting `TRSDOS.bin` while preserving UART1 streaming connections.

---

## System Architecture

```
+------------------------------------+                         +------------------------------------+
|        Agon Light 2 (Target)       |                         |          Host PC / Server          |
|                                    |                         |                                    |
|  +------------------------------+  |                         |  +------------------------------+  |
|  |           TRS-OS             |  |                         |  |       fujinet-firmware       |  |
|  |     (TRSDOS / LS-DOS 6.3)    |  |                         |  |     (BUILD_COCO / DriveWire) |  |
|  |                              |  |                         |  +---------------+--------------+  |
|  |  +------------------------+  |  |                         |                  |                 |
|  |  | Drive :0 (Boot RAM)    |  |  |                         |                  |                 |
|  |  | SYS files + FN*.CMD    |  |  |                         |                  |                 |
|  |  +------------------------+  |  |                         |                  |                 |
|  |  | Native FN*.CMD Tools   |  |  |   OP_FUJI Transactions  |                  v                 |
|  |  | (Status, Mount, Hosts) |  |  | <=======================>          FujiNet Config Engine     |
|  |  +------------------------+  |  |       (Opcode 0xE2)     |          (8 Host & Device Slots)   |
|  |  | driver-FDCDVR.S        |  |  |                         |                  |                 |
|  |  | (Drives :1 through :7) |  |  |   DriveWire OP_READ/WR  |                  v                 |
|  |  +------------------------+  |  | <=======================>          Universal Media Handler   |
|  +--------------+---------------+  |      (0x52 / 0x57)      |          (JV1 / DiskDISK / HDV)    |
|                 | UART1            |                         +------------------+-----------------+
|                 | (115200 8-N-1)   |                                            ^
|  +--------------v---------------+  |                                            |
|  |  Olimex MOD-WIFI-ESP8266     |  |         TCP/IP (Port 65504)                |
|  |  (Transparent CIPMODE=1)     | <=============================================+
|  +------------------------------+  |   (openstream via Agon MOS bootloader)
+------------------------------------+
```

---

## Operating Lifecycle

1. **Bootstrap via Agon MOS (`openstream`):**
   * Before booting TRS-OS, Agon Quark MOS uses `openstream` (from `agon-net-utils`) to connect the Olimex MOD-WIFI-ESP8266 coprocessor to the FujiNet server (`port 65504`) and enter transparent UART-to-TCP passthrough mode (`CIPMODE=1`).
   * `fujinet-firmware` accepts the connection and remains **completely silent**, transmitting zero bytes until TRS-OS sends its first command.
2. **Boot TRS-OS (`OSboot.bin`):**
   * MOS launches `OSboot.bin TRSDOS.bin`, handing CPU control to TRS-OS while preserving the active transparent stream on UART1.
   * `TRSDOS.bin` initializes the embedded RAM drive as **Drive `:0`** and presents the `TRSDOS Ready` prompt.
3. **TRS-OS Operation:**
   * User mounts disk images onto **Drives `:1` through `:7`** using the native TRSDOS FujiNet utilities (`FNSTATUS`, `FNMOUNT`, `FNLISTDEVS`, `FNSETDEV`, `FNMOUNTIMG`).
   * Disk I/O to Drives `:1`–`:7` occurs via binary DriveWire `OP_READ` (0x52) and `OP_WRITE` (0x57) commands with 16-bit big-endian checksums.

---

## Native TRSDOS FujiNet Utilities

This suite ports the functionality of the reference NitrOS-9 FujiNet utilities (`~/development/git/nitros9-main/level1/cmds/fn*.as`) to native TRSDOS `.CMD` binaries:

| Utility | Syntax | Purpose |
| :--- | :--- | :--- |
| `FNSTATUS` | `FNSTATUS` | Displays FujiNet firmware version, Wi-Fi SSID, IP, MAC, gateway, and connection status. |
| `FNLISTHOSTS` | `FNLISTHOSTS` | Lists all 8 configured host slots (0–7) and their remote hostnames or TNFS addresses. |
| `FNGETHOST` | `FNGETHOST <slot>` | Displays the hostname assigned to a specific host slot. |
| `FNSETHOST` | `FNSETHOST <slot> <hostname>` | Sets or updates the remote hostname/URL for a given host slot. |
| `FNMOUNT` | `FNMOUNT <slot>` | Mounts a host slot (connects to remote TNFS server or local storage). |
| `FNLISTDEVS` | `FNLISTDEVS` | Lists all virtual device slots, their mounted filenames, access modes (RO/RW), and mapped drive letters. |
| `FNGETDEV` | `FNGETDEV <slot>` | Displays the full image path currently mounted in a virtual device slot. |
| `FNSETDEV` | `FNSETDEV <dev_slot> <host_slot> <mode> <path>` | Configures a device slot with a specific disk image on a host slot. |
| `FNMOUNTIMG` | `FNMOUNTIMG <dev_slot> [mode] [drive]` | Mounts the disk image in the specified device slot to target TRSDOS Drive `:1` through `:7`. |

---

## Related Repositories & Reference Codebases

* **`agon-net-utils`** (`~/development/git/agon-net-utils`): Agon Quark MOS network utilities (`openstream`, `sendstream`, `closestream`).
* **`trs-net-tcp`** (`~/development/git/trs-net-tcp`): Previous Python-based TCP/serial disk daemon for TRS-OS.
* **`trs-os-zds`** (`~/development/git/trs-os-zds`): TRS-OS build automation and Agon Light 2 patchset.
* **`fujinet-firmware`** (`~/development/git/fujinet-firmware`): Core multi-platform FujiNet firmware.
* **`nitros9-main`** (`~/development/git/nitros9-main`): Reference 6809 assembly FujiNet utilities for NitrOS-9 (`level1/cmds/fn*.as`).
* **`OSboot`** ([sijnstra/agon-projects/OSboot](https://github.com/sijnstra/agon-projects/tree/main/OSboot)): MOS bootstrap loader for launching `TRSDOS.bin`.
