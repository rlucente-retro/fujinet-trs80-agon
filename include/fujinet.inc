; ==============================================================================
; fujinet.inc - FujiNet and DriveWire Protocol Equates & Structure Layouts
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
; ==============================================================================

; ------------------------------------------------------------------------------
; DriveWire & FujiNet Bus Wire Protocol Opcodes
; ------------------------------------------------------------------------------
OP_NOP          EQU     00h     ; DriveWire NOP opcode
OP_TIME         EQU     23h     ; '#' - DriveWire query server time
OP_WRITE        EQU     57h     ; 'W' - DriveWire write 256-byte sector
OP_READEX       EQU     0D2h    ; Extended 256-byte sector read with 16-bit checksum
OP_FUJI         EQU     0E2h    ; FujiNet configuration / management command prefix
OP_TERM         EQU     0FBh    ; Terminal pass-through
OP_RESET        EQU     0FEh    ; DriveWire server reset

; ------------------------------------------------------------------------------
; FujiNet Configuration Sub-Commands (Transmitted after OP_FUJI 0xE2)
; ------------------------------------------------------------------------------
FUJI_CMD_POLL           EQU     00h     ; Poll FujiNet active readiness
FUJI_CMD_GET_RESPONSE   EQU     01h     ; Read transaction payload response buffer
FUJI_CMD_GET_ERROR      EQU     02h     ; Read transaction status / error code
FUJI_CMD_SCAN_NETWORKS  EQU     11h     ; Trigger scan for Wi-Fi access points
FUJI_CMD_GET_SCAN_RES   EQU     12h     ; Fetch list of scanned Wi-Fi access points
FUJI_CMD_SET_SSID       EQU     13h     ; Configure Wi-Fi SSID and passphrase
FUJI_CMD_GET_SSID       EQU     14h     ; Query currently configured Wi-Fi SSID
FUJI_CMD_GET_WIFI_STAT  EQU     15h     ; Query Wi-Fi connection status (03 = connected)
FUJI_CMD_DISCONNECT     EQU     16h     ; Disconnect from current Wi-Fi network
FUJI_CMD_GET_DEV_FILE   EQU     0DAh    ; Fetch full 256-byte path of mounted image
FUJI_CMD_GET_ADAPTER_CFG EQU    0E8h    ; Query 140-byte AdapterConfig block
FUJI_CMD_UNMOUNT_IMAGE  EQU     0E9h    ; Unmount disk image from specified device slot
FUJI_CMD_GET_TIME       EQU     0EAh    ; Query FujiNet RTC / network time block
FUJI_CMD_SET_TIME       EQU     0EBh    ; Write network time block
FUJI_CMD_CREATE_IMAGE   EQU     0ECh    ; Create new blank disk image on host
FUJI_CMD_GET_DEV_SLOTS  EQU     0F2h    ; Query device slot table (152B / 304B)
FUJI_CMD_SET_HOST_SLOTS EQU     0F3h    ; Write complete 256-byte host table
FUJI_CMD_GET_HOST_SLOTS EQU     0F4h    ; Query 256-byte host slot table (8 slots x 32B)
FUJI_CMD_SET_DEVICE_SLOT EQU    0F7h    ; Assign disk image to device slot
FUJI_CMD_MOUNT_IMAGE    EQU     0F8h    ; Mount assigned image into device slot
FUJI_CMD_MOUNT_HOST     EQU     0F9h    ; Connect to remote host / mount TNFS export
FUJI_CMD_WRITE_HOSTS    EQU     0FAh    ; Persist host slot table to NVS / flash
FUJI_CMD_WRITE_DEVICES  EQU     0FBh    ; Persist device slot table to NVS / flash
FUJI_CMD_RESET          EQU     0FFh    ; Software reset FujiNet adapter state machine

; ------------------------------------------------------------------------------
; FujiNet Transaction Status Return Codes (Response to FUJI_CMD_GET_ERROR)
; ------------------------------------------------------------------------------
FUJI_STAT_OK            EQU     01h     ; Transaction succeeded (FUJI$E.OK)
FUJI_STAT_ERROR         EQU     90h     ; General transaction failure
FUJI_STAT_NOT_FOUND     EQU     91h     ; Requested host or file not found
FUJI_STAT_BAD_DEV       EQU     92h     ; Invalid device slot specified
FUJI_STAT_TIMEOUT       EQU     93h     ; Network / TNFS communication timeout

; ------------------------------------------------------------------------------
; FujiNet Image Access Modes
; ------------------------------------------------------------------------------
FUJI_MODE_READ          EQU     01h     ; Read-only mode
FUJI_MODE_WRITE         EQU     02h     ; Write-only mode
FUJI_MODE_READWRITE     EQU     03h     ; Read-write mode

; ------------------------------------------------------------------------------
; FujiNet Adapter Configuration Structure (140 bytes total, returned by 0xE8)
; ------------------------------------------------------------------------------
CFG_OFF_SSID            EQU     0       ; Wi-Fi SSID string (33 bytes, null-term)
CFG_LEN_SSID            EQU     33
CFG_OFF_HOSTNAME        EQU     33      ; FujiNet hostname (64 bytes, null-term)
CFG_LEN_HOSTNAME        EQU     64
CFG_OFF_IP              EQU     97      ; Local IPv4 address (4 bytes: a.b.c.d)
CFG_LEN_IP              EQU     4
CFG_OFF_GATEWAY         EQU     101     ; Gateway IPv4 address (4 bytes)
CFG_LEN_GATEWAY         EQU     4
CFG_OFF_NETMASK         EQU     105     ; Subnet mask IPv4 (4 bytes)
CFG_LEN_NETMASK         EQU     4
CFG_OFF_DNS             EQU     109     ; Primary DNS IPv4 (4 bytes)
CFG_LEN_DNS             EQU     4
CFG_OFF_MAC             EQU     113     ; Wi-Fi MAC address (6 bytes)
CFG_LEN_MAC             EQU     6
CFG_OFF_BSSID           EQU     119     ; Access point BSSID (6 bytes)
CFG_LEN_BSSID           EQU     6
CFG_OFF_FW_VER          EQU     125     ; Firmware version string (15 bytes, null-term)
CFG_LEN_FW_VER          EQU     15
ADAPTER_CONFIG_SIZE     EQU     140     ; Total size of adapter config block

; ------------------------------------------------------------------------------
; FujiNet Host Slot Table Layout (returned by 0xF4, updated by 0xF3)
; ------------------------------------------------------------------------------
MAX_HOST_SLOTS          EQU     8       ; 8 remote host slots (:0 to :7)
HOST_SLOT_SIZE          EQU     32      ; 32 bytes per host slot (null-term string)
HOST_TABLE_SIZE         EQU     256     ; 8 * 32 = 256 bytes total table size

; ------------------------------------------------------------------------------
; FujiNet Device Slot Table Layout (returned by 0xF2)
; ------------------------------------------------------------------------------
DEV_OFF_HOST_SLOT       EQU     0       ; Host slot index (0..7, or 0xFF = local)
DEV_OFF_MODE            EQU     1       ; Access mode (1 = RO, 2 = WO, 3 = RW)
DEV_OFF_FILENAME        EQU     2       ; Filename string (36 bytes, null-term)
DEV_SLOT_SIZE           EQU     38      ; 38 bytes per device slot entry

MAX_DEV_SLOTS_STAGE1    EQU     4       ; Stage 1 baseline: 4 virtual device slots
MAX_DEV_SLOTS_STAGE2    EQU     8       ; Stage 2 multi-drive: 8 virtual device slots
DEV_TABLE_SIZE_STAGE1   EQU     152     ; 4 * 38 = 152 bytes
DEV_TABLE_SIZE_STAGE2   EQU     304     ; 8 * 38 = 304 bytes

; ------------------------------------------------------------------------------
; FujiNet Network / Modem Device Commands (Devices 0x71 - 0x78)
; ------------------------------------------------------------------------------
NET_CMD_OPEN            EQU     01h     ; Open network connection
NET_CMD_CLOSE           EQU     02h     ; Close network connection
NET_CMD_READ            EQU     03h     ; Read network stream bytes
NET_CMD_WRITE           EQU     04h     ; Write network stream bytes
NET_CMD_STATUS          EQU     05h     ; Query network channel status
NET_CMD_SET_BAUD        EQU     06h     ; Set virtual baud rate
NET_CMD_PARSE_URL       EQU     07h     ; Parse URL string
