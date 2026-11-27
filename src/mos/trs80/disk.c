/*
 * disk.c - Universal TRS-80 Disk Container Abstraction Layer
 *
 * Supports:
 *   - DiskDISK (.dsk): 256-byte Misosys header + linear 256B sectors
 *   - JV1      (.dsk / .jv1): Headerless linear 256B sectors
 *   - JV3      (.dsk / .jv3): 8704-byte (0x2200) indexed descriptor table + data
 *   - DMK      (.dmk): David Keil raw track bitstream container (FM/MFM, CRC-16)
 *   - Reed HDV (.hdv): 256-byte Matthew Reed header (0x56 0xCB) + linear sectors
 *   - Flat HDV (.hdv / .img): Headerless linear hard disk sector stream
 */

#include "trs80.h"
#include <stdio.h>
#include <string.h>

#define JV3_HEADER_SIZE   8704u  /* 0x2200 */
#define JV3_MAX_SECTORS   2901u
#define JV3_FLAG_DENSITY  0x80
#define JV3_FLAG_SIDE     0x10
#define JV3_FLAG_SIZE     0x03

#define DMK_HEADER_SIZE   16u
#define DMK_IDAM_BYTES    128u
#define DMK_MAX_IDAMS     64u
#define DMK_FLAG_SS       0x10u
#define DMK_FLAG_SD_ONLY  0x40u

const char *disk_format_name(disk_format_t fmt) {
    switch (fmt) {
        case FMT_DISKDISK: return "DiskDISK";
        case FMT_JV1:      return "JV1";
        case FMT_JV3:      return "JV3";
        case FMT_REED_HDV: return "Reed HDV";
        case FMT_FLAT_HDV: return "Flat HDV";
        case FMT_DMK:      return "DMK";
        default:           return "Unknown";
    }
}

/* --------------------------------------------------------------------------
 * DMK Track Bitstream Engine (Read / Write with CRC-16 CCITT)
 * -------------------------------------------------------------------------- */

typedef struct {
    uint32_t data_off;   /* File offset of first sector payload byte */
    uint8_t  dam_byte;   /* Data Address Mark (0xFB normal, 0xF8..0xFA deleted) */
    uint8_t  step;       /* 1 = normal (MFM or SD-only DMK), 2 = FM byte-doubled */
    bool     is_dd;      /* true = MFM double-density (IDAM bit 15 = 1) */
} dmk_sec_loc_t;

static uint16_t crc16_ccitt_update(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000u) crc = (uint16_t)((crc << 1) ^ 0x1021u);
        else               crc = (uint16_t)(crc << 1);
    }
    return crc;
}

static bool dmk_is_dam(uint8_t b) {
    return (b == 0xFB || b == 0xF8 || b == 0xF9 || b == 0xFA);
}

/* Scan a single DMK track's IDAM table to count sectors and find min/max sector IDs */
static uint8_t dmk_scan_track_secs(host_file_t *hf, uint32_t trk_off, uint16_t track_len,
                                   uint8_t flags, uint8_t *out_min_sec, uint8_t *out_max_sec) {
    uint8_t idam_raw[DMK_IDAM_BYTES];
    if (!pio_seek(hf, trk_off) || pio_read(hf, idam_raw, DMK_IDAM_BYTES) != DMK_IDAM_BYTES) {
        return 0;
    }

    uint8_t count = 0;
    uint8_t min_s = 0xFF;
    uint8_t max_s = 0;
    uint16_t prev_off = 0;

    for (uint8_t i = 0; i < DMK_MAX_IDAMS; i++) {
        uint16_t entry = (uint16_t)idam_raw[i * 2u] | ((uint16_t)idam_raw[i * 2u + 1u] << 8);
        if (entry == 0) break;

        uint16_t off = entry & 0x3FFFu;
        if (off < DMK_IDAM_BYTES || off >= track_len) return 0;
        if (prev_off != 0 && off < prev_off) return 0;
        prev_off = off;

        bool is_dd = (entry & 0x8000u) != 0;
        uint8_t step = (!is_dd && !(flags & DMK_FLAG_SD_ONLY)) ? 2u : 1u;
        uint8_t id_win[16];
        uint32_t need = (uint32_t)step * 7u;
        if (off + need > track_len) continue;
        if (!pio_seek(hf, trk_off + off) || pio_read(hf, id_win, need) != need) return 0;
        if (id_win[0] != 0xFE) return 0;

        uint8_t sec = id_win[3u * step];
        uint8_t szc = id_win[4u * step];
        if (szc != 1) continue; /* Only 256-byte sectors */

        if (sec < min_s) min_s = sec;
        if (sec > max_s) max_s = sec;
        count++;
    }

    if (count > 0) {
        if (out_min_sec) *out_min_sec = min_s;
        if (out_max_sec) *out_max_sec = max_s;
    }
    return count;
}

/* Probe whether an open file is a valid David Keil DMK disk image */
static bool probe_dmk(host_file_t *hf, const uint8_t hdr[256], trs_disk_t *d) {
    if (hf->size < DMK_HEADER_SIZE + DMK_IDAM_BYTES + 256u) return false;
    if (hdr[0] != 0x00 && hdr[0] != 0xFF) return false;

    uint8_t tracks = hdr[1];
    uint16_t track_len = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    uint8_t flags = hdr[4];

    if (tracks < 1 || tracks > 96) return false;
    if (track_len <= DMK_IDAM_BYTES || track_len > 16384u) return false;

    /* Bytes 5..11 must be zero */
    for (int i = 5; i <= 11; i++) {
        if (hdr[i] != 0x00) return false;
    }

    /* Bytes 12..15 must be 0x00000000 (virtual) or 0x12345678 LE (real disk) */
    uint32_t sig = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8) |
                   ((uint32_t)hdr[14] << 16) | ((uint32_t)hdr[15] << 24);
    if (sig != 0x00000000u && sig != 0x12345678u) return false;

    uint8_t file_sides = (flags & DMK_FLAG_SS) ? 1u : 2u;
    uint32_t expected = DMK_HEADER_SIZE + (uint32_t)tracks * file_sides * track_len;
    if (hf->size < expected || hf->size > expected + 512u) return false;

    /* Validate Track 0 Side 0 IDAMs (must contain at least 1 valid 0xFE IDAM) */
    uint8_t t0_min = 0, t0_max = 0;
    uint8_t t0_cnt = dmk_scan_track_secs(hf, DMK_HEADER_SIZE, track_len, flags, &t0_min, &t0_max);
    if (t0_cnt == 0) return false;

    /* Probe Track 1 Side 0 for primary data geometry (handles mixed FM Track 0 + MFM Track 1..N) */
    uint8_t min_s = t0_min, max_s = t0_max, cnt = t0_cnt;
    if (tracks > 1) {
        uint32_t t1_off = DMK_HEADER_SIZE + (uint32_t)file_sides * track_len;
        uint8_t t1_min = 0, t1_max = 0;
        uint8_t t1_cnt = dmk_scan_track_secs(hf, t1_off, track_len, flags, &t1_min, &t1_max);
        if (t1_cnt > 0) {
            min_s = t1_min;
            max_s = t1_max;
            cnt = t1_cnt;
        }
    }

    /* Determine logical heads (check if Side 1 actually holds sectors when file_sides == 2) */
    uint8_t logical_heads = file_sides;
    if (file_sides == 2) {
        uint8_t s1_cnt = dmk_scan_track_secs(hf, DMK_HEADER_SIZE + track_len, track_len, flags, NULL, NULL);
        if (s1_cnt == 0 && tracks > 1) {
            uint32_t t1s1_off = DMK_HEADER_SIZE + 3u * track_len;
            s1_cnt = dmk_scan_track_secs(hf, t1s1_off, track_len, flags, NULL, NULL);
        }
        if (s1_cnt == 0) logical_heads = 1;
    }

    uint8_t spt = (uint8_t)(max_s - min_s + 1u);
    if (spt < cnt) spt = cnt;

    d->fmt = FMT_DMK;
    d->data_offset = DMK_HEADER_SIZE;
    d->cyls = tracks;
    d->heads = logical_heads;
    d->spt = spt;
    d->sec_per_cyl = (uint16_t)d->heads * d->spt;
    d->write_protect = (hdr[0] == 0xFF) ? 1u : 0u;
    d->dmk_track_len = track_len;
    d->dmk_flags = flags;
    d->dmk_sec_base = min_s;
    return true;
}

/* Locate a logical sector (LSN) inside a DMK file */
static bool dmk_locate_sector(trs_disk_t *d, uint32_t lsn, dmk_sec_loc_t *out) {
    uint16_t cyl = (uint16_t)(lsn / d->sec_per_cyl);
    uint16_t rem = (uint16_t)(lsn % d->sec_per_cyl);
    uint8_t side = (uint8_t)(rem / d->spt);
    uint8_t sec  = (uint8_t)((rem % d->spt) + d->dmk_sec_base);

    if (cyl >= d->cyls) return false;
    uint8_t file_sides = (d->dmk_flags & DMK_FLAG_SS) ? 1u : 2u;
    if (side >= file_sides) return false;

    uint32_t trk_off = DMK_HEADER_SIZE + ((uint32_t)cyl * file_sides + side) * d->dmk_track_len;
    uint8_t idam_raw[DMK_IDAM_BYTES];
    if (!pio_seek(&d->hf, trk_off) || pio_read(&d->hf, idam_raw, DMK_IDAM_BYTES) != DMK_IDAM_BYTES) {
        return false;
    }

    uint8_t win[160];
    for (uint8_t i = 0; i < DMK_MAX_IDAMS; i++) {
        uint16_t entry = (uint16_t)idam_raw[i * 2u] | ((uint16_t)idam_raw[i * 2u + 1u] << 8);
        if (entry == 0) break;

        uint16_t id_off = entry & 0x3FFFu;
        if (id_off < DMK_IDAM_BYTES || id_off >= d->dmk_track_len) continue;

        bool is_dd = (entry & 0x8000u) != 0;
        uint8_t step = (!is_dd && !(d->dmk_flags & DMK_FLAG_SD_ONLY)) ? 2u : 1u;

        uint32_t avail = (uint32_t)d->dmk_track_len - id_off;
        uint32_t want = (uint32_t)step * 72u;
        if (want > sizeof(win)) want = sizeof(win);
        if (want > avail) want = avail;
        if (want < (uint32_t)step * 12u) continue;

        if (!pio_seek(&d->hf, trk_off + id_off) || pio_read(&d->hf, win, want) != want) {
            return false;
        }

        if (win[0] != 0xFE) continue;
        if (win[1u * step] != (uint8_t)cyl) continue;
        if (win[3u * step] != sec) continue;
        if (win[4u * step] != 1u) continue; /* 256-byte sector */

        /* Scan gap after 7-byte ID field (0xFE C H R N CRC1 CRC2) for Data Address Mark */
        uint32_t pos = 7u * step;
        bool saw_sync = false;
        while (pos < want) {
            uint8_t b = win[pos];
            if (is_dd) {
                if (b == 0xA1) {
                    saw_sync = true;
                } else if (saw_sync && dmk_is_dam(b)) {
                    uint32_t data_rel = id_off + pos + step;
                    if (data_rel + (uint32_t)step * 258u <= d->dmk_track_len) {
                        out->data_off = trk_off + data_rel;
                        out->dam_byte = b;
                        out->step     = step;
                        out->is_dd    = is_dd;
                        return true;
                    }
                    break;
                } else {
                    saw_sync = false;
                }
            } else {
                if (b == 0x00) {
                    saw_sync = true;
                } else if (saw_sync && dmk_is_dam(b)) {
                    uint32_t data_rel = id_off + pos + step;
                    if (data_rel + (uint32_t)step * 258u <= d->dmk_track_len) {
                        out->data_off = trk_off + data_rel;
                        out->dam_byte = b;
                        out->step     = step;
                        out->is_dd    = is_dd;
                        return true;
                    }
                    break;
                } else if (b != 0xFF) {
                    saw_sync = false;
                }
            }
            pos += step;
        }
    }
    return false;
}

/* Decode JV3 sector size code (bits 0..1 of flags) for an in-use entry */
static uint16_t jv3_sector_bytes(uint8_t flags) {
    switch (flags & JV3_FLAG_SIZE) {
        case 0: return 256;
        case 1: return 128;
        case 2: return 512;
        case 3: return 1024;
    }
    return 256;
}

/* Check whether an open file looks like a JV3 container */
static bool probe_jv3(host_file_t *hf, uint16_t *out_cyls, uint8_t *out_heads,
                      uint8_t *out_spt, uint8_t *out_wp) {
    if (hf->size < JV3_HEADER_SIZE + 256u) return false;

    uint8_t wp = 0x55;
    if (!pio_seek(hf, JV3_HEADER_SIZE - 1u) || pio_read(hf, &wp, 1) != 1) {
        return false;
    }
    if (wp != 0x00 && wp != 0xFF) return false;

    /* Scan the 2901 3-byte descriptors in chunks inside g_io_buf (3072B = 1024 entries) */
    uint8_t max_trk = 0;
    uint8_t max_sec = 0;
    uint8_t max_side = 0;
    uint32_t used_entries = 0;
    uint32_t data_bytes = 0;
    uint32_t idx = 0;

    if (!pio_seek(hf, 0)) return false;

    while (idx < JV3_MAX_SECTORS) {
        uint32_t batch = JV3_MAX_SECTORS - idx;
        if (batch > 1024u) batch = 1024u;
        uint32_t nbytes = batch * 3u;
        if (pio_read(hf, g_io_buf, nbytes) != nbytes) return false;

        for (uint32_t i = 0; i < batch; i++) {
            uint8_t trk = g_io_buf[i * 3u + 0u];
            uint8_t sec = g_io_buf[i * 3u + 1u];
            uint8_t flg = g_io_buf[i * 3u + 2u];
            if (trk == 0xFF) {
                /* Free entry: in valid JV3, sec is typically 0xFF */
                if (sec != 0xFF && used_entries == 0) return false;
                continue;
            }
            if (trk >= 100 || sec >= 40) return false;
            used_entries++;
            if (trk > max_trk) max_trk = trk;
            if (sec > max_sec) max_sec = sec;
            if (flg & JV3_FLAG_SIDE) max_side = 1;
            data_bytes += jv3_sector_bytes(flg);
        }
        idx += batch;
    }

    if (used_entries < 10) return false;
    /* File size must be at least header + payload (allow slight padding) */
    if (hf->size < JV3_HEADER_SIZE + data_bytes) return false;

    *out_cyls = (uint16_t)max_trk + 1u;
    *out_heads = (uint8_t)(max_side + 1u);
    *out_spt = (uint8_t)(max_sec + 1u);
    *out_wp = (wp == 0xFF) ? 1 : 0;
    return true;
}

/* Locate byte offset of (cyl, side, sec) inside JV3 data area */
static bool jv3_find_sector_offset(trs_disk_t *d, uint32_t lsn, uint32_t *out_off) {
    uint16_t cyl = (uint16_t)(lsn / d->sec_per_cyl);
    uint16_t rem = (uint16_t)(lsn % d->sec_per_cyl);
    uint8_t side = (uint8_t)(rem / d->spt);
    uint8_t sec  = (uint8_t)(rem % d->spt);

    uint32_t cur_off = JV3_HEADER_SIZE;
    uint32_t idx = 0;
    if (!pio_seek(&d->hf, 0)) return false;

    while (idx < JV3_MAX_SECTORS) {
        uint32_t batch = JV3_MAX_SECTORS - idx;
        if (batch > 1024u) batch = 1024u;
        uint32_t nbytes = batch * 3u;
        if (pio_read(&d->hf, g_io_buf, nbytes) != nbytes) return false;

        for (uint32_t i = 0; i < batch; i++) {
            uint8_t t = g_io_buf[i * 3u + 0u];
            uint8_t s = g_io_buf[i * 3u + 1u];
            uint8_t f = g_io_buf[i * 3u + 2u];
            if (t == 0xFF) {
                /* Free slot still occupies 256 bytes only if non-trailing; standard JV3 advances 256 */
                cur_off += 256u;
                continue;
            }
            uint8_t e_side = (f & JV3_FLAG_SIDE) ? 1u : 0u;
            uint16_t e_sz  = jv3_sector_bytes(f);
            if (t == (uint8_t)cyl && s == sec && e_side == side) {
                *out_off = cur_off;
                return true;
            }
            cur_off += e_sz;
        }
        idx += batch;
    }
    return false;
}

/* Check whether a 256-byte sector looks like a plausible LS-DOS / TRSDOS GAT */
static bool looks_like_gat(const uint8_t *gat) {
    /* Volume name at 0xD0..0xD7 should be printable ASCII */
    int printable = 0;
    for (int i = 0xD0; i < 0xD8; i++) {
        if (gat[i] >= 0x20 && gat[i] <= 0x7E) printable++;
    }
    if (printable < 4) return false;
    /* Granules per cylinder - 1 at 0xCC is typically 1..7 */
    if (gat[0xCC] > 7 && gat[0xCC] != 0xFF) return false;
    return true;
}

/* Finalize GAT-derived granule parameters */
static void finalize_granules(trs_disk_t *d) {
    uint8_t gat[256];
    d->sec_per_cyl = (uint16_t)d->heads * (uint16_t)d->spt;
    if (d->sec_per_cyl == 0) d->sec_per_cyl = 36;

    /*
     * LS-DOS 6.3 GAT byte 0xCC stores (sectors_per_granule - 1) in bits 0..4
     * (e.g. 5 => 6 sec/gran on 18-spt DD, 4 => 5 sec/gran on 10-spt SD).
     * Cross-check with GAT lockout table (gat[0x60]) where unmasked low bits
     * indicate active granules/cylinder (e.g. 0xF8 => 3 grans/cyl on 40x1x18).
     */
    if (disk_read_sector(d, (uint32_t)d->dir_cyl * d->sec_per_cyl, gat)) {
        uint8_t spg = (uint8_t)((gat[0xCC] & 0x1Fu) + 1u);
        if (spg >= 1 && spg <= 32 && (d->sec_per_cyl % spg) == 0 &&
            (d->sec_per_cyl / spg) >= 1 && (d->sec_per_cyl / spg) <= 8) {
            d->sec_per_gran  = spg;
            d->grans_per_cyl = (uint8_t)(d->sec_per_cyl / spg);
        } else if ((d->sec_per_cyl % 6u) == 0 && (d->sec_per_cyl / 6u) <= 8) {
            d->sec_per_gran  = 6;
            d->grans_per_cyl = (uint8_t)(d->sec_per_cyl / 6u);
        } else if ((d->sec_per_cyl % 5u) == 0 && (d->sec_per_cyl / 5u) <= 8) {
            d->sec_per_gran  = 5;
            d->grans_per_cyl = (uint8_t)(d->sec_per_cyl / 5u);
        } else {
            d->grans_per_cyl = 2;
            d->sec_per_gran  = (uint8_t)(d->sec_per_cyl / 2u);
        }
    } else {
        if ((d->sec_per_cyl % 6u) == 0 && (d->sec_per_cyl / 6u) <= 8) {
            d->sec_per_gran  = 6;
            d->grans_per_cyl = (uint8_t)(d->sec_per_cyl / 6u);
        } else if ((d->sec_per_cyl % 5u) == 0 && (d->sec_per_cyl / 5u) <= 8) {
            d->sec_per_gran  = 5;
            d->grans_per_cyl = (uint8_t)(d->sec_per_cyl / 5u);
        } else {
            d->grans_per_cyl = 2;
            d->sec_per_gran  = (uint8_t)(d->sec_per_cyl / 2u);
        }
    }
    if (d->sec_per_gran == 0) d->sec_per_gran = 1;

    /* LS-DOS directory sector count: 32 sectors max (256 DECs), or sec_per_cyl - 2 */
    if (d->sec_per_cyl >= 34) {
        d->dir_sectors = 32;
    } else if (d->sec_per_cyl > 2) {
        d->dir_sectors = (uint8_t)(d->sec_per_cyl - 2u);
    } else {
        d->dir_sectors = 8;
    }
}

bool disk_open(trs_disk_t *d, const char *path, bool writable) {
    memset(d, 0, sizeof(*d));
    if (!pio_open(&d->hf, path, writable, false)) {
        return false;
    }

    uint8_t hdr[256];
    memset(hdr, 0, sizeof(hdr));
    if (pio_read(&d->hf, hdr, 256) < 256) {
        pio_close(&d->hf);
        return false;
    }

    /* Tier 1: Matthew Reed HDV (magic 0x56, 0xCB) */
    if (hdr[0] == 0x56 && hdr[1] == 0xCB && hdr[4] > 0 && hdr[5] > 0) {
        d->fmt = FMT_REED_HDV;
        d->data_offset = 256;
        d->cyls = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
        d->spt = hdr[4];
        d->heads = hdr[5];
        d->write_protect = hdr[6] ? 1 : 0;
        d->sec_per_cyl = (uint16_t)d->heads * d->spt;
        /* Read boot sector 0 to find directory cylinder */
        uint8_t sec0[256];
        d->dir_cyl = 1;
        if (disk_read_sector(d, 0, sec0) && sec0[2] > 0 && sec0[2] < d->cyls) {
            d->dir_cyl = sec0[2];
        }
        finalize_granules(d);
        return true;
    }

    /* Tier 2: Misosys DiskDISK ("DiskDISK") */
    if (memcmp(hdr, "DiskDISK", 8) == 0) {
        d->fmt = FMT_DISKDISK;
        d->data_offset = 256;

        /* Authentic DiskDISK stores 10-byte DCT at hdr[8..17] (or 7-byte block at hdr[0xF9..0xFF]) */
        if (hdr[8] == 0xC3 && hdr[14] > 0 && (hdr[15] & 0x1F) > 0) {
            d->write_protect = (hdr[11] & 0x80) ? 1 : 0;
            d->heads = (hdr[12] & 0x20) ? 2 : 1;
            d->cyls  = (uint16_t)hdr[14] + 1u;
            d->spt   = (uint8_t)((hdr[15] & 0x1F) + 1u);
            d->dir_cyl = hdr[17];
        } else {
            /* Fallback: standard 40-cyl DS/DD LS-DOS */
            d->heads = 2;
            d->cyls  = 40;
            d->spt   = 18;
            d->dir_cyl = 1;
        }
        d->sec_per_cyl = (uint16_t)d->heads * d->spt;

        /* Verify dir_cyl against Sector 0 if hdr[17] was 0 */
        if (d->dir_cyl == 0) {
            uint8_t sec0[256];
            if (disk_read_sector(d, 0, sec0) && sec0[2] > 0 && sec0[2] < d->cyls) {
                d->dir_cyl = sec0[2];
            } else {
                d->dir_cyl = 17;
            }
        }
        finalize_granules(d);
        return true;
    }

    /* Tier 3: David Keil DMK track bitstream container */
    if (probe_dmk(&d->hf, hdr, d)) {
        uint8_t sec0[256];
        d->dir_cyl = 0;
        if (disk_read_sector(d, 0, sec0) && sec0[2] > 0 && sec0[2] < d->cyls) {
            d->dir_cyl = sec0[2];
        } else {
            static const uint8_t cand_dc[2] = { 17, 1 };
            for (int k = 0; k < 2; k++) {
                uint8_t dc = cand_dc[k];
                if (dc < d->cyls) {
                    uint8_t probe_gat[256];
                    if (disk_read_sector(d, (uint32_t)dc * d->sec_per_cyl, probe_gat) &&
                        looks_like_gat(probe_gat)) {
                        d->dir_cyl = dc;
                        break;
                    }
                }
            }
            if (d->dir_cyl == 0) d->dir_cyl = (d->cyls > 17) ? 17 : 1;
        }
        finalize_granules(d);
        return true;
    }

    /* Tier 4: JV3 indexed descriptor table */
    uint16_t jv3_c = 0;
    uint8_t jv3_h = 0, jv3_s = 0, jv3_wp = 0;
    if (probe_jv3(&d->hf, &jv3_c, &jv3_h, &jv3_s, &jv3_wp)) {
        d->fmt = FMT_JV3;
        d->data_offset = JV3_HEADER_SIZE;
        d->cyls = jv3_c;
        d->heads = jv3_h;
        d->spt = jv3_s;
        d->write_protect = jv3_wp;
        d->sec_per_cyl = (uint16_t)d->heads * d->spt;

        uint8_t sec0[256];
        d->dir_cyl = 17;
        if (disk_read_sector(d, 0, sec0) && sec0[2] > 0 && sec0[2] < d->cyls) {
            d->dir_cyl = sec0[2];
        }
        finalize_granules(d);
        return true;
    }

    /* Tier 4: Linear headerless stream (JV1 or Flat HDV) */
    d->data_offset = 0;
    uint32_t total_sec = d->hf.size / 256u;
    if (d->hf.size > 1572864u) {
        d->fmt = FMT_FLAT_HDV;
        d->spt = 32;
        d->heads = 4;
        d->sec_per_cyl = (uint16_t)d->heads * d->spt;
        d->cyls = (uint16_t)(total_sec / d->sec_per_cyl);
        if (d->cyls == 0) d->cyls = 1;
        d->dir_cyl = (hdr[2] > 0 && hdr[2] < d->cyls) ? hdr[2] : 1;
        finalize_granules(d);
        return true;
    }

    /* JV1 floppy auto-geometry */
    d->fmt = FMT_JV1;
    uint8_t sec0_dir = hdr[2];
    /* Try candidate (heads, spt) geometries */
    static const uint8_t cand_h[] = { 1, 2, 1, 2 };
    static const uint8_t cand_s[] = { 10, 18, 18, 10 };
    bool found = false;
    for (int i = 0; i < 4; i++) {
        uint16_t spc = (uint16_t)cand_h[i] * cand_s[i];
        uint16_t c = (uint16_t)(total_sec / spc);
        if (c < 18) continue;
        d->heads = cand_h[i];
        d->spt = cand_s[i];
        d->sec_per_cyl = spc;
        d->cyls = c;

        uint8_t try_dc[3] = { sec0_dir, 17, 1 };
        for (int k = 0; k < 3; k++) {
            uint8_t dc = try_dc[k];
            if (dc == 0 || dc >= c) continue;
            uint8_t probe_gat[256];
            if (disk_read_sector(d, (uint32_t)dc * spc, probe_gat) && looks_like_gat(probe_gat)) {
                d->dir_cyl = dc;
                found = true;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        /* Sensible default for JV1 */
        if (total_sec <= 450) {
            d->heads = 1; d->spt = 10;
        } else if (total_sec <= 800) {
            d->heads = 1; d->spt = 18;
        } else {
            d->heads = 2; d->spt = 18;
        }
        d->sec_per_cyl = (uint16_t)d->heads * d->spt;
        d->cyls = (uint16_t)(total_sec / d->sec_per_cyl);
        if (d->cyls == 0) d->cyls = 40;
        d->dir_cyl = (sec0_dir > 0 && sec0_dir < d->cyls) ? sec0_dir : 17;
    }

    finalize_granules(d);
    return true;
}

void disk_close(trs_disk_t *d) {
    pio_close(&d->hf);
    d->gat_loaded = false;
}

bool disk_read_sector(trs_disk_t *d, uint32_t lsn, uint8_t *buf256) {
    if (d->fmt == FMT_DMK) {
        dmk_sec_loc_t loc;
        if (!dmk_locate_sector(d, lsn, &loc)) {
            memset(buf256, 0, 256);
            return false;
        }
        uint32_t raw_len = 256u * loc.step;
        uint8_t raw[512];
        if (!pio_seek(&d->hf, loc.data_off) || pio_read(&d->hf, raw, raw_len) != raw_len) {
            memset(buf256, 0, 256);
            return false;
        }
        if (loc.step == 1) {
            memcpy(buf256, raw, 256);
        } else {
            for (int i = 0; i < 256; i++) buf256[i] = raw[i * 2];
        }
        return true;
    }

    uint32_t off;
    if (d->fmt == FMT_JV3) {
        if (!jv3_find_sector_offset(d, lsn, &off)) {
            memset(buf256, 0, 256);
            return false;
        }
    } else {
        off = d->data_offset + lsn * 256u;
    }
    if (!pio_seek(&d->hf, off)) return false;
    return pio_read(&d->hf, buf256, 256) == 256;
}

bool disk_write_sector(trs_disk_t *d, uint32_t lsn, const uint8_t *buf256) {
    if (!d->hf.writable || d->write_protect) return false;
    if (d->fmt == FMT_DMK) {
        dmk_sec_loc_t loc;
        if (!dmk_locate_sector(d, lsn, &loc)) return false;

        /* Recompute authentic WD1771/WD1793 CRC-16 CCITT (0x1021) */
        uint16_t crc = 0xFFFFu;
        if (loc.is_dd) {
            crc = crc16_ccitt_update(crc, 0xA1);
            crc = crc16_ccitt_update(crc, 0xA1);
            crc = crc16_ccitt_update(crc, 0xA1);
        }
        crc = crc16_ccitt_update(crc, loc.dam_byte);
        for (int i = 0; i < 256; i++) {
            crc = crc16_ccitt_update(crc, buf256[i]);
        }

        uint8_t out_raw[516];
        uint32_t out_len = 258u * loc.step;
        if (loc.step == 1) {
            memcpy(out_raw, buf256, 256);
            out_raw[256] = (uint8_t)(crc >> 8);
            out_raw[257] = (uint8_t)(crc & 0xFFu);
        } else {
            for (int i = 0; i < 256; i++) {
                out_raw[i * 2]     = buf256[i];
                out_raw[i * 2 + 1] = buf256[i];
            }
            out_raw[512] = (uint8_t)(crc >> 8);
            out_raw[513] = (uint8_t)(crc >> 8);
            out_raw[514] = (uint8_t)(crc & 0xFFu);
            out_raw[515] = (uint8_t)(crc & 0xFFu);
        }
        if (!pio_seek(&d->hf, loc.data_off)) return false;
        return pio_write(&d->hf, out_raw, out_len) == out_len;
    }

    uint32_t off;
    if (d->fmt == FMT_JV3) {
        if (!jv3_find_sector_offset(d, lsn, &off)) return false;
    } else {
        off = d->data_offset + lsn * 256u;
    }
    if (!pio_seek(&d->hf, off)) return false;
    return pio_write(&d->hf, buf256, 256) == 256;
}

/*
 * Format a brand-new, sequentially written (100% contiguous on FAT32) LS-DOS 6.3 image.
 * Supports DiskDISK (default), JV1, and Reed HDV.
 */
bool disk_format_new(const char *path, disk_format_t fmt, uint16_t cyls,
                     uint8_t heads, uint8_t spt, uint8_t dir_cyl,
                     const char *volname) {
    if (fmt != FMT_DISKDISK && fmt != FMT_JV1 && fmt != FMT_REED_HDV) {
        return false;
    }
    if (cyls < 2 || heads < 1 || spt < 4 || dir_cyl >= cyls) {
        return false;
    }

    host_file_t hf;
    if (!pio_open(&hf, path, true, true)) {
        return false;
    }

    uint16_t sec_per_cyl = (uint16_t)heads * spt;
    uint32_t expected_bytes = (uint32_t)cyls * sec_per_cyl * 256u +
                              ((fmt == FMT_JV1) ? 0u : 256u);
    (void)pio_hint_contiguous(&hf, expected_bytes);
    uint8_t grans_per_cyl;
    uint8_t sec_per_gran;
    if ((sec_per_cyl % 6u) == 0 && (sec_per_cyl / 6u) <= 8) {
        sec_per_gran  = 6;
        grans_per_cyl = (uint8_t)(sec_per_cyl / 6u);
    } else if ((sec_per_cyl % 5u) == 0 && (sec_per_cyl / 5u) <= 8) {
        sec_per_gran  = 5;
        grans_per_cyl = (uint8_t)(sec_per_cyl / 5u);
    } else {
        grans_per_cyl = 2;
        sec_per_gran  = (uint8_t)(sec_per_cyl / 2u);
    }
    uint8_t full_gran_mask = (uint8_t)((1u << grans_per_cyl) - 1u);

    /* 1. Optional 256-byte container header */
    if (fmt == FMT_DISKDISK || fmt == FMT_REED_HDV) {
        uint8_t hdr[256];
        memset(hdr, 0, sizeof(hdr));
        if (fmt == FMT_DISKDISK) {
            memcpy(hdr, "DiskDISK", 8);
            /* Authentic 10-byte DCT block at offset 8..17 */
            hdr[8]  = 0xC3;                          /* JP opcode */
            hdr[9]  = 0x08;                          /* Driver vector placeholder */
            hdr[10] = 0x2E;
            hdr[11] = 0x40;                          /* Double density, RW */
            hdr[12] = (heads > 1) ? 0x20 : 0x00;     /* Bit 5 = double-sided */
            hdr[13] = 0x00;                          /* Current cyl */
            hdr[14] = (uint8_t)(cyls - 1u);          /* Max cylinder */
            hdr[15] = (uint8_t)(spt - 1u);           /* Sectors/track - 1 */
            {
                uint8_t gpt = (heads > 1 && (grans_per_cyl % heads) == 0)
                                  ? (uint8_t)(grans_per_cyl / heads)
                                  : grans_per_cyl;
                uint8_t spg = (gpt > 0 && (spt % gpt) == 0) ? (uint8_t)(spt / gpt) : 6u;
                hdr[16] = (uint8_t)(((gpt - 1u) << 5) | ((spg - 1u) & 0x1Fu));
            }
            hdr[17] = dir_cyl;                       /* Directory cylinder */
        } else {
            hdr[0] = 0x56;
            hdr[1] = 0xCB;
            hdr[2] = (uint8_t)(cyls & 0xFF);
            hdr[3] = (uint8_t)(cyls >> 8);
            hdr[4] = spt;
            hdr[5] = heads;
            hdr[6] = 0x00; /* RW */
        }
        if (pio_write(&hf, hdr, 256) != 256) {
            pio_close(&hf);
            return false;
        }
    }

    /* 2. Build Sector 0 (boot sector marker) and GAT sector */
    uint8_t sec0[256];
    memset(sec0, 0, sizeof(sec0));
    sec0[0] = 0x00;
    sec0[1] = 0xFE;
    sec0[2] = dir_cyl;

    uint8_t gat[256];
    memset(gat, 0, sizeof(gat));
    /* Mark cylinders >= cyls as locked/unavailable (0xFF) */
    for (uint16_t c = cyls; c < 0xC0; c++) {
        gat[c] = 0xFF;
    }
    /* Mark directory cylinder as allocated */
    gat[dir_cyl] = full_gran_mask;
    /* Unused cylinder lock table 0x60..0xBF = 0x00 for available cyls, 0xFF beyond */
    for (uint16_t c = 0xC0; c < 0xCB; c++) {
        gat[c] = 0xFF;
    }
    gat[0xCB] = 0x62;                            /* LS-DOS 6.2/6.3 format version */
    gat[0xCC] = (uint8_t)(sec_per_gran - 1u);    /* Sectors/granule - 1 */
    gat[0xCD] = 0x62;
    gat[0xCE] = 0xE0;
    gat[0xCF] = 0x42;

    /* 8-byte space-padded volume name at 0xD0..0xD7 */
    memset(&gat[0xD0], ' ', 8);
    if (volname && volname[0]) {
        for (int i = 0; i < 8 && volname[i]; i++) {
            char ch = volname[i];
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            gat[0xD0 + i] = (uint8_t)ch;
        }
    } else {
        memcpy(&gat[0xD0], "NEWDISK ", 8);
    }
    memcpy(&gat[0xD8], "01/01/26", 8);

    /* 3. Sequentially stream all sectors in 4 KiB chunks (guarantees 100% FAT32 contiguity) */
    uint32_t total_sec = (uint32_t)cyls * sec_per_cyl;
    uint32_t gat_lsn   = (uint32_t)dir_cyl * sec_per_cyl;
    uint32_t lsn = 0;

    while (lsn < total_sec) {
        uint32_t batch = total_sec - lsn;
        if (batch > 16u) batch = 16u; /* 16 * 256 = 4096 bytes */
        memset(g_io_buf, 0, batch * 256u);

        for (uint32_t i = 0; i < batch; i++) {
            uint32_t cur = lsn + i;
            if (cur == 0) {
                memcpy(&g_io_buf[i * 256u], sec0, 256);
            } else if (cur == gat_lsn) {
                memcpy(&g_io_buf[i * 256u], gat, 256);
            }
        }

        if (pio_write(&hf, g_io_buf, batch * 256u) != batch * 256u) {
            pio_close(&hf);
            return false;
        }
        lsn += batch;
    }

    pio_close(&hf);
    return true;
}

/* --------------------------------------------------------------------------
 * Container Conversion: DMK / JV3 / JV1 / DiskDISK / HDV -> Contiguous Linear
 * -------------------------------------------------------------------------- */
static uint8_t g_conv_buf[4096];

bool disk_convert(const char *src_path, const char *dst_path,
                  disk_format_t dst_fmt, uint32_t *out_missing) {
    if (dst_fmt != FMT_DISKDISK && dst_fmt != FMT_JV1 && dst_fmt != FMT_REED_HDV) {
        return false;
    }

    trs_disk_t src;
    if (!disk_open(&src, src_path, false)) {
        printf("Error: Cannot open source image '%s'\n", src_path);
        return false;
    }

    host_file_t out;
    if (!pio_open(&out, dst_path, true, true)) {
        printf("Error: Cannot create destination image '%s'\n", dst_path);
        disk_close(&src);
        return false;
    }

    /* Hint FatFs to allocate a 100% contiguous free cluster run for SPI0 driver */
    {
        uint32_t need_bytes = (uint32_t)src.cyls * src.sec_per_cyl * 256u +
                              ((dst_fmt == FMT_JV1) ? 0u : 256u);
        (void)pio_hint_contiguous(&out, need_bytes);
    }

    /* 1. Write optional 256-byte linear container header */
    if (dst_fmt == FMT_DISKDISK || dst_fmt == FMT_REED_HDV) {
        uint8_t hdr[256];
        memset(hdr, 0, sizeof(hdr));
        if (dst_fmt == FMT_DISKDISK) {
            memcpy(hdr, "DiskDISK", 8);
            hdr[8]  = 0xC3;
            hdr[9]  = 0x08;
            hdr[10] = 0x2E;
            hdr[11] = (src.spt >= 15) ? 0x40u : 0x00u; /* DD vs SD */
            hdr[12] = (src.heads > 1) ? 0x20u : 0x00u;
            hdr[13] = 0x00;
            hdr[14] = (uint8_t)(src.cyls > 0 ? src.cyls - 1u : 39u);
            hdr[15] = (uint8_t)(src.spt > 0 ? src.spt - 1u : 17u);
            {
                uint8_t gpc = (src.grans_per_cyl >= 1 && src.grans_per_cyl <= 8)
                                  ? src.grans_per_cyl : 6u;
                uint8_t gpt = (src.heads > 1 && (gpc % src.heads) == 0)
                                  ? (uint8_t)(gpc / src.heads) : gpc;
                uint8_t spg = (gpt > 0 && (src.spt % gpt) == 0)
                                  ? (uint8_t)(src.spt / gpt) : 6u;
                hdr[16] = (uint8_t)(((gpt - 1u) << 5) | ((spg - 1u) & 0x1Fu));
            }
            hdr[17] = src.dir_cyl;
        } else {
            hdr[0] = 0x56;
            hdr[1] = 0xCB;
            hdr[2] = (uint8_t)(src.cyls & 0xFFu);
            hdr[3] = (uint8_t)(src.cyls >> 8);
            hdr[4] = src.spt;
            hdr[5] = src.heads;
            hdr[6] = 0x00;
        }
        if (pio_write(&out, hdr, 256) != 256) {
            pio_close(&out);
            disk_close(&src);
            return false;
        }
    }

    /* 2. Sequentially stream all sectors into 4 KiB contiguous FAT32 writes */
    uint32_t total_sec = (uint32_t)src.cyls * src.sec_per_cyl;
    uint32_t missing = 0;
    uint32_t lsn = 0;

    while (lsn < total_sec) {
        uint32_t batch = total_sec - lsn;
        if (batch > 16u) batch = 16u;
        for (uint32_t i = 0; i < batch; i++) {
            uint8_t *slot = &g_conv_buf[i * 256u];
            if (!disk_read_sector(&src, lsn + i, slot)) {
                memset(slot, 0, 256);
                missing++;
            }
        }
        if (pio_write(&out, g_conv_buf, batch * 256u) != batch * 256u) {
            pio_close(&out);
            disk_close(&src);
            return false;
        }
        lsn += batch;
    }

    uint32_t total_bytes = total_sec * 256u + ((dst_fmt == FMT_JV1) ? 0u : 256u);
    printf("Converted %s (%s) -> %s (%s): %ux%ux%u (%lu sectors, %lu bytes)\n",
           src_path, disk_format_name(src.fmt),
           dst_path, disk_format_name(dst_fmt),
           src.cyls, src.heads, src.spt,
           (unsigned long)total_sec, (unsigned long)total_bytes);
    if (missing > 0) {
        printf("Note: %lu unmapped/missing sector(s) zero-filled (e.g. mixed-density Track 0)\n",
               (unsigned long)missing);
    }

    if (out_missing) *out_missing = missing;
    pio_close(&out);
    disk_close(&src);
    return true;
}

/* Subcommand: *trs80 convert <src_img> <dst_img> [-f diskdisk|jv1|hdv] */
int cmd_convert(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: trs80 convert <src_img> <dst_img> [-f diskdisk|jv1|hdv]\n");
        return 1;
    }
    const char *src_path = NULL;
    const char *dst_path = NULL;
    disk_format_t dst_fmt = FMT_DISKDISK;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            i++;
            if (str_casecmp(argv[i], "jv1") == 0) dst_fmt = FMT_JV1;
            else if (str_casecmp(argv[i], "hdv") == 0) dst_fmt = FMT_REED_HDV;
            else if (str_casecmp(argv[i], "diskdisk") == 0 || str_casecmp(argv[i], "dsk") == 0) dst_fmt = FMT_DISKDISK;
            else {
                printf("Error: Unsupported target format '%s' (use diskdisk, jv1, or hdv)\n", argv[i]);
                return 1;
            }
        } else if (!src_path) {
            src_path = argv[i];
        } else if (!dst_path) {
            dst_path = argv[i];
        }
    }

    if (!src_path || !dst_path) {
        printf("Usage: trs80 convert <src_img> <dst_img> [-f diskdisk|jv1|hdv]\n");
        return 1;
    }

    return disk_convert(src_path, dst_path, dst_fmt, NULL) ? 0 : 1;
}
