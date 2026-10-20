# TRS-OS & LS-DOS Boot Architecture on Agon Light 2

**Document Version:** 1.2.0  
**Target Hardware:** Olimex Agon Light 2 (Zilog eZ80F92 @ 18.432 MHz)  
**Operating Systems:** Agon Quark MOS $\to$ FNBOOT / OSboot $\to$ TRS-OS (eZ80 HAL/BIOS) $\to$ TRSDOS 6.3.1 / LS-DOS 6.3.1  

---

## 1. Executive Summary & Architectural Distinctions

A common source of confusion in this environment is the terminology surrounding **FNBOOT / OSboot**, **TRS-OS**, and **TRSDOS / LS-DOS**. These are three separate, cooperating software layers:

```
+-------------------------------------------------------------------------------+
| 1. FNBOOT / OSboot (Agon Quark MOS Environment)                               |
|    - Quark MOS visual launcher & pre-boot manager (eZ80 Z80 mode).            |
|    - Configures VDP video/font, manages FujiNet slots & local SD images.      |
|    - Stages lean 64 KB TRSDOS.bin into SRAM, remaps memory, sets platform tag.|
+-------------------------------------------------------------------------------+
                                        |
                                        v Jumps to 0x000000 (__init)
+-------------------------------------------------------------------------------+
| 2. TRS-OS (Daniel Paul Martin / FujiNet Port)                                 |
|    - eZ80 Hardware Abstraction Layer (HAL), BIOS, and Initial Program Loader. |
|    - Handles CPU registers, eZ80 Timers, UART0 (VDP), UART1 (UEXT), SPI0 (SD).|
|    - Emulates Model 4 hardware (video RAM, keyboard matrix, floppy controller)|
|    - Routes Drive :0..:7 to DriveWire (driver-DWDVR) or Local SD (driver-SDDVR|
+-------------------------------------------------------------------------------+
                                        |
                                        v Hands off to DOSINIT -> zCMNDI
+-------------------------------------------------------------------------------+
| 3. TRSDOS / LS-DOS 6.3.1 (Logical Systems Inc. / Tandy)                       |
|    - Authentic 1980s TRS-80 Model 4 disk operating system.                    |
|    - Runs in Z80 compatibility mode (ADL = 0) with a standard 64 KB map.      |
|    - 100% authentic LSI code for SVCs, SYS0..SYS6 overlays, FMS, and CLI.    |
|    - Unaware of eZ80/Agon hardware; issues standard Model 4 sector I/O.       |
+-------------------------------------------------------------------------------+
```

### The Three Entities Defined:
1. **`FNBOOT` / `OSboot`**: Agon Quark MOS bootstrap utilities. `FNBOOT` provides an interactive pre-boot visual browser to inspect FujiNet status, navigate remote TNFS archives, select local Agon SD images, configure drives `:0`–`:7`, program the ESP32 VDP with TRS-80 font/colors, load the lean `TRSDOS.bin` image, and transfer control to the OS. `OSboot` provides a fast direct-boot path.
2. **`TRS-OS` (`TRSDOS_7`)**: Daniel Paul Martin's port and hardware abstraction layer (HAL) for the Zilog eZ80 processor. It provides the BIOS, CPU initializers, video terminal translation, interrupt tables, 60 Hz timer tasks, and virtual block storage drivers (`driver-FDCDVR.S`) that route sector requests across DriveWire (`driver-DWDVR.S`) and local SD (`driver-SDDVR.S`).
3. **`TRSDOS / LS-DOS 6.3.1`**: The authentic, historical disk operating system originally created by Logical Systems Inc. (LSI) as LS-DOS 6.3 and licensed by Tandy Corporation as TRSDOS 6.3.1 for the TRS-80 Model 4. It contains the system overlays (`SYS0/SYS` through `SYS6/SYS`), supervisor calls (SVCs), file management routines (`SYS4`), and the command line interpreter (`SYS6` `zCMNDI`).

---

## 2. The Complete Boot Chain

The transition from Quark MOS to the `TRSDOS Ready` prompt follows a four-step lifecycle:

```
[ Agon Quark MOS ]
       |
       |  *FNBOOT (Interactive UI)  OR  *OSboot -2f TRSDOS.bin (Direct Boot)
       v
+-------------------------------------------------------------------------------+
| STEP 1: FNBOOT / OSboot (Quark MOS Environment)                               |
| 1. Queries FujiNet via OP_FUJI (0xE2); enumerates TNFS & local SD .dsk files. |
| 2. Mounts selected images into FujiNet Device Slots (or configures SD LBAs).  |
| 3. Sets VDP text color (-2 green) and TRS-80 8x10 font (-f).                  |
| 4. Reads lean 64 KB TRSDOS.bin from microSD into staging SRAM.                |
| 5. Executes on-chip SRAM trampoline to remap SRAM down to 0x000000.           |
| 6. Writes platform tag '3' to UART0 Scratchpad Register (UART0_SPR = 3).      |
| 7. Jumps to entry point at 0x000000 (__init).                                 |
+-------------------------------------------------------------------------------+
       |
       v
+-------------------------------------------------------------------------------+
| STEP 2: TRS-OS CPU & HAL Initialization (SYS0_IPL/ipl-eZ80_CPU.S)             |
| 1. Disables interrupts (DI), sets Interrupt Mode 2 (IM 2).                    |
| 2. Sets up 24-bit stack pointer at 0x0100FF, sets 16-bit SP to STACK$.        |
| 3. Reads UART0_SPR: sees tag '3' (Agon) -> branches to NOINITIAL (warm boot). |
| 4. Skips destructive cold-boot peripheral resets.                             |
| 5. finish_init: Explicitly configures Port C Mode 7 for UART1 (PC0/PC1),      |
|    sets BRG divisor to 10 (115,200 baud), sets 8-N-1 (LCTL=03h), zeroes IER  |
|    (polled mode), and enables/flushes hardware FIFOs (FCTL=07h).              |
| 6. Fills video RAM with spaces, loads welcome banner, jumps to STARTBIOS.     |
+-------------------------------------------------------------------------------+
       |
       v
+-------------------------------------------------------------------------------+
| STEP 3: TRS-OS BIOS & Network Storage Mounting (SYS0_IPL/ipl-BIOS.s)          |
| 1. Sets up 60 Hz real-time clock interrupts on eZ80 Timer 1 (TMR1).           |
| 2. Initializes DCT0$ with JP FDCDVR (pointing to DriveWire Slot 0 or Local SD)|
| 3. Issues OP_READEX (0xD2) to read Sector 0 from FujiNet Slot 0 over UART1.   |
| 4. Checks container format (DiskDISK +256B offset vs JV1 flat).               |
| 5. Reads Directory GAT to derive cylinders, sides, and sectors/track.         |
| 6. Populates DCT0$ geometry; clears keyboard buffer and jumps to DOSINIT.     |
+-------------------------------------------------------------------------------+
       |
       v
+-------------------------------------------------------------------------------+
| STEP 4: Authentic TRSDOS / LS-DOS 6.3.1 Initialization (SYS0_IPL/ipl-DOS.S)   |
| 1. Inspects AUTO command buffer (INBUF$) and SYSGEN on Network Drive :0.      |
| 2. Sets terminal driver parameters and starts background task timer (EI).     |
| 3. Executes system initialization chain (zICNFG).                             |
| 4. Loads system overlays (SYS0/SYS through SYS6/SYS) from Network Drive :0.   |
| 5. Transfers control to command interpreter (zCMNDI in SYS6/SYS).             |
| 6. Displays the authentic "TRSDOS Ready" prompt.                              |
+-------------------------------------------------------------------------------+
```

---

## 3. Detailed Step-by-Step Execution

### Step 1: `FNBOOT` / `OSboot` Execution in Quark MOS
* **Interactive Pre-Boot Management (`FNBOOT`):**
  * `FNBOOT` is a native Quark MOS application executing in eZ80 24-bit mode.
  * It communicates with the FujiNet peripheral over UART1 via `OP_FUJI` (`0xE2`), querying adapter status (`0xE8`) and host slots (`0xF4`).
  * It presents an interactive VDP visual browser allowing the user to:
    1. Browse remote directories on TNFS servers or the FujiNet on-board SD card.
    2. Browse local `.dsk` files on the Agon Light 2's on-board microSD card.
    3. Assign disk images to TRSDOS Logical Drives `:0` through `:7`.
  * For FujiNet assignments, `FNBOOT` sends `SetDevFile` (`0xE2`) and `MountImg` (`0xF8`).
  * For Local SD assignments, `FNBOOT` determines the physical LBA and writes it to the driver mailbox at `0x000F00`.
* **Lean Binary Sizing & SRAM Preservation:**
  * In the legacy RAM-disk design, `TRSDOS.bin` was a **491,520-byte (480 KiB)** monolithic binary embedding a 425 KB RAM disk image at `0x010200`, consuming almost the entire 512 KiB SRAM.
  * In the **Pure FujiNet Client Architecture**, the embedded RAM image is completely eliminated. `TRSDOS.bin` shrinks to a lean **66,048-byte (~64 KiB)** kernel and HAL binary (`0x000000`–`0x00FFFF`).
  * **~448 KiB of external SRAM remains completely free** for sector read caches, user workspaces, or extended memory banks!
* **Hardware SRAM Remapping:**
  * In Quark MOS, Flash ROM is mapped at `0x000000`–`0x03FFFF` and external SRAM is at `0x040000`–`0x0BFFFF`.
  * `FNBOOT` loads `TRSDOS.bin` into staging SRAM, executes a trampoline from on-chip SRAM (`0xFFE000`), reprograms Chip Selects (`CS0`, `CS1`, `RAM_CTL`) to remap SRAM down to `0x000000`, copies the 64 KB kernel to base zero, sets `UART0_SPR = 3`, and jumps to `0x000000` (`__init`).

### Step 2: `TRS-OS` CPU & Platform Setup (`ipl-eZ80_CPU.S`)
* **Entry (`__init`):** Execution begins at `0x000000` with interrupts disabled (`DI`) in Z80 compatibility mode (`ADL = 0`).
* **Platform Signature Check (`tst_initial`):** Reads `UART0_SPR`. Detecting `3` (Agon Light) branches immediately to **`NOINITIAL`** (warm boot), preserving MOS memory configurations and active VDP states.
* **Explicit UART1 Hardware Reinitialization (`finish_init`):**
  * When `openstream` exited in MOS, MOS zeroed UART1 registers. `TRS-OS` explicitly re-arms the hardware:
    1. Port C Mode 7: Configures `PC0` (TXD1) and `PC1` (RXD1).
    2. Baud Rate: Sets BRG divisor to `10` ($18,432,000 / (16 \times 115,200)$), giving an exact 115,200 baud.
    3. Line Framing: Sets `UART1_LCTL = 0x03` (8-N-1).
    4. Polled Mode: Zeroes `UART1_IER = 0x00` (prevents unhandled interrupt crashes).
    5. FIFOs: Sets `UART1_FCTL = 0x07` (enables and flushes RX/TX FIFOs).
  * Direct serial links (MOD-USB-RS232) and Wi-Fi streaming links (MOD-WIFI-ESP8266) resume communication immediately without losing state.

### Step 3: `TRS-OS` BIOS & Storage Mounting (`ipl-BIOS.s`)
* **Real-Time Clock Interrupts:** eZ80 Timer 1 (`TMR1`) is programmed to generate 60 Hz periodic interrupts matching the TRS-80 Model 4 heartbeat clock.
* **Direct Network Mounting of Drive `:0`:**
  * Legacy RAM slice routines (`Slice 0`, `Slice 1`, `ramdrv$`) are neutralized.
  * `DCT0$` is initialized with `JP FDCDVR` (`0xC3`) pointing to the block driver.
  * `ipl-BIOS.s` issues a read of Track 0, Sector 0 via `FDCDVR` over UART1 (`OP_READEX` `0xD2`, Slot 0, LSN 0).
  * **Dynamic Geometry Derivation:**
    1. Checks Sector 0 for `'DiskDISK'` container header. If found, sets `dw_offset[0] = 1`, extracts DCT parameters from offset `0xF9`, and reads LSN 1.
    2. If flat JV1, sets `dw_offset[0] = 0`, reads Directory Cylinder Sector 0 (GAT), and extracts cylinders, sides, and sectors per track.
    3. Populates `DCT0$+3` through `DCT0$+9` with authentic geometry.
  * Hands off execution to `DOSINIT`.

### Step 4: Authentic `TRSDOS / LS-DOS 6.3.1` Initialization (`ipl-DOS.S`)
* **Entry (`DOSINIT`):** Inspects Track 0, Sector 2 of Drive `:0` over DriveWire to check for `AUTO` startup commands and `SYSGEN` settings.
* **Overlay Loading (`zICNFG`):** Loads the standard LS-DOS system overlays (`SYS0/SYS` through `SYS6/SYS`) directly from Drive `:0` across the network.
* **Command Interpreter Handover (`zCMNDI`):** Sends cursor home/clear escapes to the VDP console and jumps to `zCMNDI` in `SYS6/SYS`.
* Displays the authentic startup banner:
  ```text
  TRSDOS Ready
  ```

---

## 4. Driver Architecture: Preserved vs. Emulated/Intercepted

| Subsystem / Driver | Real Model 4 Hardware | Agon Light 2 Implementation under `TRS-OS` | Status in System |
| :--- | :--- | :--- | :--- |
| **Video Display (`*DO`)** | Memory-mapped video at `0xF800`–`0xFFFF` (80×24 / 64×16). | Intercepted in `videoRAM.s`; characters converted to terminal stream over UART0 to ESP32 VDP. | **Emulated / Intercepted** via UART0. |
| **Keyboard Input (`*KI`)** | Memory-mapped matrix scan ports (`0x3801`–`0x3880`). | Intercepted in `SVC09-keyin.s`; reads ASCII keys received from VDP over UART0. | **Emulated / Intercepted** via UART0. |
| **Floppy Disk Controller** | WD1773 / WD1793 FDC I/O ports (`0xF0`–`0xF3`). | Intercepted in `driver-FDCDVR.S`: Routes to DriveWire (`driver-DWDVR.S` via UART1) or Local SD (`driver-SDDVR.S` via SPI0). | **Emulated / Intercepted** (DriveWire Bus + SPI0). |
| **Real-Time Clock** | Heartbeat interrupt (30/60 Hz). | eZ80 Timer 1 (`TMR1`) programmed to fire 60 Hz interrupts; synchronized via `OP_TIME` (`0x23`). | **Hardware Adapted** to eZ80 timer. |
| **Line Printer (`*PR`)** | Centronics parallel printer port at port `0xF8`. | Intercepted in `driver-PRDVR.S`; routed to UART1 `OP_PRINT` (`0x50`) to FujiNet spooler. | **Emulated / Intercepted** via UART1. |
| **System Calls (`SVC`)** | `RST 28h` vector through Page 1 table. | 100% authentic Roy Soltoff / Logical Systems LS-DOS 6.3.1 SVC dispatch table. | **100% Preserved Authentic LS-DOS Code.** |
| **File System Manager** | `SYS4/SYS` (Directory parsing, FCBs, file extents, GAT/HIT). | Exact, original binary code from LS-DOS 6.3.1 executed directly by eZ80 CPU. | **100% Preserved Authentic LS-DOS Code.** |
| **Command Interpreter** | `SYS6/SYS` (Command line parser, batch JCL, utility execution). | Exact, original binary code from LS-DOS 6.3.1 executed directly by eZ80 CPU. | **100% Preserved Authentic LS-DOS Code.** |

---

## 5. How TRSDOS/LS-DOS Runs on the eZ80 Agon Light

The ability of a 1983 operating system to execute seamlessly on a modern eZ80 microcontroller is made possible by several architectural factors:

1. **eZ80 Z80-Compatibility Mode (`ADL = 0`):**
   * The Zilog eZ80 processor powers up with full backward compatibility with the Z80 instruction set.
   * In Z80 mode (`ADL = 0`), the CPU uses 16-bit addresses and 8-bit/16-bit registers identical to an original Z80.
   * Instructions execute with authentic Z80 semantics, but at roughly 3× the throughput per clock cycle due to pipelining. At 18.432 MHz, the Agon Light behaves like an effective **~54 MHz TRS-80 Model 4**.

2. **Clean Operating System Abstraction (The Power of LS-DOS):**
   * Unlike systems where programs poked directly into floppy controller chips, LS-DOS was built with a clean supervisor architecture:
     * File I/O is accessed through `@OPEN`, `@READ`, `@WRITE`, `@CLOSE`.
     * Console I/O is accessed through `@KEYIN` and `@DSPLY`.
     * Disk drivers are accessed through standardized Device Control Tables (`DCT`).
   * Because `TRS-OS` replaces low-level hardware drivers while leaving supervisor tables (`Pages 0–3` and `DCTs`) in their exact historical memory locations, the authentic LS-DOS kernel runs completely unaware that physical floppy drives have been replaced by DriveWire network packets and SPI0 flash blocks.

---

## 6. How FujiNet Fits into this Architecture

FujiNet integrates as a native peripheral server across the layered execution chain:

1. **Pre-Boot Orchestration (`FNBOOT` in MOS):** `FNBOOT` runs under Quark MOS to configure FujiNet host slots, attach remote disk images to device slots, and configure local SD card images before launching the OS.
2. **`TRS-OS` HAL manages UART1:** `TRS-OS` configures the eZ80's internal UART1 registers during `finish_init`, maintaining the streaming connection to FujiNet across the MOS-to-TRSDOS boundary.
3. **`driver-FDCDVR.S` translates disk requests:** When LS-DOS requests a sector from a network drive (`:0` through `:7`), `driver-FDCDVR.S` translates the sector request into a DriveWire `OP_READEX` (`0xD2`) packet and streams it over UART1.
4. **DCT Enablement & Geometry Synchronization:** When network volumes are mounted via `FNMNTIMG`, the utility enables the target drive's DCT (`JP FDCDVR`), queries Sector 0 and the directory GAT, and populates the drive's geometry dynamically.
5. **In-DOS Utilities (`FN*.CMD`):** Commands like `FNSTATUS`, `FNHOSTS`, `FNDEVS`, and `FNMOUNT` execute from the `TRSDOS Ready` prompt as ordinary LS-DOS applications, issuing `OP_FUJI` (`0xE2`) transactions to configure the remote peripheral on the fly.
