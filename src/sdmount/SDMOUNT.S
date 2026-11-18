; ==============================================================================
; SDMOUNT.S - Mount or Unmount Local Agon MicroSD Card Image (SPI0)
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Canonical LS-DOS Syntax:
;   SDMOUNT
;     Interactive Menu Mode: Displays numbered list of available disk images
;     from the microSD card mapping table (sdmap.txt).
;
;   SDMOUNT :<drv> (IMAGE="<path>" | INDEX=<n> | LBA=<lba>, MODE=RW|RO)
;   SDMOUNT (DRV=<drv>, FILE="<path>", MODE=RW)
;   SDMOUNT (D=<drv>, I=<n>)
;   SDMOUNT :<drv> (OFFLINE | 0)
;
; Positional Shorthand:
;   SDMOUNT :<drv> ["<path>" | <index> | <lba>] [mode 1-3]
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; Reset state
    xor     a
    ld      (matched_path), a
    ld      (have_target), a
    ld      (have_drv), a
    ld      a, FUJI_MODE_READWRITE
    ld      (target_mode), a

    ; Skip command name
    ld      hl, INBUF$
    call    fuji_skip_cmd

    ; Check if command line is empty -> Menu mode
    ld      a, (hl)
    cp      CR
    jp      z, sdmnt_menu_mode
    or      a
    jp      z, sdmnt_menu_mode

    ; Check if there is a '(' anywhere in command line
    push    hl
sdmnt_chk_paren:
    ld      a, (hl)
    cp      CR
    jr      z, sdmnt_not_paren
    or      a
    jr      z, sdmnt_not_paren
    cp      '('
    jr      z, sdmnt_is_paren
    inc     hl
    jr      sdmnt_chk_paren

sdmnt_not_paren:
    pop     hl
    jp      sdmnt_parse_positional

sdmnt_is_paren:
    pop     hl
    ; Parenthesized parameter syntax: check if drive is before '('
    call    skip_spaces
    cp      '('
    jr      z, sdmnt_enter_paren

    ; Parse drive token before '(' (e.g. ":1" or "1")
    ld      de, arg_buf
    call    fuji_parse_arg
    jr      c, sdmnt_enter_paren
    ld      de, arg_buf
    call    fuji_parse_slot
    jr      c, sdmnt_enter_paren
    ld      (target_drv), a
    ld      a, 1
    ld      (have_drv), a

sdmnt_enter_paren:
    ; Advance HL until '(' is passed
sdmnt_fnd_op:
    ld      a, (hl)
    cp      CR
    jp      z, sdmnt_eval_params
    or      a
    jp      z, sdmnt_eval_params
    cp      '('
    jr      z, sdmnt_past_op
    inc     hl
    jr      sdmnt_fnd_op
sdmnt_past_op:
    inc     hl

    ; Loop through parameters inside (...)
sdmnt_kw_loop:
    call    skip_spaces_commas
    cp      ')'
    jp      z, sdmnt_eval_params
    cp      CR
    jp      z, sdmnt_eval_params
    or      a
    jp      z, sdmnt_eval_params

    ; Extract keyword into kw_buf
    ld      de, kw_buf
sdmnt_get_kw:
    ld      a, (hl)
    cp      '='
    jr      z, sdmnt_kw_val
    cp      ','
    jr      z, sdmnt_kw_term
    cp      ')'
    jr      z, sdmnt_kw_term
    cp      ' '
    jr      z, sdmnt_kw_term
    cp      09h
    jr      z, sdmnt_kw_term
    cp      CR
    jr      z, sdmnt_kw_term
    or      a
    jr      z, sdmnt_kw_term

    call    to_upper
    ld      (de), a
    inc     de
    inc     hl
    jr      sdmnt_get_kw

sdmnt_kw_term:
    xor     a
    ld      (de), a             ; Null-terminate standalone keyword
    jr      sdmnt_dispatch_kw

sdmnt_kw_val:
    xor     a
    ld      (de), a             ; Null-terminate keyword
    inc     hl                  ; Skip '='
    call    skip_spaces
    ld      de, val_buf
    call    fuji_parse_arg
    ; Fall through to dispatch

sdmnt_dispatch_kw:
    ; 1. DRV / D / DRIVE
    ld      de, kw_buf
    ld      hl, kw_drv
    call    str_cmp_ci
    jr      z, skw_set_drv
    ld      hl, kw_d
    call    str_cmp_ci
    jr      z, skw_set_drv
    ld      hl, kw_drive
    call    str_cmp_ci
    jr      z, skw_set_drv

    ; 2. IMAGE / FILE / PATH / IMG / INDEX / I / IDX / LBA
    ld      hl, kw_image
    call    str_cmp_ci
    jr      z, skw_set_image
    ld      hl, kw_file
    call    str_cmp_ci
    jr      z, skw_set_image
    ld      hl, kw_path
    call    str_cmp_ci
    jr      z, skw_set_image
    ld      hl, kw_img
    call    str_cmp_ci
    jr      z, skw_set_image
    ld      hl, kw_index
    call    str_cmp_ci
    jr      z, skw_set_image
    ld      hl, kw_i
    call    str_cmp_ci
    jr      z, skw_set_image
    ld      hl, kw_idx
    call    str_cmp_ci
    jr      z, skw_set_image
    ld      hl, kw_lba
    call    str_cmp_ci
    jr      z, skw_set_image

    ; 3. MODE / M
    ld      hl, kw_mode
    call    str_cmp_ci
    jr      z, skw_set_mode
    ld      hl, kw_m
    call    str_cmp_ci
    jr      z, skw_set_mode

    ; 4. Standalone flags
    ld      hl, kw_offline
    call    str_cmp_ci
    jr      z, skw_set_unmount
    ld      hl, kw_unmount
    call    str_cmp_ci
    jr      z, skw_set_unmount
    ld      hl, kw_zero
    call    str_cmp_ci
    jr      z, skw_set_unmount
    ld      hl, kw_menu
    call    str_cmp_ci
    jp      z, sdmnt_menu_mode
    ld      hl, kw_list
    call    str_cmp_ci
    jp      z, sdmnt_menu_mode

    jp      sdmnt_usage

skw_set_drv:
    ld      de, val_buf
    call    fuji_parse_slot
    jp      c, sdmnt_bad_drv
    ld      (target_drv), a
    ld      a, 1
    ld      (have_drv), a
    jp      sdmnt_kw_loop

skw_set_image:
    ld      hl, val_buf
    ld      de, arg_target
    ld      bc, 64
    ldir
    ld      a, 1
    ld      (have_target), a
    jp      sdmnt_kw_loop

skw_set_mode:
    ld      a, (val_buf)
    cp      '1'
    jr      z, skw_mode_ro
    cp      '3'
    jr      z, skw_mode_rw
    cp      'R'
    jr      nz, sdmnt_bad_mode
    ld      a, (val_buf+1)
    cp      'O'
    jr      z, skw_mode_ro
    cp      'o'
    jr      z, skw_mode_ro
    cp      'W'
    jr      z, skw_mode_rw
    cp      'w'
    jr      z, skw_mode_rw
    jp      sdmnt_bad_mode
skw_mode_ro:
    ld      a, FUJI_MODE_READ
    ld      (target_mode), a
    jp      sdmnt_kw_loop
skw_mode_rw:
    ld      a, FUJI_MODE_READWRITE
    ld      (target_mode), a
    jp      sdmnt_kw_loop

skw_set_unmount:
    ld      a, '0'
    ld      (arg_target), a
    xor     a
    ld      (arg_target+1), a
    ld      a, 1
    ld      (have_target), a
    jp      sdmnt_kw_loop

sdmnt_eval_params:
    ld      a, (have_target)
    or      a
    jr      z, sdmnt_no_target_eval
    ld      a, (have_drv)
    or      a
    jp      z, sdmnt_need_drv
    jp      sdmnt_process_target

sdmnt_no_target_eval:
    ld      a, (have_drv)
    or      a
    jp      nz, sdmnt_usage_drv
    jp      sdmnt_menu_mode

; -----------------------------------------------------------------------------
; Positional Argument Parsing
; -----------------------------------------------------------------------------
sdmnt_parse_positional:
    ld      de, arg_buf
    call    fuji_parse_arg
    jp      c, sdmnt_menu_mode

    ld      de, arg_buf
    call    fuji_parse_slot
    jp      c, sdmnt_bad_drv
    ld      (target_drv), a
    ld      a, 1
    ld      (have_drv), a

    ld      de, arg_target
    call    fuji_parse_arg
    jp      c, sdmnt_usage_drv
    ld      a, 1
    ld      (have_target), a

    ld      de, arg_buf
    call    fuji_parse_arg
    jr      c, sdmnt_process_target

    ld      a, (arg_buf)
    cp      '1'
    jr      z, pos_mode_ro
    cp      '3'
    jr      z, pos_mode_rw
    cp      'R'
    jr      nz, sdmnt_bad_mode
    ld      a, (arg_buf+1)
    cp      'O'
    jr      z, pos_mode_ro
    cp      'o'
    jr      z, pos_mode_ro
    cp      'W'
    jr      z, pos_mode_rw
    cp      'w'
    jr      z, pos_mode_rw
    jp      sdmnt_bad_mode
pos_mode_ro:
    ld      a, FUJI_MODE_READ
    ld      (target_mode), a
    jr      sdmnt_process_target
pos_mode_rw:
    ld      a, FUJI_MODE_READWRITE
    ld      (target_mode), a

; -----------------------------------------------------------------------------
; Process Target (Path, Index, LBA, or 0)
; -----------------------------------------------------------------------------
sdmnt_process_target:
    ; Check if target is "0" (unmount request)
    ld      a, (arg_target)
    cp      '0'
    jr      nz, sdmnt_chk_catalog
    ld      a, (arg_target + 1)
    or      a
    jp      z, sdmnt_unmount_req

sdmnt_chk_catalog:
    ; Check if argument is a small decimal index (1 to 255)
    ld      de, arg_target
    call    parse_dec16
    jr      c, sdmnt_try_path_match

    ld      a, h
    or      a
    jr      nz, sdmnt_try_path_match
    ld      a, l
    or      a
    jr      z, sdmnt_try_path_match
    ld      (match_idx), a

    call    map_stream_init
    jr      c, sdmnt_try_lba_parse

    ld      b, 1                ; Line index starts at 1
sdmnt_idx_loop:
    push    bc
    call    map_get_line
    jr      c, sdmnt_idx_eof
    call    map_parse_line
    jr      c, sdmnt_idx_next

    pop     bc
    push    bc
    ld      a, (match_idx)
    cp      b
    jr      z, sdmnt_match_found

sdmnt_idx_next:
    pop     bc
    inc     b
    jr      sdmnt_idx_loop

sdmnt_idx_eof:
    pop     bc

sdmnt_try_path_match:
    call    map_stream_init
    jr      c, sdmnt_try_lba_parse

sdmnt_path_loop:
    call    map_get_line
    jr      c, sdmnt_try_lba_parse
    call    map_parse_line
    jr      c, sdmnt_path_loop

    ld      hl, map_entry_path
    ld      de, arg_target
    call    str_suffix_match
    jr      nz, sdmnt_path_loop

sdmnt_match_found:
    ld      hl, map_entry_lba
    ld      de, target_lba
    ldi
    ldi
    ldi
    ldi

    ld      hl, map_entry_path
    ld      de, matched_path
    ld      b, 62
sdmnt_cpy_mp:
    ld      a, (hl)
    ld      (de), a
    or      a
    jr      z, sdmnt_do_mount
    inc     hl
    inc     de
    djnz    sdmnt_cpy_mp
    xor     a
    ld      (de), a
    jr      sdmnt_do_mount

sdmnt_try_lba_parse:
    ld      de, arg_target
    call    parse_lba_u32
    jr      c, sdmnt_target_not_found
    xor     a
    ld      (matched_path), a
    jr      sdmnt_do_mount

sdmnt_target_not_found:
    call    map_stream_init
    jr      c, sdmnt_err_no_map
    ld      hl, msg_err_not_found
    _DSPLY
    _EXIT

sdmnt_err_no_map:
    ld      hl, msg_bad_lba
    _DSPLY
    _EXIT

sdmnt_unmount_req:
    xor     a
    ld      (target_lba+0), a
    ld      (target_lba+1), a
    ld      (target_lba+2), a
    ld      (target_lba+3), a
    jp      sdmnt_do_mount

; -----------------------------------------------------------------------------
; Mount / Unmount Action
; -----------------------------------------------------------------------------
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
    jp      z, sdmnt_unmount

    ; Store target_lba into sddvr_base_lba[target_drv]
    ld      a, (target_drv)
    rlca
    rlca
    ld      e, a
    ld      d, 0
    ld      hl, sddvr_base_lba
    add     hl, de
    ex      de, hl
    ld      hl, target_lba
    ldi
    ldi
    ldi
    ldi

    ; Set drv_target_table[target_drv] = DRV_TARGET_SD (0)
    ld      a, (target_drv)
    ld      e, a
    ld      d, 0
    ld      hl, drv_target_table
    add     hl, de
    ld      (hl), DRV_TARGET_SD

    ; Calculate DCT address
    ld      a, (target_drv)
    or      a
    jr      z, sdmnt_update_dct0

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
    ld      (target_dct), hl

    ex      de, hl
    ld      hl, DCT0$
    ld      bc, DCT_SIZE
    ldir

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
    ld      c, a

    ld      ix, (target_dct)
    ld      a, (ix + 4)
    and     0F0h
    or      c
    ld      (ix + 4), a

    xor     a
    ld      (ix + 5), a

    ld      a, (target_mode)
    cp      FUJI_MODE_READ
    jr      nz, sdmnt_rw
    set     7, (ix + 3)
    jr      sdmnt_display

sdmnt_rw:
    res     7, (ix + 3)
    jr      sdmnt_display

sdmnt_update_dct0:
    ld      ix, DCT0$
    ld      a, (target_mode)
    cp      FUJI_MODE_READ
    jr      nz, sdmnt_rw0
    set     7, (ix + 3)
    jr      sdmnt_display
sdmnt_rw0:
    res     7, (ix + 3)

sdmnt_display:
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (target_drv)
    call    fuji_print_dec8
    ld      hl, msg_mid
    _DSPLY

    ld      a, (matched_path)
    or      a
    jr      z, sdmnt_disp_lba_only

    ld      hl, msg_colon_quote
    _DSPLY
    ld      hl, matched_path
    call    fuji_print_str
    ld      hl, msg_quote_sp
    _DSPLY

sdmnt_disp_lba_only:
    ld      hl, msg_lba_pfx
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
; Unmount Action
; -----------------------------------------------------------------------------
sdmnt_unmount:
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

    ld      a, (target_drv)
    or      a
    jr      z, sdmnt_unm_disp

    ld      l, a
    ld      h, 0
    add     hl, hl
    ld      d, h
    ld      e, l
    add     hl, hl
    add     hl, hl
    add     hl, de
    ld      de, DCT0$
    add     hl, de
    xor     a
    ld      (hl), a

sdmnt_unm_disp:
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (target_drv)
    call    fuji_print_dec8
    ld      hl, msg_unm_done
    _DSPLY
    _EXIT

; -----------------------------------------------------------------------------
; Interactive Menu Mode
; -----------------------------------------------------------------------------
sdmnt_menu_mode:
    call    map_stream_init
    jr      c, sdmnt_menu_no_map

    ld      hl, msg_menu_hdr
    _DSPLY

    ld      b, 1
    xor     a
    ld      (screen_lines), a

sdmnt_menu_loop:
    push    bc
    call    map_get_line
    jr      c, sdmnt_menu_done

    call    map_parse_line
    jr      c, sdmnt_menu_skip

    pop     bc
    push    bc
    ld      a, b
    cp      10
    jr      nc, smm_pr_idx
    ld      hl, msg_one_sp
    _DSPLY
    pop     bc
    push    bc
    ld      a, b
smm_pr_idx:
    call    fuji_print_dec8

    ld      hl, msg_hex_pfx
    _DSPLY
    ld      hl, map_entry_lba
    call    print_hex32

    ld      hl, msg_two_sp
    _DSPLY
    ld      hl, map_entry_path
    call    fuji_print_str
    call    fuji_newline

    ld      a, (screen_lines)
    inc     a
    ld      (screen_lines), a
    cp      18
    jr      c, sdmnt_menu_skip

    ld      hl, msg_press_key
    _DSPLY
    _KEY
    call    fuji_newline
    xor     a
    ld      (screen_lines), a

sdmnt_menu_skip:
    pop     bc
    inc     b
    jr      sdmnt_menu_loop

sdmnt_menu_done:
    pop     bc
    ld      hl, msg_menu_footer
    _DSPLY
    _EXIT

sdmnt_menu_no_map:
    ld      hl, msg_no_map_active
    _DSPLY
    ld      hl, msg_usage
    _DSPLY
    _EXIT

sdmnt_usage:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

sdmnt_usage_drv:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

sdmnt_need_drv:
    ld      hl, msg_need_drv
    _DSPLY
    _EXIT

sdmnt_bad_drv:
    ld      hl, msg_bad_drv
    _DSPLY
    _EXIT

sdmnt_bad_mode:
    ld      hl, msg_bad_mode
    _DSPLY
    _EXIT

; -----------------------------------------------------------------------------
; Helpers
; -----------------------------------------------------------------------------
skip_spaces:
    ld      a, (hl)
    cp      ' '
    ret     nz
    inc     hl
    jr      skip_spaces

skip_spaces_commas:
    ld      a, (hl)
    cp      ' '
    jr      z, ssc_inc
    cp      09h
    jr      z, ssc_inc
    cp      ','
    jr      z, ssc_inc
    ret
ssc_inc:
    inc     hl
    jr      skip_spaces_commas

to_upper:
    cp      'a'
    ret     c
    cp      'z' + 1
    ret     nc
    sub     20h
    ret

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

; -----------------------------------------------------------------------------
; Messages & Parameter Keywords
; -----------------------------------------------------------------------------
msg_usage:
    DB  "Usage: SDMOUNT :<drv> (IMAGE=\"<path>\", MODE=RW)", CR
    DB  "   or: SDMOUNT (DRV=<drv>, FILE=\"<path>\", MODE=RW)", CR
    DB  "   or: SDMOUNT (D=<drv>, I=<index>)", CR
    DB  "   or: SDMOUNT (Menu)", CR, 03h

msg_need_drv:       DB  "Error: Drive must be specified (e.g. :1 or DRV=1)", CR, 03h
msg_bad_drv:        DB  "Error: Drive must be 0 to 7", CR, 03h
msg_bad_lba:        DB  "Error: Invalid starting LBA (hex or decimal)", CR, 03h
msg_bad_mode:       DB  "Error: Mode must be 1/RO or 3/RW", CR, 03h
msg_err_not_found:  DB  "Error: Image or LBA not found in SD mapping table", CR, 03h
msg_no_map_active:  DB  "No SD mapping table active (use raw LBA or run *trs80 map in MOS)", CR, 03h

msg_pfx:            DB  "Drive :", 03h
msg_mid:            DB  " mounted on Local SD", 03h
msg_colon_quote:    DB  ": \"", 03h
msg_quote_sp:       DB  "\" ", 03h
msg_lba_pfx:        DB  "(Base LBA: 0x", 03h
msg_mode_pfx:       DB  ", Mode: ", 03h
msg_mode_ro:        DB  "RO", 03h
msg_mode_rw:        DB  "RW", 03h
msg_sfx:            DB  ").", CR, 03h
msg_unm_done:       DB  " unmounted.", CR, 03h

msg_menu_hdr:
    DB  "Available MicroSD Disk Images:", CR
    DB  " #   Base LBA    Path", CR
    DB  "--- ----------- ----------------------------------------", CR, 03h

msg_menu_footer:
    DB  "--------------------------------------------------------", CR
    DB  "To mount: SDMOUNT :<drv> (I=<#>) or SDMOUNT :<drv> (IMAGE=\"<path>\")", CR, 03h

msg_press_key:      DB  "[Press any key for next page...]", 03h
msg_hex_pfx:        DB  "  0x", 03h
msg_one_sp:         DB  " ", 03h
msg_two_sp:         DB  "  ", 03h

; Parameter Keyword Strings
kw_drv:             DB  "DRV", 0
kw_d:               DB  "D", 0
kw_drive:           DB  "DRIVE", 0
kw_image:           DB  "IMAGE", 0
kw_file:            DB  "FILE", 0
kw_path:            DB  "PATH", 0
kw_img:             DB  "IMG", 0
kw_index:           DB  "INDEX", 0
kw_i:               DB  "I", 0
kw_idx:             DB  "IDX", 0
kw_lba:             DB  "LBA", 0
kw_mode:            DB  "MODE", 0
kw_m:               DB  "M", 0
kw_offline:         DB  "OFFLINE", 0
kw_unmount:         DB  "UNMOUNT", 0
kw_zero:            DB  "0", 0
kw_menu:            DB  "MENU", 0
kw_list:            DB  "LIST", 0

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
target_drv:         DB  0
target_mode:        DB  0
target_dct:         DW  0
match_idx:          DB  0
screen_lines:       DB  0
have_target:        DB  0
have_drv:           DB  0
target_lba:         DS  4
tmp32:              DS  4
arg_buf:            DS  32
arg_target:         DS  64
matched_path:       DS  64
kw_buf:             DS  16
val_buf:            DS  64

; -----------------------------------------------------------------------------
; Libraries
; -----------------------------------------------------------------------------
    INCLUDE "sd_lib.inc"
    INCLUDE "fuji_lib.inc"

    END     start
