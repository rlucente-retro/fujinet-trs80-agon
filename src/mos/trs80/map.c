/*
 * map.c - Recursive MicroSD Disk Image Mapper (*trs80 map)
 *
 * Scans a configurable root directory recursively on the Agon FAT32 SD card,
 * resolves the physical 32-bit starting Base LBA and contiguity of every
 * discovered TRS-80 disk image (.dsk, .jv1, .jv3, .hdv), and writes a flat
 * sdmap.txt catalog preserving relative directory hierarchy.
 */

#include "trs80.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_QUEUED_DIRS  32
#define MAX_PATH_LEN     128

typedef struct {
    char dir[MAX_PATH_LEN];
} dir_node_t;

static bool is_disk_image_name(const char *name) {
    if (!name || name[0] == '.') return false;
    return str_ends_with_ci(name, ".dsk") ||
           str_ends_with_ci(name, ".jv1") ||
           str_ends_with_ci(name, ".jv3") ||
           str_ends_with_ci(name, ".hdv");
}

/* Strip leading '/' or "./" so sdmap.txt stores clean relative paths (e.g. disks/base/testdisk1.dsk) */
static const char *normalize_rel_path(const char *full) {
    while (full[0] == '/' || (full[0] == '.' && full[1] == '/')) {
        if (full[0] == '/') full++;
        else full += 2;
    }
    return full;
}

#ifndef HOST_TEST

#include <agon/mos.h>

/* Static FatFS objects keep eZ80 stack usage tiny inside the 32 KiB MOSlet window */
static DIR     g_map_dir;
static FILINFO g_map_fno;

/*
 * Resolve starting physical 32-bit Base LBA and verify 100% FAT32 contiguity
 * of an existing file using standard MOS FatFS calls (mos_fopen/mos_getfil/mos_flseek).
 */
static bool resolve_fat32_lba(const char *path, uint32_t *out_lba,
                              uint32_t *out_size, bool *out_contig) {
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

    /* Verify 100% contiguity across every allocated cluster */
    bool contig = true;
    uint32_t cbytes = csize * 512u;
    uint32_t nclust = (fsize + cbytes - 1u) / cbytes;

    if (nclust > 1u) {
        if ((fil->obj.stat & 0x03u) == 0x02u) {
            /* FatFS already marked this object contiguous */
            contig = true;
        } else {
            for (uint32_t k = 1u; k < nclust; k++) {
                uint32_t ofs = k * cbytes;
                if (ofs + 1u < fsize) ofs += 1u;
                if (mos_flseek(fh, ofs) != FR_OK) {
                    contig = false;
                    break;
                }
                if (fil->clust != sclust + k) {
                    contig = false;
                    break;
                }
            }
        }
    }

    *out_contig = contig;
    mos_fclose(fh);
    return true;
}

int cmd_map(int argc, char **argv) {
    const char *scan_dir = NULL;
    const char *out_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
        } else if (argv[i][0] == '-') {
            printf("Unknown option: %s\n", argv[i]);
            return 1;
        } else if (!scan_dir) {
            scan_dir = argv[i];
        }
    }

    /* Default scan directory: /disks if it exists, otherwise current directory */
    if (!scan_dir) {
        if (ffs_dopen(&g_map_dir, "/disks") == FR_OK) {
            ffs_dclose(&g_map_dir);
            scan_dir = "/disks";
        } else {
            scan_dir = ".";
        }
    }

    char default_out[MAX_PATH_LEN];
    if (!out_file) {
        if (strcmp(scan_dir, "/") == 0) {
            snprintf(default_out, sizeof(default_out), "/sdmap.txt");
        } else if (strcmp(scan_dir, ".") == 0) {
            snprintf(default_out, sizeof(default_out), "sdmap.txt");
        } else {
            snprintf(default_out, sizeof(default_out), "%s/sdmap.txt", scan_dir);
        }
        out_file = default_out;
    }

    /* Allocate BFS directory queue (avoid deep stack frames) */
    dir_node_t *queue = (dir_node_t *)malloc(sizeof(dir_node_t) * MAX_QUEUED_DIRS);
    if (!queue) {
        printf("Error: Out of memory\n");
        return 1;
    }

    host_file_t out;
    if (!pio_open(&out, out_file, true, true)) {
        printf("Error: Cannot create map file '%s'\n", out_file);
        free(queue);
        return 1;
    }

    printf("Scanning '%s' -> '%s'...\n", scan_dir, out_file);

    int qhead = 0, qtail = 0;
    strncpy(queue[qtail].dir, scan_dir, MAX_PATH_LEN - 1);
    queue[qtail].dir[MAX_PATH_LEN - 1] = '\0';
    qtail++;

    uint16_t mapped = 0;
    uint16_t fragmented = 0;
    char full_path[MAX_PATH_LEN];
    char line[160];

    while (qhead < qtail) {
        const char *cur_dir = queue[qhead++].dir;
        if (ffs_dopen(&g_map_dir, cur_dir) != FR_OK) {
            continue;
        }

        while (ffs_dread(&g_map_dir, &g_map_fno) == FR_OK && g_map_fno.fname[0] != '\0') {
            if (g_map_fno.fname[0] == '.') continue;

            if (strcmp(cur_dir, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", g_map_fno.fname);
            } else if (strcmp(cur_dir, ".") == 0) {
                snprintf(full_path, sizeof(full_path), "%s", g_map_fno.fname);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", cur_dir, g_map_fno.fname);
            }

            if (g_map_fno.fattrib & AM_DIR) {
                if (qtail < MAX_QUEUED_DIRS) {
                    strncpy(queue[qtail].dir, full_path, MAX_PATH_LEN - 1);
                    queue[qtail].dir[MAX_PATH_LEN - 1] = '\0';
                    qtail++;
                }
            } else if (is_disk_image_name(g_map_fno.fname)) {
                uint32_t lba = 0, fsize = 0;
                bool contig = false;
                if (resolve_fat32_lba(full_path, &lba, &fsize, &contig)) {
                    const char *rel = normalize_rel_path(full_path);
                    int n = snprintf(line, sizeof(line), "%s,0x%08lX\n", rel, (unsigned long)lba);
                    if (n > 0) pio_write(&out, line, (uint32_t)n);
                    printf("  0x%08lX  %-36s %s\n",
                           (unsigned long)lba, rel, contig ? "[OK]" : "[FRAGMENTED!]");
                    mapped++;
                    if (!contig) fragmented++;
                } else {
                    printf("  [SKIP] Cannot resolve LBA for %s\n", full_path);
                }
            }
        }
        ffs_dclose(&g_map_dir);
    }

    pio_close(&out);
    free(queue);

    /* Resolve Base LBA & contiguity of the generated sdmap.txt itself */
    uint32_t map_lba = 0, map_len = 0;
    bool map_contig = false;
    printf("Mapped %u disk image(s)", mapped);
    if (fragmented > 0) printf(" (%u FRAGMENTED warning)", fragmented);
    printf(".\n");

    if (resolve_fat32_lba(out_file, &map_lba, &map_len, &map_contig)) {
        printf("Map file: %s\n", out_file);
        printf("  Base LBA:   0x%08lX\n", (unsigned long)map_lba);
        printf("  Length:     %lu bytes (%s)\n",
               (unsigned long)map_len, map_contig ? "Contiguous" : "FRAGMENTED");
        printf("  LS-DOS test: SETMAP 0x%lX %lu\n",
               (unsigned long)map_lba, (unsigned long)map_len);
    }

    return 0;
}

#else /* HOST_TEST: POSIX directory traversal for host unit tests */

#include <dirent.h>
#include <sys/stat.h>

int cmd_map(int argc, char **argv) {
    const char *scan_dir = ".";
    const char *out_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
        } else if (argv[i][0] != '-') {
            scan_dir = argv[i];
        }
    }

    char default_out[MAX_PATH_LEN];
    if (!out_file) {
        snprintf(default_out, sizeof(default_out), "%s/sdmap.txt", scan_dir);
        out_file = default_out;
    }

    dir_node_t queue[MAX_QUEUED_DIRS];
    int qhead = 0, qtail = 0;
    strncpy(queue[qtail++].dir, scan_dir, MAX_PATH_LEN - 1);

    host_file_t out;
    if (!pio_open(&out, out_file, true, true)) {
        printf("Error: Cannot create '%s'\n", out_file);
        return 1;
    }

    uint16_t mapped = 0;
    uint32_t fake_lba = 0x00001000u;
    while (qhead < qtail) {
        const char *cur = queue[qhead++].dir;
        DIR *dp = opendir(cur);
        if (!dp) continue;
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.') continue;
            char full[MAX_PATH_LEN];
            snprintf(full, sizeof(full), "%s/%s", cur, de->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                if (qtail < MAX_QUEUED_DIRS) {
                    strncpy(queue[qtail++].dir, full, MAX_PATH_LEN - 1);
                }
            } else if (is_disk_image_name(de->d_name)) {
                const char *rel = normalize_rel_path(full);
                char line[160];
                int n = snprintf(line, sizeof(line), "%s,0x%08lX\n", rel, (unsigned long)fake_lba);
                if (n > 0) pio_write(&out, line, (uint32_t)n);
                fake_lba += 0x400u;
                mapped++;
            }
        }
        closedir(dp);
    }

    pio_close(&out);
    printf("Host map complete: %u disk image(s) -> %s\n", mapped, out_file);
    return 0;
}

#endif
