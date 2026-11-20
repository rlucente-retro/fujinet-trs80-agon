/*
 * main.c - Multicall CLI Executive for *trs80 (Quark MOS / Console8 MOS)
 *
 * Modeled after Toolshed (os9/decb) with unified comma addressing:
 *   <host_image_path>,<guest_file>
 */

#include "trs80.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Shared 4 KiB I/O scratch buffer (keeps MOSlet memory well inside 32 KiB) */
uint8_t g_io_buf[4096];

int str_casecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

bool str_ends_with_ci(const char *s, const char *suffix) {
    size_t sl = strlen(s);
    size_t fl = strlen(suffix);
    if (sl < fl) return false;
    return str_casecmp(s + sl - fl, suffix) == 0;
}

/*
 * Split Toolshed comma notation: "image.dsk,FILE/CMD" -> img="image.dsk", guest="FILE/CMD".
 * Returns true if a comma was present (indicating virtual disk reference).
 */
bool split_comma_spec(const char *arg, char *img_out, size_t img_sz,
                      char *guest_out, size_t guest_sz) {
    const char *comma = strchr(arg, ',');
    if (!comma) {
        strncpy(img_out, arg, img_sz - 1);
        img_out[img_sz - 1] = '\0';
        if (guest_sz > 0) guest_out[0] = '\0';
        return false;
    }
    size_t ilen = (size_t)(comma - arg);
    if (ilen >= img_sz) ilen = img_sz - 1;
    memcpy(img_out, arg, ilen);
    img_out[ilen] = '\0';

    strncpy(guest_out, comma + 1, guest_sz - 1);
    guest_out[guest_sz - 1] = '\0';
    return true;
}

static int print_help(void) {
    printf("trs80 - Unified TRS-80 / LS-DOS 6.3 Disk Utility for Agon MOS\n");
    printf("Supports: DiskDISK, JV1, JV3, Reed HDV, Flat HDV\n\n");
    printf("Commands:\n");
    printf("  trs80 dir    <img.dsk>[,pat] [-e] [-a] [-s]   List directory\n");
    printf("  trs80 copy   [-l] [-r] <src> <dst>            Copy MOS <-> image\n");
    printf("  trs80 delete <img.dsk>,<file>                 Delete guest file\n");
    printf("  trs80 rename <img.dsk>,<old> <new>            Rename guest file\n");
    printf("  trs80 format <img.dsk> [-f fmt] [-c N] [-s H] [-v vol]\n");
    printf("  trs80 check  <img.dsk>                        Audit GAT/HIT/extents\n");
    printf("  trs80 free   <img.dsk>                        Show disk capacity\n");
    printf("  trs80 dump   <img.dsk>[,file] [-s sec]        Hex/ASCII sector dump\n");
    printf("  trs80 attr   <img.dsk>,<file> [flags]         View/set attributes\n");
    printf("  trs80 map    [scan_dir] [-o mapfile]          Build sdmap.txt LBAs\n");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        return print_help();
    }

    const char *cmd = argv[1];
    int sub_argc = argc - 1;
    char **sub_argv = argv + 1;

    if (str_casecmp(cmd, "dir") == 0 || str_casecmp(cmd, "ls") == 0) {
        return cmd_dir(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "copy") == 0 || str_casecmp(cmd, "cp") == 0) {
        return cmd_copy(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "delete") == 0 || str_casecmp(cmd, "del") == 0 || str_casecmp(cmd, "rm") == 0) {
        return cmd_delete(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "rename") == 0 || str_casecmp(cmd, "ren") == 0 || str_casecmp(cmd, "mv") == 0) {
        return cmd_rename(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "format") == 0 || str_casecmp(cmd, "fmt") == 0) {
        return cmd_format(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "check") == 0 || str_casecmp(cmd, "fsck") == 0) {
        return cmd_check(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "free") == 0 || str_casecmp(cmd, "df") == 0) {
        return cmd_free(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "dump") == 0) {
        return cmd_dump(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "attr") == 0) {
        return cmd_attr(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "map") == 0) {
        return cmd_map(sub_argc, sub_argv);
    }
    if (str_casecmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        return print_help();
    }

    printf("Unknown subcommand: '%s' (run 'trs80 help')\n", cmd);
    return 1;
}
