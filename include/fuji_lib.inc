; ==============================================================================
; fuji_lib.inc - Shared FujiNet OP_FUJI (0xE2) Transaction Engine & UART1 Driver
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode, ADL = 0)
; Operating System: TRS-80 Model 4 Environments (TRSDOS 6.3.1 / LS-DOS 6.3)
; ==============================================================================
;
; This include library provides modular UART1 polled communication, transaction
; execution, status verification, payload receiving, and formatting routines
; shared across all FN*.CMD utilities.
;
; Prerequisites: Caller must include:
;   INCLUDE "svc.inc"
;   INCLUDE "equ.inc"
;   INCLUDE "fujinet.inc"
; ==============================================================================

; ------------------------------------------------------------------------------
; fuji_init - Initialize UART1 FIFO and flush pending bytes
; ------------------------------------------------------------------------------
fuji_init:
    push    af
    ; Reset and enable 16-byte FIFO (FCR = 07h: FIFO enable, RX clear, TX clear)
    ld      a, 07h
    out0    (UART1_FCR), a
    ; Ensure interrupts disabled on UART1 (IER = 00h)
    xor     a
    out0    (UART1_IER), a
    ; Drain any lingering bytes in RX FIFO
    call    fuji_flush
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_flush - Drain any pending bytes from UART1 RX buffer
; ------------------------------------------------------------------------------
fuji_flush:
    push    af
fflush_drain:
    in0     a, (UART1_LSR)
    and     UART_LSR_DR         ; Bit 0: Data Ready?
    jr      z, fflush_done
    in0     a, (UART1_RBR)      ; Discard byte from FIFO
    jr      fflush_drain
fflush_done:
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_tx_byte - Transmit a single byte in A over UART1 with defensive timeout
; Entry: A = byte to transmit
; Exit:  Carry clear (NC) = success, Carry set (C) = timeout
; Preserves: BC, DE, HL
; ------------------------------------------------------------------------------
fuji_tx_byte:
    push    bc
    push    de
    ld      d, a                ; Save data byte
    ld      bc, 0               ; Timeout counter: 65536 loops (~46 ms)
ftx_wait:
    in0     a, (UART1_LSR)
    and     UART_LSR_THRE       ; Transmitter holding register empty? (20h)
    jr      nz, ftx_ready
    dec     bc
    ld      a, b
    or      c
    jr      nz, ftx_wait

    ; Hardware timeout on transmit
    pop     de
    pop     bc
    scf
    ret

ftx_ready:
    ld      a, d
    out0    (UART1_THR), a      ; Send byte
    pop     de
    pop     bc
    or      a                   ; Clear Carry (NC)
    ret

; ------------------------------------------------------------------------------
; fuji_tx_bytes - Transmit BC bytes from memory buffer HL
; Entry: HL = pointer to data buffer, BC = byte count
; Exit:  Carry clear (NC) = success, Carry set (C) = timeout
; Preserves: DE
; ------------------------------------------------------------------------------
fuji_tx_bytes:
    push    hl
    push    bc
ftxb_loop:
    ld      a, b
    or      c
    jr      z, ftxb_done
    ld      a, (hl)
    inc     hl
    call    fuji_tx_byte
    jr      c, ftxb_err
    dec     bc
    jr      ftxb_loop
ftxb_done:
    pop     bc
    pop     hl
    or      a                   ; Clear Carry
    ret
ftxb_err:
    pop     bc
    pop     hl
    scf                         ; Set Carry
    ret

; ------------------------------------------------------------------------------
; fuji_rx_byte - Receive a single byte from UART1 with ~2.3 second timeout
; Exit:
;   Success: Carry clear (NC), A = received byte
;   Timeout: Carry set (C), A = 0FFh
; Preserves: BC, DE, HL
; ------------------------------------------------------------------------------
fuji_rx_byte:
    push    bc
    push    de
    ld      e, 50               ; Outer loop: 50 iterations (~2.3 seconds)
frx_outer:
    ld      bc, 0               ; Inner loop: 65,536 iterations (~46 ms)
frx_inner:
    in0     a, (UART1_LSR)
    and     UART_LSR_DR         ; Data ready in RX FIFO? (01h)
    jr      nz, frx_got_char
    dec     bc
    ld      a, b
    or      c
    jr      nz, frx_inner
    dec     e
    jr      nz, frx_outer

    ; Timeout expired
    pop     de
    pop     bc
    ld      a, 0FFh
    scf                         ; Carry set indicates timeout
    ret

frx_got_char:
    in0     a, (UART1_RBR)      ; Read received character
    pop     de
    pop     bc
    or      a                   ; Clear Carry (NC)
    ret

; ------------------------------------------------------------------------------
; fuji_rx_bytes - Receive BC bytes into memory buffer HL
; Entry: HL = destination buffer pointer, BC = byte count
; Exit:  Carry clear (NC) = success, Carry set (C) = timeout
; Preserves: DE
; ------------------------------------------------------------------------------
fuji_rx_bytes:
    push    hl
    push    bc
frxb_loop:
    ld      a, b
    or      c
    jr      z, frxb_done
    call    fuji_rx_byte
    jr      c, frxb_err
    ld      (hl), a
    inc     hl
    dec     bc
    jr      frxb_loop
frxb_done:
    pop     bc
    pop     hl
    or      a                   ; Clear Carry
    ret
frxb_err:
    pop     bc
    pop     hl
    scf                         ; Set Carry
    ret

; ------------------------------------------------------------------------------
; fuji_poll - Check if FujiNet is active and responding (OP_FUJI 0x00)
; Exit:
;   Success: Z flag set, Carry clear (NC), A = 01h (FUJI_STAT_OK)
;   Failure: NZ flag set, Carry set (C), A = error status or 0FFh
; ------------------------------------------------------------------------------
fuji_poll:
    call    fuji_flush
    ld      a, OP_FUJI          ; 0xE2
    call    fuji_tx_byte
    jr      c, fpoll_fail
    ld      a, FUJI_CMD_POLL    ; 0x00
    call    fuji_tx_byte
    jr      c, fpoll_fail
    call    fuji_rx_byte
    jr      c, fpoll_fail
    cp      FUJI_STAT_OK        ; 0x01?
    ret     z                   ; Return Z=1, C=0 if OK
fpoll_fail:
    scf                         ; Set Carry
    or      0FFh                ; Ensure NZ
    ret

; ------------------------------------------------------------------------------
; fuji_cmd - Transmit OP_FUJI command opcode in A without parameters
; Entry: A = command opcode (FUJI_CMD_*)
; Exit:  Carry clear (NC) = success, Carry set (C) = timeout
; ------------------------------------------------------------------------------
fuji_cmd:
    push    bc
    ld      b, a                ; Save command opcode
    ld      a, OP_FUJI          ; 0xE2
    call    fuji_tx_byte
    jr      c, fcmd_fail
    ld      a, b
    call    fuji_tx_byte
    jr      c, fcmd_fail
    pop     bc
    or      a                   ; Clear Carry
    ret
fcmd_fail:
    pop     bc
    scf
    ret

; ------------------------------------------------------------------------------
; fuji_cmd_params - Transmit OP_FUJI command in A followed by BC parameter bytes
; Entry: A = command opcode, HL = parameter buffer, BC = parameter count
; Exit:  Carry clear (NC) = success, Carry set (C) = timeout
; ------------------------------------------------------------------------------
fuji_cmd_params:
    push    hl
    push    bc
    push    af                  ; Save command opcode
    ld      a, OP_FUJI          ; 0xE2
    call    fuji_tx_byte
    jr      c, fcp_fail1
    pop     af                  ; Restore command opcode
    call    fuji_tx_byte
    jr      c, fcp_fail2
    call    fuji_tx_bytes       ; Send BC parameter bytes from HL
    jr      c, fcp_fail2
    pop     bc
    pop     hl
    or      a                   ; Clear Carry
    ret
fcp_fail1:
    pop     af                  ; Clean saved command opcode from stack
fcp_fail2:
    pop     bc
    pop     hl
    scf
    ret

; ------------------------------------------------------------------------------
; fuji_get_error - Query transaction completion / error status
; Sends [0xE2, 0x02], reads 1-byte status return
; Exit:
;   Success: Z flag set, Carry clear (NC), A = 01h (FUJI_STAT_OK)
;   Failure: NZ flag set, Carry set (C), A = error status code or 0FFh
; ------------------------------------------------------------------------------
fuji_get_error:
    ld      a, OP_FUJI          ; 0xE2
    call    fuji_tx_byte
    jr      c, fge_timeout
    ld      a, FUJI_CMD_GET_ERROR ; 0x02
    call    fuji_tx_byte
    jr      c, fge_timeout
    call    fuji_rx_byte
    jr      c, fge_timeout
    cp      FUJI_STAT_OK        ; 0x01?
    jr      nz, fge_err
    or      a                   ; Clear Carry, Z set
    ret
fge_err:
    scf                         ; Set Carry, NZ set
    ret
fge_timeout:
    ld      a, 0FFh
    scf                         ; Set Carry
    or      a                   ; NZ set
    ret

; ------------------------------------------------------------------------------
; fuji_get_response - Fetch transaction response payload
; Sends [0xE2, 0x01], reads BC bytes into buffer HL
; Entry: HL = destination buffer, BC = expected byte count
; Exit:  Carry clear (NC) = success, Carry set (C) = timeout
; ------------------------------------------------------------------------------
fuji_get_response:
    push    hl
    push    bc
    ld      a, OP_FUJI          ; 0xE2
    call    fuji_tx_byte
    jr      c, fgr_fail
    ld      a, FUJI_CMD_GET_RESPONSE ; 0x01
    call    fuji_tx_byte
    jr      c, fgr_fail
    pop     bc
    pop     hl
    call    fuji_rx_bytes       ; Read BC bytes into buffer HL
    ret
fgr_fail:
    pop     bc
    pop     hl
    scf
    ret

; ------------------------------------------------------------------------------
; fuji_putc - Output single character in A to console via _DSP (SVC 2)
; Preserves all registers
; ------------------------------------------------------------------------------
fuji_putc:
    push    af
    push    bc
    push    de
    push    hl
    ld      c, a                ; SVC_DSP expects character in register C
    _DSP
    pop     hl
    pop     de
    pop     bc
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_newline - Emit Carriage Return (0x0D) to console
; Preserves all registers
; ------------------------------------------------------------------------------
fuji_newline:
    push    af
    ld      a, CR               ; 0x0D
    call    fuji_putc
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_print_str - Print null-terminated string pointed to by HL
; Preserves: BC, DE, HL
; ------------------------------------------------------------------------------
fuji_print_str:
    push    af
    push    hl
fps_loop:
    ld      a, (hl)
    inc     hl
    or      a
    jr      z, fps_done
    call    fuji_putc
    jr      fps_loop
fps_done:
    pop     hl
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_print_dec8 - Print unsigned 8-bit integer in A as decimal (1-3 digits)
; Preserves all registers
; ------------------------------------------------------------------------------
fuji_print_dec8:
    push    af
    push    bc
    push    de
    ld      c, a                ; C = value to print
    ld      b, 0                ; B = flag (1 if leading non-zero printed)

    ; Hundreds digit
    ld      a, c
    ld      d, 0
fpd_div100:
    sub     100
    jr      c, fpd_div100_done
    inc     d
    jr      fpd_div100
fpd_div100_done:
    add     a, 100
    ld      c, a                ; C = remainder
    ld      a, d
    or      a
    jr      z, fpd_skip100
    add     a, '0'
    call    fuji_putc
    ld      b, 1                ; Non-zero printed
fpd_skip100:
    ; Tens digit
    ld      a, c
    ld      d, 0
fpd_div10:
    sub     10
    jr      c, fpd_div10_done
    inc     d
    jr      fpd_div10
fpd_div10_done:
    add     a, 10
    ld      c, a                ; C = ones digit
    ld      a, d
    or      a
    jr      nz, fpd_print10
    ld      a, b
    or      a
    jr      z, fpd_skip10       ; Suppress leading zero
fpd_print10:
    ld      a, d
    add     a, '0'
    call    fuji_putc
fpd_skip10:
    ; Ones digit (always printed)
    ld      a, c
    add     a, '0'
    call    fuji_putc

    pop     de
    pop     bc
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_print_hex8 - Print 8-bit value in A as 2 uppercase hex digits
; Preserves all registers
; ------------------------------------------------------------------------------
fuji_print_hex8:
    push    af
    push    bc
    ld      b, a                ; Save original value
    ; High nibble
    rrca
    rrca
    rrca
    rrca
    and     0Fh
    call    fuji_nibble_hex
    call    fuji_putc
    ; Low nibble
    ld      a, b
    and     0Fh
    call    fuji_nibble_hex
    call    fuji_putc
    pop     bc
    pop     af
    ret

fuji_nibble_hex:
    cp      10
    jr      c, fnh_num
    add     a, 'A' - 10
    ret
fnh_num:
    add     a, '0'
    ret

; ------------------------------------------------------------------------------
; fuji_print_ipv4 - Print 4-byte IPv4 address at (HL) in A.B.C.D decimal format
; Preserves: BC, DE, HL
; ------------------------------------------------------------------------------
fuji_print_ipv4:
    push    af
    push    bc
    push    hl
    ld      b, 4
fp_ip_loop:
    ld      a, (hl)
    inc     hl
    call    fuji_print_dec8
    dec     b
    jr      z, fp_ip_done
    ld      a, '.'
    call    fuji_putc
    jr      fp_ip_loop
fp_ip_done:
    pop     hl
    pop     bc
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_print_mac - Print 6-byte MAC address at (HL) in XX:XX:XX:XX:XX:XX hex format
; Preserves: BC, DE, HL
; ------------------------------------------------------------------------------
fuji_print_mac:
    push    af
    push    bc
    push    hl
    ld      b, 6
fp_mac_loop:
    ld      a, (hl)
    inc     hl
    call    fuji_print_hex8
    dec     b
    jr      z, fp_mac_done
    ld      a, ':'
    call    fuji_putc
    jr      fp_mac_loop
fp_mac_done:
    pop     hl
    pop     bc
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_print_error - Format and display FujiNet error message via _DSPLY
; Entry: A = status / error code
; Preserves: BC, DE, HL
; ------------------------------------------------------------------------------
fuji_print_error:
    push    af
    push    hl
    cp      FUJI_STAT_ERROR         ; 90h
    jr      z, fpe_gen
    cp      FUJI_STAT_NOT_FOUND     ; 91h
    jr      z, fpe_fnf
    cp      FUJI_STAT_BAD_DEV       ; 92h
    jr      z, fpe_bdev
    cp      FUJI_STAT_TIMEOUT       ; 93h
    jr      z, fpe_netto
    cp      0FFh                    ; FFh (Hardware timeout)
    jr      z, fpe_hwto

    ; Unknown error code - display header and numeric hex value
    ld      hl, msg_err_unk
    _DSPLY
    pop     hl
    pop     af
    push    af
    push    hl
    call    fuji_print_hex8
    call    fuji_newline
    pop     hl
    pop     af
    ret

fpe_gen:
    ld      hl, msg_err_gen
    _DSPLY
    pop     hl
    pop     af
    ret

fpe_fnf:
    ld      hl, msg_err_fnf
    _DSPLY
    pop     hl
    pop     af
    ret

fpe_bdev:
    ld      hl, msg_err_bdev
    _DSPLY
    pop     hl
    pop     af
    ret

fpe_netto:
    ld      hl, msg_err_netto
    _DSPLY
    pop     hl
    pop     af
    ret

fpe_hwto:
    ld      hl, msg_err_hwto
    _DSPLY
    pop     hl
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_skip_cmd - Advance HL to first argument in command line buffer
; If HL already points to arguments (e.g. passed in HL by LS-DOS), preserves it.
; If HL points to the command name, skips the command word and trailing spaces.
; Entry: HL = command line pointer (typically entry HL, or INBUF$)
; Exit:  HL = pointer to first non-space argument char, or CR/0 if none
; Preserves: AF, BC, DE
; ------------------------------------------------------------------------------
fuji_skip_cmd:
    push    af
fsc_lead_sp:
    ld      a, (hl)
    cp      CHR_SP
    jr      z, fsc_next_lead
    cp      09h                 ; Tab
    jr      z, fsc_next_lead
    cp      ','
    jr      z, fsc_next_lead
    jr      fsc_chk_cmd
fsc_next_lead:
    inc     hl
    jr      fsc_lead_sp

fsc_chk_cmd:
    cp      CR
    jr      z, fsc_done
    or      a
    jr      z, fsc_done
    cp      '"'
    jr      z, fsc_done         ; Argument: double-quoted string
    cp      27h                 ; Single quote (0x27)
    jr      z, fsc_done         ; Argument: single-quoted string
    cp      '0'
    jr      c, fsc_is_cmd
    cp      3Ah                 ; '9' + 1
    jr      c, fsc_done         ; Argument: numeric digit (slot 0-7)

fsc_is_cmd:
    ; HL points to a command name (letters)
fsc_loop1:
    ld      a, (hl)
    cp      CR                  ; 0x0D
    jr      z, fsc_done
    or      a                   ; Null terminator
    jr      z, fsc_done
    cp      CHR_SP              ; 0x20
    jr      z, fsc_spaces
    cp      09h                 ; Tab
    jr      z, fsc_spaces
    cp      ','
    jr      z, fsc_spaces
    inc     hl
    jr      fsc_loop1

fsc_spaces:
    inc     hl
    ld      a, (hl)
    cp      CHR_SP
    jr      z, fsc_spaces
    cp      09h
    jr      z, fsc_spaces
    cp      ','
    jr      z, fsc_spaces

fsc_done:
    pop     af
    ret

; ------------------------------------------------------------------------------
; fuji_parse_arg - Parse next token from HL into buffer DE (quoted or unquoted)
; Handles both double-quoted ("..."), single-quoted ('...'), and unquoted tokens.
; Delimiters between unquoted tokens: space, tab, comma, CR, null.
; Entry: HL = command line pointer, DE = destination buffer pointer
; Exit:  Carry clear (NC) = token extracted (null-terminated in DE)
;        Carry set (C) = no argument found (end of line)
;        HL = advanced past argument
; Preserves: BC
; ------------------------------------------------------------------------------
fuji_parse_arg:
    push    bc
fpa_skip_sp:
    ld      a, (hl)
    cp      CR
    jr      z, fpa_none
    or      a
    jr      z, fpa_none
    cp      CHR_SP
    jr      z, fpa_next_sp
    cp      09h
    jr      z, fpa_next_sp
    cp      ','
    jr      z, fpa_next_sp
    jr      fpa_check_quote
fpa_next_sp:
    inc     hl
    jr      fpa_skip_sp

fpa_none:
    pop     bc
    scf                         ; Carry set: no token
    ret

fpa_check_quote:
    cp      '"'                 ; Double quote?
    jr      z, fpa_quoted
    cp      27h                 ; Single quote (0x27)?
    jr      z, fpa_quoted

    ; Unquoted token: read until space, tab, comma, CR, or null
fpa_unq_loop:
    ld      a, (hl)
    cp      CR
    jr      z, fpa_unq_done
    or      a
    jr      z, fpa_unq_done
    cp      CHR_SP
    jr      z, fpa_unq_done
    cp      09h
    jr      z, fpa_unq_done
    cp      ','
    jr      z, fpa_unq_done
    ld      (de), a
    inc     hl
    inc     de
    jr      fpa_unq_loop

fpa_unq_done:
    xor     a
    ld      (de), a             ; Null terminate
    pop     bc
    or      a                   ; Clear Carry
    ret

    ; Quoted token: read until matching quote (saved in C), CR, or null
fpa_quoted:
    ld      c, a                ; C = opening quote character
    inc     hl                  ; Skip opening quote
fpa_q_loop:
    ld      a, (hl)
    cp      CR
    jr      z, fpa_q_done
    or      a
    jr      z, fpa_q_done
    cp      c                   ; Matching closing quote?
    jr      z, fpa_q_close
    ld      (de), a
    inc     hl
    inc     de
    jr      fpa_q_loop

fpa_q_close:
    inc     hl                  ; Skip closing quote
fpa_q_done:
    xor     a
    ld      (de), a             ; Null terminate
    pop     bc
    or      a                   ; Clear Carry
    ret

; ------------------------------------------------------------------------------
; fuji_parse_slot - Convert 1-digit ASCII string at DE to integer 0..7
; Entry: DE = pointer to null-terminated argument string
; Exit:  Carry clear (NC) = valid, A = slot number (0..7)
;        Carry set (C) = invalid slot specification
; Preserves: BC, DE, HL
; ------------------------------------------------------------------------------
fuji_parse_slot:
    push    hl
    ld      a, (de)
    sub     '0'
    jr      c, fps_inv          ; < '0'
    cp      8                   ; >= 8?
    jr      nc, fps_inv
    inc     de
    ld      h, a                ; Save slot
    ld      a, (de)
    dec     de
    or      a
    jr      nz, fps_inv
    ld      a, h                ; Restore slot
    pop     hl
    or      a                   ; Clear Carry
    ret
fps_inv:
    pop     hl
    scf                         ; Set Carry
    ret

msg_err_gen:    DB "FujiNet error: General command failure", CR
msg_err_fnf:    DB "FujiNet error: Host or file not found", CR
msg_err_bdev:   DB "FujiNet error: Invalid device slot", CR
msg_err_netto:  DB "FujiNet error: Network / server timeout", CR
msg_err_hwto:   DB "FujiNet error: No response from adapter (timeout)", CR
msg_err_unk:    DB "FujiNet error code: 0x", 03h
