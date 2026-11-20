/*
 * lsdos.c - LS-DOS 6.3 / TRSDOS 6.x Filesystem Engine & Subcommands
 *
 * Implements: dir, copy, delete, rename, format, check, free, dump, attr
 * Binary-compatible with tools/audit_filesystem.py and tools/inject_file_to_dsk.py.
 */

#include "trs80.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* --------------------------------------------------------------------------
 * Low-level LS-DOS 6.3 primitives
 * -------------------------------------------------------------------------- */

uint8_t lsdos_hit_hash(const char *name8, const char *ext3) {
    uint8_t a = 0;
    for (int i = 0; i < 8; i++) {
        a ^= (uint8_t)name8[i];
        a = (uint8_t)((a << 1) | (a >> 7));
    }
    for (int i = 0; i < 3; i++) {
        a ^= (uint8_t)ext3[i];
        a = (uint8_t)((a << 1) | (a >> 7));
    }
    return (a == 0) ? 1u : a;
}

/* Parse "NAME/EXT" or "NAME.EXT" or "NAME" into space-padded uppercase 8+3 */
bool lsdos_parse_filename(const char *spec, char name8[8], char ext3[3]) {
    if (!spec || !spec[0]) return false;
    memset(name8, ' ', 8);
    memset(ext3, ' ', 3);

    int ni = 0, ei = 0;
    bool in_ext = false;
    for (const char *p = spec; *p; p++) {
        char c = *p;
        if (c == '/' || c == '.') {
            if (in_ext) return false;
            in_ext = true;
            continue;
        }
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (!in_ext) {
            if (ni >= 8) return false;
            name8[ni++] = c;
        } else {
            if (ei >= 3) return false;
            ext3[ei++] = c;
        }
    }
    return ni > 0;
}

void lsdos_format_name(const lsdos_entry_t *e, char out[14]) {
    if (e->ext[0]) {
        snprintf(out, 14, "%s/%s", e->name, e->ext);
    } else {
        snprintf(out, 14, "%s", e->name);
    }
}

bool lsdos_load_dir_meta(trs_disk_t *d) {
    uint32_t gat_lsn = (uint32_t)d->dir_cyl * d->sec_per_cyl;
    if (!disk_read_sector(d, gat_lsn, d->gat)) return false;
    if (!disk_read_sector(d, gat_lsn + 1u, d->hit)) return false;
    d->gat_loaded = true;
    return true;
}

bool lsdos_save_dir_meta(trs_disk_t *d) {
    uint32_t gat_lsn = (uint32_t)d->dir_cyl * d->sec_per_cyl;
    if (!disk_write_sector(d, gat_lsn, d->gat)) return false;
    if (!disk_write_sector(d, gat_lsn + 1u, d->hit)) return false;
    return true;
}

bool lsdos_read_dec(trs_disk_t *d, uint8_t dec, uint8_t raw32[32]) {
    uint8_t sec_idx = dec & 0x1Fu;
    uint8_t ent_idx = (dec >> 5) & 0x07u;
    if (sec_idx >= d->dir_sectors) return false;

    uint32_t lsn = (uint32_t)d->dir_cyl * d->sec_per_cyl + 2u + sec_idx;
    uint8_t sec[256];
    if (!disk_read_sector(d, lsn, sec)) return false;
    memcpy(raw32, &sec[(uint16_t)ent_idx * 32u], 32);
    return true;
}

bool lsdos_write_dec(trs_disk_t *d, uint8_t dec, const uint8_t raw32[32]) {
    uint8_t sec_idx = dec & 0x1Fu;
    uint8_t ent_idx = (dec >> 5) & 0x07u;
    if (sec_idx >= d->dir_sectors) return false;

    uint32_t lsn = (uint32_t)d->dir_cyl * d->sec_per_cyl + 2u + sec_idx;
    uint8_t sec[256];
    if (!disk_read_sector(d, lsn, sec)) return false;
    memcpy(&sec[(uint16_t)ent_idx * 32u], raw32, 32);
    return disk_write_sector(d, lsn, sec);
}

static void decode_entry(uint8_t dec, const uint8_t raw[32], uint8_t hit_byte, lsdos_entry_t *e) {
    memset(e, 0, sizeof(*e));
    e->dec       = dec;
    e->dir_sec   = (uint8_t)((dec & 0x1Fu) + 2u);
    e->entry_idx = (dec >> 5) & 0x07u;
    e->status    = raw[0];
    e->eof_off   = raw[3];
    e->lrl       = raw[4];
    e->ern       = (uint16_t)raw[20] | ((uint16_t)raw[21] << 8);
    e->hit_byte  = hit_byte;
    memcpy(e->raw, raw, 32);

    int n = 8;
    while (n > 0 && raw[5 + n - 1] == ' ') n--;
    for (int i = 0; i < n; i++) e->name[i] = (char)raw[5 + i];
    e->name[n] = '\0';

    int x = 3;
    while (x > 0 && raw[13 + x - 1] == ' ') x--;
    for (int i = 0; i < x; i++) e->ext[i] = (char)raw[13 + i];
    e->ext[x] = '\0';
}

uint32_t lsdos_file_byte_size(const lsdos_entry_t *e) {
    if (e->ern == 0) return 0;
    if (e->eof_off == 0) return (uint32_t)e->ern * 256u;
    return (uint32_t)(e->ern - 1u) * 256u + e->eof_off;
}

static bool wildcard_match(const char *pat, const char *str) {
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return true;
            while (*str) {
                if (wildcard_match(pat, str)) return true;
                str++;
            }
            return false;
        }
        if (*pat == '?') {
            if (!*str) return false;
            pat++; str++;
            continue;
        }
        char pc = *pat, sc = *str;
        if (pc >= 'a' && pc <= 'z') pc = (char)(pc - 32);
        if (sc >= 'a' && sc <= 'z') sc = (char)(sc - 32);
        if (pc != sc) return false;
        pat++; str++;
    }
    return *str == '\0';
}

bool lsdos_match_pattern(const char *pattern, const char *name, const char *ext) {
    if (!pattern || !pattern[0]) return true;
    char full1[14], full2[14];
    if (ext[0]) {
        snprintf(full1, sizeof(full1), "%s/%s", name, ext);
        snprintf(full2, sizeof(full2), "%s.%s", name, ext);
    } else {
        snprintf(full1, sizeof(full1), "%s", name);
        snprintf(full2, sizeof(full2), "%s", name);
    }
    return wildcard_match(pattern, full1) || wildcard_match(pattern, full2);
}

/* Follow FPDE + any chained FXDE entries to collect all extents */
int lsdos_collect_extents(trs_disk_t *d, const uint8_t raw32[32],
                          lsdos_extent_t *exts, int max_exts) {
    int count = 0;
    uint8_t cur[32];
    memcpy(cur, raw32, 32);

    for (int chain = 0; chain < 16; chain++) {
        bool chained = false;
        for (int i = 0; i < 5; i++) {
            uint8_t cyl = cur[22 + i * 2];
            uint8_t gb  = cur[23 + i * 2];
            if (cyl == 0xFF) return count;
            if (cyl == 0xFE) {
                /* Follow FXDE link */
                uint8_t fxde_dec = gb;
                if (!lsdos_read_dec(d, fxde_dec, cur)) return count;
                chained = true;
                break;
            }
            if (count < max_exts) {
                exts[count].cyl        = cyl;
                exts[count].gran_start = (gb >> 5) & 0x07u;
                exts[count].gran_count = (uint8_t)((gb & 0x1Fu) + 1u);
                count++;
            }
        }
        if (!chained) break;
    }
    return count;
}

bool lsdos_find_file(trs_disk_t *d, const char *spec, lsdos_entry_t *out) {
    char name8[8], ext3[3];
    if (!lsdos_parse_filename(spec, name8, ext3)) return false;
    if (!d->gat_loaded && !lsdos_load_dir_meta(d)) return false;

    uint32_t dir_base = (uint32_t)d->dir_cyl * d->sec_per_cyl + 2u;
    uint8_t sec[256];

    for (uint8_t s = 0; s < d->dir_sectors; s++) {
        if (!disk_read_sector(d, dir_base + s, sec)) return false;
        for (uint8_t ent = 0; ent < 8; ent++) {
            const uint8_t *raw = &sec[(uint16_t)ent * 32u];
            uint8_t st = raw[0];
            /* Active primary FPDE: bit 4 (0x10 = IN-USE), not FXDE (bit 7 = 0x80) */
            if ((st & 0x10u) && !(st & 0x80u)) {
                if (memcmp(&raw[5], name8, 8) == 0 && memcmp(&raw[13], ext3, 3) == 0) {
                    uint8_t dec = (uint8_t)((ent << 5) | s);
                    if (out) decode_entry(dec, raw, d->hit[dec], out);
                    return true;
                }
            }
        }
    }
    return false;
}

static bool find_free_dec(trs_disk_t *d, uint8_t *out_dec) {
    uint32_t dir_base = (uint32_t)d->dir_cyl * d->sec_per_cyl + 2u;
    uint8_t sec[256];
    for (uint8_t s = 0; s < d->dir_sectors; s++) {
        if (!disk_read_sector(d, dir_base + s, sec)) return false;
        for (uint8_t ent = 0; ent < 8; ent++) {
            const uint8_t *raw = &sec[(uint16_t)ent * 32u];
            if ((raw[0] & 0xD0u) == 0) { /* Neither active FPDE nor FXDE */
                *out_dec = (uint8_t)((ent << 5) | s);
                return true;
            }
        }
    }
    return false;
}

static void free_file_extents(trs_disk_t *d, const uint8_t raw32[32]) {
    uint8_t cur[32];
    memcpy(cur, raw32, 32);

    for (int chain = 0; chain < 16; chain++) {
        bool chained = false;
        uint8_t next_dec = 0;
        for (int i = 0; i < 5; i++) {
            uint8_t cyl = cur[22 + i * 2];
            uint8_t gb  = cur[23 + i * 2];
            if (cyl == 0xFF) break;
            if (cyl == 0xFE) {
                next_dec = gb;
                chained = true;
                break;
            }
            uint8_t gs = (gb >> 5) & 0x07u;
            uint8_t gc = (uint8_t)((gb & 0x1Fu) + 1u);
            for (uint8_t g = 0; g < gc; g++) {
                uint16_t abs_g = (uint16_t)gs + g;
                uint16_t ec = (uint16_t)cyl + (abs_g / d->grans_per_cyl);
                uint8_t  eg = (uint8_t)(abs_g % d->grans_per_cyl);
                if (ec < 0xC0 && eg < 8) {
                    d->gat[ec] &= (uint8_t)~(1u << eg);
                }
            }
        }
        if (!chained) break;
        /* Read next FXDE, then zero it out */
        if (!lsdos_read_dec(d, next_dec, cur)) break;
        uint8_t zero32[32];
        memset(zero32, 0, sizeof(zero32));
        lsdos_write_dec(d, next_dec, zero32);
        d->hit[next_dec] = 0;
    }
}

/* Allocate grans_needed across up to 5 extents (prefer cylinders >= 20, then 0..19) */
static int alloc_extents(trs_disk_t *d, uint16_t grans_needed, lsdos_extent_t exts[5]) {
    uint8_t tmp_gat[256];
    memcpy(tmp_gat, d->gat, 256);

    uint16_t rem = grans_needed;
    int ext_cnt = 0;

    /* First pass: try single contiguous run if <= grans_per_cyl */
    if (rem <= d->grans_per_cyl) {
        for (int pass = 0; pass < 2; pass++) {
            uint16_t start_c = (pass == 0) ? (d->cyls > 20 ? 20 : 0) : 0;
            uint16_t end_c   = (pass == 0) ? d->cyls : (d->cyls > 20 ? 20 : 0);
            for (uint16_t c = start_c; c < end_c && c < 0xC0; c++) {
                if (c == d->dir_cyl) continue;
                uint8_t gb = tmp_gat[c];
                for (uint8_t g = 0; g + rem <= d->grans_per_cyl; g++) {
                    uint8_t mask = (uint8_t)(((1u << rem) - 1u) << g);
                    if ((gb & mask) == 0) {
                        tmp_gat[c] |= mask;
                        exts[0].cyl = (uint8_t)c;
                        exts[0].gran_start = g;
                        exts[0].gran_count = (uint8_t)rem;
                        memcpy(d->gat, tmp_gat, 256);
                        return 1;
                    }
                }
            }
        }
    }

    /* Multi-extent greedy allocation (up to 5 extents) */
    for (int pass = 0; pass < 2 && rem > 0; pass++) {
        uint16_t start_c = (pass == 0) ? (d->cyls > 20 ? 20 : 0) : 0;
        uint16_t end_c   = (pass == 0) ? d->cyls : (d->cyls > 20 ? 20 : 0);
        for (uint16_t c = start_c; c < end_c && c < 0xC0 && rem > 0; c++) {
            if (c == d->dir_cyl) continue;
            uint8_t g = 0;
            while (g < d->grans_per_cyl && rem > 0) {
                if (tmp_gat[c] & (1u << g)) { g++; continue; }
                uint8_t run = 0;
                while (g + run < d->grans_per_cyl && run < rem && !(tmp_gat[c] & (1u << (g + run)))) {
                    run++;
                }
                if (ext_cnt >= 5) return -1; /* Exceeds primary FPDE 5-extent limit */
                uint8_t mask = (uint8_t)(((1u << run) - 1u) << g);
                tmp_gat[c] |= mask;
                exts[ext_cnt].cyl = (uint8_t)c;
                exts[ext_cnt].gran_start = g;
                exts[ext_cnt].gran_count = run;
                ext_cnt++;
                rem = (uint16_t)(rem - run);
                g = (uint8_t)(g + run);
            }
        }
    }

    if (rem > 0) return -1;
    memcpy(d->gat, tmp_gat, 256);
    return ext_cnt;
}

/* --------------------------------------------------------------------------
 * Subcommand: *trs80 dir <image.dsk>[,<pattern>] [-e] [-a] [-s]
 * -------------------------------------------------------------------------- */
int cmd_dir(int argc, char **argv) {
    bool opt_ext = false, opt_all = false, opt_sum = false;
    const char *target = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (const char *p = argv[i] + 1; *p; p++) {
                if (*p == 'e' || *p == 'E') opt_ext = true;
                else if (*p == 'a' || *p == 'A') opt_all = true;
                else if (*p == 's' || *p == 'S') opt_sum = true;
                else {
                    printf("Unknown option: -%c\n", *p);
                    return 1;
                }
            }
        } else if (!target) {
            target = argv[i];
        }
    }

    if (!target) {
        printf("Usage: trs80 dir <image.dsk>[,<pattern>] [-e] [-a] [-s]\n");
        return 1;
    }

    char img[160], pat[32];
    split_comma_spec(target, img, sizeof(img), pat, sizeof(pat));

    trs_disk_t d;
    if (!disk_open(&d, img, false)) {
        printf("Error: Cannot open disk image '%s'\n", img);
        return 1;
    }
    if (!lsdos_load_dir_meta(&d)) {
        printf("Error: Cannot read GAT/HIT on cylinder %u\n", d.dir_cyl);
        disk_close(&d);
        return 1;
    }

    char vol[9], date[9];
    memcpy(vol, &d.gat[0xD0], 8); vol[8] = '\0';
    memcpy(date, &d.gat[0xD8], 8); date[8] = '\0';
    printf("Volume: %.8s  Date: %.8s  Format: %s (%ux%ux%u, DirCyl %u)\n",
           vol, date, disk_format_name(d.fmt), d.cyls, d.heads, d.spt, d.dir_cyl);

    if (!opt_sum) {
        if (opt_ext) {
            printf("DEC  ST   HIT  Filename     ERN  EOF  Bytes   Extents\n");
            printf("---- ---- ---- ------------ ---- ---- ------- --------------------\n");
        } else {
            printf("Filename     Attr   Bytes   Sectors\n");
            printf("------------ ------ ------- -------\n");
        }
    }

    uint32_t dir_base = (uint32_t)d.dir_cyl * d.sec_per_cyl + 2u;
    uint8_t sec[256];
    uint16_t file_count = 0;
    uint32_t total_bytes = 0;
    uint32_t total_secs = 0;

    for (uint8_t s = 0; s < d.dir_sectors; s++) {
        if (!disk_read_sector(&d, dir_base + s, sec)) break;
        for (uint8_t ent = 0; ent < 8; ent++) {
            const uint8_t *raw = &sec[(uint16_t)ent * 32u];
            uint8_t st = raw[0];
            if (!((st & 0x10u) && !(st & 0x80u))) continue;

            uint8_t dec = (uint8_t)((ent << 5) | s);
            lsdos_entry_t e;
            decode_entry(dec, raw, d.hit[dec], &e);

            /* System/invisible flag: bit 6 (0x40) = SYS, bit 3 (0x08) = INV */
            bool is_sys = (st & 0x48u) != 0;
            if (is_sys && !opt_all) continue;
            if (pat[0] && !lsdos_match_pattern(pat, e.name, e.ext)) continue;

            uint32_t fbytes = lsdos_file_byte_size(&e);
            file_count++;
            total_bytes += fbytes;
            total_secs += e.ern;

            if (!opt_sum) {
                char fn[14];
                lsdos_format_name(&e, fn);
                if (opt_ext) {
                    printf("0x%02X 0x%02X 0x%02X %-12s %4u %4u %7lu ",
                           e.dec, e.status, e.hit_byte, fn, e.ern, e.eof_off,
                           (unsigned long)fbytes);
                    lsdos_extent_t exts[16];
                    int n = lsdos_collect_extents(&d, e.raw, exts, 16);
                    for (int k = 0; k < n; k++) {
                        if (k > 0) printf(",");
                        printf("C%u:G%u+%u", exts[k].cyl, exts[k].gran_start, exts[k].gran_count);
                    }
                    printf("\n");
                } else {
                    char attr[7];
                    attr[0] = (st & 0x40u) ? 'S' : '-';
                    attr[1] = (st & 0x08u) ? 'I' : '-';
                    attr[2] = (st & 0x20u) ? 'U' : '-';
                    attr[3] = (char)('0' + (st & 0x07u));
                    attr[4] = '\0';
                    printf("%-12s %-6s %7lu %7u\n", fn, attr, (unsigned long)fbytes, e.ern);
                }
            }
        }
    }

    uint16_t free_grans = 0;
    for (uint16_t c = 0; c < d.cyls && c < 0xC0; c++) {
        for (uint8_t g = 0; g < d.grans_per_cyl; g++) {
            if (!(d.gat[c] & (1u << g))) free_grans++;
        }
    }
    uint32_t free_kb = ((uint32_t)free_grans * d.sec_per_gran * 256u) / 1024u;
    printf("%u file(s), %lu bytes (%lu sectors), %lu KiB free (%u grans)\n",
           file_count, (unsigned long)total_bytes, (unsigned long)total_secs,
           (unsigned long)free_kb, free_grans);

    disk_close(&d);
    return 0;
}

/* --------------------------------------------------------------------------
 * Subcommand: *trs80 free <image.dsk>
 * -------------------------------------------------------------------------- */
int cmd_free(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: trs80 free <image.dsk>\n");
        return 1;
    }
    char img[160], unused[32];
    split_comma_spec(argv[1], img, sizeof(img), unused, sizeof(unused));

    trs_disk_t d;
    if (!disk_open(&d, img, false) || !lsdos_load_dir_meta(&d)) {
        printf("Error: Cannot open or read GAT from '%s'\n", img);
        disk_close(&d);
        return 1;
    }

    char vol[9];
    memcpy(vol, &d.gat[0xD0], 8); vol[8] = '\0';

    uint16_t total_grans = (uint16_t)(d.cyls * d.grans_per_cyl);
    uint16_t used_grans = 0;
    for (uint16_t c = 0; c < d.cyls && c < 0xC0; c++) {
        for (uint8_t g = 0; g < d.grans_per_cyl; g++) {
            if (d.gat[c] & (1u << g)) used_grans++;
        }
    }
    uint16_t free_grans = (uint16_t)(total_grans - used_grans);
    uint32_t gran_bytes = (uint32_t)d.sec_per_gran * 256u;

    printf("Volume:    %.8s (%s)\n", vol, disk_format_name(d.fmt));
    printf("Geometry:  %u cyls, %u heads, %u sec/trk (%lu sectors)\n",
           d.cyls, d.heads, d.spt, (unsigned long)((uint32_t)d.cyls * d.sec_per_cyl));
    printf("Granules:  %u/cyl (%u sec = %lu bytes/gran)\n",
           d.grans_per_cyl, d.sec_per_gran, (unsigned long)gran_bytes);
    printf("Allocated: %u / %u granules (%lu KiB)\n",
           used_grans, total_grans, (unsigned long)((used_grans * gran_bytes) / 1024u));
    printf("Free:      %u granules (%lu KiB / %lu bytes)\n",
           free_grans, (unsigned long)((free_grans * gran_bytes) / 1024u),
           (unsigned long)(free_grans * gran_bytes));

    disk_close(&d);
    return 0;
}

/* --------------------------------------------------------------------------
 * Subcommand: *trs80 copy [-l] [-r] <src> <dst>
 * -------------------------------------------------------------------------- */

/* Read entire TRS-80 file to host file handle (with optional CR->LF conversion) */
static bool extract_to_host(trs_disk_t *d, const lsdos_entry_t *e, host_file_t *out, bool conv_eol) {
    lsdos_extent_t exts[16];
    int n_ext = lsdos_collect_extents(d, e->raw, exts, 16);
    uint32_t rem_bytes = lsdos_file_byte_size(e);
    uint8_t sec[256];

    for (int i = 0; i < n_ext && rem_bytes > 0; i++) {
        uint32_t base_lsn = (uint32_t)exts[i].cyl * d->sec_per_cyl +
                            (uint32_t)exts[i].gran_start * d->sec_per_gran;
        uint16_t n_sec = (uint16_t)exts[i].gran_count * d->sec_per_gran;
        for (uint16_t s = 0; s < n_sec && rem_bytes > 0; s++) {
            if (!disk_read_sector(d, base_lsn + s, sec)) return false;
            uint16_t chunk = (rem_bytes > 256u) ? 256u : (uint16_t)rem_bytes;
            if (conv_eol) {
                for (uint16_t k = 0; k < chunk; k++) {
                    if (sec[k] == 0x0D) sec[k] = 0x0A;
                }
            }
            if (pio_write(out, sec, chunk) != chunk) return false;
            rem_bytes -= chunk;
        }
    }
    return true;
}

/* Write from host file handle into TRS-80 image (with optional LF/CRLF->CR conversion) */
static bool inject_from_host(host_file_t *in, trs_disk_t *d, const char *guest_spec,
                             bool replace, bool conv_eol, uint32_t *out_bytes) {
    char name8[8], ext3[3];
    if (!lsdos_parse_filename(guest_spec, name8, ext3)) {
        printf("Error: Invalid TRS-80 filename '%s'\n", guest_spec);
        return false;
    }
    if (!lsdos_load_dir_meta(d)) return false;

    /* Check if destination already exists */
    lsdos_entry_t existing;
    uint8_t dec = 0;
    if (lsdos_find_file(d, guest_spec, &existing)) {
        if (!replace) {
            printf("Error: Destination '%s' already exists (use -r to replace)\n", guest_spec);
            return false;
        }
        free_file_extents(d, existing.raw);
        dec = existing.dec;
    } else {
        if (!find_free_dec(d, &dec)) {
            printf("Error: Directory full\n");
            return false;
        }
    }

    /* Compute final payload size (accounting for optional CRLF->CR strip) */
    uint32_t final_size = in->size;
    if (conv_eol) {
        final_size = 0;
        pio_seek(in, 0);
        uint32_t left = in->size;
        uint8_t prev = 0;
        while (left > 0) {
            uint32_t chunk = (left > 1024u) ? 1024u : left;
            uint32_t got = pio_read(in, g_io_buf, chunk);
            if (got == 0) break;
            for (uint32_t k = 0; k < got; k++) {
                uint8_t c = g_io_buf[k];
                if (c == 0x0A && prev == 0x0D) {
                    /* Strip LF after CR */
                } else {
                    final_size++;
                }
                prev = c;
            }
            left -= got;
        }
    }

    uint16_t sectors_needed = (uint16_t)((final_size + 255u) / 256u);
    if (sectors_needed == 0) sectors_needed = 1;
    uint16_t grans_needed = (uint16_t)((sectors_needed + d->sec_per_gran - 1u) / d->sec_per_gran);
    if (grans_needed == 0) grans_needed = 1;

    lsdos_extent_t exts[5];
    int n_ext = alloc_extents(d, grans_needed, exts);
    if (n_ext <= 0) {
        printf("Error: Insufficient free space or too fragmented (%u granules needed)\n", grans_needed);
        return false;
    }

    /* Stream payload into allocated extents */
    pio_seek(in, 0);
    uint8_t sec[256];
    uint16_t sec_pos = 0;
    uint32_t in_rem = in->size;
    uint8_t prev = 0;
    int cur_ext = 0;
    uint16_t cur_ext_sec = 0;
    uint16_t written_secs = 0;
    memset(sec, 0, sizeof(sec));

    while (in_rem > 0) {
        uint32_t chunk = (in_rem > 1024u) ? 1024u : in_rem;
        uint32_t got = pio_read(in, g_io_buf, chunk);
        if (got == 0) break;
        in_rem -= got;

        for (uint32_t k = 0; k < got; k++) {
            uint8_t c = g_io_buf[k];
            if (conv_eol) {
                if (c == 0x0A && prev == 0x0D) { prev = c; continue; }
                prev = c;
                if (c == 0x0A) c = 0x0D;
            }
            sec[sec_pos++] = c;
            if (sec_pos == 256) {
                while (cur_ext < n_ext &&
                       cur_ext_sec >= (uint16_t)exts[cur_ext].gran_count * d->sec_per_gran) {
                    cur_ext++;
                    cur_ext_sec = 0;
                }
                if (cur_ext >= n_ext) return false;
                uint32_t lsn = (uint32_t)exts[cur_ext].cyl * d->sec_per_cyl +
                               (uint32_t)exts[cur_ext].gran_start * d->sec_per_gran +
                               cur_ext_sec;
                if (!disk_write_sector(d, lsn, sec)) return false;
                cur_ext_sec++;
                written_secs++;
                sec_pos = 0;
                memset(sec, 0, sizeof(sec));
            }
        }
    }

    /* Flush final partial sector (or 1 zero sector for 0-byte file) */
    if (sec_pos > 0 || written_secs == 0) {
        while (cur_ext < n_ext &&
               cur_ext_sec >= (uint16_t)exts[cur_ext].gran_count * d->sec_per_gran) {
            cur_ext++;
            cur_ext_sec = 0;
        }
        if (cur_ext >= n_ext) return false;
        uint32_t lsn = (uint32_t)exts[cur_ext].cyl * d->sec_per_cyl +
                       (uint32_t)exts[cur_ext].gran_start * d->sec_per_gran +
                       cur_ext_sec;
        if (!disk_write_sector(d, lsn, sec)) return false;
        written_secs++;
    }

    /* Build authentic 32-byte FPDE */
    uint8_t fpde[32];
    memset(fpde, 0, sizeof(fpde));
    fpde[0] = 0x10;
    fpde[3] = (uint8_t)(final_size % 256u);
    fpde[4] = 0x00;
    memcpy(&fpde[5], name8, 8);
    memcpy(&fpde[13], ext3, 3);
    fpde[20] = (uint8_t)(written_secs & 0xFF);
    fpde[21] = (uint8_t)(written_secs >> 8);
    for (int i = 0; i < 5; i++) {
        if (i < n_ext) {
            fpde[22 + i * 2] = exts[i].cyl;
            fpde[23 + i * 2] = (uint8_t)((exts[i].gran_start << 5) | ((exts[i].gran_count - 1u) & 0x1Fu));
        } else {
            fpde[22 + i * 2] = 0xFF;
            fpde[23 + i * 2] = 0x00;
        }
    }

    d->hit[dec] = lsdos_hit_hash(name8, ext3);
    if (!lsdos_write_dec(d, dec, fpde)) return false;
    if (!lsdos_save_dir_meta(d)) return false;

    if (out_bytes) *out_bytes = final_size;
    return true;
}

int cmd_copy(int argc, char **argv) {
    bool conv_eol = false, replace = false;
    const char *src = NULL, *dst = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (const char *p = argv[i] + 1; *p; p++) {
                if (*p == 'l' || *p == 'L') conv_eol = true;
                else if (*p == 'r' || *p == 'R') replace = true;
                else {
                    printf("Unknown option: -%c\n", *p);
                    return 1;
                }
            }
        } else if (!src) {
            src = argv[i];
        } else if (!dst) {
            dst = argv[i];
        }
    }

    if (!src || !dst) {
        printf("Usage: trs80 copy [-l] [-r] <src> <dst>\n");
        printf("  Extract: trs80 copy image.dsk,ELIZA/BAS eliza.bas\n");
        printf("  Inject:  trs80 copy prog.cmd image.dsk,PROG/CMD\n");
        printf("  Clone:   trs80 copy d1.dsk,FILE d2.dsk,FILE\n");
        return 1;
    }

    char src_img[160], src_guest[32];
    char dst_img[160], dst_guest[32];
    bool src_is_dsk = split_comma_spec(src, src_img, sizeof(src_img), src_guest, sizeof(src_guest));
    bool dst_is_dsk = split_comma_spec(dst, dst_img, sizeof(dst_img), dst_guest, sizeof(dst_guest));

    if (src_is_dsk && !dst_is_dsk) {
        /* Extract: virtual disk -> native FAT32 */
        if (!src_guest[0]) {
            printf("Error: Source filename inside image required\n");
            return 1;
        }
        if (!replace && pio_exists(dst)) {
            printf("Error: Destination '%s' exists (use -r to overwrite)\n", dst);
            return 1;
        }
        trs_disk_t d;
        if (!disk_open(&d, src_img, false)) {
            printf("Error: Cannot open '%s'\n", src_img);
            return 1;
        }
        lsdos_entry_t e;
        if (!lsdos_find_file(&d, src_guest, &e)) {
            printf("Error: File '%s' not found in '%s'\n", src_guest, src_img);
            disk_close(&d);
            return 1;
        }
        host_file_t out;
        if (!pio_open(&out, dst, true, true)) {
            printf("Error: Cannot create '%s'\n", dst);
            disk_close(&d);
            return 1;
        }
        bool ok = extract_to_host(&d, &e, &out, conv_eol);
        uint32_t bytes = lsdos_file_byte_size(&e);
        pio_close(&out);
        disk_close(&d);
        if (!ok) {
            printf("Error: Extraction failed\n");
            return 1;
        }
        printf("Extracted %s -> %s (%lu bytes)\n", src, dst, (unsigned long)bytes);
        return 0;
    } else if (!src_is_dsk && dst_is_dsk) {
        /* Inject: native FAT32 -> virtual disk */
        if (!dst_guest[0]) {
            printf("Error: Target filename inside image required\n");
            return 1;
        }
        host_file_t in;
        if (!pio_open(&in, src, false, false)) {
            printf("Error: Cannot open source file '%s'\n", src);
            return 1;
        }
        trs_disk_t d;
        if (!disk_open(&d, dst_img, true)) {
            printf("Error: Cannot open '%s' for writing\n", dst_img);
            pio_close(&in);
            return 1;
        }
        uint32_t written = 0;
        bool ok = inject_from_host(&in, &d, dst_guest, replace, conv_eol, &written);
        pio_close(&in);
        disk_close(&d);
        if (!ok) return 1;
        printf("Injected %s -> %s (%lu bytes)\n", src, dst, (unsigned long)written);
        return 0;
    } else if (src_is_dsk && dst_is_dsk) {
        /* Clone: virtual disk -> virtual disk (via temporary staging file) */
        const char *tmp = "trs80_cp.tmp";
        trs_disk_t d1;
        if (!disk_open(&d1, src_img, false)) {
            printf("Error: Cannot open '%s'\n", src_img);
            return 1;
        }
        lsdos_entry_t e;
        if (!lsdos_find_file(&d1, src_guest, &e)) {
            printf("Error: File '%s' not found in '%s'\n", src_guest, src_img);
            disk_close(&d1);
            return 1;
        }
        host_file_t tf;
        if (!pio_open(&tf, tmp, true, true)) {
            printf("Error: Cannot create temp staging file\n");
            disk_close(&d1);
            return 1;
        }
        bool ok = extract_to_host(&d1, &e, &tf, false);
        pio_close(&tf);
        disk_close(&d1);
        if (!ok) {
            printf("Error: Read failed during clone\n");
            return 1;
        }

        if (!pio_open(&tf, tmp, false, false)) return 1;
        trs_disk_t d2;
        if (!disk_open(&d2, dst_img, true)) {
            printf("Error: Cannot open '%s' for writing\n", dst_img);
            pio_close(&tf);
            return 1;
        }
        uint32_t written = 0;
        ok = inject_from_host(&tf, &d2, dst_guest[0] ? dst_guest : src_guest, replace, conv_eol, &written);
        pio_close(&tf);
        disk_close(&d2);
#ifdef HOST_TEST
        remove(tmp);
#else
        extern uint8_t mos_del(const char *filename);
        mos_del(tmp);
#endif
        if (!ok) return 1;
        printf("Cloned %s -> %s (%lu bytes)\n", src, dst, (unsigned long)written);
        return 0;
    } else {
        printf("Error: At least one path must reference a disk image (image.dsk,FILE)\n");
        return 1;
    }
}

/* --------------------------------------------------------------------------
 * Subcommand: *trs80 delete <image.dsk>,<filename>
 * -------------------------------------------------------------------------- */
int cmd_delete(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: trs80 delete <image.dsk>,<filename>\n");
        return 1;
    }
    char img[160], guest[32];
    if (!split_comma_spec(argv[1], img, sizeof(img), guest, sizeof(guest)) || !guest[0]) {
        printf("Error: Specify <image.dsk>,<filename>\n");
        return 1;
    }

    trs_disk_t d;
    if (!disk_open(&d, img, true)) {
        printf("Error: Cannot open '%s' for writing\n", img);
        return 1;
    }
    lsdos_entry_t e;
    if (!lsdos_find_file(&d, guest, &e)) {
        printf("Error: File '%s' not found\n", guest);
        disk_close(&d);
        return 1;
    }

    free_file_extents(&d, e.raw);
    uint8_t zero32[32];
    memset(zero32, 0, sizeof(zero32));
    d.hit[e.dec] = 0;
    bool ok = lsdos_write_dec(&d, e.dec, zero32) && lsdos_save_dir_meta(&d);
    disk_close(&d);

    if (!ok) {
        printf("Error: Failed updating directory/GAT\n");
        return 1;
    }
    char fn[14];
    lsdos_format_name(&e, fn);
    printf("Deleted %s (DEC 0x%02X)\n", fn, e.dec);
    return 0;
}

/* --------------------------------------------------------------------------
 * Subcommand: *trs80 rename <image.dsk>,<old_name> <new_name>
 * -------------------------------------------------------------------------- */
int cmd_rename(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: trs80 rename <image.dsk>,<old_name> <new_name>\n");
        return 1;
    }
    char img[160], old_guest[32];
    if (!split_comma_spec(argv[1], img, sizeof(img), old_guest, sizeof(old_guest)) || !old_guest[0]) {
        printf("Error: Specify <image.dsk>,<old_name> <new_name>\n");
        return 1;
    }
    char new8[8], ext3[3];
    if (!lsdos_parse_filename(argv[2], new8, ext3)) {
        printf("Error: Invalid new filename '%s'\n", argv[2]);
        return 1;
    }

    trs_disk_t d;
    if (!disk_open(&d, img, true)) {
        printf("Error: Cannot open '%s' for writing\n", img);
        return 1;
    }
    lsdos_entry_t e;
    if (!lsdos_find_file(&d, old_guest, &e)) {
        printf("Error: File '%s' not found\n", old_guest);
        disk_close(&d);
        return 1;
    }
    if (lsdos_find_file(&d, argv[2], NULL)) {
        printf("Error: Target '%s' already exists\n", argv[2]);
        disk_close(&d);
        return 1;
    }

    memcpy(&e.raw[5], new8, 8);
    memcpy(&e.raw[13], ext3, 3);
    d.hit[e.dec] = lsdos_hit_hash(new8, ext3);
    bool ok = lsdos_write_dec(&d, e.dec, e.raw) && lsdos_save_dir_meta(&d);
    disk_close(&d);

    if (!ok) {
        printf("Error: Rename failed\n");
        return 1;
    }
    printf("Renamed %s -> %s (HIT 0x%02X)\n", old_guest, argv[2], d.hit[e.dec]);
    return 0;
}

/* --------------------------------------------------------------------------
 * Subcommand: *trs80 format <image.dsk> [-f format] [-c cyls] [-s sides] [-d dircyl] [-v volname]
 * -------------------------------------------------------------------------- */
int cmd_format(int argc, char **argv) {
    const char *path = NULL;
    disk_format_t fmt = FMT_DISKDISK;
    uint16_t cyls = 40;
    uint8_t heads = 2;
    uint8_t spt = 18;
    uint8_t dir_cyl = 1;
    bool dir_set = false;
    const char *vol = "NEWDISK";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            i++;
            if (str_casecmp(argv[i], "diskdisk") == 0) fmt = FMT_DISKDISK;
            else if (str_casecmp(argv[i], "jv1") == 0) fmt = FMT_JV1;
            else if (str_casecmp(argv[i], "hdv") == 0) { fmt = FMT_REED_HDV; cyls = 203; heads = 4; spt = 32; }
            else { printf("Unknown format '%s' (use diskdisk, jv1, hdv)\n", argv[i]); return 1; }
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cyls = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            heads = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            spt = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            dir_cyl = (uint8_t)atoi(argv[++i]);
            dir_set = true;
        } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            vol = argv[++i];
        } else if (!path && argv[i][0] != '-') {
            path = argv[i];
        }
    }

    if (!path) {
        printf("Usage: trs80 format <image.dsk> [-f diskdisk|jv1|hdv] [-c cyls] [-s sides] [-t spt] [-d dircyl] [-v volname]\n");
        return 1;
    }
    if (!dir_set && fmt == FMT_JV1 && cyls >= 35) dir_cyl = 17;

    printf("Formatting %s (%s, %ux%ux%u, DirCyl %u, Vol '%s')...\n",
           path, disk_format_name(fmt), cyls, heads, spt, dir_cyl, vol);
    if (!disk_format_new(path, fmt, cyls, heads, spt, dir_cyl, vol)) {
        printf("Error: Format failed\n");
        return 1;
    }
    printf("Format complete (%lu bytes).\n",
           (unsigned long)((uint32_t)cyls * heads * spt * 256u +
                           ((fmt == FMT_DISKDISK || fmt == FMT_REED_HDV) ? 256u : 0u)));
    return 0;
}

/* --------------------------------------------------------------------------
 * Subcommand: *trs80 check <image.dsk>
 * Structural audit matching tools/audit_filesystem.py
 * -------------------------------------------------------------------------- */
int cmd_check(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: trs80 check <image.dsk>\n");
        return 1;
    }
    char img[160], unused[32];
    split_comma_spec(argv[1], img, sizeof(img), unused, sizeof(unused));

    trs_disk_t d;
    if (!disk_open(&d, img, false) || !lsdos_load_dir_meta(&d)) {
        printf("Error: Cannot open or read GAT/HIT from '%s'\n", img);
        disk_close(&d);
        return 1;
    }

    char vol[9];
    memcpy(vol, &d.gat[0xD0], 8); vol[8] = '\0';
    printf("LS-DOS 6.3 Structural Audit: %s\n", img);
    printf("Format: %s  Volume: %.8s  Geometry: %ux%ux%u  DirCyl: %u  Grans/Cyl: %u\n",
           disk_format_name(d.fmt), vol, d.cyls, d.heads, d.spt, d.dir_cyl, d.grans_per_cyl);

    /* Track claimed granules (up to 192 cyls * 8 bits = 192 bytes) and active DECs (32 bytes) */
    uint8_t claimed[192];
    uint8_t active_dec[32];
    memset(claimed, 0, sizeof(claimed));
    memset(active_dec, 0, sizeof(active_dec));

    uint16_t active_files = 0;
    uint16_t fxde_count = 0;
    uint16_t double_allocs = 0;
    uint16_t hit_errors = 0;
    uint16_t gat_orphans = 0;
    uint16_t gat_missing = 0;

    uint32_t dir_base = (uint32_t)d.dir_cyl * d.sec_per_cyl + 2u;
    uint8_t sec[256];

    for (uint8_t s = 0; s < d.dir_sectors; s++) {
        if (!disk_read_sector(&d, dir_base + s, sec)) break;
        for (uint8_t ent = 0; ent < 8; ent++) {
            const uint8_t *raw = &sec[(uint16_t)ent * 32u];
            uint8_t st = raw[0];
            uint8_t dec = (uint8_t)((ent << 5) | s);
            if (!((st & 0x10u) && !(st & 0x80u))) continue;

            active_files++;
            active_dec[dec >> 3] |= (uint8_t)(1u << (dec & 7u));

            lsdos_entry_t e;
            decode_entry(dec, raw, d.hit[dec], &e);

            /* HIT verification (all active files have non-zero HIT) */
            if (e.hit_byte == 0) {
                printf("[!] Missing HIT for %s/%s (DEC 0x%02X)\n", e.name, e.ext, dec);
                hit_errors++;
            }

            /* Walk primary + FXDE extents (wrapping across cylinder boundaries) */
            uint8_t cur[32];
            memcpy(cur, raw, 32);
            for (int chain = 0; chain < 16; chain++) {
                bool chained = false;
                for (int i = 0; i < 5; i++) {
                    uint8_t cyl = cur[22 + i * 2];
                    uint8_t gb  = cur[23 + i * 2];
                    if (cyl == 0xFF) break;
                    if (cyl == 0xFE) {
                        fxde_count++;
                        if (!lsdos_read_dec(&d, gb, cur)) break;
                        chained = true;
                        break;
                    }
                    uint8_t gs = (gb >> 5) & 0x07u;
                    uint8_t gc = (uint8_t)((gb & 0x1Fu) + 1u);
                    for (uint8_t g = 0; g < gc; g++) {
                        uint16_t abs_g = (uint16_t)gs + g;
                        uint16_t ec = (uint16_t)cyl + (abs_g / d.grans_per_cyl);
                        uint8_t  eg = (uint8_t)(abs_g % d.grans_per_cyl);
                        if (ec < 192 && eg < 8) {
                            uint8_t bit = (uint8_t)(1u << eg);
                            if (claimed[ec] & bit) {
                                printf("[!] Double-allocated C%u:G%u (%s/%s)\n",
                                       ec, eg, e.name, e.ext);
                                double_allocs++;
                            }
                            claimed[ec] |= bit;
                        }
                    }
                }
                if (!chained) break;
            }
        }
    }

    /* Cross-check GAT vs claimed granules */
    uint16_t alloc_grans = 0;
    for (uint16_t c = 0; c < d.cyls && c < 192; c++) {
        for (uint8_t g = 0; g < d.grans_per_cyl; g++) {
            uint8_t bit = (uint8_t)(1u << g);
            bool in_gat = (d.gat[c] & bit) != 0;
            bool in_ext = (claimed[c] & bit) != 0;
            if (in_gat) alloc_grans++;
            if (c == d.dir_cyl) {
                if (!in_gat) {
                    printf("[!] Dir cyl %u gran %u not marked in GAT\n", c, g);
                    gat_missing++;
                }
            } else {
                if (in_ext && !in_gat) {
                    printf("[!] Claimed C%u:G%u missing from GAT\n", c, g);
                    gat_missing++;
                } else if (in_gat && !in_ext) {
                    gat_orphans++;
                }
            }
        }
    }

    /* Check stale HIT entries */
    uint16_t stale_hits = 0;
    for (int dec = 0; dec < 256; dec++) {
        bool active = (active_dec[dec >> 3] & (1u << (dec & 7))) != 0;
        if (!active && d.hit[dec] != 0) {
            printf("[!] Stale HIT at DEC 0x%02X: 0x%02X\n", dec, d.hit[dec]);
            stale_hits++;
        }
    }

    printf("Active files:      %u (FXDE links: %u)\n", active_files, fxde_count);
    printf("Allocated grans:   %u / %u\n", alloc_grans, (uint16_t)(d.cyls * d.grans_per_cyl));
    printf("Double allocs:     %u\n", double_allocs);
    printf("Missing GAT bits:  %u\n", gat_missing);
    printf("Orphan GAT bits:   %u\n", gat_orphans);
    printf("HIT errors/stale:  %u / %u\n", hit_errors, stale_hits);

    disk_close(&d);

    if (double_allocs == 0 && gat_missing == 0 && hit_errors == 0 && stale_hits == 0) {
        printf("RESULT: STRUCTURALLY SOUND\n");
        return 0;
    }
    printf("RESULT: INTEGRITY ISSUES DETECTED\n");
    return 1;
}

/* --------------------------------------------------------------------------
 * Subcommand: *trs80 dump <image.dsk>[,<filename>] [-s sector]
 * -------------------------------------------------------------------------- */
static void print_hex_block(const uint8_t *buf, uint16_t len, uint32_t base_off) {
    for (uint16_t i = 0; i < len; i += 16) {
        printf("%04lX: ", (unsigned long)(base_off + i));
        for (uint16_t j = 0; j < 16; j++) {
            if (i + j < len) printf("%02X ", buf[i + j]);
            else printf("   ");
        }
        printf(" ");
        for (uint16_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = buf[i + j];
            putchar((c >= 0x20 && c <= 0x7E) ? (char)c : '.');
        }
        printf("\n");
    }
}

int cmd_dump(int argc, char **argv) {
    const char *target = NULL;
    int32_t sector = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            sector = (int32_t)strtol(argv[++i], NULL, 0);
        } else if (!target && argv[i][0] != '-') {
            target = argv[i];
        }
    }
    if (!target) {
        printf("Usage: trs80 dump <image.dsk>[,<filename>] [-s sector]\n");
        return 1;
    }

    char img[160], guest[32];
    split_comma_spec(target, img, sizeof(img), guest, sizeof(guest));

    trs_disk_t d;
    if (!disk_open(&d, img, false)) {
        printf("Error: Cannot open '%s'\n", img);
        return 1;
    }

    uint8_t sec[256];
    if (guest[0] && strcmp(guest, "@") != 0) {
        lsdos_entry_t e;
        if (!lsdos_find_file(&d, guest, &e)) {
            printf("Error: File '%s' not found\n", guest);
            disk_close(&d);
            return 1;
        }
        lsdos_extent_t exts[16];
        int n_ext = lsdos_collect_extents(&d, e.raw, exts, 16);
        uint32_t want = (sector >= 0) ? (uint32_t)sector : 0;
        uint32_t cur = 0;
        bool found = false;
        for (int i = 0; i < n_ext && !found; i++) {
            uint16_t n_sec = (uint16_t)exts[i].gran_count * d.sec_per_gran;
            if (want < cur + n_sec) {
                uint32_t lsn = (uint32_t)exts[i].cyl * d.sec_per_cyl +
                               (uint32_t)exts[i].gran_start * d.sec_per_gran +
                               (want - cur);
                printf("File %s/%s sector %lu (LSN %lu):\n",
                       e.name, e.ext, (unsigned long)want, (unsigned long)lsn);
                if (disk_read_sector(&d, lsn, sec)) {
                    print_hex_block(sec, 256, want * 256u);
                    found = true;
                }
            }
            cur += n_sec;
        }
        if (!found) printf("Error: Sector %ld out of range for file\n", (long)sector);
    } else {
        uint32_t lsn = (sector >= 0) ? (uint32_t)sector : 0;
        printf("Image %s LSN %lu:\n", img, (unsigned long)lsn);
        if (!disk_read_sector(&d, lsn, sec)) {
            printf("Error: Cannot read LSN %lu\n", (unsigned long)lsn);
            disk_close(&d);
            return 1;
        }
        print_hex_block(sec, 256, lsn * 256u);
    }

    disk_close(&d);
    return 0;
}

/* --------------------------------------------------------------------------
 * Subcommand: *trs80 attr <image.dsk>,<filename> [+/-S +/-I +/-U 0..7]
 * -------------------------------------------------------------------------- */
int cmd_attr(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: trs80 attr <image.dsk>,<filename> [+S|-S|+I|-I|+U|-U|0..7]\n");
        return 1;
    }
    char img[160], guest[32];
    if (!split_comma_spec(argv[1], img, sizeof(img), guest, sizeof(guest)) || !guest[0]) {
        printf("Error: Specify <image.dsk>,<filename>\n");
        return 1;
    }

    bool modify = (argc >= 3);
    trs_disk_t d;
    if (!disk_open(&d, img, modify)) {
        printf("Error: Cannot open '%s'\n", img);
        return 1;
    }
    lsdos_entry_t e;
    if (!lsdos_find_file(&d, guest, &e)) {
        printf("Error: File '%s' not found\n", guest);
        disk_close(&d);
        return 1;
    }

    uint8_t st = e.status;
    if (modify) {
        for (int i = 2; i < argc; i++) {
            const char *a = argv[i];
            if ((a[0] == '+' || a[0] == '-') && a[1]) {
                bool set = (a[0] == '+');
                char f = (char)toupper((unsigned char)a[1]);
                uint8_t bit = 0;
                if (f == 'S') bit = 0x40;
                else if (f == 'I') bit = 0x08;
                else if (f == 'U') bit = 0x20;
                else { printf("Unknown flag: %s\n", a); disk_close(&d); return 1; }
                if (set) st |= bit; else st &= (uint8_t)~bit;
            } else if (a[0] >= '0' && a[0] <= '7' && a[1] == '\0') {
                st = (uint8_t)((st & 0xF8u) | (uint8_t)(a[0] - '0'));
            } else {
                printf("Invalid attribute token: %s\n", a);
                disk_close(&d);
                return 1;
            }
        }
        e.raw[0] = st;
        if (!lsdos_write_dec(&d, e.dec, e.raw)) {
            printf("Error: Failed updating directory entry\n");
            disk_close(&d);
            return 1;
        }
    }

    char fn[14];
    lsdos_format_name(&e, fn);
    printf("%-12s Status: 0x%02X  SYS=%u  INV=%u  UPD=%u  PROT=%u\n",
           fn, st, (st & 0x40u) ? 1 : 0, (st & 0x08u) ? 1 : 0,
           (st & 0x20u) ? 1 : 0, st & 0x07u);

    disk_close(&d);
    return 0;
}
