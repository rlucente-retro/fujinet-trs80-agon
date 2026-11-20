/*
 * sdmap.c - SD Map Auto-Discovery, Parsing, Recursive Rescan & fnboot.ini Config
 */

#include "fnboot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int fn_casecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

bool fn_ends_with_ci(const char *s, const char *suffix) {
    size_t sl = strlen(s);
    size_t fl = strlen(suffix);
    if (sl < fl) return false;
    return fn_casecmp(s + sl - fl, suffix) == 0;
}

static const char *strip_leading_slash(const char *p) {
    while (p[0] == '/' || (p[0] == '.' && p[1] == '/')) {
        if (p[0] == '/') p++;
        else p += 2;
    }
    return p;
}

#ifndef HOST_TEST

#include <agon/mos.h>

static bool is_disk_ext(const char *name) {
    if (!name || name[0] == '.') return false;
    return fn_ends_with_ci(name, ".dsk") ||
           fn_ends_with_ci(name, ".jv1") ||
           fn_ends_with_ci(name, ".jv3") ||
           fn_ends_with_ci(name, ".hdv");
}

static DIR     g_dir;
static FILINFO g_fno;

bool fnboot_resolve_lba(const char *path, uint32_t *out_lba, uint32_t *out_size, bool *out_contig) {
    uint8_t fh = mos_fopen(path, FA_OPEN_EXISTING | FA_READ);
    if (fh == 0) return false;

    FIL *fil = mos_getfil(fh);
    if (!fil || !fil->obj.fs || fil->obj.sclust < 2u) {
        mos_fclose(fh);
        return false;
    }

    FATFS *fs = (FATFS *)fil->obj.fs;
    uint32_t sclust   = fil->obj.sclust;
    uint32_t fsize    = fil->obj.objsize;
    uint32_t csize    = fs->csize;
    uint32_t database = fs->database;

    if (csize == 0 || database == 0) {
        mos_fclose(fh);
        return false;
    }

    *out_lba  = database + (sclust - 2u) * csize;
    *out_size = fsize;

    bool contig = true;
    uint32_t cbytes = csize * 512u;
    uint32_t nclust = (fsize + cbytes - 1u) / cbytes;
    if (nclust > 1u && (fil->obj.stat & 0x03u) != 0x02u) {
        for (uint32_t k = 1u; k < nclust; k++) {
            uint32_t ofs = k * cbytes;
            if (ofs + 1u < fsize) ofs += 1u;
            if (mos_flseek(fh, ofs) != FR_OK || fil->clust != sclust + k) {
                contig = false;
                break;
            }
        }
    }

    if (out_contig) *out_contig = contig;
    mos_fclose(fh);
    return true;
}

static bool read_text_file(const char *path, char *buf, size_t max_len, size_t *out_len) {
    uint8_t fh = mos_fopen(path, FA_OPEN_EXISTING | FA_READ);
    if (fh == 0) return false;
    uint24_t n = mos_fread(fh, buf, (uint24_t)(max_len - 1));
    mos_fclose(fh);
    buf[n] = '\0';
    if (out_len) *out_len = (size_t)n;
    return true;
}

static bool write_text_file(const char *path, const char *buf, size_t len) {
    uint8_t fh = mos_fopen(path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fh == 0) return false;
    uint24_t n = mos_fwrite(fh, (char *)buf, (uint24_t)len);
    mos_fclose(fh);
    return (size_t)n == len;
}

bool fnboot_rescan_sd(fnboot_ctx_t *ctx, const char *scan_dir, const char *out_map) {
    if (!scan_dir || !scan_dir[0]) {
        if (ffs_dopen(&g_dir, "/disks") == FR_OK) {
            ffs_dclose(&g_dir);
            scan_dir = "/disks";
        } else {
            scan_dir = ".";
        }
    }
    char def_out[MAX_PATH_LEN];
    if (!out_map || !out_map[0]) {
        if (strcmp(scan_dir, "/") == 0) snprintf(def_out, sizeof(def_out), "/sdmap.txt");
        else if (strcmp(scan_dir, ".") == 0) snprintf(def_out, sizeof(def_out), "sdmap.txt");
        else snprintf(def_out, sizeof(def_out), "%s/sdmap.txt", scan_dir);
        out_map = def_out;
    }

    char queue[24][MAX_PATH_LEN];
    int qh = 0, qt = 0;
    strncpy(queue[qt++], scan_dir, MAX_PATH_LEN - 1);
    queue[0][MAX_PATH_LEN - 1] = '\0';

    uint8_t out_fh = mos_fopen(out_map, FA_CREATE_ALWAYS | FA_WRITE);
    if (out_fh == 0) return false;

    char full[MAX_PATH_LEN];
    char line[140];

    while (qh < qt) {
        const char *cur = queue[qh++];
        if (ffs_dopen(&g_dir, cur) != FR_OK) continue;
        while (ffs_dread(&g_dir, &g_fno) == FR_OK && g_fno.fname[0] != '\0') {
            if (g_fno.fname[0] == '.') continue;
            if (strcmp(cur, "/") == 0) snprintf(full, sizeof(full), "/%s", g_fno.fname);
            else if (strcmp(cur, ".") == 0) snprintf(full, sizeof(full), "%s", g_fno.fname);
            else snprintf(full, sizeof(full), "%s/%s", cur, g_fno.fname);

            if (g_fno.fattrib & AM_DIR) {
                if (qt < 24) {
                    strncpy(queue[qt], full, MAX_PATH_LEN - 1);
                    queue[qt][MAX_PATH_LEN - 1] = '\0';
                    qt++;
                }
            } else if (is_disk_ext(g_fno.fname)) {
                uint32_t lba = 0, sz = 0;
                bool ctg = false;
                if (fnboot_resolve_lba(full, &lba, &sz, &ctg)) {
                    const char *rel = strip_leading_slash(full);
                    int n = snprintf(line, sizeof(line), "%s,0x%08lX\n", rel, (unsigned long)lba);
                    if (n > 0) mos_fwrite(out_fh, line, (uint24_t)n);
                }
            }
        }
        ffs_dclose(&g_dir);
    }

    mos_fclose(out_fh);
    return fnboot_load_map_file(ctx, out_map);
}

#else /* HOST_TEST shims */

#include <sys/stat.h>

bool fnboot_resolve_lba(const char *path, uint32_t *out_lba, uint32_t *out_size, bool *out_contig) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *out_lba = 0x00000EA4u;
    *out_size = (uint32_t)st.st_size;
    if (out_contig) *out_contig = true;
    return true;
}

static bool read_text_file(const char *path, char *buf, size_t max_len, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    size_t n = fread(buf, 1, max_len - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return true;
}

static bool write_text_file(const char *path, const char *buf, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    size_t n = fwrite(buf, 1, len, fp);
    fclose(fp);
    return n == len;
}

bool fnboot_rescan_sd(fnboot_ctx_t *ctx, const char *scan_dir, const char *out_map) {
    (void)ctx; (void)scan_dir; (void)out_map;
    return false;
}

#endif

/* --------------------------------------------------------------------------
 * sdmap.txt Parser & Auto-Discovery
 * -------------------------------------------------------------------------- */

static char g_text_buf[4096];

bool fnboot_load_map_file(fnboot_ctx_t *ctx, const char *path) {
    uint32_t lba = 0, sz = 0;
    bool contig = false;
    if (!fnboot_resolve_lba(path, &lba, &sz, &contig)) {
        return false;
    }

    size_t nread = 0;
    if (!read_text_file(path, g_text_buf, sizeof(g_text_buf), &nread)) {
        return false;
    }

    ctx->sd_count = 0;
    char *p = g_text_buf;
    while (*p && ctx->sd_count < MAX_SD_IMAGES) {
        char *eol = p;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        char saved = *eol;
        *eol = '\0';

        char *comma = strchr(p, ',');
        if (comma && comma > p) {
            *comma = '\0';
            const char *lba_str = comma + 1;
            while (*lba_str == ' ' || *lba_str == '\t') lba_str++;
            uint32_t entry_lba = (uint32_t)strtoul(lba_str, NULL, 16);
            if (entry_lba != 0) {
                sd_image_t *img = &ctx->sd_images[ctx->sd_count++];
                strncpy(img->path, p, MAX_PATH_LEN - 1);
                img->path[MAX_PATH_LEN - 1] = '\0';
                img->base_lba = entry_lba;
            }
        }

        if (saved == '\0') break;
        p = eol + 1;
        while (*p == '\r' || *p == '\n') p++;
    }

    strncpy(ctx->map_path, path, MAX_PATH_LEN - 1);
    ctx->map_path[MAX_PATH_LEN - 1] = '\0';
    ctx->map_lba    = lba;
    ctx->map_len    = (uint16_t)((sz > 65535u) ? 65535u : sz);
    ctx->map_contig = contig;
    ctx->map_found  = true;
    return true;
}

/*
 * Discover active sdmap.txt following documented Section 7.2 probe order:
 *   1. CLI -m <mapfile>
 *   2. MAPFILE= from fnboot.ini / /fnboot.cfg (already in ctx->map_path)
 *   3. ./sdmap.txt -> /disks/sdmap.txt -> /trs80/sdmap.txt -> /sdmap.txt
 */
bool fnboot_discover_map(fnboot_ctx_t *ctx) {
    ctx->map_found = false;
    ctx->map_lba   = 0;
    ctx->map_len   = 0;
    ctx->sd_count  = 0;

    if (ctx->cli_map_override[0]) {
        if (fnboot_load_map_file(ctx, ctx->cli_map_override)) return true;
    }
    if (ctx->map_path[0]) {
        if (fnboot_load_map_file(ctx, ctx->map_path)) return true;
    }

    static const char *const candidates[] = {
        "sdmap.txt",
        "/disks/sdmap.txt",
        "/trs80/sdmap.txt",
        "/sdmap.txt"
    };
    for (int i = 0; i < 4; i++) {
        if (fnboot_load_map_file(ctx, candidates[i])) {
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------------------------------
 * fnboot.ini / fnboot.cfg Persistence
 * -------------------------------------------------------------------------- */

static void trim_line(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' ||
                       s[len - 1] == ' '  || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static bool lookup_sd_lba(const fnboot_ctx_t *ctx, const char *query, uint32_t *out_lba, char *out_path) {
    const char *q = strip_leading_slash(query);
    for (int i = 0; i < ctx->sd_count; i++) {
        if (fn_casecmp(ctx->sd_images[i].path, q) == 0 ||
            fn_ends_with_ci(ctx->sd_images[i].path, q)) {
            *out_lba = ctx->sd_images[i].base_lba;
            if (out_path) {
                strncpy(out_path, ctx->sd_images[i].path, MAX_PATH_LEN - 1);
                out_path[MAX_PATH_LEN - 1] = '\0';
            }
            return true;
        }
    }
    return false;
}

bool fnboot_load_ini(fnboot_ctx_t *ctx, const char *ini_path) {
    size_t nread = 0;
    if (!read_text_file(ini_path, g_text_buf, sizeof(g_text_buf), &nread)) {
        return false;
    }

    /* Pass 1: parse global keys (including MAPFILE) before DRV entries */
    char *p = g_text_buf;
    while (*p) {
        char *eol = p;
        while (*eol && *eol != '\n') eol++;
        char saved = *eol;
        *eol = '\0';
        trim_line(p);

        if (p[0] && p[0] != '#' && p[0] != ';') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char *key = p;
                char *val = eq + 1;
                if (fn_casecmp(key, "MAPFILE") == 0 && !ctx->cli_map_override[0]) {
                    strncpy(ctx->map_path, val, MAX_PATH_LEN - 1);
                    ctx->map_path[MAX_PATH_LEN - 1] = '\0';
                } else if (fn_casecmp(key, "KERNEL") == 0) {
                    strncpy(ctx->kernel_path, val, MAX_PATH_LEN - 1);
                    ctx->kernel_path[MAX_PATH_LEN - 1] = '\0';
                } else if (fn_casecmp(key, "COLOR") == 0) {
                    int c = atoi(val);
                    if (c >= 1 && c <= 7) ctx->term_color = (uint8_t)c;
                } else if (fn_casecmp(key, "FONT") == 0) {
                    ctx->use_font1 = (atoi(val) != 0);
                } else if (fn_casecmp(key, "VT100") == 0) {
                    ctx->use_vt100 = (atoi(val) != 0);
                }
                *eq = '=';
            }
        }
        if (saved == '\0') break;
        p = eol + 1;
    }

    /* Load SD map now so DRV0..DRV7 can resolve SD paths */
    fnboot_discover_map(ctx);

    /* Re-read ini for Pass 2 (DRV0..DRV7) since load_map_file reused g_text_buf */
    if (!read_text_file(ini_path, g_text_buf, sizeof(g_text_buf), &nread)) {
        return true;
    }
    p = g_text_buf;
    while (*p) {
        char *eol = p;
        while (*eol && *eol != '\n') eol++;
        char saved = *eol;
        *eol = '\0';
        trim_line(p);

        if ((p[0] == 'D' || p[0] == 'd') &&
            (p[1] == 'R' || p[1] == 'r') &&
            (p[2] == 'V' || p[2] == 'v') &&
            p[3] >= '0' && p[3] <= '7' && p[4] == '=') {
            int drv = p[3] - '0';
            char *val = p + 5;
            drive_cfg_t *dc = &ctx->drives[drv];

            if (fn_casecmp(val, "OFFLINE") == 0) {
                dc->target = DRV_TARGET_OFFLINE;
                dc->sd_lba = 0;
                dc->label[0] = '\0';
            } else if ((val[0] == 'D' || val[0] == 'd') &&
                       (val[1] == 'W' || val[1] == 'w') && val[2] == ':') {
                dc->target  = DRV_TARGET_DW;
                dc->dw_slot = (uint8_t)atoi(val + 3);
                dc->mode    = 1; /* RO default for Stage 1 DW */
                snprintf(dc->label, sizeof(dc->label), "FujiNet Slot %u", dc->dw_slot);
            } else if ((val[0] == 'S' || val[0] == 's') &&
                       (val[1] == 'D' || val[1] == 'd') && val[2] == ':') {
                char *spec = val + 3;
                uint8_t mode = 3; /* RW default */
                char *col = strrchr(spec, ':');
                if (col && (fn_casecmp(col + 1, "RO") == 0 || fn_casecmp(col + 1, "RW") == 0)) {
                    mode = (fn_casecmp(col + 1, "RO") == 0) ? 1 : 3;
                    *col = '\0';
                }
                uint32_t lba = 0;
                char resolved[MAX_PATH_LEN];
                if (lookup_sd_lba(ctx, spec, &lba, resolved)) {
                    dc->target = DRV_TARGET_SD;
                    dc->mode   = mode;
                    dc->sd_lba = lba;
                    strncpy(dc->label, resolved, MAX_PATH_LEN - 1);
                    dc->label[MAX_PATH_LEN - 1] = '\0';
                } else if (spec[0] == '0' && (spec[1] == 'x' || spec[1] == 'X')) {
                    dc->target = DRV_TARGET_SD;
                    dc->mode   = mode;
                    dc->sd_lba = (uint32_t)strtoul(spec, NULL, 16);
                    snprintf(dc->label, sizeof(dc->label), "LBA 0x%08lX", (unsigned long)dc->sd_lba);
                }
            }
        }
        if (saved == '\0') break;
        p = eol + 1;
    }

    return true;
}

bool fnboot_save_ini(const fnboot_ctx_t *ctx, const char *ini_path) {
    int pos = 0;
    int cap = (int)sizeof(g_text_buf);

    pos += snprintf(g_text_buf + pos, (size_t)(cap - pos),
                    "# FNBOOT configuration\n"
                    "KERNEL=%s\n"
                    "MAPFILE=%s\n"
                    "COLOR=%u\n"
                    "FONT=%u\n"
                    "VT100=%u\n",
                    ctx->kernel_path,
                    ctx->map_path,
                    ctx->term_color,
                    ctx->use_font1 ? 1u : 0u,
                    ctx->use_vt100 ? 1u : 0u);

    for (int i = 0; i < MAX_DRIVES && pos < cap - 128; i++) {
        const drive_cfg_t *dc = &ctx->drives[i];
        if (dc->target == DRV_TARGET_DW) {
            pos += snprintf(g_text_buf + pos, (size_t)(cap - pos),
                            "DRV%d=DW:%u\n", i, dc->dw_slot);
        } else if (dc->target == DRV_TARGET_SD && dc->sd_lba != 0) {
            pos += snprintf(g_text_buf + pos, (size_t)(cap - pos),
                            "DRV%d=SD:%s:%s\n", i, dc->label,
                            (dc->mode == 1) ? "RO" : "RW");
        } else {
            pos += snprintf(g_text_buf + pos, (size_t)(cap - pos),
                            "DRV%d=OFFLINE\n", i);
        }
    }

    return write_text_file(ini_path, g_text_buf, (size_t)pos);
}
