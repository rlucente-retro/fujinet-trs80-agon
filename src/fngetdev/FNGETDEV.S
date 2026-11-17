; ==============================================================================
; FNGETDEV.S - Query Full Path of Disk Image in Virtual Device Slot (0-3)
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   FNGETDEV <slot>
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; Parse <slot> from command line buffer INBUF$
    ld      hl, INBUF$
    call    fuji_skip_cmd
    ld      de, arg_slot
    call    fuji_parse_arg
    jr      c, getdev_usage

    ld      de, arg_slot
    call    fuji_parse_slot
    jr      c, getdev_bad_slot
    cp      MAX_DEV_SLOTS_STAGE1
    jr      nc, getdev_bad_slot
    ld      (target_slot), a

    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; Issue FUJI_CMD_GET_DEV_FILE (0xDA) with 1-byte slot parameter
    ld      a, FUJI_CMD_GET_DEV_FILE
    ld      hl, target_slot
    ld      bc, 1
    call    fuji_cmd_params
    jr      c, getdev_comm_err

    ; Check command execution status
    call    fuji_get_error
    jr      c, getdev_err

    ; Read 256-byte pathname response
    ld      hl, path_buf
    ld      bc, MAX_FILENAME_LEN
    call    fuji_get_response
    jr      c, getdev_comm_err

    ; Display "Device Slot <target_slot>: "
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (target_slot)
    call    fuji_print_dec8
    ld      hl, msg_colon_sp
    _DSPLY

    ; Check if path is empty
    ld      a, (path_buf)
    or      a
    jr      z, getdev_empty

    ; Print path string
    ld      hl, path_buf
    call    fuji_print_str
    call    fuji_newline
    _EXIT

getdev_empty:
    ld      hl, msg_empty
    _DSPLY
    _EXIT

getdev_usage:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

getdev_bad_slot:
    ld      hl, msg_bad_slot
    _DSPLY
    _EXIT

getdev_comm_err:
    ld      a, 0FFh
    call    fuji_print_error
    _EXIT

getdev_err:
    call    fuji_print_error
    _EXIT

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_usage:      DB  "Usage: FNGETDEV <slot 0-3>", CR, 03h
msg_bad_slot:   DB  "Error: Slot must be 0 to 3", CR, 03h
msg_pfx:        DB  "Device Slot ", 03h
msg_colon_sp:   DB  ": ", 03h
msg_empty:      DB  "(empty)", CR, 03h

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
target_slot:    DB  0
arg_slot:       DS  16
path_buf:       DS  MAX_FILENAME_LEN

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
