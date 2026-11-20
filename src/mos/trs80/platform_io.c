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

#else /* Agon Quark MOS / Console8 MOS */

#include <agon/mos.h>

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

#endif
