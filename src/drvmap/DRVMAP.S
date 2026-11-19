; ==============================================================================
; DRVMAP.S - Display Unified Drive Mapping Table (Local SD, DriveWire, MEMDISK)
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   DRVMAP
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; Display table header
    ld      hl, msg_hdr1
    _DSPLY
    ld      hl, msg_hdr2
    _DSPLY
    ld      hl, msg_hdr3
    _DSPLY

    ; Iterate through logical drives :0 to :7
    xor     a
    ld      (cur_drv), a

drv_loop:
    ; Calculate DCT address: DCTn$ = DCT0$ + (cur_drv * DCT_SIZE)
    ld      a, (cur_drv)
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
    ld      (cur_dct), hl

    ; Check if drive is active: (DCTn$) == 0xC3 (JP opcode)
    ld      a, (hl)
    cp      0C3h
    jr      z, drv_active

    ; Drive is offline / unmounted
    ld      hl, msg_drv_pfx
    _DSPLY
    ld      a, (cur_drv)
    call    fuji_print_dec8
    ld      hl, msg_offline
    _DSPLY
    jr      drv_next

drv_active:
    ; Print " :<drv>   "
    ld      hl, msg_drv_pfx
    _DSPLY
    ld      a, (cur_drv)
    call    fuji_print_dec8
    ld      hl, msg_drv_sp
    _DSPLY

    ; Look up target from drv_target_table (0x0E76 + cur_drv)
    ld      a, (cur_drv)
    ld      e, a
    ld      d, 0
    ld      hl, drv_target_table
    add     hl, de
    ld      a, (hl)
    ld      (cur_target), a

    cp      DRV_TARGET_DW
    jr      z, drv_is_dw
    cp      DRV_TARGET_SD
    jr      z, drv_is_sd
    cp      DRV_TARGET_MEMDISK
    jr      z, drv_is_mem
    jr      drv_is_unk

; -----------------------------------------------------------------------------
; DriveWire Target (:n routed via UART1 to FujiNet)
; -----------------------------------------------------------------------------
drv_is_dw:
    ld      hl, msg_tgt_dw
    _DSPLY

    ; Check Mode: bit 7 of DCTn$+3 (1 = RO, 0 = RW)
    call    print_dct_mode

    ; Check Container Format: dw_offset[cur_drv] (0x0F78 + cur_drv)
    ld      a, (cur_drv)
    ld      e, a
    ld      d, 0
    ld      hl, dw_offset
    add     hl, de
    ld      a, (hl)
    or      a
    jr      z, dw_fmt_jv1
    ld      hl, msg_fmt_dd
    _DSPLY
    jr      dw_query_file
dw_fmt_jv1:
    ld      hl, msg_fmt_jv
    _DSPLY

dw_query_file:
    ; Print "Slot <drv>: \""
    ld      hl, msg_slot_pfx
    _DSPLY
    ld      a, (cur_drv)
    call    fuji_print_dec8
    ld      hl, msg_slot_colon
    _DSPLY

    ; Query FujiNet device slot via FUJI_CMD_GET_DEV_FILE (0xDA)
    ld      a, (cur_drv)
    ld      (query_slot), a
    ld      a, FUJI_CMD_GET_DEV_FILE
    ld      hl, query_slot
    ld      bc, 1
    call    fuji_cmd_params
    jr      c, dw_file_fail

    call    fuji_get_error
    jr      c, dw_file_fail

    ld      hl, path_buf
    ld      bc, MAX_FILENAME_LEN
    call    fuji_get_response
    jr      c, dw_file_fail

    ; Check if path is empty
    ld      a, (path_buf)
    or      a
    jr      z, dw_file_empty

    ld      hl, path_buf
    call    fuji_print_str
    ld      hl, msg_quote_cr
    _DSPLY
    jr      drv_next

dw_file_empty:
    ld      hl, msg_empty_q
    _DSPLY
    jr      drv_next

dw_file_fail:
    ld      hl, msg_unassigned
    _DSPLY
    jr      drv_next

; -----------------------------------------------------------------------------
; Local SD Target (:n routed via SPI0 to on-board microSD)
; -----------------------------------------------------------------------------
drv_is_sd:
    ld      hl, msg_tgt_sd
    _DSPLY

    ; Check Mode: bit 7 of DCTn$+3
    call    print_dct_mode

    ; Format column
    ld      hl, msg_fmt_sd
    _DSPLY

    ; Read 32-bit Base LBA from sddvr_base_lba (0xF408 + cur_drv * 4)
    ld      a, (cur_drv)
    rlca
    rlca                        ; A = cur_drv * 4
    ld      e, a
    ld      d, 0
    ld      hl, sddvr_base_lba
    add     hl, de
    ld      (cur_lba_ptr), hl

    ; Check if Base LBA is zero (unassigned)
    ld      a, (hl)
    inc     hl
    or      (hl)
    inc     hl
    or      (hl)
    inc     hl
    or      (hl)
    jr      z, sd_lba_zero

    ; Try resolving path from sdmap.txt
    call    map_stream_init
    jr      c, sd_print_raw_lba

sd_search_loop:
    call    map_get_line
    jr      c, sd_print_raw_lba
    call    map_parse_line
    jr      c, sd_search_loop

    ; Compare 4 bytes of map_entry_lba with (cur_lba_ptr)
    ld      hl, (cur_lba_ptr)
    ld      de, map_entry_lba
    ld      b, 4
sd_cmp_lba:
    ld      a, (de)
    cp      (hl)
    jr      nz, sd_search_loop
    inc     de
    inc     hl
    djnz    sd_cmp_lba

    ; Match found in sdmap.txt! Print "\"<path>\" (Base LBA: 0xXXXXXXXX)"
    ld      hl, msg_quote
    _DSPLY
    ld      hl, map_entry_path
    call    fuji_print_str
    ld      hl, msg_quote_lba_pfx
    _DSPLY
    ld      hl, (cur_lba_ptr)
    call    print_hex32
    ld      hl, msg_close_paren_cr
    _DSPLY
    jr      drv_next

sd_print_raw_lba:
    ; Print Base LBA: 0xXXXXXXXX
    ld      hl, msg_lba_pfx
    _DSPLY
    ld      hl, (cur_lba_ptr)
    call    print_hex32
    call    fuji_newline
    jr      drv_next

sd_lba_zero:
    ld      hl, msg_no_lba
    _DSPLY
    jr      drv_next

; -----------------------------------------------------------------------------
; MEMDISK Target (:n routed to High SRAM Banks 1..7)
; -----------------------------------------------------------------------------
drv_is_mem:
    ld      hl, msg_tgt_mem
    _DSPLY
    jr      drv_next

; -----------------------------------------------------------------------------
; Unknown Target Fallback
; -----------------------------------------------------------------------------
drv_is_unk:
    ld      hl, msg_tgt_unk
    _DSPLY
    ld      a, (cur_target)
    call    fuji_print_dec8
    call    fuji_newline

drv_next:
    ; Increment drive index
    ld      a, (cur_drv)
    inc     a
    ld      (cur_drv), a
    cp      8
    jp      c, drv_loop

    ; Display footer line
    ld      hl, msg_div
    _DSPLY
    _EXIT

; -----------------------------------------------------------------------------
; Subroutines
; -----------------------------------------------------------------------------

; Print "RO   " or "RW   " based on bit 7 of DCTn$+3
print_dct_mode:
    ld      ix, (cur_dct)
    bit     7, (ix + 3)
    jr      nz, pdm_ro
    ld      hl, msg_mode_rw
    _DSPLY
    ret
pdm_ro:
    ld      hl, msg_mode_ro
    _DSPLY
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
msg_hdr1:       DB  "TRSDOS Unified Drive Mapping:", CR
msg_hdr2:       DB  "Drive Target     Mode Format   Details", CR
msg_hdr3:       DB  "----- ---------- ---- -------- ----------------------------------------", CR

msg_div:
    DB  "------------------------------------------------------------------------", CR, 03h

msg_drv_pfx:    DB  " :", 03h
msg_drv_sp:     DB  "   ", 03h
msg_offline:    DB  "   (Offline)", CR, 03h

msg_tgt_dw:     DB  "DriveWire  ", 03h
msg_tgt_sd:     DB  "Local SD   ", 03h
msg_tgt_mem:    DB  "MEMDISK    RW   RAMDisk  High SRAM (Bank 1)", CR, 03h
msg_tgt_unk:    DB  "Unknown    --   --       Target ID: ", 03h

msg_mode_ro:    DB  "RO   ", 03h
msg_mode_rw:    DB  "RW   ", 03h

msg_fmt_dd:     DB  "DiskDISK ", 03h
msg_fmt_jv:     DB  "JV1      ", 03h
msg_fmt_sd:     DB  "SPI0     ", 03h

msg_slot_pfx:   DB  "Slot ", 03h
msg_slot_colon: DB  ": \"", 03h
msg_quote_cr:   DB  "\"", CR, 03h
msg_empty_q:    DB  "(empty)\"", CR, 03h
msg_unassigned: DB  "(unassigned)\"", CR, 03h

msg_lba_pfx:    DB  "Base LBA: 0x", 03h
msg_no_lba:     DB  "(No Base LBA assigned)", CR, 03h
msg_quote:      DB  "\"", 03h
msg_quote_lba_pfx:  DB  "\" (Base LBA: 0x", 03h
msg_close_paren_cr: DB  ")", CR, 03h

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
cur_drv:        DB  0
cur_target:     DB  0
cur_dct:        DW  0
cur_lba_ptr:    DW  0
query_slot:     DB  0
path_buf:       DS  MAX_FILENAME_LEN

; -----------------------------------------------------------------------------
; Libraries
; -----------------------------------------------------------------------------
    INCLUDE "sd_lib.inc"
    INCLUDE "fuji_lib.inc"

    END     start
