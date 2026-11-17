; ==============================================================================
; FNMNTIMG.S - Mount Virtual Disk Image and Synchronize TRSDOS DCT Geometry
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   FNMNTIMG <slot 0-3> [mode 1-3]
;   Modes: 1 = Read-Only (RO), 2 = Write-Only (WO), 3 = Read-Write (RW)
;   Default mode: 1 (Read-Only)
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; Parse <slot> from command line buffer INBUF$
    ld      hl, INBUF$
    call    fuji_skip_cmd
    ld      de, arg_buf
    call    fuji_parse_arg
    jr      c, mntimg_usage

    ld      de, arg_buf
    call    fuji_parse_slot
    jr      c, mntimg_bad_slot
    cp      MAX_DEV_SLOTS_STAGE1
    jr      nc, mntimg_bad_slot
    ld      (target_slot), a

    ; Parse optional [mode] (defaults to 1 = RO)
    ld      a, FUJI_MODE_READ
    ld      (target_mode), a

    ld      de, arg_buf
    call    fuji_parse_arg
    jr      c, mntimg_do_mount   ; No mode argument provided: use default (1)

    ld      a, (arg_buf)
    cp      '1'
    jr      c, mntimg_bad_mode
    cp      '4'
    jr      nc, mntimg_bad_mode
    sub     '0'
    ld      (target_mode), a

mntimg_do_mount:
    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; Prepare 2-byte mount request: [0] = slot, [1] = mode
    ld      a, (target_slot)
    ld      (mount_req_slot), a
    ld      a, (target_mode)
    ld      (mount_req_mode), a

    ; Issue FUJI_CMD_MOUNT_IMAGE (0xF8) with 2-byte parameter payload
    ld      a, FUJI_CMD_MOUNT_IMAGE
    ld      hl, mount_req
    ld      bc, 2
    call    fuji_cmd_params
    jr      c, mntimg_comm_err

    ; Check command execution status
    call    fuji_get_error
    jr      c, mntimg_err

    ; -------------------------------------------------------------------------
    ; Probe Sector 0 of mounted image over DriveWire via OP_READEX (0xD2)
    ; to detect container header: DiskDISK (offset 1) vs raw JV1 (offset 0)
    ; -------------------------------------------------------------------------
    ld      a, OP_READEX
    ld      (sec0_req+0), a
    ld      a, (target_slot)
    ld      (sec0_req+1), a
    xor     a
    ld      (sec0_req+2), a
    ld      (sec0_req+3), a
    ld      (sec0_req+4), a

    call    fuji_flush
    ld      hl, sec0_req
    ld      bc, 5
    call    fuji_tx_bytes
    jr      c, mntimg_probe_fail

    ld      hl, sec0_buf
    ld      bc, 256
    call    fuji_rx_bytes
    jr      c, mntimg_probe_fail

    ; Calculate 16-bit big-endian unsigned checksum
    ld      hl, sec0_buf
    ld      bc, 256
    ld      de, 0               ; DE = checksum accumulator
sec0_chk_loop:
    ld      a, (hl)
    inc     hl
    add     a, e
    ld      e, a
    jr      nc, sec0_chk_nc
    inc     d
sec0_chk_nc:
    dec     bc
    ld      a, b
    or      c
    jr      nz, sec0_chk_loop

    ; Transmit 2-byte checksum (D=MSB, E=LSB)
    ld      a, d
    call    fuji_tx_byte
    jr      c, mntimg_probe_fail
    ld      a, e
    call    fuji_tx_byte
    jr      c, mntimg_probe_fail

    ; Receive 1-byte wire status (0x00 = success)
    call    fuji_rx_byte
    jr      c, mntimg_probe_fail
    or      a
    jr      nz, mntimg_probe_fail

    ; Check if Sector 0 begins with "DiskDISK" (8 bytes)
    ld      hl, sec0_buf
    ld      de, diskdisk_magic
    ld      b, 8
sec0_cmp:
    ld      a, (de)
    cp      (hl)
    jr      nz, sec0_is_jv1
    inc     hl
    inc     de
    djnz    sec0_cmp

    ; Image has DiskDISK header: container offset = 1
    ld      a, 1
    ld      (container_offset), a
    jr      sec0_probe_done

sec0_is_jv1:
    ; Image is raw JV1: container offset = 0
    xor     a
    ld      (container_offset), a
    jr      sec0_probe_done

mntimg_probe_fail:
    ; On probe communication failure, default to DiskDISK offset 1
    ld      a, 1
    ld      (container_offset), a

sec0_probe_done:
    ; -------------------------------------------------------------------------
    ; Live Operating System Integration:
    ; 1. Set dw_offset[slot] to container offset (1 for DiskDISK, 0 for JV1)
    ; 2. Set drv_target_table[slot] entry to DRV_TARGET_DW (1)
    ; 3. Synchronize target DCTn$ from DCT0$ template
    ; 4. Set 1-hot physical drive select bit in DCTn$+4
    ; 5. Set write-protect bit according to mode
    ; -------------------------------------------------------------------------
    ld      a, (target_slot)
    ld      e, a
    ld      d, 0
    ld      hl, dw_offset
    add     hl, de
    ld      a, (container_offset)
    ld      (hl), a             ; Set container offset in kernel dw_offset table

    ld      a, (target_slot)
    ld      e, a
    ld      d, 0
    ld      hl, drv_target_table
    add     hl, de
    ld      (hl), DRV_TARGET_DW ; Route sector requests to DriveWire (UART1)

    ; Calculate DCT address: DCTn$ = DCT0$ + (slot * DCT_SIZE)
    ld      a, (target_slot)
    or      a
    jr      z, mntimg_update_dct0 ; If slot 0, don't copy over itself

    ; Multiply slot (in A) by DCT_SIZE (10)
    ld      l, a
    ld      h, 0
    add     hl, hl              ; *2
    ld      d, h
    ld      e, l                ; DE = *2
    add     hl, hl              ; *4
    add     hl, hl              ; *8
    add     hl, de              ; *8 + *2 = *10
    ld      de, DCT0$
    add     hl, de              ; HL = DCTn$
    ld      (target_dct), hl

    ; Copy 10 bytes from DCT0$ template into DCTn$
    ex      de, hl              ; DE = DCTn$
    ld      hl, DCT0$
    ld      bc, DCT_SIZE
    ldir

    ; Compute 1-hot physical drive select mask: 1 << slot
    ; Slot 0 -> 0x01, Slot 1 -> 0x02, Slot 2 -> 0x04, Slot 3 -> 0x08
    ld      a, (target_slot)
    ld      b, a
    ld      a, 1
    inc     b
    dec     b
    jr      z, drive_mask_done
drive_mask_loop:
    add     a, a                ; Shift left
    djnz    drive_mask_loop
drive_mask_done:
    ld      c, a                ; C = 1-hot drive mask

    ; Set drive select code in DCTn$+4
    ld      ix, (target_dct)
    ld      a, (ix + 4)
    and     0F0h                ; Preserve Alien (0x10) and Double-Sided (0x20)
    or      c                   ; Set 1-hot physical drive bit
    ld      (ix + 4), a

    ; Reset current cylinder to 0
    xor     a
    ld      (ix + 5), a

    ; Configure write-protect bit (bit 7 of byte 3) based on mode
    ld      a, (target_mode)
    cp      FUJI_MODE_READ
    jr      nz, mntimg_rw
    set     7, (ix + 3)         ; Set write-protect bit (RO)
    jr      mntimg_display

mntimg_rw:
    res     7, (ix + 3)         ; Clear write-protect bit (RW)
    jr      mntimg_display

mntimg_update_dct0:
    ; For drive 0, update write-protect bit according to mode
    ld      ix, DCT0$
    ld      a, (target_mode)
    cp      FUJI_MODE_READ
    jr      nz, mntimg_rw0
    set     7, (ix + 3)
    jr      mntimg_display
mntimg_rw0:
    res     7, (ix + 3)

mntimg_display:
    ; Display confirmation
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (target_slot)
    call    fuji_print_dec8
    ld      hl, msg_mid
    _DSPLY
    ld      a, (target_slot)
    call    fuji_print_dec8
    ld      hl, msg_mode_pfx
    _DSPLY

    ld      a, (target_mode)
    cp      FUJI_MODE_READ
    jr      z, mntimg_pr_ro
    cp      FUJI_MODE_WRITE
    jr      z, mntimg_pr_wo
    cp      FUJI_MODE_READWRITE
    jr      z, mntimg_pr_rw
    ld      hl, msg_mode_unk
    _DSPLY
    jr      mntimg_pr_fmt

mntimg_pr_ro:
    ld      hl, msg_mode_ro
    _DSPLY
    jr      mntimg_pr_fmt

mntimg_pr_wo:
    ld      hl, msg_mode_wo
    _DSPLY
    jr      mntimg_pr_fmt

mntimg_pr_rw:
    ld      hl, msg_mode_rw
    _DSPLY

mntimg_pr_fmt:
    ld      a, (container_offset)
    or      a
    jr      z, mntimg_fmt_jv1
    ld      hl, msg_fmt_dd
    _DSPLY
    _EXIT

mntimg_fmt_jv1:
    ld      hl, msg_fmt_jv
    _DSPLY
    _EXIT

mntimg_usage:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

mntimg_bad_slot:
    ld      hl, msg_bad_slot
    _DSPLY
    _EXIT

mntimg_bad_mode:
    ld      hl, msg_bad_mode
    _DSPLY
    _EXIT

mntimg_comm_err:
    ld      a, 0FFh
    call    fuji_print_error
    _EXIT

mntimg_err:
    call    fuji_print_error
    _EXIT

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_usage:      DB  "Usage: FNMNTIMG <slot 0-3> [mode 1-3]", CR, 03h
msg_bad_slot:   DB  "Error: Slot must be 0 to 3", CR, 03h
msg_bad_mode:   DB  "Error: Mode must be 1 (RO), 2 (WO), or 3 (RW)", CR, 03h
msg_pfx:        DB  "Device Slot ", 03h
msg_mid:        DB  " mounted as Drive :", 03h
msg_mode_pfx:   DB  " (Mode: ", 03h
msg_mode_ro:    DB  "RO", 03h
msg_mode_wo:    DB  "WO", 03h
msg_mode_rw:    DB  "RW", 03h
msg_mode_unk:   DB  "--", 03h
msg_fmt_dd:     DB  ", Format: DiskDISK).", CR, 03h
msg_fmt_jv:     DB  ", Format: JV1).", CR, 03h
diskdisk_magic: DB  "DiskDISK"

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
target_slot:      DB  0
target_mode:      DB  0
container_offset: DB  1
target_dct:       DW  0
arg_buf:          DS  16

mount_req:
mount_req_slot:   DB  0
mount_req_mode:   DB  0

sec0_req:         DS  5
sec0_buf:         DS  256

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
