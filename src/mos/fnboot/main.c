/*
 * main.c - FNBOOT Pre-Boot Visual Storage Manager & Launcher (Quark MOS)
 *
 * Interactive visual launcher for TRS-80 / LS-DOS 6.3 on Olimex Agon Light 2:
 *   - Probes FujiNet adapter status, host slots, and device slots over UART1
 *   - Auto-discovers sdmap.txt (CLI -> fnboot.ini -> ./ -> /disks/ -> /trs80/ -> /)
 *   - Assigns logical drives :0..:7 to DriveWire or Local MicroSD images
 *   - Browses FujiNet TNFS directories & local SD disk catalogs
 *   - Stages lowcore SRAM mailbox (sddvr_map_lba, sddvr_base_lba, drv_target_table, DCTs)
 *   - Boots TRSDOS.bin via OSboot with configured TRS-80 font & color
 */

#include "fnboot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef HOST_TEST
#include <agon/mos.h>
#endif

static fnboot_ctx_t g_ctx;

static const char *color_name(uint8_t c) {
    switch (c) {
        case 1: return "Red";
        case 2: return "Green";
        case 3: return "Yellow";
        case 4: return "Blue";
        case 5: return "Magenta";
        case 6: return "Cyan";
        case 7: return "White";
        default: return "Green";
    }
}

/* Agon VDP screen primitives (VDU 12 = CLS, VDU 17 = Text color) */
static void ui_cls(void) {
#ifndef HOST_TEST
    putch(12);
#else
    printf("\n------------------------------------------------------------------------\n");
#endif
}

static void ui_color(uint8_t fg) {
#ifndef HOST_TEST
    putch(17);
    putch(fg);
#else
    (void)fg;
#endif
}

static char ui_getch(void) {
#ifndef HOST_TEST
    return getch();
#else
    int c = getchar();
    return (c == EOF) ? 'q' : (char)c;
#endif
}

static void ui_readline(const char *prompt, char *buf, size_t max_len) {
    printf("%s", prompt);
#ifndef HOST_TEST
    fflush(stdout);
    buf[0] = '\0';
    mos_editline(buf, (uint24_t)max_len, 1);
    printf("\n");
#else
    if (!fgets(buf, (int)max_len, stdin)) buf[0] = '\0';
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
#endif
}

static void init_defaults(fnboot_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    strcpy(ctx->kernel_path, "/TRSDOS.bin");
    ctx->term_color = 2; /* Classic TRS-80 green */
    ctx->use_font1  = true;
    ctx->use_vt100  = false;

    /* Default Drive :0 = FujiNet DriveWire Slot 0; :1..:7 = Offline */
    ctx->drives[0].target  = DRV_TARGET_DW;
    ctx->drives[0].mode    = 1;
    ctx->drives[0].dw_slot = 0;
    strcpy(ctx->drives[0].label, "FujiNet Slot 0");

    for (int i = 1; i < MAX_DRIVES; i++) {
        ctx->drives[i].target = DRV_TARGET_OFFLINE;
        ctx->drives[i].mode   = 3;
    }
}

static void sync_fuji_labels(fnboot_ctx_t *ctx) {
    if (!ctx->fuji.online) return;
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (ctx->drives[i].target == DRV_TARGET_DW && ctx->drives[i].dw_slot < 4) {
            const char *f = ctx->fuji.devs[ctx->drives[i].dw_slot].file;
            if (f[0]) {
                snprintf(ctx->drives[i].label, sizeof(ctx->drives[i].label),
                         "Slot %u: %s", ctx->drives[i].dw_slot, f);
            }
        }
    }
}

static void render_main_screen(const fnboot_ctx_t *ctx, const char *status_msg) {
    ui_cls();

    ui_color(11); /* Bright yellow header */
    printf("===============================================================================\n");
    printf(" FNBOOT - FujiNet & MicroSD Pre-Boot Manager  [TRS-80 / LS-DOS 6.3 on Agon]    \n");
    printf("===============================================================================\n");

    ui_color(15); /* White */
    if (ctx->fuji.online) {
        printf(" FujiNet : ");
        ui_color(10);
        printf("ONLINE");
        ui_color(15);
        printf("  IP: %u.%u.%u.%u  Host: %.20s  FW: %.14s\n",
               ctx->fuji.ip[0], ctx->fuji.ip[1], ctx->fuji.ip[2], ctx->fuji.ip[3],
               ctx->fuji.hostname[0] ? ctx->fuji.hostname : "(none)",
               ctx->fuji.firmware[0] ? ctx->fuji.firmware : "?");
    } else {
        printf(" FujiNet : ");
        ui_color(9);
        printf("OFFLINE");
        ui_color(15);
        printf(" (Local MicroSD Boot Mode Available)\n");
    }

    if (ctx->map_found) {
        printf(" SD Map  : %-28s LBA 0x%08lX (%uB, %d imgs%s)\n",
               ctx->map_path, (unsigned long)ctx->map_lba, ctx->map_len, ctx->sd_count,
               ctx->map_contig ? "" : ", FRAG!");
    } else {
        printf(" SD Map  : (none found - press R to scan /disks or M to set path)\n");
    }

    printf(" Kernel  : %-28s Display: %s, Font1=%s, %s\n",
           ctx->kernel_path, color_name(ctx->term_color),
           ctx->use_font1 ? "ON" : "OFF",
           ctx->use_vt100 ? "VT100" : "ADDS25");

    ui_color(14); /* Cyan table header */
    printf("-------------------------------------------------------------------------------\n");
    printf(" Drv  Target     Mode  Assigned Image / Slot\n");
    printf(" ---  ---------  ----  -------------------------------------------------------\n");

    for (int i = 0; i < MAX_DRIVES; i++) {
        const drive_cfg_t *dc = &ctx->drives[i];
        ui_color(15);
        printf("  :%d  ", i);
        if (dc->target == DRV_TARGET_DW) {
            ui_color(14);
            printf("DriveWire  ");
            ui_color(15);
            printf("%-4s  %.54s\n", (dc->mode == 1) ? "RO" : "RW", dc->label);
        } else if (dc->target == DRV_TARGET_SD) {
            ui_color(10);
            printf("Local SD   ");
            ui_color(15);
            printf("%-4s  %.40s (0x%lX)\n",
                   (dc->mode == 1) ? "RO" : "RW", dc->label, (unsigned long)dc->sd_lba);
        } else {
            ui_color(8);
            printf("Offline    --    (unassigned)\n");
        }
    }

    ui_color(14);
    printf("-------------------------------------------------------------------------------\n");
    ui_color(11);
    printf(" [0-7] Configure Drive   [F] FujiNet TNFS Browser   [R] Rescan SD Map\n");
    printf(" [M]   Select Map File   [C] Cycle Color (%-7s)  [S] Save /fnboot.ini\n",
           color_name(ctx->term_color));
    printf(" [B]   Boot TRSDOS       [Q] Quit to MOS\n");
    ui_color(15);

    if (status_msg && status_msg[0]) {
        printf("\n > %s\n", status_msg);
    }
    printf("\nSelect command: ");
    fflush(stdout);
}

static void configure_drive(fnboot_ctx_t *ctx, int drv, char *status, size_t status_sz) {
    ui_cls();
    ui_color(11);
    printf("=== Configure Logical Drive :%d ===\n\n", drv);
    ui_color(15);

    printf("  [S] Assign Local MicroSD Disk Image (from %s)\n",
           ctx->map_found ? ctx->map_path : "no map");
    printf("  [D] Assign FujiNet DriveWire Slot (0-3)\n");
    printf("  [T] Toggle Access Mode (RO / RW)\n");
    if (drv > 0) {
        printf("  [O] Set Drive :%d Offline\n", drv);
    }
    printf("  [Esc/Q] Cancel\n\nChoice: ");
    fflush(stdout);

    char ch = (char)toupper((unsigned char)ui_getch());
    printf("%c\n", ch);

    if (ch == 'O' && drv > 0) {
        ctx->drives[drv].target = DRV_TARGET_OFFLINE;
        ctx->drives[drv].sd_lba = 0;
        ctx->drives[drv].label[0] = '\0';
        snprintf(status, status_sz, "Drive :%d set Offline.", drv);
        return;
    }

    if (ch == 'T') {
        ctx->drives[drv].mode = (ctx->drives[drv].mode == 1) ? 3 : 1;
        snprintf(status, status_sz, "Drive :%d mode set to %s.",
                 drv, (ctx->drives[drv].mode == 1) ? "RO" : "RW");
        return;
    }

    if (ch == 'D') {
        char num[8];
        ui_readline("FujiNet Device Slot (0-3): ", num, sizeof(num));
        if (num[0] >= '0' && num[0] <= '3') {
            uint8_t slot = (uint8_t)(num[0] - '0');
            ctx->drives[drv].target  = DRV_TARGET_DW;
            ctx->drives[drv].dw_slot = slot;
            ctx->drives[drv].mode    = 1; /* RO default for Stage 1 */
            snprintf(ctx->drives[drv].label, sizeof(ctx->drives[drv].label),
                     "FujiNet Slot %u", slot);
            sync_fuji_labels(ctx);
            snprintf(status, status_sz, "Drive :%d -> DriveWire Slot %u.", drv, slot);
        }
        return;
    }

    if (ch == 'S') {
        if (!ctx->map_found || ctx->sd_count == 0) {
            snprintf(status, status_sz, "No SD images loaded. Press R to scan /disks first.");
            return;
        }
        printf("\nAvailable Local MicroSD Images:\n");
        for (int i = 0; i < ctx->sd_count; i++) {
            printf("  %2d) %-44s (0x%08lX)\n",
                   i + 1, ctx->sd_images[i].path, (unsigned long)ctx->sd_images[i].base_lba);
        }
        char sel[16];
        ui_readline("\nSelect image # (blank to cancel): ", sel, sizeof(sel));
        int idx = atoi(sel);
        if (idx >= 1 && idx <= ctx->sd_count) {
            const sd_image_t *im = &ctx->sd_images[idx - 1];
            ctx->drives[drv].target = DRV_TARGET_SD;
            ctx->drives[drv].sd_lba = im->base_lba;
            ctx->drives[drv].mode   = (drv == 0) ? 1 : 3;
            strncpy(ctx->drives[drv].label, im->path, MAX_PATH_LEN - 1);
            ctx->drives[drv].label[MAX_PATH_LEN - 1] = '\0';
            snprintf(status, status_sz, "Drive :%d -> Local SD '%s' (0x%08lX).",
                     drv, im->path, (unsigned long)im->base_lba);
        }
    }
}

static void browse_fujinet(fnboot_ctx_t *ctx, char *status, size_t status_sz) {
    ui_cls();
    ui_color(11);
    printf("=== FujiNet Host & TNFS Browser ===\n\n");
    ui_color(15);

    if (!ctx->fuji.online) {
        printf("Re-probing FujiNet on UART1...\n");
        if (!fuji_mos_probe(&ctx->fuji)) {
            snprintf(status, status_sz, "FujiNet offline (check openstream or serial link).");
            return;
        }
    }

    printf("Configured FujiNet Host Slots:\n");
    for (int i = 0; i < 8; i++) {
        printf("  %d) %s\n", i, ctx->fuji.hosts[i][0] ? ctx->fuji.hosts[i] : "(empty)");
    }

    char hbuf[8];
    ui_readline("\nSelect Host Slot (0-7, blank to cancel): ", hbuf, sizeof(hbuf));
    if (hbuf[0] < '0' || hbuf[0] > '7') return;
    uint8_t hslot = (uint8_t)(hbuf[0] - '0');

    printf("Mounting Host Slot %u...\n", hslot);
    if (!fuji_mos_mount_host(hslot)) {
        snprintf(status, status_sz, "Failed to mount FujiNet Host Slot %u.", hslot);
        return;
    }

    char dir_path[MAX_PATH_LEN];
    strcpy(dir_path, "/");
    if (!fuji_mos_open_dir(hslot, dir_path)) {
        snprintf(status, status_sz, "Failed to open directory '/' on Host Slot %u.", hslot);
        return;
    }

    char entries[20][40];
    int count = 0;
    printf("\nFiles on Host %u (%s):\n", hslot, ctx->fuji.hosts[hslot]);
    while (count < 20 && fuji_mos_read_dir_entry(38, entries[count])) {
        printf("  %2d) %s\n", count + 1, entries[count]);
        count++;
    }
    fuji_mos_close_dir();

    if (count == 0) {
        snprintf(status, status_sz, "No files returned on Host Slot %u.", hslot);
        return;
    }

    char sel[8], dsel[8];
    ui_readline("\nSelect file # to mount (blank to cancel): ", sel, sizeof(sel));
    int idx = atoi(sel);
    if (idx < 1 || idx > count) return;

    ui_readline("Mount into FujiNet Device Slot (0-3): ", dsel, sizeof(dsel));
    if (dsel[0] < '0' || dsel[0] > '3') return;
    uint8_t dslot = (uint8_t)(dsel[0] - '0');

    char remote[MAX_PATH_LEN];
    if (entries[idx - 1][0] == '/') {
        snprintf(remote, sizeof(remote), "%s", entries[idx - 1]);
    } else {
        snprintf(remote, sizeof(remote), "/%s", entries[idx - 1]);
    }

    if (fuji_mos_set_dev_file(dslot, hslot, 1, remote) && fuji_mos_mount_image(dslot, 1)) {
        fuji_mos_probe(&ctx->fuji);
        sync_fuji_labels(ctx);
        snprintf(status, status_sz, "Mounted '%s' in FujiNet Slot %u (RO).", remote, dslot);
    } else {
        snprintf(status, status_sz, "Failed mounting '%s' in FujiNet Slot %u.", remote, dslot);
    }
}

static void print_usage(void) {
    printf("FNBOOT - FujiNet & MicroSD Pre-Boot Manager for TRS-80 on Agon Light 2\n");
    printf("Usage: fnboot [-m <mapfile>] [-k <kernel>] [-c <1-7>] [-v] [-b]\n");
    printf("  -m, --map <path>    Override SD mapping file (default probe: ./, /disks/, /trs80/, /)\n");
    printf("  -k <path>           Kernel binary path (default: /TRSDOS.bin)\n");
    printf("  -c <1-7>            TRS-80 screen color (default: 2 = Green)\n");
    printf("  -v                  Use VT100 terminal mode instead of ADDS25\n");
    printf("  -b, --boot          Non-interactive quick boot using discovered map & fnboot.ini\n");
}

int main(int argc, char *argv[]) {
    init_defaults(&g_ctx);

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--map") == 0) && i + 1 < argc) {
            strncpy(g_ctx.cli_map_override, argv[++i], MAX_PATH_LEN - 1);
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            strncpy(g_ctx.kernel_path, argv[++i], MAX_PATH_LEN - 1);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            int c = atoi(argv[++i]);
            if (c >= 1 && c <= 7) g_ctx.term_color = (uint8_t)c;
        } else if (strcmp(argv[i], "-v") == 0) {
            g_ctx.use_vt100 = true;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--boot") == 0) {
            g_ctx.quick_boot = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
    }

    /* 1. Load optional configuration file (fnboot.ini or /fnboot.cfg) */
    bool ini_loaded = false;
    if (fnboot_load_ini(&g_ctx, "/fnboot.ini")) {
        ini_loaded = true;
    } else if (fnboot_load_ini(&g_ctx, "fnboot.ini")) {
        ini_loaded = true;
    } else if (fnboot_load_ini(&g_ctx, "/fnboot.cfg")) {
        ini_loaded = true;
    } else {
        fnboot_discover_map(&g_ctx);
    }

    /* 2. Probe FujiNet over UART1 (fast non-blocking timeout if offline) */
    fuji_mos_probe(&g_ctx.fuji);
    sync_fuji_labels(&g_ctx);

    /* If no explicit INI and FujiNet is offline, default Drive :0 to first SD image */
    if (!ini_loaded && !g_ctx.fuji.online && g_ctx.drives[0].target == DRV_TARGET_DW && g_ctx.sd_count > 0) {
        g_ctx.drives[0].target = DRV_TARGET_SD;
        g_ctx.drives[0].sd_lba = g_ctx.sd_images[0].base_lba;
        g_ctx.drives[0].mode   = 1;
        strncpy(g_ctx.drives[0].label, g_ctx.sd_images[0].path, MAX_PATH_LEN - 1);
        g_ctx.drives[0].label[MAX_PATH_LEN - 1] = '\0';
    }

    /* 3. Non-interactive quick boot (-b) */
    if (g_ctx.quick_boot) {
        return fnboot_launch(&g_ctx);
    }

    /* 4. Interactive visual pre-boot menu loop */
    char status[96] = "Ready. Press [B] to boot TRSDOS or [0-7] to assign drives.";
    for (;;) {
        render_main_screen(&g_ctx, status);
        char key = (char)toupper((unsigned char)ui_getch());

        if (key >= '0' && key <= '7') {
            configure_drive(&g_ctx, key - '0', status, sizeof(status));
        } else if (key == 'F') {
            browse_fujinet(&g_ctx, status, sizeof(status));
        } else if (key == 'M') {
            char mpath[MAX_PATH_LEN];
            printf("\n");
            ui_readline("Enter SD map file path: ", mpath, sizeof(mpath));
            if (mpath[0]) {
                if (fnboot_load_map_file(&g_ctx, mpath)) {
                    snprintf(status, sizeof(status), "Loaded '%s' (%d images, LBA 0x%08lX).",
                             mpath, g_ctx.sd_count, (unsigned long)g_ctx.map_lba);
                } else {
                    snprintf(status, sizeof(status), "Failed to open map file '%s'.", mpath);
                }
            }
        } else if (key == 'R') {
            char sdir[MAX_PATH_LEN];
            printf("\n");
            ui_readline("Directory to scan (blank = /disks): ", sdir, sizeof(sdir));
            if (fnboot_rescan_sd(&g_ctx, sdir[0] ? sdir : "/disks", NULL)) {
                snprintf(status, sizeof(status), "Rescanned '%s' -> '%s' (%d images).",
                         sdir[0] ? sdir : "/disks", g_ctx.map_path, g_ctx.sd_count);
            } else {
                snprintf(status, sizeof(status), "Rescan failed for '%s'.",
                         sdir[0] ? sdir : "/disks");
            }
        } else if (key == 'C') {
            g_ctx.term_color = (uint8_t)((g_ctx.term_color % 7u) + 1u);
            snprintf(status, sizeof(status), "Terminal color set to %u (%s).",
                     g_ctx.term_color, color_name(g_ctx.term_color));
        } else if (key == 'S') {
            if (fnboot_save_ini(&g_ctx, "/fnboot.ini")) {
                snprintf(status, sizeof(status), "Saved configuration to /fnboot.ini.");
            } else {
                snprintf(status, sizeof(status), "Failed writing /fnboot.ini.");
            }
        } else if (key == 'B' || key == '\r' || key == '\n') {
            printf("\n");
            return fnboot_launch(&g_ctx);
        } else if (key == 'Q' || key == 27) {
            ui_cls();
            printf("Exited FNBOOT to MOS.\n");
            return 0;
        }
    }
}
