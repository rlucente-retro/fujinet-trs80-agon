; ==============================================================================
; HELLO.S - Minimal Proof-of-Concept Command Utility for TRSDOS 6.3 / LS-DOS 6.3
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
;
; Purpose:
;   Demonstrates an authentic LS-DOS command module (/CMD) running in user space.
;   - Loads at standard user memory origin: 0x2600 (USER_PROG_BASE)
;   - Clears the display console via _CLS (SVC 105)
;   - Displays a greeting banner via _DSPLY (SVC 10)
;   - Cleanly returns to the 'LS-DOS Ready' prompt via _EXIT (SVC 22)
; ==============================================================================

    .INCLUDE    "include/svc.inc"
    .INCLUDE    "include/equ.inc"

    ORG         USER_PROG_BASE

start:
    ; 1. Clear display screen
    _CLS

    ; 2. Display greeting string (HL -> ETX-terminated buffer)
    ld          hl, hello_msg
    _DSPLY

    ; 3. Clean exit back to LS-DOS Ready prompt
    _EXIT

; ------------------------------------------------------------------------------
; Message Data
; In LS-DOS 6.3, SVC 10 (@DSPLY) expects a string terminated with ETX (0x03).
; ------------------------------------------------------------------------------
hello_msg:
    DB          CR, LF
    DB          "==================================================", CR, LF
    DB          " Hello from authentic LS-DOS CMD on Agon Light 2! ", CR, LF
    DB          " Phase 3: Build Pipeline & CMD Packager Qualified  ", CR, LF
    DB          "==================================================", CR, LF
    DB          ETX

    END         start
