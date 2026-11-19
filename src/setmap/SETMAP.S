; ==============================================================================
; SETMAP.S - Test & Diagnostic Utility to Stage SD Map Mailbox in Lowcore RAM
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   SETMAP <lba> [len]
;   Stages sddvr_map_lba (0xF428) and sddvr_map_len (0xF42C).
;   Specifying LBA = 0 clears the mailbox. Default length: 512 bytes.
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ld      hl, INBUF$
    call    fuji_skip_cmd
    ld      de, arg_lba
    call    fuji_parse_arg
    jr      c, setmap_usage
    ld      (cmd_ptr), hl       ; Save command-line pointer for optional [len]

    ; Parse <lba> argument
    ld      de, arg_lba
    call    parse_lba_u32
    jr      c, setmap_bad_lba

    ; Store 4 bytes into sddvr_map_lba (0xF428)
    ld      hl, target_lba
    ld      de, sddvr_map_lba
    ldi
    ldi
    ldi
    ldi

    ; Check if LBA is 0 (clearing mailbox)
    ld      hl, target_lba
    ld      a, (hl)
    inc     hl
    or      (hl)
    inc     hl
    or      (hl)
    inc     hl
    or      (hl)
    jr      z, setmap_clear_len

    ; Default length: 512 bytes
    ld      hl, 512
    ld      (sddvr_map_len), hl

    ; Parse optional [len]
    ld      hl, (cmd_ptr)       ; Restore command-line pointer
    ld      de, arg_len
    call    fuji_parse_arg
    jr      c, setmap_disp

    ld      de, arg_len
    call    parse_dec16
    jr      c, setmap_bad_len
    ld      (sddvr_map_len), hl
    jr      setmap_disp

setmap_clear_len:
    xor     a
    ld      hl, 0
    ld      (sddvr_map_len), hl

setmap_disp:
    ld      hl, msg_pfx
    _DSPLY
    ld      hl, sddvr_map_lba
    call    print_hex32
    ld      hl, msg_len_pfx
    _DSPLY
    ld      hl, (sddvr_map_len)
    call    print_dec16
    ld      hl, msg_sfx
    _DSPLY
    _EXIT

setmap_usage:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

setmap_bad_lba:
    ld      hl, msg_err_lba
    _DSPLY
    _EXIT

setmap_bad_len:
    ld      hl, msg_err_len
    _DSPLY
    _EXIT

; -----------------------------------------------------------------------------
; Subroutines
; -----------------------------------------------------------------------------

; Parse null-terminated decimal string at DE -> HL (16-bit)
parse_dec16:
    ld      hl, 0
    ld      a, (de)
    or      a
    scf
    ret     z
pd16_loop:
    ld      a, (de)
    or      a
    ret     z
    cp      '0'
    jr      c, pd16_err
    cp      '9' + 1
    jr      nc, pd16_err
    sub     '0'
    ld      c, a

    add     hl, hl
    jr      c, pd16_err
    ld      b, h
    ld      a, l
    add     hl, hl
    jr      c, pd16_err
    add     hl, hl
    jr      c, pd16_err
    add     a, l
    ld      l, a
    ld      a, b
    adc     a, h
    ld      h, a
    jr      c, pd16_err

    ld      b, 0
    add     hl, bc
    jr      c, pd16_err

    inc     de
    jr      pd16_loop
pd16_err:
    scf
    ret

; Parse null-terminated string at DE into 32-bit target_lba
parse_lba_u32:
    xor     a
    ld      (target_lba+0), a
    ld      (target_lba+1), a
    ld      (target_lba+2), a
    ld      (target_lba+3), a

    ld      a, (de)
    cp      '0'
    jr      nz, pl_check_hex_char
    inc     de
    ld      a, (de)
    cp      'x'
    jr      z, pl_is_hex
    cp      'X'
    jr      z, pl_is_hex
    dec     de

pl_check_hex_char:
    push    de
pl_scan:
    ld      a, (de)
    or      a
    jr      z, pl_is_dec
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
    inc     de
pl_hex_loop:
    ld      a, (de)
    or      a
    ret     z
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
    call    shl32_4
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
    ret     z
    cp      '0'
    jr      c, pl_err
    cp      '9' + 1
    jr      nc, pl_err
    sub     '0'
    push    af
    call    mul32_10
    pop     af
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

mul32_10:
    ld      hl, target_lba
    ld      de, tmp32
    ldi
    ldi
    ldi
    ldi

    xor     a
    ld      hl, target_lba
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)

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

print_hex32:
    push    hl
    inc     hl
    inc     hl
    inc     hl
    ld      a, (hl)
    call    fuji_print_hex8
    dec     hl
    ld      a, (hl)
    call    fuji_print_hex8
    dec     hl
    ld      a, (hl)
    call    fuji_print_hex8
    dec     hl
    ld      a, (hl)
    call    fuji_print_hex8
    pop     hl
    ret

; Print 16-bit integer in HL as unsigned decimal (suppressing leading zeros)
print_dec16:
    xor     a
    ld      (pd16_nz), a
    ld      bc, -10000
    call    pd16_digit
    ld      bc, -1000
    call    pd16_digit
    ld      bc, -100
    call    pd16_digit
    ld      bc, -10
    call    pd16_digit
    ld      a, l
    add     a, '0'
    call    fuji_putc
    ret

pd16_digit:
    ld      a, '0' - 1
pd16_sub:
    inc     a
    add     hl, bc
    jr      c, pd16_sub
    sbc     hl, bc
    cp      '0'
    jr      nz, pd16_emit
    ld      d, a
    ld      a, (pd16_nz)
    or      a
    ret     z                   ; Suppress leading zero
    ld      a, d
pd16_emit:
    ld      d, a
    ld      a, 1
    ld      (pd16_nz), a
    ld      a, d
    call    fuji_putc
    ret

; -----------------------------------------------------------------------------
; Messages
; -----------------------------------------------------------------------------
msg_usage:      DB  "Usage: SETMAP <lba> [len]", CR, 03h
msg_err_lba:    DB  "Error: Invalid starting LBA (hex or decimal)", CR, 03h
msg_err_len:    DB  "Error: Invalid byte length", CR, 03h
msg_pfx:        DB  "SD Map Mailbox set: Base LBA 0x", 03h
msg_len_pfx:    DB  ", Length: ", 03h
msg_sfx:        DB  " bytes.", CR, 03h

; -----------------------------------------------------------------------------
; Buffers
; -----------------------------------------------------------------------------
cmd_ptr:        DW  0
pd16_nz:        DB  0
target_lba:     DS  4
tmp32:          DS  4
arg_lba:        DS  32
arg_len:        DS  16

    INCLUDE "fuji_lib.inc"

    END     start
