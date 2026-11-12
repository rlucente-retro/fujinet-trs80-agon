; ==============================================================================
; equ.inc - TRSDOS 6.3.1 / LS-DOS 6.3 System Equates and Memory Map
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; ==============================================================================

; ------------------------------------------------------------------------------
; Standard ASCII Control Characters
; ------------------------------------------------------------------------------
ASCII_NULL      EQU     00h     ; Null character
ASCII_SOH       EQU     01h     ; Start of Heading
ASCII_STX       EQU     02h     ; Start of Text
ASCII_ETX       EQU     03h     ; End of Text (TRSDOS @DSPLY string terminator)
ASCII_EOT       EQU     04h     ; End of Transmission
ASCII_BEL       EQU     07h     ; Bell
ASCII_BS        EQU     08h     ; Backspace
ASCII_TAB       EQU     09h     ; Horizontal Tabulation
ASCII_LF        EQU     0Ah     ; Line Feed
ASCII_VT        EQU     0Bh     ; Vertical Tabulation
ASCII_FF        EQU     0Ch     ; Form Feed
ASCII_CR        EQU     0Dh     ; Carriage Return
ASCII_ESC       EQU     1Bh     ; Escape
ASCII_SPACE     EQU     20h     ; Space
ASCII_DEL       EQU     7Fh     ; Delete / Rubout

; Short aliases for convenience
NULL            EQU     00h
ETX             EQU     03h
BEL             EQU     07h
BS              EQU     08h
TAB             EQU     09h
LF              EQU     0Ah
FF              EQU     0Ch
CR              EQU     0Dh
ESC             EQU     1Bh
CHR_SP          EQU     20h
SP_CHAR         EQU     20h

; ------------------------------------------------------------------------------
; Memory Map & Architecture Bounds
; ------------------------------------------------------------------------------
USER_PROG_BASE  EQU     2600h   ; Standard LS-DOS .CMD user program origin
SYSRES_TOP      EQU     25FFh   ; Top of resident TRSDOS kernel / SYSRES
SYS_PAGE4       EQU     0400h   ; TRSDOS system page 4 workspace
COREEND$        EQU     0F3FFh  ; Top of 64KB user memory space
SECTOR_SIZE     EQU     256     ; TRSDOS standard sector size in bytes

; ------------------------------------------------------------------------------
; TRSDOS 6.3 / LS-DOS Lowcore Memory Addresses (0x0000 - 0x04FF)
; ------------------------------------------------------------------------------
LDRV$           EQU     0023h   ; Current default logical drive number (0..7)
TIME$           EQU     002Dh   ; System time string buffer ("HH:MM:SS", 8 bytes)
DATE$           EQU     0033h   ; System date string buffer ("MM/DD/YY", 8 bytes)
OSRLS$          EQU     003Bh   ; OS release number
OSVER$          EQU     0085h   ; OS version byte (0x63 for TRSDOS 6.3)
SFCB$           EQU     008Ch   ; System file control block
BUR$            EQU     0200h   ; Boot/IPL RAM bank
BAR$            EQU     0201h   ; Base bank register
LBANK$          EQU     0202h   ; Current active expanded SRAM bank (0..7)
KIDCB$          EQU     0208h   ; Keyboard Input Device Control Block
DODCB$          EQU     0210h   ; Display Output Device Control Block
PRDCB$          EQU     0218h   ; Printer Device Control Block
STACK$          EQU     0380h   ; System stack base pointer
ZERO$           EQU     0401h   ; System zero constant
PAKNAM$         EQU     0410h   ; Boot volume pack name buffer (8 bytes)
INBUF$          EQU     0420h   ; Command line input buffer (256 bytes)

; ------------------------------------------------------------------------------
; Drive Control Tables (DCTs) - 10 bytes per logical drive (:0 to :7)
; ------------------------------------------------------------------------------
DCT$            EQU     0470h   ; Base pointer to DCT table (Drive :0)
DCT0$           EQU     0470h   ; Drive :0 Control Table
DCT1$           EQU     047Ah   ; Drive :1 Control Table
DCT2$           EQU     0484h   ; Drive :2 Control Table
DCT3$           EQU     048Eh   ; Drive :3 Control Table
DCT4$           EQU     0498h   ; Drive :4 Control Table
DCT5$           EQU     04A2h   ; Drive :5 Control Table
DCT6$           EQU     04ACh   ; Drive :6 Control Table
DCT7$           EQU     04B6h   ; Drive :7 Control Table
DCT_SIZE        EQU     10      ; Size of each DCT entry in bytes
MAX_DRIVES      EQU     8       ; Maximum supported logical drives (:0..:7)

; Offsets within each 10-byte DCT entry
DCT_OFF_BRANCH  EQU     0       ; Driver branch instruction (0xC3 = active, 0x00 = empty)
DCT_OFF_ENTRY   EQU     1       ; 16-bit driver entry point address (little-endian)
DCT_OFF_TYPE    EQU     3       ; Disk type / geometry flags
DCT_OFF_STEP    EQU     4       ; Step rate & head settling time
DCT_OFF_CYL     EQU     5       ; Current head cylinder position
DCT_OFF_SEC     EQU     6       ; Sectors per track (e.g. 18)
DCT_OFF_HEADS   EQU     7       ; Head count / sides flag (0 = single, 1 = double)
DCT_OFF_MAXCYL  EQU     8       ; Total cylinder count (e.g. 80 or 203)
DCT_OFF_CFG     EQU     9       ; Media configuration / format byte

; DCT Driver Opcode States
DCT_ACTIVE_OP   EQU     0C3h    ; 'JP' opcode: marks drive active and configured
DCT_EMPTY_OP    EQU     00h     ; 'NOP' opcode: marks drive slot unmounted / offline

; ------------------------------------------------------------------------------
; Agon Light 2 TRSDOS 7 Storage Router Equates
; ------------------------------------------------------------------------------
drv_target_table EQU    0E76h   ; 8-byte drive target table (:0 to :7)
DRV_TARGET_SD   EQU     0       ; Target: Local Agon MicroSD card (SPI0)
DRV_TARGET_DW   EQU     1       ; Target: FujiNet DriveWire (UART1)
DRV_TARGET_MEMDISK EQU  2       ; Target: High SRAM MEMDISK (Banks 1..7)

; Local MicroSD Card Driver State & LBA Mailbox (resident at 0xF400)
sddvr_inited    EQU     0F400h  ; MicroSD driver initialized flag (1 = ready)
sd_subsector    EQU     0F401h  ; Sub-sector selector (0 = low 256B, 1 = high 256B)
sd_cmd_buf      EQU     0F402h  ; 6-byte SPI CMD17 buffer
sddvr_base_lba  EQU     0F408h  ; 8-entry table of 32-bit base LBAs (4B per drive)

; ------------------------------------------------------------------------------
; Standard LS-DOS 6.3 / TRSDOS 7 Error Codes
; ------------------------------------------------------------------------------
LSERR_NONE      EQU     0       ; Successful completion (no error)
LSERR_PARM      EQU     16      ; Parameter error / syntax error in command line
LSERR_DRV       EQU     24      ; Illegal drive number
LSERR_NOTRDY    EQU     25      ; Drive not ready / no response
LSERR_WP        EQU     26      ; Disk write protected
LSERR_NOFILE    EQU     27      ; File not found
LSERR_EXTERR    EQU     28      ; Disk I/O error
LSERR_EOF       EQU     29      ; End of file encountered
LSERR_DIRFUL    EQU     30      ; Directory space full
LSERR_DSKFUL    EQU     31      ; Disk space full
LSERR_RO        EQU     32      ; File is read only
LSERR_FNF       EQU     40      ; Command file not found
LSERR_SVC       EQU     43      ; Illegal supervisor call

; ------------------------------------------------------------------------------
; Olimex Agon Light 2 Hardware Port Equates (Zilog eZ80 UART1)
; ------------------------------------------------------------------------------
UART1_RBR       EQU     0D0h    ; Receiver Buffer Register (Read Only)
UART1_THR       EQU     0D0h    ; Transmitter Holding Register (Write Only)
UART1_BRG_L     EQU     0D0h    ; Baud Rate Generator Low Byte (DLAB = 1)
UART1_IER       EQU     0D1h    ; Interrupt Enable Register
UART1_BRG_H     EQU     0D1h    ; Baud Rate Generator High Byte (DLAB = 1)
UART1_IIR       EQU     0D2h    ; Interrupt Identification Register (Read Only)
UART1_FCR       EQU     0D2h    ; FIFO Control Register (Write Only)
UART1_LCR       EQU     0D3h    ; Line Control Register
UART1_MCR       EQU     0D4h    ; Modem Control Register
UART1_LSR       EQU     0D5h    ; Line Status Register
UART1_MSR       EQU     0D6h    ; Modem Status Register
UART1_SPR       EQU     0D7h    ; Scratchpad Register

; UART Line Status Register (LSR) Bit Masks
UART_LSR_DR     EQU     01h     ; Bit 0: Data Ready in receiver FIFO
UART_LSR_OE     EQU     02h     ; Bit 1: Overrun Error
UART_LSR_PE     EQU     04h     ; Bit 2: Parity Error
UART_LSR_FE     EQU     08h     ; Bit 3: Framing Error
UART_LSR_BI     EQU     10h     ; Bit 4: Break Interrupt
UART_LSR_THRE   EQU     20h     ; Bit 5: Transmitter Holding Register Empty
UART_LSR_TEMT   EQU     40h     ; Bit 6: Transmitter Empty
UART_LSR_ERR    EQU     80h     ; Bit 7: Error in receiver FIFO
