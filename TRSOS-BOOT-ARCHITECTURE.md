# TRS-OS & LS-DOS Boot Architecture on Agon Light 2

**Document Version:** 1.0.0  
**Target Hardware:** Olimex Agon Light 2 (Zilog eZ80F92 @ 18.432 MHz)  
**Operating Systems:** Agon Quark MOS $\to$ TRS-OS (eZ80 HAL/BIOS) $\to$ TRSDOS 6.3.1 / LS-DOS 6.3.1  

---

## 1. Executive Summary & Architectural Distinctions

A common source of confusion in this environment is the terminology surrounding **OSboot**, **TRS-OS**, and **TRSDOS / LS-DOS**. These are three separate, cooperating software layers:

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

### The Three Entities Defined:
1. **`OSboot` (`OSboot.bin`)**: Shawn Sijnstra's MOS bootstrap loader utility. It executes from Quark MOS, programs the ESP32 Video Display Processor (VDP) with text modes and custom fonts, loads the monolithic `TRSDOS.bin` image into memory, flags the platform type, and transfers control to the operating system.
2. **`TRS-OS` (`TRSDOS_7`)**: Daniel Paul Martin's modern port and hardware abstraction layer (HAL) for the Zilog eZ80 processor. It provides the BIOS, CPU initializers, video terminal translation, interrupt tables, timer tasks, and virtual floppy drivers (`driver-FDCDVR.S`) that simulate a TRS-80 Model 4 environment on modern eZ80 microcontrollers.
3. **`TRSDOS / LS-DOS 6.3.1`**: The authentic, historical disk operating system originally created by Logical Systems Inc. (LSI) as LS-DOS 6.3 and licensed by Tandy Corporation as TRSDOS 6.3.1 for the TRS-80 Model 4. It contains the system overlays (`SYS0/SYS` through `SYS6/SYS`), supervisor calls (SVCs), file management routines, and the command line interpreter (`TRSDOS Ready`).

---

## 2. The Complete Boot Chain

The transition from Quark MOS to the `TRSDOS Ready` prompt follows a four-step lifecycle:

```
[ Agon Quark MOS ]
       |
       |  *openstream <host> 65504  (Optional: connects ESP8266 Wi-Fi)
       |  *OSboot -2f TRSDOS.bin
       v
+-------------------------------------------------------------------------------+
| STEP 1: OSboot (Quark MOS Environment)                                        |
| 1. Parses CLI flags: color (-2), terminal mode (-v), TRS-80 font (-f).        |
| 2. Transmits VDP escape sequences over UART0 to configure Agon console.       |
| 3. Reads TRSDOS.bin from microSD and loads it into eZ80 RAM at 0x000000.      |
| 4. Bypasses secondary disk image loading (non-persistent RAM disk :1 removed).|
| 5. Writes platform tag '3' to UART0 Scratchpad Register (UART0_SPR = 3).      |
| 6. Jumps to entry point at 0x000000 (__init).                                 |
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
| STEP 3: TRS-OS BIOS & Virtual Storage Initialization (SYS0_IPL/ipl-BIOS.s)    |
| 1. Sets up 60 Hz real-time clock interrupts on eZ80 Timer 1 (TMR1).           |
| 2. Initializes RAM drive Slice Table (Slice 0 at 0x010000, Slice 1 at 0x010100)|
| 3. Mounts minimal Drive :0 from Slice 1 Volume 0 (bootstrap kernel & FN tools)|
| 4. Reads Sector 0 and Directory GAT to dynamically configure DCT0$.           |
| 5. Configures Drives :1..:7 for FujiNet DriveWire network mounting.           |
| 6. Clears keyboard buffer and jumps to DOSINIT.                               |
+-------------------------------------------------------------------------------+
       |
       v
+-------------------------------------------------------------------------------+
| STEP 4: TRSDOS / LS-DOS 6.3.1 System Initialization (SYS0_IPL/ipl-DOS.S)      |
| 1. Inspects AUTO command buffer (INBUF$) and SYSGEN status on Drive :0.       |
| 2. Sets terminal driver parameters and starts background task timer (EI).     |
| 3. Executes system initialization chain (zICNFG).                             |
| 4. Transmits cursor home/clear escapes to VDP console.                        |
| 5. Jumps to LS-DOS command line interpreter (zCMNDI in SYS6/SYS).             |
| 6. Displays the authentic "TRSDOS Ready" prompt.                              |
+-------------------------------------------------------------------------------+
```

---

## 3. Detailed Step-by-Step Execution

### Step 1: `OSboot` Execution in Quark MOS
* **Invocation:** The user enters `*OSboot -2f TRSDOS.bin` at the MOS `*` prompt.
* **Console Preparation:** `OSboot` issues MOS VDP calls over internal UART0 to program the ESP32 VDP:
  * `-2`: Selects green phosphor text color.
  * `-f`: Commands the VDP to switch to Font 1 (the authentic 8×10 TRS-80 Model 4 font loaded from `TRS80M4pG.F10`).
  * Terminal mode: Selects ADDS25 terminal emulation (default) or VT100 (`-v`).
* **Binary Structure & Memory Layout:**
  * `TRSDOS.bin` is a **monolithic binary image** structured into two distinct regions:
    1. **eZ80 Kernel & Slice Tables (66,048 bytes / `0x010200`):** Addresses `0x000000`–`0x00FFFF` contain the 64 KB operating system kernel (`SYS0_IPL` through `SYS6` core). Address `0x010000` holds the 256-byte `Slice 0` table, and `0x010100` holds the 256-byte `Slice 1` table.
    2. **Bootstrap Volume 0 DiskDISK Image (Appended at `0x010200`):** In the reference system distribution, Volume 0 is a 425,472-byte DiskDISK boot image (`66,048 + 425,472 = 491,520 bytes = 480 KiB`), extending through physical address `0x077FFF`. In a minimal bootstrap deployment, a streamlined DiskDISK image containing the essential kernel overlays and `FN*.CMD` utilities is appended at `0x010200`.
  * *Note on Binary Sizing:* A standalone 66,048-byte binary contains *only* the kernel and slice pointers without any disk volume. The fully bootable monolithic image deployed to the SD card for `OSboot` is **491,520 bytes (480 KiB)**.
* **Hardware SRAM Remapping & Loading Sequence:**
  * In standard Quark MOS operation, Agon Light 2 on-board Flash ROM is mapped at `0x000000`–`0x03FFFF`, while 512 KiB external SRAM is mapped from `0x040000` to `0x0BFFFF`. `OSboot` itself runs as a MOS application loaded at `0x040000`.
  * Because `0x000000` is initially read-only Flash, `OSboot` cannot directly load `TRSDOS.bin` to address zero.
  * Instead, `OSboot`:
    1. Reads `TRSDOS.bin` from microSD into a high-SRAM staging buffer (e.g. `0x080000`).
    2. Relocates a small execution trampoline into the eZ80 on-chip 8 KiB SRAM (`RAM_ADDR_U` set to `0xFFE000` or `0x0B8000`).
    3. Executes from on-chip SRAM to reprogram the eZ80 Chip Select registers (`CS0`, `CS1`, `RAM_CTL`), remapping external SRAM down to base `0x000000` and moving Flash ROM out of the lower 64 KB address space.
    4. Copies the staged binary data to address `0x000000`.
    5. Writes the platform tag `3` into the UART0 Scratchpad Register (`UART0_SPR = 0x03`).
    6. Jumps to the entry point at physical address `0x000000` (`__init`).
* **Bypass of Secondary RAM Disk:** Secondary disk image loading (`[diskfile]` into `0x045300`) is omitted. All secondary and persistent storage is redirected to remote FujiNet drives.

### Step 2: `TRS-OS` CPU & Platform Setup (`ipl-eZ80_CPU.S`)
* **Entry (`__init`):** Execution begins at `0x000000` with interrupts disabled (`DI`) in Z80 compatibility mode (`ADL = 0`).
* **Stack Configuration:** Sets the 24-bit extended stack pointer to `0x0100FF` and sets the 16-bit stack pointer `SP` to `STACK$` (`0x03FF`).
* **Platform Signature Check (`tst_initial`):**
  * Reads `UART0_SPR`.
  * If the register contains zero (indicating a cold power-on reset), it executes a full chip hardware initialization.
  * Because `UART0_SPR` contains `3`, it identifies the Agon platform (`vendor_agon$`) and branches immediately to **`NOINITIAL`**.
* **The Warm-Boot Bypass (`NOINITIAL`):**
  * Bypasses the cold-boot peripheral reset routines so that MOS memory configurations and active VDP states are not destroyed.
* **Explicit Hardware Reinitialization (`finish_init`):**
  * When `openstream` exited in MOS via `mos_uclose()`, MOS zeroed the eZ80 CPU's UART1 control registers (`LCTL = 0`, `FCTL = 0`, `IER = 0`).
  * In `finish_init`, `TRS-OS` explicitly reconfigures the eZ80F92 hardware:
    1. **Port C Mode 7:** Configures `PC0` (TXD1) and `PC1` (RXD1) as alternate functions for UART1.
    2. **Baud Rate:** Sets Baud Rate Generator divisor to `10` ($18,432,000 / (16 \times 115,200)$), giving an exact 115,200 baud.
    3. **Line Framing:** Sets `UART1_LCTL = 0x03` (8 data bits, 1 stop bit, no parity).
    4. **Interrupt Masking:** Sets `UART1_IER = 0x00` (pure polled I/O, preventing unhandled interrupt crashes).
    5. **FIFO Management:** Sets `UART1_FCTL = 0x07` (enables and flushes the 16-byte RX and TX FIFOs).
* **Display Handshake:** Clears the Model 4 text buffer (`CRTBGN$`) with spaces and jumps to `STARTBIOS`.

### Step 3: `TRS-OS` BIOS & Storage Mounting (`ipl-BIOS.s`)
* **Real-Time Clock Interrupts:**
  * Sets up eZ80 Timer 1 (`TMR1`) to generate 60 Hz periodic interrupts matching the TRS-80 Model 4 heartbeat clock.
  * Installs the interrupt vector pointing to the LS-DOS task scheduler.
* **The Virtual RAM Slice System:**
  * To run without physical Western Digital floppy disk controller chips, Daniel Paul Martin designed a RAM-based disk system:
  * **Slice 0** is located at `0x010000` in eZ80 RAM. It contains pointers to virtual disk slices.
  * **Slice 1** is located at `0x010100`. The first 3 bytes provide an offset to the boot volume (Volume 0 at `0x010200`).
* **Mounting Drive `:0` (The Bootstrap Drive):**
  * `TRSDOS.bin` embeds an authentic LS-DOS system disk directly inside its image in RAM (Slice 1 Volume 0 at `0x010200`).
  * `ipl-BIOS.s` inspects the first 8 bytes of Volume 0. If it contains `'DiskDISK'`, it extracts the embedded Device Control Table (DCT) parameters (`DCT+3` through `DCT+9`). If flat, it mounts it as a JV1 floppy.
  * Populates `DCT0$` in low memory, sets current cylinder to 0, and reads Sector 0 using the floppy driver (`FDCDVR`).
  * Reads the Granule Allocation Table (GAT) to verify that the disk was formatted by LS-DOS 6.x.
* **Refactoring `ipl-BIOS.s` for Drive `:1` & Network Drives:**
  * In upstream TRS-OS, `ipl-BIOS.s` unconditionally searched Slice 1 for Volume 1 in RAM, populated `DCT1$`, and executed `CKDRIVE 1, mounterr1`, halting boot if a secondary RAM disk was missing.
  * For FujiNet integration, `ipl-BIOS.s` is refactored:
    1. The Volume 1 RAM scan and `CKDRIVE 1` failure halt are neutralized.
    2. `DCT1$` is initialized with `JP FDCDVR` (`0xC3`) pointing to the DriveWire block driver, marked write-protected (`bit 7, DCT+3 = 1`), and assigned default geometry until an image is attached.
    3. `DCT2$` through `DCT7$` (which in authentic LS-DOS start with `RET` `0xC9` stubs) are dynamically enabled by `FNMNTIMG` as disk images are attached.
* **Firmware Device Slot Availability:**
  * In **Stage 1 (Stock CoCo Firmware Baseline):** Upstream CoCo firmware pins `MAX_DWDISK_DEVICES = 4`. Drives `:1` through `:4` map to FujiNet Device Slots 0 through 3 (FujiNet Web UI Drives 1–4).
  * In **Stage 2 (Patched Firmware):** With firmware recompiled for `MAX_DWDISK_DEVICES = 8`, all seven drives (`:1` through `:7`) map to Slots 0 through 6 (Web UI Drives 1–7).
* **Handover to DOS:** Clears the keyboard buffer and executes `GOTO DOSINIT`.

### Step 4: `TRSDOS / LS-DOS 6.3.1` Initialization (`ipl-DOS.S` $\to$ `ipl-trsdos.s`)
* **Entry (`DOSINIT`):**
  * Inspects Track 0, Sector 2 of Drive `:0` to check for an `AUTO` startup command string and `SYSGEN` configuration settings.
* **Console Terminal Handshake:**
  * Configures console terminal parameters.
  * Enables system interrupts (`EI`), starting the 60 Hz background tasker.
* **System Initialization Chain (`zICNFG`):**
  * Loads and executes the standard LS-DOS system initialization overlays (`SYS0` through `SYS5`).
  * Initializes the Device Control Blocks (DCBs) for keyboard (`*KI`), display (`*DO`), and printer (`*PR`).
* **Command Shell Entry (`zCMNDI`):**
  * Sends VT100/ADDS25 cursor home and clear-screen sequences to the VDP console.
  * Transfers control to `zCMNDI` in `SYS6/SYS` (the LS-DOS command line interpreter).
  * `SYS6/SYS` displays the startup banner and outputs:
    ```text
    TRSDOS Ready
    ```

---

## 4. Driver Architecture: Preserved vs. Emulated/Intercepted

The table below summarizes how TRS-80 Model 4 hardware facilities are handled under `TRS-OS` on the Agon Light 2:

| Subsystem / Driver | Real TRS-80 Model 4 Hardware | Agon Light 2 Implementation under `TRS-OS` | Status in System |
| :--- | :--- | :--- | :--- |
| **Video Display (`*DO`)** | Memory-mapped video at `0xF800`–`0xFFFF` (80×24 / 64×16). | Intercepted in `videoRAM.s`; characters converted to terminal stream over UART0 to ESP32 VDP. | **Emulated / Intercepted** via UART0 terminal emulation. |
| **Keyboard Input (`*KI`)** | Memory-mapped matrix scan ports (`0x3801`–`0x3880`). | Intercepted in `SVC09-keyin.s`; reads ASCII keys received from VDP over UART0. | **Emulated / Intercepted** via UART0. |
| **Floppy Disk Controller** | Western Digital WD1773 / WD1793 FDC I/O ports (`0xF0`–`0xF3`). | Intercepted in `driver-FDCDVR.S`: Drive `:0` routed to minimal bootstrap RAM Slice; Drives `:1`..`:7` routed to UART1 (FujiNet Device Slots 0..6). | **Emulated / Intercepted** (Minimal Boot RAM + UART1 Network). |
| **Real-Time Clock** | Hardware heartbeat interrupt (30/60 Hz). | eZ80 Timer 1 (`TMR1`) programmed to fire periodic 60 Hz interrupts; updates `TIME$` and `DATE$`. | **Hardware Adapted** to eZ80 on-chip timer. |
| **Line Printer (`*PR`)** | Centronics parallel printer port at I/O port `0xF8`. | Intercepted in `driver-PRDVR.S`; routed to UART1 `OP_PRINT` (`0x50`) to FujiNet. | **Emulated / Intercepted** via UART1. |
| **System Calls (`SVC`)** | `RST 28h` (RST 40 decimal) vector through Page 1 table. | 100% authentic Roy Soltoff / Logical Systems LS-DOS 6.3.1 SVC dispatch table preserved. | **100% Preserved Authentic LS-DOS Code.** |
| **File System Manager** | `SYS4/SYS` (Directory parsing, FCBs, file extents, GAT/HIT). | Exact, original binary code from LS-DOS 6.3.1 executed directly by eZ80 CPU. | **100% Preserved Authentic LS-DOS Code.** |
| **Command Interpreter** | `SYS6/SYS` (Command line parser, batch JCL, utility execution). | Exact, original binary code from LS-DOS 6.3.1 executed directly by eZ80 CPU. | **100% Preserved Authentic LS-DOS Code.** |

---

## 5. How TRSDOS/LS-DOS Runs on the eZ80 Agon Light

The ability of a 1983 operating system to execute seamlessly on a modern eZ80 microcontroller is made possible by several architectural factors:

1. **eZ80 Z80-Compatibility Mode (`ADL = 0`):**
   * The Zilog eZ80 processor powers up with full backward compatibility with the Z80 instruction set.
   * When operating in Z80 mode (`ADL = 0`), the CPU uses 16-bit addresses and 8-bit/16-bit registers identical to an original Z80.
   * Instructions execute with authentic Z80 semantics, but at roughly 3× the throughput per clock cycle due to eZ80 pipelining. At 18.432 MHz, the Agon Light behaves like an effective **~54 MHz TRS-80 Model 4**.

2. **Clean Operating System Abstraction (The Power of LS-DOS):**
   * Unlike many retro systems where programs routinely bypassed the OS and poked directly into hardware chips, LS-DOS was built with a clean, advanced supervisor architecture:
     * File I/O is accessed through `@OPEN`, `@READ`, `@WRITE`, `@CLOSE`.
     * Console I/O is accessed through `@KEYIN` and `@DSPLY`.
     * Disk drivers are accessed through standardized Device Control Tables (`DCT`).
   * Because Daniel Paul Martin's `TRS-OS` wrapper replaces the low-level hardware drivers while leaving the supervisor tables (`Page 0`, `Page 1`, `Page 2`, `Page 3`, and `DCTs`) in their exact historical memory locations, the authentic LS-DOS kernel runs completely unaware that physical floppy drives and CRT monitors have been replaced by RAM slices and a serial VDP.

3. **Software Compatibility:**
   * Any well-behaved TRS-80 Model 4 program that uses standard LS-DOS SVC calls or accesses the screen through standard DOS drivers runs natively on the Agon Light without modification or recompilation.

---

## 6. How FujiNet Fits into this Architecture

Understanding this layered architecture clarifies exactly where FujiNet integrates:

1. **`OSboot` is untouched:** `OSboot` loads `TRSDOS.bin` (which embeds bootstrap Volume 0 in RAM at `0x010200`) and reconfigures the screen as it always has.
2. **`TRS-OS` HAL manages UART1:** `TRS-OS` configures the eZ80's internal UART1 registers during `finish_init`, maintaining the streaming connection to FujiNet across the MOS-to-TRSDOS boundary.
3. **`driver-FDCDVR.S` redirects network drives:** When LS-DOS requests a sector from a network drive (Drives `:1`–`:4` in Stage 1, expanding to `:1`–`:7` in Stage 2), `driver-FDCDVR.S` translates the sector request into a DriveWire `OP_READEX` (`0xD2`) packet and streams it over UART1 instead of reading from an in-memory RAM slice. Drive `:0` remains the sole in-memory RAM slice for bootstrapping.
4. **DCT Enablement & Geometry Synchronization:** Authentic LS-DOS marks drives `:2`–`:7` as `RET` stubs by default. When network volumes are mounted via `FNMNTIMG`, the utility enables the target drive's DCT (`JP FDCDVR`), queries the remote image's Sector 0 and directory GAT, and configures the drive's geometry dynamically.
5. **FujiNet Utilities (`FN*.CMD`) run inside TRSDOS:** Commands like `FNSTATUS`, `FNHOSTS`, and `FNMOUNT` execute from the `TRSDOS Ready` prompt as ordinary LS-DOS command applications, issuing `OP_FUJI` (`0xE2`) serial transactions to configure the external peripheral.
