; ==============================================================================
; FNGETH.S - Query and Display a Specific FujiNet Host Slot (0-7)
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   FNGETH <slot>
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
    jr      c, geth_usage

    ld      de, arg_buf
    call    fuji_parse_slot
    jr      c, geth_bad_slot
    ld      (target_slot), a

    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; Issue FUJI_CMD_GET_HOST_SLOTS (0xF4)
    ld      a, FUJI_CMD_GET_HOST_SLOTS
    call    fuji_cmd
    jr      c, geth_comm_err

    ; Check status
    call    fuji_get_error
    jr      c, geth_err

    ; Read 256-byte table
    ld      hl, hosts_buf
    ld      bc, HOST_TABLE_SIZE
    call    fuji_get_response
    jr      c, geth_comm_err

    ; Calculate slot offset: slot * 32
    ld      a, (target_slot)
    ld      l, a
    ld      h, 0
    add     hl, hl              ; * 2
    add     hl, hl              ; * 4
    add     hl, hl              ; * 8
    add     hl, hl              ; * 16
    add     hl, hl              ; * 32
    ld      de, hosts_buf
    add     hl, de              ; HL = pointer to targeted 32-byte entry

    ; Display "Host Slot X: "
    push    hl
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (target_slot)
    call    fuji_print_dec8
    ld      hl, msg_sep
    _DSPLY
    pop     hl

    ; Check if empty
    ld      a, (hl)
    or      a
    jr      nz, print_host
    ld      hl, msg_empty
    _DSPLY
    _EXIT

print_host:
    call    fuji_print_str
    call    fuji_newline
    _EXIT

geth_usage:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

geth_bad_slot:
    ld      hl, msg_bad_slot
    _DSPLY
    _EXIT

geth_comm_err:
    ld      a, 0FFh
    call    fuji_print_error
    _EXIT

geth_err:
    call    fuji_print_error
    _EXIT

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_usage:  DB  "Usage: FNGETH <slot 0-7>", CR, 03h
msg_bad_slot: DB "Error: Slot must be 0 to 7", CR, 03h
msg_pfx:    DB  "Host Slot ", 03h
msg_sep:    DB  ": ", 03h
msg_empty:  DB  "(empty)", CR, 03h

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
target_slot:    DB  0
arg_buf:        DS  16
hosts_buf:      DS  HOST_TABLE_SIZE

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
