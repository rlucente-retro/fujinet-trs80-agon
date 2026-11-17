; ==============================================================================
; SDMOUNT.S - Mount or Unmount Local Agon MicroSD Card Image (SPI0)
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   SDMOUNT <drv 0-7> <lba> [mode 1-3]
;   Where <lba> is the 32-bit starting sector LBA (hex 0x... or decimal).
;   Specifying LBA = 0 unmounts the drive.
;   Modes: 1 = Read-Only (RO), 3 = Read-Write (RW). Default mode: 3 (RW).
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; Parse <drv> from command line buffer INBUF$
    ld      hl, INBUF$
    call    fuji_skip_cmd
    ld      de, arg_buf
    call    fuji_parse_arg
    jr      c, sdmnt_usage

    ld      de, arg_buf
    call    fuji_parse_slot
    jr      c, sdmnt_bad_drv
    ld      (target_drv), a

    ; Parse <lba> argument
    ld      de, arg_lba
    call    fuji_parse_arg
    jr      c, sdmnt_usage

    ld      de, arg_lba
    call    parse_lba_u32
    jr      c, sdmnt_bad_lba

    ; Parse optional [mode] (defaults to 3 = RW for local SD card)
    ld      a, FUJI_MODE_READWRITE
    ld      (target_mode), a

    ld      de, arg_buf
    call    fuji_parse_arg
    jr      c, sdmnt_do_mount   ; No mode argument provided: use default (3 = RW)

    ld      a, (arg_buf)
    cp      '1'
    jr      z, sdmnt_set_ro
    cp      '3'
    jr      z, sdmnt_set_rw
    jr      sdmnt_bad_mode

sdmnt_set_ro:
    ld      a, FUJI_MODE_READ
    ld      (target_mode), a
    jr      sdmnt_do_mount

sdmnt_set_rw:
    ld      a, FUJI_MODE_READWRITE
    ld      (target_mode), a

sdmnt_do_mount:
    ; Check if LBA is 0 (unmount request)
    ld      hl, target_lba
    ld      a, (hl)
    inc     hl
    or      (hl)
    inc     hl
    or      (hl)
    inc     hl
    or      (hl)
    jr      z, sdmnt_unmount

    ; -------------------------------------------------------------------------
    ; Mount Action:
    ; 1. Store 4-byte target_lba into sddvr_base_lba (0xF408 + target_drv * 4)
    ; 2. Set drv_target_table (0x0E76 + target_drv) to DRV_TARGET_SD (0)
    ; 3. Synchronize target DCTn$ from DCT0$ template
    ; 4. Set 1-hot physical drive select bit in DCTn$+4
    ; 5. Set write-protect bit according to mode
    ; -------------------------------------------------------------------------
    ld      a, (target_drv)
    rlca
    rlca                        ; A = target_drv * 4
    ld      e, a
    ld      d, 0
    ld      hl, sddvr_base_lba
    add     hl, de              ; HL = sddvr_base_lba[target_drv]
    ex      de, hl              ; DE = destination in sddvr_base_lba
    ld      hl, target_lba
    ldi
    ldi
    ldi
    ldi

    ; Set drv_target_table to DRV_TARGET_SD (0)
    ld      a, (target_drv)
    ld      e, a
    ld      d, 0
    ld      hl, drv_target_table
    add     hl, de
    ld      (hl), DRV_TARGET_SD ; Route sector requests to Local SD (SPI0)

    ; Calculate DCT address: DCTn$ = DCT0$ + (target_drv * DCT_SIZE)
    ld      a, (target_drv)
    or      a
    jr      z, sdmnt_update_dct0 ; If drive 0, don't overwrite template

    ; Multiply target_drv by DCT_SIZE (10)
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

    ; Compute 1-hot physical drive select mask: 1 << target_drv
    ld      a, (target_drv)
    ld      b, a
    ld      a, 1
    inc     b
    dec     b
    jr      z, sdmnt_mask_done
sdmnt_mask_loop:
    add     a, a
    djnz    sdmnt_mask_loop
sdmnt_mask_done:
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
    jr      nz, sdmnt_rw
    set     7, (ix + 3)         ; Set write-protect bit (RO)
    jr      sdmnt_display

sdmnt_rw:
    res     7, (ix + 3)         ; Clear write-protect bit (RW)
    jr      sdmnt_display

sdmnt_update_dct0:
    ; For drive 0, update write-protect bit according to mode
    ld      ix, DCT0$
    ld      a, (target_mode)
    cp      FUJI_MODE_READ
    jr      nz, sdmnt_rw0
    set     7, (ix + 3)
    jr      sdmnt_display
sdmnt_rw0:
    res     7, (ix + 3)

sdmnt_display:
    ; Display confirmation
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (target_drv)
    call    fuji_print_dec8
    ld      hl, msg_mid
    _DSPLY
    ld      hl, target_lba
    call    print_hex32
    ld      hl, msg_mode_pfx
    _DSPLY

    ld      a, (target_mode)
    cp      FUJI_MODE_READ
    jr      z, sdmnt_pr_ro
    ld      hl, msg_mode_rw
    _DSPLY
    jr      sdmnt_done

sdmnt_pr_ro:
    ld      hl, msg_mode_ro
    _DSPLY

sdmnt_done:
    ld      hl, msg_sfx
    _DSPLY
    _EXIT

; -----------------------------------------------------------------------------
; Unmount Action (LBA = 0)
; -----------------------------------------------------------------------------
sdmnt_unmount:
    ; Zero out sddvr_base_lba[target_drv]
    ld      a, (target_drv)
    rlca
    rlca
    ld      e, a
    ld      d, 0
    ld      hl, sddvr_base_lba
    add     hl, de
    xor     a
    ld      (hl), a
    inc     hl
    ld      (hl), a
    inc     hl
    ld      (hl), a
    inc     hl
    ld      (hl), a

    ; If target_drv != 0, deactivate DCTn$
    ld      a, (target_drv)
    or      a
    jr      z, sdmnt_unm_disp

    ld      l, a
    ld      h, 0
    add     hl, hl              ; *2
    ld      d, h
    ld      e, l
    add     hl, hl              ; *4
    add     hl, hl              ; *8
    add     hl, de              ; *10
    ld      de, DCT0$
    add     hl, de
    xor     a
    ld      (hl), a             ; Set (DCTn$) = 0x00 (Offline)

sdmnt_unm_disp:
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (target_drv)
    call    fuji_print_dec8
    ld      hl, msg_unm_done
    _DSPLY
    _EXIT

sdmnt_usage:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

sdmnt_bad_drv:
    ld      hl, msg_bad_drv
    _DSPLY
    _EXIT

sdmnt_bad_lba:
    ld      hl, msg_bad_lba
    _DSPLY
    _EXIT

sdmnt_bad_mode:
    ld      hl, msg_bad_mode
    _DSPLY
    _EXIT

; -----------------------------------------------------------------------------
; Subroutines: 32-bit Integer Parser & Printer
; -----------------------------------------------------------------------------

; Parse null-terminated string at DE into 32-bit target_lba
; Supports hex (0x... or containing A-F) and decimal
parse_lba_u32:
    xor     a
    ld      (target_lba+0), a
    ld      (target_lba+1), a
    ld      (target_lba+2), a
    ld      (target_lba+3), a

    ; Check if string begins with "0x" or "0X"
    ld      a, (de)
    cp      '0'
    jr      nz, pl_check_hex_char
    inc     de
    ld      a, (de)
    cp      'x'
    jr      z, pl_is_hex
    cp      'X'
    jr      z, pl_is_hex
    dec     de                  ; Restore DE

pl_check_hex_char:
    push    de
pl_scan:
    ld      a, (de)
    or      a
    jr      z, pl_is_dec        ; All digits were decimal
    cp      'a'
    jr      c, pl_s1
    cp      'g'
    jr      c, pl_scan_found_hex
pl_s1:
    cp      'A'
    jr      c, pl_s2
    cp      'G'
    jr      c, pl_scan_found_hex
pl_s2:
    inc     de
    jr      pl_scan

pl_scan_found_hex:
    pop     de
    jr      pl_hex_loop

pl_is_hex:
    inc     de                  ; Skip 'x' or 'X'
pl_hex_loop:
    ld      a, (de)
    or      a
    ret     z                   ; Null terminator: done
    cp      '0'
    jr      c, pl_err
    cp      '9' + 1
    jr      c, pl_h_dec
    cp      'A'
    jr      c, pl_err
    cp      'F' + 1
    jr      c, pl_h_upper
    cp      'a'
    jr      c, pl_err
    cp      'f' + 1
    jr      c, pl_h_lower
    jr      pl_err

pl_h_dec:
    sub     '0'
    jr      pl_h_add
pl_h_upper:
    sub     'A' - 10
    jr      pl_h_add
pl_h_lower:
    sub     'a' - 10

pl_h_add:
    push    af
    call    shl32_4             ; target_lba <<= 4
    pop     af
    ld      hl, target_lba
    add     a, (hl)
    ld      (hl), a
    inc     de
    jr      pl_hex_loop

pl_is_dec:
    pop     de
pl_dec_loop:
    ld      a, (de)
    or      a
    ret     z                   ; Null terminator: done
    cp      '0'
    jr      c, pl_err
    cp      '9' + 1
    jr      nc, pl_err
    sub     '0'
    push    af
    call    mul32_10            ; target_lba *= 10
    pop     af
    ; Add A (0..9) to target_lba
    ld      hl, target_lba
    add     a, (hl)
    ld      (hl), a
    inc     hl
    ld      a, 0
    adc     a, (hl)
    ld      (hl), a
    inc     hl
    ld      a, 0
    adc     a, (hl)
    ld      (hl), a
    inc     hl
    ld      a, 0
    adc     a, (hl)
    ld      (hl), a
    inc     de
    jr      pl_dec_loop

pl_err:
    scf
    ret

; Shift target_lba left by 4 bits
shl32_4:
    ld      b, 4
s4_loop:
    xor     a
    ld      hl, target_lba
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)
    djnz    s4_loop
    ret

; Multiply 32-bit target_lba by 10: val * 10 = (val * 2) + (val * 8)
mul32_10:
    ; tmp32 = target_lba
    ld      hl, target_lba
    ld      de, tmp32
    ldi
    ldi
    ldi
    ldi

    ; target_lba <<= 1 (val * 2)
    xor     a
    ld      hl, target_lba
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)

    ; tmp32 <<= 3 (val * 8)
    ld      b, 3
m10_tmp_loop:
    xor     a
    ld      hl, tmp32
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)
    djnz    m10_tmp_loop

    ; target_lba += tmp32 (val * 2 + val * 8 = val * 10)
    ld      hl, tmp32
    ld      de, target_lba
    xor     a
    ld      a, (de)
    add     a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    ld      a, (de)
    adc     a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    ld      a, (de)
    adc     a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    ld      a, (de)
    adc     a, (hl)
    ld      (de), a
    ret

; Print 32-bit integer in hex from little-endian memory pointed to by HL
print_hex32:
    push    hl
    inc     hl
    inc     hl
    inc     hl
    ld      a, (hl)             ; Byte 3 (MSB)
    call    fuji_print_hex8
    dec     hl
    ld      a, (hl)             ; Byte 2
    call    fuji_print_hex8
    dec     hl
    ld      a, (hl)             ; Byte 1
    call    fuji_print_hex8
    dec     hl
    ld      a, (hl)             ; Byte 0 (LSB)
    call    fuji_print_hex8
    pop     hl
    ret

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_usage:      DB  "Usage: SDMOUNT <drv 0-7> <lba> [mode 1-3]", CR, 03h
msg_bad_drv:    DB  "Error: Drive must be 0 to 7", CR, 03h
msg_bad_lba:    DB  "Error: Invalid starting LBA (hex or decimal)", CR, 03h
msg_bad_mode:   DB  "Error: Mode must be 1 (RO) or 3 (RW)", CR, 03h
msg_pfx:        DB  "Drive :", 03h
msg_mid:        DB  " mounted on Local SD (Base LBA: 0x", 03h
msg_mode_pfx:   DB  ", Mode: ", 03h
msg_mode_ro:    DB  "RO", 03h
msg_mode_rw:    DB  "RW", 03h
msg_sfx:        DB  ").", CR, 03h
msg_unm_done:   DB  " unmounted.", CR, 03h

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
target_drv:     DB  0
target_mode:    DB  0
target_dct:     DW  0
target_lba:     DS  4
tmp32:          DS  4
arg_buf:        DS  16
arg_lba:        DS  32

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
