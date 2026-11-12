; ==============================================================================
; HELLO.S - Minimal Proof-of-Concept Command Utility for TRSDOS 6.3 / LS-DOS 6.3
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
;
; Purpose:
;   Demonstrates an authentic LS-DOS command module (/CMD) running in user space.
;   - Loads at standard user memory origin: 0x2600 (USER_PROG_BASE)
;   - Clears the display console via _CLS (SVC 105)
;   - Displays greeting banner lines via _DSPLY (SVC 10)
;   - Cleanly returns to the 'LS-DOS Ready' prompt via _EXIT (SVC 22)
; ==============================================================================

    .INCLUDE    "include/svc.inc"
    .INCLUDE    "include/equ.inc"

    ORG         USER_PROG_BASE

start:
    ; 1. Clear display screen
    _CLS

    ; 2. Display greeting banner lines via _DSPLY (SVC 10)
    ; In LS-DOS 6.3, SVC 10 (@DSPLY) prints characters up to CR (0x0D) or ETX (0x03).
    ; Each line ending with CR prints its content, emits a newline, and returns.
    ld          hl, line1
    _DSPLY
    ld          hl, line2
    _DSPLY
    ld          hl, line3
    _DSPLY
    ld          hl, line4
    _DSPLY

    ; 3. Clean exit back to LS-DOS Ready prompt
    _EXIT

; ------------------------------------------------------------------------------
; Banner Lines
; Each line terminates with CR (0x0D) to display the line and advance cursor.
; ------------------------------------------------------------------------------
line1:
    DB          "==================================================", CR
line2:
    DB          " Hello from authentic LS-DOS CMD on Agon Light 2! ", CR
line3:
    DB          " Phase 3: Build Pipeline & CMD Packager Qualified  ", CR
line4:
    DB          "==================================================", CR

    END         start
