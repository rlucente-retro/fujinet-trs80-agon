/*
 * platform_io.c - Host File I/O Abstraction for Agon Quark MOS & Native Host Tests
 */

#include "trs80.h"
#include <string.h>

#ifdef HOST_TEST

#include <stdio.h>
#include <sys/stat.h>

bool pio_open(host_file_t *hf, const char *path, bool write, bool create) {
    memset(hf, 0, sizeof(*hf));
    const char *mode;
    if (create) {
        mode = "w+b";
    } else if (write) {
        mode = "r+b";
    } else {
        mode = "rb";
    }
    FILE *fp = fopen(path, mode);
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    hf->fp = fp;
    hf->open = true;
    hf->writable = write || create;
    hf->size = (sz > 0) ? (uint32_t)sz : 0;
    return true;
}

void pio_close(host_file_t *hf) {
    if (hf->open && hf->fp) {
        fclose((FILE *)hf->fp);
    }
    hf->fp = NULL;
    hf->open = false;
}

bool pio_seek(host_file_t *hf, uint32_t offset) {
    if (!hf->open || !hf->fp) return false;
    return fseek((FILE *)hf->fp, (long)offset, SEEK_SET) == 0;
}

uint32_t pio_read(host_file_t *hf, void *buf, uint32_t len) {
    if (!hf->open || !hf->fp) return 0;
    return (uint32_t)fread(buf, 1, len, (FILE *)hf->fp);
}

uint32_t pio_write(host_file_t *hf, const void *buf, uint32_t len) {
    if (!hf->open || !hf->fp || !hf->writable) return 0;
    uint32_t n = (uint32_t)fwrite(buf, 1, len, (FILE *)hf->fp);
    long pos = ftell((FILE *)hf->fp);
    if (pos > 0 && (uint32_t)pos > hf->size) {
        hf->size = (uint32_t)pos;
    }
    return n;
}

bool pio_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool pio_hint_contiguous(host_file_t *hf, uint32_t total_bytes) {
    (void)hf;
    (void)total_bytes;
    return true;
}

#else /* Agon Quark MOS / Console8 MOS */

#include <agon/mos.h>
#include <ez80f92.h>

bool pio_open(host_file_t *hf, const char *path, bool write, bool create) {
    memset(hf, 0, sizeof(*hf));
    uint8_t mode;
    if (create) {
        mode = FA_CREATE_ALWAYS | FA_WRITE | FA_READ;
    } else if (write) {
        mode = FA_OPEN_EXISTING | FA_READ | FA_WRITE;
    } else {
        mode = FA_OPEN_EXISTING | FA_READ;
    }
    uint8_t fh = mos_fopen(path, mode);
    if (fh == 0) {
        return false;
    }
    FIL *fil = mos_getfil(fh);
    hf->fh = fh;
    hf->open = true;
    hf->writable = write || create;
    hf->size = fil ? fil->obj.objsize : 0;
    return true;
}

void pio_close(host_file_t *hf) {
    if (hf->open && hf->fh) {
        mos_fclose(hf->fh);
    }
    hf->fh = 0;
    hf->open = false;
}

bool pio_seek(host_file_t *hf, uint32_t offset) {
    if (!hf->open || !hf->fh) return false;
    return mos_flseek(hf->fh, offset) == FR_OK;
}

uint32_t pio_read(host_file_t *hf, void *buf, uint32_t len) {
    if (!hf->open || !hf->fh) return 0;
    uint32_t total = 0;
    uint8_t *p = (uint8_t *)buf;
    while (total < len) {
        uint24_t chunk = (len - total > 16384u) ? 16384u : (uint24_t)(len - total);
        uint24_t n = mos_fread(hf->fh, (char *)(p + total), chunk);
        total += n;
        if (n < chunk) break;
    }
    return total;
}

uint32_t pio_write(host_file_t *hf, const void *buf, uint32_t len) {
    if (!hf->open || !hf->fh || !hf->writable) return 0;
    uint32_t total = 0;
    const uint8_t *p = (const uint8_t *)buf;
    while (total < len) {
        uint24_t chunk = (len - total > 16384u) ? 16384u : (uint24_t)(len - total);
        uint24_t n = mos_fwrite(hf->fh, (char *)(p + total), chunk);
        total += n;
        if (n < chunk) break;
    }
    FIL *fil = mos_getfil(hf->fh);
    if (fil && fil->obj.objsize > hf->size) {
        hf->size = fil->obj.objsize;
    }
    return total;
}

bool pio_exists(const char *path) {
    uint8_t fh = mos_fopen(path, FA_OPEN_EXISTING | FA_READ);
    if (fh != 0) {
        mos_fclose(fh);
        return true;
    }
    return false;
}

/* --------------------------------------------------------------------------
 * Read-only SPI0 CMD17 block read + FAT32 contiguous-run allocator hint
 *
 * Agon MOS compiles FatFs with FF_USE_EXPAND=0, so create_chain() starts
 * allocating at fs->last_clst + 1 and can fragment new disk images across
 * small holes left by deleted files. Scanning the FAT32 table read-only to
 * locate a contiguous free run >= need_clusters and setting fs->last_clst
 * lets FatFs itself allocate the entire image in one unbroken cluster run.
 * -------------------------------------------------------------------------- */
static uint8_t mos_spi_tx_rx(uint8_t b) {
    IO(SPI_TSR) = b;
    while (!(IO(SPI_SR) & 0x80u)) {
    }
    return IO(SPI_RBR);
}

static void mos_sd_deselect(void) {
    IO(PB_DR) = (uint8_t)(IO(PB_DR) | 0x10u);
    (void)mos_spi_tx_rx(0xFFu);
}

static bool mos_sd_read_512(uint32_t lba, uint8_t buf[512]) {
    uint8_t cmd[6];
    cmd[0] = 0x51u; /* CMD17 READ_SINGLE_BLOCK */
    cmd[1] = (uint8_t)((lba >> 24) & 0xFFu);
    cmd[2] = (uint8_t)((lba >> 16) & 0xFFu);
    cmd[3] = (uint8_t)((lba >> 8) & 0xFFu);
    cmd[4] = (uint8_t)(lba & 0xFFu);
    cmd[5] = 0xFFu;

    IO(PB_DR) = (uint8_t)(IO(PB_DR) & 0xEFu); /* Assert /CS (PB4 low) */
    (void)mos_spi_tx_rx(0xFFu);

    uint8_t r = 0xFFu;
    for (int i = 0; i < 6; i++) {
        r = mos_spi_tx_rx(cmd[i]);
    }

    /* Handle both fab-agon-emulator (immediate R1 on byte 6) and real SD hardware */
    if (r & 0x80u) {
        for (int tries = 0; tries < 255; tries++) {
            r = mos_spi_tx_rx(0xFFu);
            if (!(r & 0x80u)) break;
        }
    }
    if (r != 0x00u) {
        mos_sd_deselect();
        return false;
    }

    /* Wait for Data Start Token (0xFE) */
    bool got_fe = false;
    for (uint16_t tries = 0; tries < 5000u; tries++) {
        if (mos_spi_tx_rx(0xFFu) == 0xFEu) {
            got_fe = true;
            break;
        }
    }
    if (!got_fe) {
        mos_sd_deselect();
        return false;
    }

    for (uint16_t i = 0; i < 512u; i++) {
        buf[i] = mos_spi_tx_rx(0xFFu);
    }
    (void)mos_spi_tx_rx(0xFFu); /* Discard CRC1 */
    (void)mos_spi_tx_rx(0xFFu); /* Discard CRC2 */
    mos_sd_deselect();
    return true;
}

bool pio_hint_contiguous(host_file_t *hf, uint32_t total_bytes) {
    if (!hf || !hf->open || !hf->fh || total_bytes == 0) return false;

    FIL *fil = mos_getfil(hf->fh);
    if (!fil || !fil->obj.fs || fil->obj.sclust != 0u) return false;

    FATFS *fs = (FATFS *)fil->obj.fs;
    if (fs->fs_type != 3u || fs->csize == 0u || fs->fatbase == 0u ||
        fs->fsize == 0u || fs->n_fatent <= 2u) {
        return false;
    }

    uint32_t cbytes = (uint32_t)fs->csize * 512u;
    uint32_t need   = (total_bytes + cbytes - 1u) / cbytes;
    if (need <= 1u) return true;

    uint8_t sec_buf[512];
    uint32_t run_start = 0;
    uint32_t run_len   = 0;

    for (uint32_t s = 0; s < fs->fsize; s++) {
        uint32_t base_cl = s * 128u;
        if (base_cl >= fs->n_fatent) break;

        uint32_t lba = fs->fatbase + s;
        if (fs->winsect == lba) {
            memcpy(sec_buf, fs->win, 512u);
        } else if (!mos_sd_read_512(lba, sec_buf)) {
            return false;
        }

        for (uint16_t i = 0; i < 128u; i++) {
            uint32_t cl = base_cl + i;
            if (cl < 2u) continue;
            if (cl >= fs->n_fatent) break;

            uint32_t ent = (uint32_t)sec_buf[i * 4u + 0u] |
                           ((uint32_t)sec_buf[i * 4u + 1u] << 8) |
                           ((uint32_t)sec_buf[i * 4u + 2u] << 16) |
                           ((uint32_t)sec_buf[i * 4u + 3u] << 24);
            if ((ent & 0x0FFFFFFFu) == 0u) {
                if (run_len == 0u) run_start = cl;
                run_len++;
                if (run_len >= need) {
                    /* Point FatFs allocator right before the contiguous free run */
                    fs->last_clst = run_start - 1u;
                    fs->fsi_flag |= 1u;
                    return true;
                }
            } else {
                run_len = 0u;
            }
        }
    }
    return false;
}

#endif
