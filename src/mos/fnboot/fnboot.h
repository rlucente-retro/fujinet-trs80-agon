/*
 * fnboot.h - FujiNet & MicroSD Pre-Boot Visual Manager for TRS-80 / Agon Light 2
 * Target: Quark MOS / Console8 MOS (eZ80 ADL Mode @ 0x040000, /bin/fnboot.bin)
 *         Also compiles natively on host with -DHOST_TEST for automated testing.
 */

#ifndef FNBOOT_H
#define FNBOOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* TRSDOS.bin fixed lowcore mailbox offsets (verified against TRSDOS.lst) */
#define TRSDOS_KERNEL_SIZE      66048u
#define OFF_DCT0                0x0470u   /* 8 x 10B DCTs (0x0470..0x04BF) */
#define DCT_SIZE                10u
#define OFF_DRV_TARGET_TABLE    0x0E76u   /* 8 bytes: 0=SD, 1=DW, 2=MEMDISK */
#define OFF_DW_OFFSET           0x0F78u   /* 8 bytes: 1=DiskDISK, 0=JV1 */
#define OFF_SDDVR_INITED        0xF400u   /* 1 byte */
#define OFF_SDDVR_BASE_LBA      0xF408u   /* 8 x 4B little-endian Base LBAs */
#define OFF_SDDVR_MAP_LBA       0xF428u   /* 4B little-endian sdmap.txt Base LBA */
#define OFF_SDDVR_MAP_LEN       0xF42Cu   /* 2B little-endian sdmap.txt byte len */

/* Drive targets */
#define DRV_TARGET_SD           0
#define DRV_TARGET_DW           1
#define DRV_TARGET_MEMDISK      2
#define DRV_TARGET_OFFLINE      0xFF

/* FujiNet OP_FUJI commands */
#define OP_FUJI                 0xE2
#define FUJI_CMD_POLL           0x00
#define FUJI_CMD_GET_RESPONSE   0x01
#define FUJI_CMD_GET_ERROR      0x02
#define FUJI_CMD_GET_DEV_FILE   0xDA
#define FUJI_CMD_SET_DEV_FILE   0xE2
#define FUJI_CMD_GET_ADAPTER    0xE8
#define FUJI_CMD_UNMOUNT_IMG    0xE9
#define FUJI_CMD_GET_DEV_SLOTS  0xF2
#define FUJI_CMD_GET_HOST_SLOTS 0xF4
#define FUJI_CMD_CLOSE_DIR      0xF5
#define FUJI_CMD_READ_DIR_ENT   0xF6
#define FUJI_CMD_OPEN_DIR       0xF7
#define FUJI_CMD_MOUNT_IMG      0xF8
#define FUJI_CMD_MOUNT_HOST     0xF9

#define FUJI_STAT_OK            0x01

/* Limits */
#define MAX_SD_IMAGES           64
#define MAX_PATH_LEN            160
#define MAX_DRIVES              8

/* SD catalog entry parsed from sdmap.txt */
typedef struct {
    char     path[MAX_PATH_LEN];
    uint32_t base_lba;
} sd_image_t;

/* Per-drive pre-boot configuration */
typedef struct {
    uint8_t  target;              /* DRV_TARGET_DW, DRV_TARGET_SD, or DRV_TARGET_OFFLINE */
    uint8_t  mode;                /* 1 = RO, 3 = RW */
    uint8_t  dw_slot;             /* FujiNet device slot (0..7) when DRV_TARGET_DW */
    uint32_t sd_lba;              /* Base LBA when DRV_TARGET_SD */
    char     label[MAX_PATH_LEN]; /* Display path or FujiNet slot filename */
} drive_cfg_t;

/* FujiNet adapter summary */
typedef struct {
    bool     online;
    char     ssid[34];
    char     hostname[65];
    uint8_t  ip[4];
    char     firmware[16];
    char     hosts[8][33];
    struct {
        uint8_t host_slot;
        uint8_t mode;
        char    file[37];
    } devs[4];
} fuji_state_t;

/* Global FNBOOT session state */
typedef struct {
    char         kernel_path[MAX_PATH_LEN];
    char         map_path[MAX_PATH_LEN];
    char         cli_map_override[MAX_PATH_LEN];
    uint32_t     map_lba;
    uint16_t     map_len;
    bool         map_contig;
    bool         map_found;
    uint8_t      term_color;   /* 1..7 (default 2 = green) */
    bool         use_font1;    /* default true (-f) */
    bool         use_vt100;    /* default false (ADDS25) */
    bool         quick_boot;   /* -b non-interactive boot */

    sd_image_t   sd_images[MAX_SD_IMAGES];
    int          sd_count;

    drive_cfg_t  drives[MAX_DRIVES];
    fuji_state_t fuji;
} fnboot_ctx_t;

/* UART1 / FujiNet client (uart_fuji.c) */
void fuji_mos_init_uart1(void);
bool fuji_mos_probe(fuji_state_t *fs);
bool fuji_mos_mount_host(uint8_t slot);
bool fuji_mos_open_dir(uint8_t host_slot, const char *path);
bool fuji_mos_read_dir_entry(uint8_t max_len, char *out_name);
void fuji_mos_close_dir(void);
bool fuji_mos_set_dev_file(uint8_t dev_slot, uint8_t host_slot, uint8_t mode, const char *path);
bool fuji_mos_mount_image(uint8_t dev_slot, uint8_t mode);

/* SD Map discovery, parser, rescan & config file (sdmap.c) */
bool fnboot_discover_map(fnboot_ctx_t *ctx);
bool fnboot_load_map_file(fnboot_ctx_t *ctx, const char *path);
bool fnboot_rescan_sd(fnboot_ctx_t *ctx, const char *scan_dir, const char *out_map);
bool fnboot_load_ini(fnboot_ctx_t *ctx, const char *ini_path);
bool fnboot_save_ini(const fnboot_ctx_t *ctx, const char *ini_path);
bool fnboot_resolve_lba(const char *path, uint32_t *out_lba, uint32_t *out_size, bool *out_contig);

/* Kernel staging & boot launcher (boot.c) */
bool fnboot_stage_kernel(const fnboot_ctx_t *ctx, const char *out_tmp_path);
int  fnboot_launch(const fnboot_ctx_t *ctx);

/* String helpers */
int  fn_casecmp(const char *a, const char *b);
bool fn_ends_with_ci(const char *s, const char *suffix);

#endif /* FNBOOT_H */
