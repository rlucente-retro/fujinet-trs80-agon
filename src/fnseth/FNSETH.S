; ==============================================================================
; FNSETH.S - Configure FujiNet Host Slot URL or Hostname (0-7)
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   FNSETH <slot> "<hostname>"
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; 1. Parse <slot> from command line buffer INBUF$
    ld      hl, INBUF$
    call    fuji_skip_cmd
    ld      de, arg_slot
    call    fuji_parse_arg
    jr      c, seth_usage

    ld      de, arg_slot
    call    fuji_parse_slot
    jr      c, seth_usage
    ld      (target_slot), a

    ; 2. Parse "<hostname>" argument (quotes preserve case)
    ld      de, arg_host
    call    fuji_parse_arg
    jr      c, seth_usage

    ; Validate hostname length (max 31 characters)
    ld      hl, arg_host
    ld      b, 0
check_len:
    ld      a, (hl)
    or      a
    jr      z, len_ok
    inc     hl
    inc     b
    ld      a, b
    cp      HOST_SLOT_SIZE      ; 32
    jr      c, check_len
    ; Hostname exceeds 31 characters
    ld      hl, msg_too_long
    _DSPLY
    _EXIT

len_ok:
    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; 3. Read current 256-byte Host Slot Table (0xF4)
    ld      a, FUJI_CMD_GET_HOST_SLOTS
    call    fuji_cmd
    jr      c, seth_comm_err

    call    fuji_get_error
    jr      c, seth_err

    ld      hl, hosts_buf
    ld      bc, HOST_TABLE_SIZE
    call    fuji_get_response
    jr      c, seth_comm_err

    ; 4. Calculate target slot destination address: hosts_buf + slot * 32
    ld      a, (target_slot)
    ld      l, a
    ld      h, 0
    add     hl, hl              ; * 2
    add     hl, hl              ; * 4
    add     hl, hl              ; * 8
    add     hl, hl              ; * 16
    add     hl, hl              ; * 32
    ld      de, hosts_buf
    add     hl, de
    push    hl                  ; Save slot start pointer

    ; Zero out the 32-byte slot
    push    hl
    pop     de
    ld      b, HOST_SLOT_SIZE
zero_slot:
    xor     a
    ld      (de), a
    inc     de
    djnz    zero_slot

    ; Copy new hostname string into slot
    pop     de                  ; DE = destination slot buffer
    ld      hl, arg_host        ; HL = source hostname
copy_host:
    ld      a, (hl)
    ld      (de), a
    or      a
    jr      z, copy_done
    inc     hl
    inc     de
    jr      copy_host

copy_done:
    ; 5. Write back complete 256-byte table via FUJI_CMD_SET_HOST_SLOTS (0xF3)
    ld      a, FUJI_CMD_SET_HOST_SLOTS
    ld      hl, hosts_buf
    ld      bc, HOST_TABLE_SIZE
    call    fuji_cmd_params
    jr      c, seth_comm_err

    call    fuji_get_error
    jr      c, seth_err

    ; 6. Persist host slots to NVS flash via FUJI_CMD_WRITE_HOSTS (0xFA)
    ld      a, FUJI_CMD_WRITE_HOSTS
    call    fuji_cmd
    jr      c, seth_comm_err

    call    fuji_get_error
    jr      c, seth_err

    ; 7. Display success confirmation
    ld      hl, msg_pfx
    _DSPLY
    ld      a, (target_slot)
    call    fuji_print_dec8
    ld      hl, msg_sep
    _DSPLY
    ld      hl, arg_host
    call    fuji_print_str
    call    fuji_newline
    _EXIT

seth_usage:
    ld      hl, msg_usage
    _DSPLY
    _EXIT

seth_comm_err:
    ld      a, 0FFh
    call    fuji_print_error
    _EXIT

seth_err:
    call    fuji_print_error
    _EXIT

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_usage:      DB  "Usage: FNSETH <slot 0-7> \"<hostname>\"", CR
msg_too_long:   DB  "Error: Hostname exceeds 31 characters", CR
msg_pfx:        DB  "Host Slot ", 03h
msg_sep:        DB  " updated: ", 03h

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
target_slot:    DB  0
arg_slot:       DS  16
arg_host:       DS  64
hosts_buf:      DS  HOST_TABLE_SIZE

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
