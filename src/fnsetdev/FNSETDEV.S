; ==============================================================================
; FNSETDEV.S - Assign Disk Image Path and Host to Virtual Device Slot (0-3)
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   FNSETDEV <dev_slot 0-3> <host_slot 0-7> <mode 1-3> "<path>"
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; Parse arguments from INBUF$
    ld      hl, INBUF$
    call    fuji_skip_cmd

    ; 1. Parse dev_slot
    ld      de, arg_buf
    call    fuji_parse_arg
    jr      c, setdev_usage

    ld      de, arg_buf
    call    fuji_parse_slot
    jr      c, setdev_bad_dev
    cp      MAX_DEV_SLOTS_STAGE1
    jr      nc, setdev_bad_dev
    ld      (target_dev_slot), a

    ; 2. Parse host_slot
    ld      de, arg_buf
    call    fuji_parse_arg
    jr      c, setdev_usage

    ld      de, arg_buf
    call    fuji_parse_slot
    jr      c, setdev_bad_host
    cp      MAX_HOST_SLOTS
    jr      nc, setdev_bad_host
    ld      (target_host_slot), a

    ; 3. Parse mode (1 = RO, 2 = WO, 3 = RW)
    ld      de, arg_buf
    call    fuji_parse_arg
    jr      c, setdev_usage

    ld      a, (arg_buf)
    cp      '1'
    jr      c, setdev_bad_mode
    cp      '4'
    jr      nc, setdev_bad_mode
    sub     '0'
    ld      (target_mode), a

    ; 4. Parse path string
    ld      de, path_arg
    call    fuji_parse_arg
    jr      c, setdev_usage

    ld      a, (path_arg)
    or      a
    jr      z, setdev_usage

    ; Prepare 259-byte transmission buffer
    ; Layout: [0] = dev_slot, [1] = host_slot, [2] = mode, [3..258] = 256-byte path
    ld      a, (target_dev_slot)
    ld      (tx_dev_slot), a
    ld      a, (target_host_slot)
    ld      (tx_host_slot), a
    ld      a, (target_mode)
    ld      (tx_mode), a

    ; Clear 256-byte path buffer to zeroes
    ld      hl, tx_path
    ld      bc, MAX_FILENAME_LEN
setdev_clr:
    xor     a
    ld      (hl), a
    inc     hl
    dec     bc
    ld      a, b
    or      c
    jr      nz, setdev_clr

    ; Copy path_arg into tx_path
    ld      hl, path_arg
    ld      de, tx_path
    ld      bc, MAX_FILENAME_LEN - 1
setdev_cp:
    ld      a, (hl)
    or      a
    jr      z, setdev_cp_done
    ld      (de), a
    inc     hl
    inc     de
    dec     bc
    ld      a, b
    or      c
    jr      nz, setdev_cp
setdev_cp_done:

    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; Issue FUJI_CMD_SET_DEV_FILE (0xE2) with 259-byte payload
    ld      a, FUJI_CMD_SET_DEV_FILE
    ld      hl, tx_block
    ld      bc, 3 + MAX_FILENAME_LEN
    call    fuji_cmd_params
    jr      c, setdev_comm_err

    ; Check command execution status
    call    fuji_get_error
    jr      c, setdev_err

    ; Display confirmation
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (target_dev_slot)
    call    fuji_print_dec8
    ld      hl, msg_host
    _DSPLY
    ld      a, (target_host_slot)
    call    fuji_print_dec8
    ld      hl, msg_mode
    _DSPLY
    ld      a, (target_mode)
    call    fuji_print_dec8
    ld      hl, msg_path
    _DSPLY
    ld      hl, path_arg
    call    fuji_print_str
    ld      hl, msg_quote_cr
    _DSPLY
    _EXIT

setdev_usage:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

setdev_bad_dev:
    ld      hl, msg_bad_dev
    _DSPLY
    _EXIT

setdev_bad_host:
    ld      hl, msg_bad_host
    _DSPLY
    _EXIT

setdev_bad_mode:
    ld      hl, msg_bad_mode
    _DSPLY
    _EXIT

setdev_comm_err:
    ld      a, 0FFh
    call    fuji_print_error
    _EXIT

setdev_err:
    call    fuji_print_error
    _EXIT

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_usage:      DB  "Usage: FNSETDEV <dev_slot 0-3> <host_slot 0-7> <mode 1-3> \"<path>\"", CR, 03h
msg_bad_dev:    DB  "Error: Device slot must be 0 to 3", CR, 03h
msg_bad_host:   DB  "Error: Host slot must be 0 to 7", CR, 03h
msg_bad_mode:   DB  "Error: Mode must be 1 (RO), 2 (WO), or 3 (RW)", CR, 03h
msg_pfx:        DB  "Device Slot ", 03h
msg_host:       DB  " configured (Host ", 03h
msg_mode:       DB  ", Mode ", 03h
msg_path:       DB  "): \"", 03h
msg_quote_cr:   DB  "\"", CR, 03h

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
target_dev_slot:  DB  0
target_host_slot: DB  0
target_mode:      DB  0
arg_buf:          DS  32
path_arg:         DS  MAX_FILENAME_LEN

tx_block:
tx_dev_slot:      DB  0
tx_host_slot:     DB  0
tx_mode:          DB  0
tx_path:          DS  MAX_FILENAME_LEN

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
