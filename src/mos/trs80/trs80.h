/*
 * trs80.h - Unified Toolshed-Style Quark MOS Utility for TRS-80 / LS-DOS 6.3
 * Target: Olimex Agon Light 2 (eZ80 ADL Mode MOSlet @ 0x0B0000)
 *         Also builds natively on host with -DHOST_TEST for automated parity testing.
 */

#ifndef TRS80_H
#define TRS80_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Container formats */
typedef enum {
    FMT_UNKNOWN  = 0,
    FMT_DISKDISK = 1,
    FMT_JV1      = 2,
    FMT_JV3      = 3,
    FMT_REED_HDV = 4,
    FMT_FLAT_HDV = 5
} disk_format_t;

/* Abstract host file handle (MOS uint8_t fh or host FILE*) */
typedef struct {
#ifdef HOST_TEST
    void *fp;
#else
    uint8_t fh;
#endif
    bool open;
    bool writable;
    uint32_t size;
} host_file_t;

/* Open TRS-80 disk container */
typedef struct {
    host_file_t hf;
    disk_format_t fmt;
    uint32_t data_offset;     /* 256 for DiskDISK/Reed HDV, 0 for JV1/Flat HDV, 0x2200 for JV3 */
    uint16_t cyls;            /* Number of cylinders (e.g. 40, 80, 203..1024) */
    uint8_t  heads;           /* 1 or 2 (up to 8 for HDV) */
    uint8_t  spt;             /* Sectors per track (10, 18, 32, etc.) */
    uint16_t sec_per_cyl;     /* heads * spt */
    uint8_t  dir_cyl;         /* Directory cylinder (e.g. 1 or 17) */
    uint8_t  grans_per_cyl;   /* Granules per cylinder (e.g. 6, 3, 2) */
    uint8_t  sec_per_gran;    /* Sectors per granule (e.g. 6, 5) */
    uint8_t  dir_sectors;     /* Directory entry sectors (32 on 36-sec cyl, else sec_per_cyl - 2) */
    uint8_t  write_protect;   /* 1 if container header marks write-protected */
    uint8_t  gat[256];        /* Cached GAT sector */
    uint8_t  hit[256];        /* Cached HIT sector */
    bool     gat_loaded;
} trs_disk_t;

/* Decoded LS-DOS 6.3 directory entry (FPDE) */
typedef struct {
    uint8_t  dec;             /* Directory Entry Code (0x00..0xFF) */
    uint8_t  dir_sec;         /* Relative sector within directory cylinder (2..33) */
    uint8_t  entry_idx;       /* Entry index within sector (0..7) */
    uint8_t  status;          /* Byte 0: status/access bits */
    uint8_t  eof_off;         /* Byte 3: EOF byte offset in last sector (0 => 256) */
    uint8_t  lrl;             /* Byte 4: Logical record length */
    char     name[9];         /* Space-trimmed uppercase name (up to 8 chars) */
    char     ext[4];          /* Space-trimmed uppercase ext (up to 3 chars) */
    uint16_t ern;             /* Bytes 20-21: Ending Record Number (sectors) */
    uint8_t  hit_byte;        /* HIT[dec] */
    uint8_t  raw[32];         /* Raw 32-byte FPDE */
} lsdos_entry_t;

/* Extent descriptor */
typedef struct {
    uint8_t cyl;
    uint8_t gran_start;
    uint8_t gran_count;
} lsdos_extent_t;

/* Platform file I/O abstraction (platform_io.c) */
bool     pio_open(host_file_t *hf, const char *path, bool write, bool create);
void     pio_close(host_file_t *hf);
bool     pio_seek(host_file_t *hf, uint32_t offset);
uint32_t pio_read(host_file_t *hf, void *buf, uint32_t len);
uint32_t pio_write(host_file_t *hf, const void *buf, uint32_t len);
bool     pio_exists(const char *path);

/* Disk container abstraction (disk.c) */
const char *disk_format_name(disk_format_t fmt);
bool     disk_open(trs_disk_t *d, const char *path, bool writable);
void     disk_close(trs_disk_t *d);
bool     disk_read_sector(trs_disk_t *d, uint32_t lsn, uint8_t *buf256);
bool     disk_write_sector(trs_disk_t *d, uint32_t lsn, const uint8_t *buf256);
bool     disk_format_new(const char *path, disk_format_t fmt, uint16_t cyls,
                         uint8_t heads, uint8_t spt, uint8_t dir_cyl,
                         const char *volname);

/* LS-DOS 6.3 filesystem engine (lsdos.c) */
uint8_t  lsdos_hit_hash(const char *name8, const char *ext3);
bool     lsdos_parse_filename(const char *spec, char name8[8], char ext3[3]);
void     lsdos_format_name(const lsdos_entry_t *e, char out[14]);
bool     lsdos_load_dir_meta(trs_disk_t *d);
bool     lsdos_save_dir_meta(trs_disk_t *d);
bool     lsdos_read_dec(trs_disk_t *d, uint8_t dec, uint8_t raw32[32]);
bool     lsdos_write_dec(trs_disk_t *d, uint8_t dec, const uint8_t raw32[32]);
bool     lsdos_find_file(trs_disk_t *d, const char *spec, lsdos_entry_t *out);
int      lsdos_collect_extents(trs_disk_t *d, const uint8_t raw32[32],
                               lsdos_extent_t *exts, int max_exts);
uint32_t lsdos_file_byte_size(const lsdos_entry_t *e);
bool     lsdos_match_pattern(const char *pattern, const char *name, const char *ext);

/* High-level subcommands */
int cmd_dir(int argc, char **argv);
int cmd_copy(int argc, char **argv);
int cmd_delete(int argc, char **argv);
int cmd_rename(int argc, char **argv);
int cmd_format(int argc, char **argv);
int cmd_check(int argc, char **argv);
int cmd_free(int argc, char **argv);
int cmd_dump(int argc, char **argv);
int cmd_attr(int argc, char **argv);
int cmd_map(int argc, char **argv);

/* Shared helpers (main.c) */
bool split_comma_spec(const char *arg, char *img_out, size_t img_sz,
                      char *guest_out, size_t guest_sz);
int  str_casecmp(const char *a, const char *b);
bool str_ends_with_ci(const char *s, const char *suffix);

/* Shared 4 KiB scratch buffer (reused across commands to keep MOSlet < 32 KiB) */
extern uint8_t g_io_buf[4096];

#endif /* TRS80_H */
