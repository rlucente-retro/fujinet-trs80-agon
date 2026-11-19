; ==============================================================================
; sd_lib.inc - Shared MicroSD Hardware SPI0 & Map File Library for TRSDOS
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
;
; Provides low-level routines to initialize SPI0, read 512-byte physical
; SD blocks directly across the eZ80 SPI0 peripheral, and stream/parse
; hierarchical disk image catalogs (sdmap.txt).
; ==============================================================================

; ------------------------------------------------------------------------------
; sd_init - Initialize Port B GPIO and SPI0 controller for MicroSD card
; Modifies: AF, BC
; ------------------------------------------------------------------------------
sd_init:
    push    af
    push    bc

    ; Check if already initialized in lowcore
    ld      a, (sddvr_inited)
    cp      1
    jr      z, sd_init_done

    ; Configure PB1, PB2, PB3 in Mode 7 (Alternate Function, mask 0x0E)
    in0     a, (PB_DDR)
    or      0Eh
    out0    (PB_DDR), a
    in0     a, (PB_ALT1)
    and     0F1h
    out0    (PB_ALT1), a
    in0     a, (PB_ALT2)
    or      0Eh
    out0    (PB_ALT2), a

    ; Configure PB4 (/CS) in Mode 2 (GPIO Output, mask 0x10)
    in0     a, (PB_DDR)
    and     0EFh
    out0    (PB_DDR), a
    in0     a, (PB_ALT1)
    and     0EFh
    out0    (PB_ALT1), a
    in0     a, (PB_ALT2)
    and     0EFh
    out0    (PB_ALT2), a

    ; Deassert /CS high (inactive)
    in0     a, (PB_DR)
    or      10h
    out0    (PB_DR), a

    ; Reset SPI0 controller
    xor     a
    out0    (SPI_CTL), a

    ; Set SPI0 BRG Divisor: 18.432 MHz / (2 * 2) = 4.608 MHz
    ld      bc, 2
    out0    (SPI_BRG_H), b
    out0    (SPI_BRG_L), c

    ; Enable SPI0 as Master: CPOL=0, CPHA=0, SPI_EN=1, MASTER_EN=1 (0x30)
    ld      a, 30h
    out0    (SPI_CTL), a

    ld      a, 1
    ld      (sddvr_inited), a

sd_init_done:
    pop     bc
    pop     af
    ret

; ------------------------------------------------------------------------------
; Low-Level SPI Helper Routines
; ------------------------------------------------------------------------------
sd_kick:
    ld      a, 0FFh
sd_tx_rx:
    out0    (SPI_TSR), a        ; Transmit byte across SPI0
sd_wait_spi:
    in0     a, (SPI_SR)         ; Poll SPI status register
    jp      p, sd_wait_spi      ; Loop while bit 7 (SPIF) is 0
    in0     a, (SPI_RBR)        ; Read received byte
    ret

sd_select:
    in0     a, (PB_DR)          ; Assert /CS (PB4 low)
    and     0EFh
    out0    (PB_DR), a
    ret

sd_deselect:
    in0     a, (PB_DR)          ; Deassert /CS (PB4 high)
    or      10h
    out0    (PB_DR), a
    call    sd_kick             ; 8 trailing clocks
    ret

; ------------------------------------------------------------------------------
; sd_read_block - Read a single 512-byte physical sector from MicroSD
;
; Inputs:
;   HL = pointer to 4-byte 32-bit Base LBA (little-endian: LSB .. MSB)
;   DE = destination 512-byte memory buffer
; Outputs:
;   Carry Flag clear (NC): Success (512 bytes loaded into buffer)
;   Carry Flag set (C): Error or timeout
; Modifies: AF, BC, DE, HL
; ------------------------------------------------------------------------------
sd_read_block:
    push    de                  ; Save destination buffer pointer

    ; Ensure SPI0 is initialized
    call    sd_init

    ; Format 6-byte CMD17 packet in sd_cmd_pkt
    ; Little-endian input at HL: (HL+0)=LSB, (HL+1), (HL+2), (HL+3)=MSB
    ; Packet: [0x51, MSB, byte2, byte1, LSB, 0xFF]
    ld      a, CMD17_READ_BLOCK
    ld      (sd_cmd_pkt+0), a
    push    hl
    inc     hl
    inc     hl
    inc     hl
    ld      a, (hl)             ; MSB
    ld      (sd_cmd_pkt+1), a
    dec     hl
    ld      a, (hl)             ; Byte 2
    ld      (sd_cmd_pkt+2), a
    dec     hl
    ld      a, (hl)             ; Byte 1
    ld      (sd_cmd_pkt+3), a
    dec     hl
    ld      a, (hl)             ; LSB
    ld      (sd_cmd_pkt+4), a
    pop     hl
    ld      a, 0FFh
    ld      (sd_cmd_pkt+5), a   ; Dummy CRC

    ; Assert /CS and send sync clock
    call    sd_select
    call    sd_kick

    ; Send 6-byte CMD17 packet
    ld      hl, sd_cmd_pkt
    ld      b, 6
sd_cmd_tx_loop:
    ld      a, (hl)
    inc     hl
    call    sd_tx_rx
    djnz    sd_cmd_tx_loop

    ; Some SPI emulators (e.g. fab-agon-emulator) return R1 on the 6th (CRC) byte.
    ; On real SD hardware, byte 6 returns 0xFF (bit 7 = 1) and R1 follows on kick.
    bit     7, a
    jr      z, sd_got_r1

    ; Wait for R1 response (bit 7 must be 0)
    ld      b, 255
sd_wait_r1_loop:
    call    sd_kick
    bit     7, a
    jr      z, sd_got_r1
    djnz    sd_wait_r1_loop
    ; R1 Timeout
    call    sd_deselect
    pop     de
    scf
    ret

sd_got_r1:
    or      a                   ; 0x00 = success
    jr      z, sd_wait_token
    ; Non-zero R1 error
    call    sd_deselect
    pop     de
    scf
    ret

sd_wait_token:
    ld      bc, 5000
sd_wait_tok_loop:
    call    sd_kick
    cp      DATA_START_TOKEN    ; 0xFE
    jr      z, sd_rx_data
    dec     bc
    ld      a, b
    or      c
    jr      nz, sd_wait_tok_loop
    ; Token Timeout
    call    sd_deselect
    pop     de
    scf
    ret

sd_rx_data:
    pop     de                  ; Restore destination buffer pointer
    push    de

    ; Read 512 bytes into (DE)
    ld      b, 0                ; 256 bytes
sd_rx_b1:
    call    sd_kick
    ld      (de), a
    inc     de
    djnz    sd_rx_b1

    ld      b, 0                ; 256 bytes
sd_rx_b2:
    call    sd_kick
    ld      (de), a
    inc     de
    djnz    sd_rx_b2

    ; Read and discard 2-byte CRC
    call    sd_kick
    call    sd_kick

    ; Deassert /CS
    call    sd_deselect

    pop     de                  ; Clean up stack
    or      a                   ; Clear carry (NC = success)
    ret

; ------------------------------------------------------------------------------
; map_stream_init - Initialize sdmap.txt streaming from lowcore mailbox
;
; Checks if sddvr_map_lba is non-zero and sddvr_map_len is non-zero.
; Exit:
;   Carry clear (NC) = stream initialized, map file active
;   Carry set (C)    = no map file active (lba == 0 or len == 0)
; Modifies: AF, DE, HL
; ------------------------------------------------------------------------------
map_stream_init:
    ; Check if sddvr_map_lba is zero
    ld      hl, sddvr_map_lba
    ld      a, (hl)
    inc     hl
    or      (hl)
    inc     hl
    or      (hl)
    inc     hl
    or      (hl)
    jr      z, msi_none

    ; Check if sddvr_map_len is zero
    ld      hl, (sddvr_map_len)
    ld      a, h
    or      l
    jr      z, msi_none

    ; Copy sddvr_map_lba to cur_map_lba (4 bytes)
    ld      hl, sddvr_map_lba
    ld      de, cur_map_lba
    ldi
    ldi
    ldi
    ldi

    ; Set cur_map_file_rem = (sddvr_map_len)
    ld      hl, (sddvr_map_len)
    ld      (cur_map_file_rem), hl

    ; Set cur_map_blk_rem = 0 (triggers block read on first character)
    ld      hl, 0
    ld      (cur_map_blk_rem), hl

    or      a                   ; Clear carry (NC = success)
    ret

msi_none:
    scf                         ; Set carry (C = no map file)
    ret

; ------------------------------------------------------------------------------
; map_get_char - Read next character from sdmap.txt stream
; Exit:
;   Carry clear (NC): A = character byte
;   Carry set (C):    End of file or read error
; Modifies: AF, BC, DE, HL
; ------------------------------------------------------------------------------
map_get_char:
    ; Check if file has reached EOF
    ld      hl, (cur_map_file_rem)
    ld      a, h
    or      l
    scf
    ret     z                   ; EOF reached

    ; Check if bytes remain in current 512-byte block buffer
    ld      hl, (cur_map_blk_rem)
    ld      a, h
    or      l
    jr      nz, mgc_read_byte

    ; Read next 512-byte block from cur_map_lba
    ld      hl, cur_map_lba
    ld      de, map_block_buf
    call    sd_read_block
    ret     c                   ; Return carry on read error

    ; Increment cur_map_lba by 1 (32-bit addition)
    ld      hl, cur_map_lba
    inc     (hl)
    jr      nz, mgc_lba_done
    inc     hl
    inc     (hl)
    jr      nz, mgc_lba_done
    inc     hl
    inc     (hl)
    jr      nz, mgc_lba_done
    inc     hl
    inc     (hl)
mgc_lba_done:

    ; Reset block pointer and block remaining counter (512 bytes)
    ld      hl, map_block_buf
    ld      (cur_map_ptr), hl
    ld      hl, 512
    ld      (cur_map_blk_rem), hl

mgc_read_byte:
    ; Read byte from (cur_map_ptr)
    ld      hl, (cur_map_ptr)
    ld      a, (hl)
    inc     hl
    ld      (cur_map_ptr), hl

    ; Decrement cur_map_blk_rem
    ld      hl, (cur_map_blk_rem)
    dec     hl
    ld      (cur_map_blk_rem), hl

    ; Decrement cur_map_file_rem
    ld      hl, (cur_map_file_rem)
    dec     hl
    ld      (cur_map_file_rem), hl

    or      a                   ; Clear carry (NC = success)
    ret

; ------------------------------------------------------------------------------
; map_get_line - Read next non-empty line from sdmap.txt into map_line_buf
; Strips CR (0x0D). Terminated by LF (0x0A) or EOF.
; Null-terminates line in map_line_buf.
; Exit:
;   Carry clear (NC): line read into map_line_buf
;   Carry set (C):    EOF reached (no more lines)
; Modifies: AF, BC, DE, HL
; ------------------------------------------------------------------------------
map_get_line:
mgl_start_line:
    ld      de, map_line_buf
    ld      b, 0                ; Char count

mgl_char_loop:
    push    bc
    push    de
    call    map_get_char
    pop     de
    pop     bc
    jr      c, mgl_eof_check

    ld      c, a                ; Save char in C
    cp      0Dh                 ; CR?
    jr      z, mgl_char_loop    ; Ignore CR
    cp      0Ah                 ; LF?
    jr      z, mgl_end_of_line

    ; Store character if within buffer bounds
    ld      a, b
    cp      78                  ; Max line length
    jr      nc, mgl_char_loop   ; Discard beyond 78 chars

    ld      a, c
    ld      (de), a
    inc     de
    inc     b
    jr      mgl_char_loop

mgl_end_of_line:
    ; Null-terminate line
    xor     a
    ld      (de), a
    ; If empty line (b == 0), continue to next line
    ld      a, b
    or      a
    jr      z, mgl_start_line
    ; Non-empty line read!
    or      a                   ; Clear carry (NC = success)
    ret

mgl_eof_check:
    ; EOF was hit. Check if we accumulated any characters in line
    ld      a, b
    or      a
    jr      z, mgl_eof_done     ; Empty line and EOF -> return Carry set
    xor     a
    ld      (de), a             ; Null-terminate last line
    ret                         ; NC = success

mgl_eof_done:
    scf
    ret

; ------------------------------------------------------------------------------
; map_parse_line - Parse map_line_buf into map_entry_path and map_entry_lba
; Line format: <relative_path>,<hex_lba>[,<flags>]
; Exit:
;   Carry clear (NC): Successfully parsed path and LBA
;   Carry set (C):    Syntax error (no comma or invalid LBA)
; Modifies: AF, BC, DE, HL
; ------------------------------------------------------------------------------
map_parse_line:
    ld      hl, map_line_buf
    ld      de, map_entry_path
    ld      b, 0                ; Path length

mpl_copy_path:
    ld      a, (hl)
    or      a
    jr      z, mpl_syntax_err   ; Null before comma -> error
    cp      ','
    jr      z, mpl_found_comma
    inc     hl
    ld      c, a
    ld      a, b
    cp      62                  ; Max path length
    jr      nc, mpl_copy_path
    ld      a, c
    ld      (de), a
    inc     de
    inc     b
    jr      mpl_copy_path

mpl_found_comma:
    xor     a
    ld      (de), a             ; Null-terminate map_entry_path
    inc     hl                  ; Skip comma

    ; Skip whitespace after comma
mpl_skip_sp:
    ld      a, (hl)
    cp      ' '
    jr      z, mpl_inc_sp
    cp      09h
    jr      z, mpl_inc_sp
    jr      mpl_parse_lba
mpl_inc_sp:
    inc     hl
    jr      mpl_skip_sp

mpl_parse_lba:
    ; Parse 32-bit hex LBA from HL into map_entry_lba
    ex      de, hl              ; DE = pointer to hex string
    call    parse_hex_u32_de
    ret

mpl_syntax_err:
    scf
    ret

; ------------------------------------------------------------------------------
; parse_hex_u32_de - Parse hex string at DE into map_entry_lba (32-bit)
; Stops at comma, space, CR, LF, or null terminator.
; Exit:
;   Carry clear (NC) = success
;   Carry set (C)    = invalid hex
; ------------------------------------------------------------------------------
parse_hex_u32_de:
    ; Zero map_entry_lba
    xor     a
    ld      (map_entry_lba+0), a
    ld      (map_entry_lba+1), a
    ld      (map_entry_lba+2), a
    ld      (map_entry_lba+3), a

    ; Skip "0x" or "0X" if present
    ld      a, (de)
    cp      '0'
    jr      nz, ph_loop
    inc     de
    ld      a, (de)
    cp      'x'
    jr      z, ph_skip_x
    cp      'X'
    jr      z, ph_skip_x
    dec     de                  ; Restore DE if not "0x"
    jr      ph_loop

ph_skip_x:
    inc     de

ph_loop:
    ld      a, (de)
    or      a
    ret     z                   ; Null: done (NC)
    cp      ','
    ret     z                   ; Comma: done (NC)
    cp      ' '
    ret     z                   ; Space: done (NC)
    cp      09h
    ret     z                   ; Tab: done (NC)
    cp      0Dh
    ret     z                   ; CR: done (NC)
    cp      0Ah
    ret     z                   ; LF: done (NC)

    cp      '0'
    jr      c, ph_err
    cp      '9' + 1
    jr      c, ph_digit
    cp      'A'
    jr      c, ph_err
    cp      'F' + 1
    jr      c, ph_upper
    cp      'a'
    jr      c, ph_err
    cp      'f' + 1
    jr      c, ph_lower
    jr      ph_err

ph_digit:
    sub     '0'
    jr      ph_add
ph_upper:
    sub     'A' - 10
    jr      ph_add
ph_lower:
    sub     'a' - 10

ph_add:
    push    af
    call    map_shl32_4         ; map_entry_lba <<= 4
    pop     af
    ld      hl, map_entry_lba
    add     a, (hl)
    ld      (hl), a
    inc     de
    jr      ph_loop

ph_err:
    scf
    ret

; Shift map_entry_lba left by 4 bits
map_shl32_4:
    ld      b, 4
mshl_loop:
    xor     a
    ld      hl, map_entry_lba
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)
    inc     hl
    rl      (hl)
    djnz    mshl_loop
    ret

; ------------------------------------------------------------------------------
; String Comparison Routines
; ------------------------------------------------------------------------------

; Compare A and B case-insensitively (ASCII A-Z, a-z)
; Returns Z flag if equal, NZ if different. Preserves BC, DE, HL.
; Note: On Z match, A is zero iff both input chars were zero.
case_cmp:
    cp      b
    ret     z
    push    bc
    cp      'a'
    jr      c, cc_a_done
    cp      'z' + 1
    jr      nc, cc_a_done
    sub     20h                 ; To uppercase
cc_a_done:
    ld      c, a
    ld      a, b
    cp      'a'
    jr      c, cc_b_done
    cp      'z' + 1
    jr      nc, cc_b_done
    sub     20h                 ; To uppercase
cc_b_done:
    cp      c
    pop     bc
    ret

; Compute length of null-terminated string at HL -> A (and B)
str_len:
    push    hl
    ld      b, 0
slen_loop:
    ld      a, (hl)
    or      a
    jr      z, slen_done
    inc     hl
    inc     b
    jr      slen_loop
slen_done:
    ld      a, b
    pop     hl
    ret

; Compare null-terminated string at HL against DE (case-insensitive)
; Returns Z if match, NZ if different
str_cmp_ci:
    push    hl
    push    de
scc_loop:
    ld      a, (de)
    ld      b, (hl)
    call    case_cmp
    jr      nz, scc_diff
    or      a
    jr      z, scc_match        ; Both reached null terminator
    inc     de
    inc     hl
    jr      scc_loop
scc_diff:
    pop     de
    pop     hl
    or      1                   ; Set NZ
    ret
scc_match:
    pop     de
    pop     hl
    xor     a                   ; Set Z
    ret

; Check if string at HL (target full path) matches query at DE
; Matches either exact string or path suffix preceded by '/'
; Example: HL = "base/boot.dsk" matches DE = "base/boot.dsk" or "boot.dsk"
; Returns Z if match, NZ if different
str_suffix_match:
    push    hl
    push    de

    ; Optional leading '/' normalization on query DE
    ld      a, (de)
    cp      '/'
    jr      nz, ssm_no_lead_slash
    inc     de
ssm_no_lead_slash:

    ; Calculate length of HL (str1)
    call    str_len
    ld      c, a                ; C = len(HL)

    ; Calculate length of DE (str2)
    ex      de, hl
    call    str_len
    ex      de, hl
    ld      b, a                ; B = len(DE)

    ; If len(HL) < len(DE): no match
    ld      a, c
    cp      b
    jr      c, ssm_diff

    ; If len(HL) == len(DE): exact match check
    jr      z, ssm_exact

    ; len(HL) > len(DE): suffix match check
    ; Calculate offset: diff = C - B
    ld      a, c
    sub     b                   ; A = diff
    push    de                  ; Save normalized DE
    ld      e, a
    ld      d, 0
    add     hl, de              ; HL -> suffix in str1
    pop     de                  ; Restore normalized DE

    ; Check if preceding char is '/'
    dec     hl
    ld      a, (hl)
    inc     hl
    cp      '/'
    jr      nz, ssm_diff

    ; Suffix is preceded by '/': compare suffix with DE
    call    str_cmp_ci
    jr      ssm_exit

ssm_diff:
    pop     de
    pop     hl
    or      1                   ; Set NZ
    ret

ssm_exact:
    call    str_cmp_ci
ssm_exit:
    pop     de
    pop     hl
    ret

; ------------------------------------------------------------------------------
; Shared Static Buffers
; ------------------------------------------------------------------------------
sd_cmd_pkt:         DS  6
cur_map_lba:        DS  4
cur_map_file_rem:   DS  2
cur_map_blk_rem:    DS  2
cur_map_ptr:        DS  2
map_block_buf:      DS  512
map_line_buf:       DS  80
map_entry_path:     DS  64
map_entry_lba:      DS  4
