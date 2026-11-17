; ==============================================================================
; FNSTATUS.S - Query and Display FujiNet Adapter Configuration
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; Origin: 0x2600 (Authentic TRSDOS 6.3 / LS-DOS 6.3 Command Module)
;
; Syntax:
;   FNSTATUS
; ==============================================================================

    INCLUDE "svc.inc"
    INCLUDE "equ.inc"
    INCLUDE "fujinet.inc"

    ORG     USER_PROG_BASE      ; 2600h

start:
    ; Initialize UART1 and flush receiver
    call    fuji_init

    ; Issue FUJI_CMD_GET_ADAPTER_CFG (0xE8)
    ld      a, FUJI_CMD_GET_ADAPTER_CFG
    call    fuji_cmd
    jr      c, stat_comm_err

    ; Check command execution status
    call    fuji_get_error
    jr      c, stat_err

    ; Read 140-byte AdapterConfig structure
    ld      hl, cfg_buf
    ld      bc, ADAPTER_CONFIG_SIZE
    call    fuji_get_response
    jr      c, stat_comm_err

    ; Display formatted adapter configuration
    ld      hl, msg_hdr
    _DSPLY
    ld      hl, msg_div
    _DSPLY

    ; 1. Firmware version
    ld      hl, msg_fw
    _DSPLY
    ld      hl, cfg_buf + CFG_OFF_FW_VER
    call    fuji_print_str
    call    fuji_newline

    ; 2. Hostname
    ld      hl, msg_host
    _DSPLY
    ld      hl, cfg_buf + CFG_OFF_HOSTNAME
    call    fuji_print_str
    call    fuji_newline

    ; 3. Wi-Fi SSID
    ld      hl, msg_ssid
    _DSPLY
    ld      hl, cfg_buf + CFG_OFF_SSID
    call    fuji_print_str
    call    fuji_newline

    ; 4. Local IP Address
    ld      hl, msg_ip
    _DSPLY
    ld      hl, cfg_buf + CFG_OFF_IP
    call    fuji_print_ipv4
    call    fuji_newline

    ; 5. Gateway
    ld      hl, msg_gw
    _DSPLY
    ld      hl, cfg_buf + CFG_OFF_GATEWAY
    call    fuji_print_ipv4
    call    fuji_newline

    ; 6. Netmask
    ld      hl, msg_nm
    _DSPLY
    ld      hl, cfg_buf + CFG_OFF_NETMASK
    call    fuji_print_ipv4
    call    fuji_newline

    ; 7. DNS Server
    ld      hl, msg_dns
    _DSPLY
    ld      hl, cfg_buf + CFG_OFF_DNS
    call    fuji_print_ipv4
    call    fuji_newline

    ; 8. MAC Address
    ld      hl, msg_mac
    _DSPLY
    ld      hl, cfg_buf + CFG_OFF_MAC
    call    fuji_print_mac
    call    fuji_newline

    ld      hl, msg_div
    _DSPLY
    _EXIT

stat_comm_err:
    ld      a, 0FFh
    call    fuji_print_error
    _EXIT

stat_err:
    call    fuji_print_error
    _EXIT

; -----------------------------------------------------------------------------
; Display Labels & Formatting Strings
; -----------------------------------------------------------------------------
msg_hdr:    DB  "FujiNet Adapter Status", CR, 03h
msg_div:    DB  "--------------------------------------------------", CR, 03h
msg_fw:     DB  "Firmware:      ", 03h
msg_host:   DB  "Hostname:      ", 03h
msg_ssid:   DB  "SSID:          ", 03h
msg_ip:     DB  "IP Address:    ", 03h
msg_gw:     DB  "Gateway:       ", 03h
msg_nm:     DB  "Netmask:       ", 03h
msg_dns:    DB  "DNS Server:    ", 03h
msg_mac:    DB  "MAC Address:   ", 03h

; -----------------------------------------------------------------------------
; Dynamic Buffers
; -----------------------------------------------------------------------------
cfg_buf:    DS  ADAPTER_CONFIG_SIZE

; -----------------------------------------------------------------------------
; FujiNet Library
; -----------------------------------------------------------------------------
    INCLUDE "fuji_lib.inc"

    END     start
