; ==============================================================================
; FNMOUNT.S - Command FujiNet to Mount a Remote Host or TNFS Server (0-7)
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   FNMOUNT <slot>
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
    jr      c, mnt_usage

    ld      de, arg_slot
    call    fuji_parse_slot
    jr      c, mnt_usage
    ld      (mount_slot), a

    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; Issue FUJI_CMD_MOUNT_HOST (0xF9) with 1-byte slot parameter
    ld      a, FUJI_CMD_MOUNT_HOST
    ld      hl, mount_slot
    ld      bc, 1
    call    fuji_cmd_params
    jr      c, mnt_comm_err

    ; Check command execution status
    call    fuji_get_error
    jr      c, mnt_err

    ; Display success confirmation
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (mount_slot)
    call    fuji_print_dec8
    ld      hl, msg_sfx
    _DSPLY
    _EXIT

mnt_usage:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

mnt_comm_err:
    ld      a, 0FFh
    call    fuji_print_error
    _EXIT

mnt_err:
    call    fuji_print_error
    _EXIT

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_usage:  DB  "Usage: FNMOUNT <slot 0-7>", CR
msg_pfx:    DB  "Host Slot ", 03h
msg_sfx:    DB  " mounted successfully.", CR

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
mount_slot: DB  0
arg_slot:   DS  16

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
