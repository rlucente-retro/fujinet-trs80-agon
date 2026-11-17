; ==============================================================================
; FNHOSTS.S - Enumerate and Display FujiNet Host Slots (0-7)
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   FNHOSTS
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; Issue FUJI_CMD_GET_HOST_SLOTS (0xF4)
    ld      a, FUJI_CMD_GET_HOST_SLOTS
    call    fuji_cmd
    jr      c, hosts_comm_err

    ; Check command execution status
    call    fuji_get_error
    jr      c, hosts_err

    ; Read 256-byte Host Slot Table (8 slots * 32 bytes)
    ld      hl, hosts_buf
    ld      bc, HOST_TABLE_SIZE
    call    fuji_get_response
    jr      c, hosts_comm_err

    ; Display header
    ld      hl, msg_hdr
    _DSPLY
    ld      hl, msg_div
    _DSPLY

    ; Loop through all 8 host slots
    ld      b, 0                ; B = slot index (0..7)
    ld      ix, hosts_buf       ; IX = pointer to current slot entry

hosts_loop:
    push    bc

    ; Display "Slot X:  "
    ld      hl, msg_slot_prefix
    _DSPLY
    ld      a, b
    call    fuji_print_dec8
    ld      hl, msg_slot_suffix
    _DSPLY

    ; Check if slot entry is empty (first byte == 0)
    ld      a, (ix+0)
    or      a
    jr      nz, print_host_name

    ; Slot is empty
    ld      hl, msg_empty
    _DSPLY
    jr      next_slot

print_host_name:
    push    ix
    pop     hl                  ; HL = pointer to hostname string
    call    fuji_print_str
    call    fuji_newline

next_slot:
    ; Advance IX by 32 bytes (HOST_SLOT_SIZE)
    ld      de, HOST_SLOT_SIZE
    add     ix, de

    pop     bc
    inc     b
    ld      a, b
    cp      MAX_HOST_SLOTS      ; 8 slots
    jr      c, hosts_loop

    ld      hl, msg_div
    _DSPLY
    _EXIT

hosts_comm_err:
    ld      a, 0FFh
    call    fuji_print_error
    _EXIT

hosts_err:
    call    fuji_print_error
    _EXIT

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_hdr:            DB  "FujiNet Host Slot Table", CR
msg_div:            DB  "--------------------------------------------------", CR
msg_slot_prefix:    DB  "Slot ", 03h
msg_slot_suffix:    DB  ":  ", 03h
msg_empty:          DB  "(empty)", CR

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
hosts_buf:          DS  HOST_TABLE_SIZE

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
