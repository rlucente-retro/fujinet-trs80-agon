/*
 * boot.c - TRSDOS.bin SRAM Mailbox Stager & OSboot Handover
 *
 * Loads TRSDOS.bin (66,048 bytes), patches lowcore mailbox tables directly:
 *   - DCT0$..DCT7$       (0x0470, 8 x 10B)
 *   - drv_target_table   (0x0E76, 8B)
 *   - dw_offset          (0x0F78, 8B)
 *   - sddvr_base_lba     (0xF408, 8 x 4B LE)
 *   - sddvr_map_lba      (0xF428, 4B LE)
 *   - sddvr_map_len      (0xF42C, 2B LE)
 * Writes staged kernel to /fnboot.tmp and invokes OSboot -<color>[v][f] /fnboot.tmp.
 */

#include "fnboot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HOST_TEST
#include <agon/mos.h>
#endif

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

/* Probe a local SD image file on MOS to detect DiskDISK header (+256B offset) and DCT params */
static bool probe_local_image(const char *rel_path, uint8_t *out_offset, uint8_t out_dct7[7]) {
    *out_offset = 1; /* Default DiskDISK */
    bool have_dct = false;

    char full[MAX_PATH_LEN + 2];
    if (rel_path[0] == '/') {
        snprintf(full, sizeof(full), "%s", rel_path);
    } else {
        snprintf(full, sizeof(full), "/%s", rel_path);
    }

    uint8_t hdr[256];
    bool ok = false;
#ifdef HOST_TEST
    FILE *fp = fopen(full, "rb");
    if (!fp) fp = fopen(rel_path, "rb");
    if (fp) {
        ok = (fread(hdr, 1, 256, fp) == 256);
        fclose(fp);
    }
#else
    uint8_t fh = mos_fopen(full, FA_OPEN_EXISTING | FA_READ);
    if (fh == 0) fh = mos_fopen(rel_path, FA_OPEN_EXISTING | FA_READ);
    if (fh != 0) {
        ok = (mos_fread(fh, (char *)hdr, 256) == 256);
        mos_fclose(fh);
    }
#endif
    if (!ok) return false;

    if (memcmp(hdr, "DiskDISK", 8) == 0) {
        *out_offset = 1;
        if (hdr[8] == 0xC3 && hdr[14] > 0) {
            memcpy(out_dct7, &hdr[11], 7);
            have_dct = true;
        }
    } else {
        *out_offset = 0;
    }
    return have_dct;
}

bool fnboot_stage_kernel(const fnboot_ctx_t *ctx, const char *out_tmp_path) {
    uint8_t *kbuf = (uint8_t *)malloc(TRSDOS_KERNEL_SIZE);
    if (!kbuf) {
        printf("Error: Out of memory staging kernel\n");
        return false;
    }

    /* 1. Load clean TRSDOS.bin */
    uint32_t nread = 0;
#ifdef HOST_TEST
    FILE *fp = fopen(ctx->kernel_path, "rb");
    if (!fp) {
        printf("Error: Cannot open kernel '%s'\n", ctx->kernel_path);
        free(kbuf);
        return false;
    }
    nread = (uint32_t)fread(kbuf, 1, TRSDOS_KERNEL_SIZE, fp);
    fclose(fp);
#else
    uint8_t fh = mos_fopen(ctx->kernel_path, FA_OPEN_EXISTING | FA_READ);
    if (fh == 0) {
        printf("Error: Cannot open kernel '%s'\n", ctx->kernel_path);
        free(kbuf);
        return false;
    }
    while (nread < TRSDOS_KERNEL_SIZE) {
        uint24_t want = (TRSDOS_KERNEL_SIZE - nread > 16384u)
                            ? 16384u
                            : (uint24_t)(TRSDOS_KERNEL_SIZE - nread);
        uint24_t got = mos_fread(fh, (char *)(kbuf + nread), want);
        nread += got;
        if (got < want) break;
    }
    mos_fclose(fh);
#endif
    if (nread < 65536u) {
        printf("Error: Kernel '%s' truncated (%lu bytes)\n",
               ctx->kernel_path, (unsigned long)nread);
        free(kbuf);
        return false;
    }

    /* 2. Stage SD Map Mailbox (0xF428 / 0xF42C) */
    if (ctx->map_found && ctx->map_lba != 0 && ctx->map_len != 0) {
        put_le32(&kbuf[OFF_SDDVR_MAP_LBA], ctx->map_lba);
        put_le16(&kbuf[OFF_SDDVR_MAP_LEN], ctx->map_len);
    } else {
        put_le32(&kbuf[OFF_SDDVR_MAP_LBA], 0);
        put_le16(&kbuf[OFF_SDDVR_MAP_LEN], 0);
    }

    /* 3. Stage Per-Drive Routing, Base LBAs, Container Offsets, and DCTs */
    for (int d = 0; d < MAX_DRIVES; d++) {
        const drive_cfg_t *dc = &ctx->drives[d];
        uint8_t *dct = &kbuf[OFF_DCT0 + (uint32_t)d * DCT_SIZE];

        if (dc->target == DRV_TARGET_SD && dc->sd_lba != 0) {
            kbuf[OFF_DRV_TARGET_TABLE + d] = DRV_TARGET_SD;
            put_le32(&kbuf[OFF_SDDVR_BASE_LBA + (uint32_t)d * 4u], dc->sd_lba);

            uint8_t off = 1;
            uint8_t dct7[7];
            if (probe_local_image(dc->label, &off, dct7)) {
                memcpy(&dct[3], dct7, 7);
            } else {
                /* Clone DCT0$ default geometry (40x2x18) */
                memcpy(&dct[3], &kbuf[OFF_DCT0 + 3], 7);
            }
            kbuf[OFF_DW_OFFSET + d] = off;

            dct[0] = 0xC3; /* JP FDCDVR (Active) */
            if (dc->mode == 1) dct[3] |= 0x80u;
            else               dct[3] &= 0x7Fu;
            uint8_t dbit = (d < 4) ? (uint8_t)(1u << d) : 0x01u;
            dct[4] = (uint8_t)((dct[4] & 0xE0u) | 0x10u | dbit); /* Alien + drive select */
            dct[5] = 0; /* Current cylinder = 0 */
        } else if (dc->target == DRV_TARGET_DW) {
            kbuf[OFF_DRV_TARGET_TABLE + d] = DRV_TARGET_DW;
            put_le32(&kbuf[OFF_SDDVR_BASE_LBA + (uint32_t)d * 4u], 0);
            kbuf[OFF_DW_OFFSET + d] = 1; /* DiskDISK default */

            dct[0] = 0xC3; /* JP FDCDVR (Active) */
            if (d > 0) {
                memcpy(&dct[3], &kbuf[OFF_DCT0 + 3], 7);
                if (dc->mode == 1) dct[3] |= 0x80u;
                else               dct[3] &= 0x7Fu;
                uint8_t dbit = (d < 4) ? (uint8_t)(1u << d) : 0x01u;
                dct[4] = (uint8_t)((dct[4] & 0xE0u) | 0x10u | dbit);
                dct[5] = 0;
            }
        } else {
            /* Offline */
            if (d > 0) {
                kbuf[OFF_DRV_TARGET_TABLE + d] = DRV_TARGET_DW;
                put_le32(&kbuf[OFF_SDDVR_BASE_LBA + (uint32_t)d * 4u], 0);
                dct[0] = 0xC9; /* RET (Offline) */
                dct[4] &= (uint8_t)~0x10u;
            }
        }
    }

    /* 4. Write staged kernel to temporary file */
    bool wok = false;
#ifdef HOST_TEST
    FILE *out = fopen(out_tmp_path, "wb");
    if (out) {
        wok = (fwrite(kbuf, 1, nread, out) == nread);
        fclose(out);
    }
#else
    uint8_t ofh = mos_fopen(out_tmp_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (ofh != 0) {
        uint32_t written = 0;
        while (written < nread) {
            uint24_t chunk = (nread - written > 16384u) ? 16384u : (uint24_t)(nread - written);
            uint24_t w = mos_fwrite(ofh, (char *)(kbuf + written), chunk);
            written += w;
            if (w < chunk) break;
        }
        mos_fclose(ofh);
        wok = (written == nread);
    }
#endif

    free(kbuf);
    if (!wok) {
        printf("Error: Failed writing staged kernel '%s'\n", out_tmp_path);
        return false;
    }
    return true;
}

int fnboot_launch(const fnboot_ctx_t *ctx) {
#ifdef HOST_TEST
    const char *staged = "fnboot.tmp";
#else
    const char *staged = "/fnboot.tmp";
#endif
    printf("Staging TRSDOS mailbox -> %s...\n", staged);
    if (!fnboot_stage_kernel(ctx, staged)) {
        return 1;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "OSboot -%u%s%s %s",
             (ctx->term_color >= 1 && ctx->term_color <= 7) ? ctx->term_color : 2u,
             ctx->use_vt100 ? "v" : "",
             ctx->use_font1 ? "f" : "",
             staged);

    printf("Booting: %s\n", cmd);
#ifndef HOST_TEST
    return (int)mos_oscli(cmd, NULL, 0);
#else
    return 0;
#endif
}
