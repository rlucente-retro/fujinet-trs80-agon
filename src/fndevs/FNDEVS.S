; ==============================================================================
; FNDEVS.S - List Virtual Device Slot Assignments & Mount Status
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   FNDEVS
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; Issue FUJI_CMD_GET_DEV_SLOTS (0xF2)
    ld      a, FUJI_CMD_GET_DEV_SLOTS
    call    fuji_cmd
    jr      c, devs_comm_err

    ; Check command execution status
    call    fuji_get_error
    jr      c, devs_err

    ; Read 152-byte device slot table (4 slots * 38 bytes)
    ld      hl, dev_slots_buf
    ld      bc, DEV_TABLE_SIZE_STAGE1
    call    fuji_get_response
    jr      c, devs_comm_err

    ; Display header
    ld      hl, msg_hdr
    _DSPLY

    ; Iterate through 4 device slots (0..3)
    ld      ix, dev_slots_buf
    xor     a
    ld      (cur_slot), a

devs_loop:
    ; Display "Slot <cur_slot>: "
    ld      hl, msg_slot_pfx
    _DSPLY
    ld      a, (cur_slot)
    call    fuji_print_dec8
    ld      hl, msg_colon_sp
    _DSPLY

    ; Check if slot is empty (host_slot == 0xFF or filename[0] == 0)
    ld      a, (ix + DEV_OFF_HOST_SLOT)
    cp      0FFh
    jr      z, devs_empty
    ld      a, (ix + DEV_OFF_FILENAME)
    or      a
    jr      z, devs_empty

    ; Print "["
    ld      a, '['
    call    fuji_putc

    ; Check mounted bit (0x40)
    ld      a, (ix + DEV_OFF_MODE)
    and     DISK_ACCESS_MODE_MOUNTED
    jr      z, devs_not_mounted
    ld      hl, msg_mounted
    _DSPLY
    jr      devs_check_mode

devs_not_mounted:
    ld      hl, msg_unmounted
    _DSPLY

devs_check_mode:
    ; Check base mode (bits 0..1: 1=RO, 2=WO, 3=RW)
    ld      a, (ix + DEV_OFF_MODE)
    and     03h
    cp      FUJI_MODE_READ
    jr      z, devs_mode_ro
    cp      FUJI_MODE_WRITE
    jr      z, devs_mode_wo
    cp      FUJI_MODE_READWRITE
    jr      z, devs_mode_rw
    ld      hl, msg_mode_unk
    _DSPLY
    jr      devs_print_host

devs_mode_ro:
    ld      hl, msg_mode_ro
    _DSPLY
    jr      devs_print_host

devs_mode_wo:
    ld      hl, msg_mode_wo
    _DSPLY
    jr      devs_print_host

devs_mode_rw:
    ld      hl, msg_mode_rw
    _DSPLY

devs_print_host:
    ; Print "] Host <host_slot>: "
    ld      hl, msg_host_pfx
    _DSPLY
    ld      a, (ix + DEV_OFF_HOST_SLOT)
    call    fuji_print_dec8
    ld      hl, msg_host_sfx
    _DSPLY

    ; Print filename (36 bytes max)
    push    ix
    pop     hl
    ld      de, DEV_OFF_FILENAME
    add     hl, de
    call    fuji_print_str
    call    fuji_newline
    jr      devs_next

devs_empty:
    ld      hl, msg_empty
    _DSPLY

devs_next:
    ; Advance IX by DEV_SLOT_SIZE (38 bytes)
    ld      de, DEV_SLOT_SIZE
    add     ix, de
    ; Increment slot index
    ld      a, (cur_slot)
    inc     a
    ld      (cur_slot), a
    cp      MAX_DEV_SLOTS_STAGE1
    jr      c, devs_loop

    _EXIT

devs_comm_err:
    ld      a, 0FFh
    call    fuji_print_error
    _EXIT

devs_err:
    call    fuji_print_error
    _EXIT

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_hdr:        DB  "FujiNet Device Slots:", CR, 03h
msg_slot_pfx:   DB  "  Slot ", 03h
msg_colon_sp:   DB  ": ", 03h
msg_mounted:    DB  "Mounted, ", 03h
msg_unmounted:  DB  "Unmounted, ", 03h
msg_mode_ro:    DB  "RO", 03h
msg_mode_wo:    DB  "WO", 03h
msg_mode_rw:    DB  "RW", 03h
msg_mode_unk:   DB  "--", 03h
msg_host_pfx:   DB  "] Host ", 03h
msg_host_sfx:   DB  ": ", 03h
msg_empty:      DB  "(empty)", CR, 03h

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
cur_slot:       DB  0
dev_slots_buf:  DS  DEV_TABLE_SIZE_STAGE1

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
