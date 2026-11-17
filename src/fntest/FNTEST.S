; ==============================================================================
; FNTEST.S - FujiNet Transaction Engine Diagnostic Test Utility
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Purpose:
;   Interactive diagnostic to verify Phase 4 Step 1:
;   1. Display and formatting helpers in include/fuji_lib.inc
;   2. Live UART1 handshake and poll query (OP_FUJI 0x00 -> 0x01)
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; 1. Clear display screen
    _CLS

    ; 2. Title & divider
    ld      hl, msg_title
    _DSPLY
    ld      hl, msg_div
    _DSPLY

    ; -------------------------------------------------------------------------
    ; Part 1: Verify Display and Formatting Helpers
    ; -------------------------------------------------------------------------
    ld      hl, msg_t1
    _DSPLY

    ; Decimal formatting: 42
    ld      hl, msg_dec
    _DSPLY
    ld      a, 42
    call    fuji_print_dec8
    call    fuji_newline

    ; Hex formatting: 0xA5
    ld      hl, msg_hex
    _DSPLY
    ld      a, 0A5h
    call    fuji_print_hex8
    call    fuji_newline

    ; IPv4 dotted-decimal formatting: 192.168.1.100
    ld      hl, msg_ip
    _DSPLY
    ld      hl, test_ip
    call    fuji_print_ipv4
    call    fuji_newline

    ; MAC colon-hex formatting: 00:1A:2B:3C:4D:5E
    ld      hl, msg_mac
    _DSPLY
    ld      hl, test_mac
    call    fuji_print_mac
    call    fuji_newline

    ; -------------------------------------------------------------------------
    ; Part 2: Hardware UART1 Poll & Live FujiNet Handshake
    ; -------------------------------------------------------------------------
    ld      hl, msg_t2
    _DSPLY

    call    fuji_init           ; Reset FIFO, disable UART interrupts, flush
    call    fuji_poll           ; Issue OP_FUJI (0xE2) + POLL (0x00), await 0x01
    jr      c, poll_failed

    ; Success
    ld      hl, msg_poll_ok
    _DSPLY
    jr      finish

poll_failed:
    ld      hl, msg_poll_fail
    _DSPLY

finish:
    ld      hl, msg_div
    _DSPLY
    ld      hl, msg_done
    _DSPLY
    _EXIT                       ; Clean exit back to LS-DOS Ready prompt

; -----------------------------------------------------------------------------
; Test Data & Static Strings
; -----------------------------------------------------------------------------
test_ip:        DB  192, 168, 1, 100
test_mac:       DB  00h, 1Ah, 2Bh, 3Ch, 4Dh, 5Eh

msg_title:      DB  "FNTEST: FujiNet Transaction Engine Diagnostic", CR
msg_div:        DB  "--------------------------------------------------", CR
msg_t1:         DB  "[1] Verifying display and formatting helpers:", CR
msg_dec:        DB  "    Decimal (42):           ", 03h
msg_hex:        DB  "    Hexadecimal (0xA5):     0x", 03h
msg_ip:         DB  "    IPv4 Address:           ", 03h
msg_mac:        DB  "    MAC Address:            ", 03h
msg_t2:         DB  "[2] Testing live FujiNet handshake (OP_FUJI 0x00)...", CR
msg_poll_ok:    DB  "    [PASS] FujiNet adapter is ONLINE and ACTIVE (0x01)", CR
msg_poll_fail:  DB  "    [FAIL] FujiNet poll failed (timeout or offline)", CR
msg_done:       DB  "Diagnostic test complete.", CR

; -----------------------------------------------------------------------------
; FujiNet Library Routines Included within 0x2600 User Workspace
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END         start
