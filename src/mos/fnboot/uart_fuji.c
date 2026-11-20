/*
 * uart_fuji.c - Direct Polled eZ80 UART1 FujiNet OP_FUJI Client for Quark MOS
 *
 * Uses direct hardware UART1 registers (0xD0..0xD5) so the active ESP8266
 * openstream TCP bridge is preserved untouched across MOS -> TRSDOS handover.
 */

#include "fnboot.h"
#include <string.h>

#ifndef HOST_TEST

#include <agon/mos.h>
#include <ez80f92.h>

void fuji_mos_init_uart1(void) {
    /* Port C Mode 7 for PC0 (TXD1) and PC1 (RXD1) */
    IO(PC_DDR)  = (uint8_t)(IO(PC_DDR)  | 0x03u);
    IO(PC_ALT1) = (uint8_t)(IO(PC_ALT1) & 0xFCu);
    IO(PC_ALT2) = (uint8_t)(IO(PC_ALT2) | 0x03u);

    /* 115,200 baud @ 18.432 MHz (divisor = 10), 8-N-1, polled mode */
    IO(UART1_LCTL)  = 0x80u;
    IO(UART1_BRG_L) = 10u;
    IO(UART1_BRG_H) = 0u;
    IO(UART1_LCTL)  = 0x03u;
    IO(UART1_IER)   = 0x00u;
    IO(UART1_MCTL)  = 0x00u;
    IO(UART1_FCTL)  = 0x07u;
}

static void uart1_flush_rx(void) {
    IO(UART1_FCTL) = 0x07u;
    for (int i = 0; i < 32; i++) {
        if (!(IO(UART1_LSR) & UART1_LSR_DR)) break;
        (void)IO(UART1_RBR);
    }
}

static bool uart1_tx_byte(uint8_t b) {
    uint24_t tries = 60000u;
    while (tries--) {
        if (IO(UART1_LSR) & UART1_LSR_THRE) {
            IO(UART1_THR) = b;
            return true;
        }
    }
    return false;
}

static bool uart1_rx_byte(uint8_t *out, uint24_t timeout_ms) {
    uint32_t start = getsysvar_time();
    uint32_t limit = (timeout_ms + 9u) / 10u; /* centiseconds */
    if (limit == 0) limit = 1;
    for (;;) {
        if (IO(UART1_LSR) & UART1_LSR_DR) {
            *out = IO(UART1_RBR);
            return true;
        }
        if ((uint32_t)(getsysvar_time() - start) >= limit) {
            return false;
        }
    }
}

static bool uart1_tx_buf(const uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        if (!uart1_tx_byte(buf[i])) return false;
    }
    return true;
}

static bool uart1_rx_buf(uint8_t *buf, uint16_t len, uint24_t timeout_ms) {
    for (uint16_t i = 0; i < len; i++) {
        if (!uart1_rx_byte(&buf[i], timeout_ms)) return false;
    }
    return true;
}

static bool fuji_get_error(uint8_t *err_out) {
    uint8_t req[2] = { OP_FUJI, FUJI_CMD_GET_ERROR };
    if (!uart1_tx_buf(req, 2)) return false;
    return uart1_rx_byte(err_out, 1500);
}

static bool fuji_get_response(uint8_t *buf, uint16_t len) {
    uint8_t req[2] = { OP_FUJI, FUJI_CMD_GET_RESPONSE };
    if (!uart1_tx_buf(req, 2)) return false;
    return uart1_rx_buf(buf, len, 1500);
}

static bool fuji_cmd_send(uint8_t cmd, const uint8_t *params, uint16_t plen) {
    uint8_t hdr[2] = { OP_FUJI, cmd };
    if (!uart1_tx_buf(hdr, 2)) return false;
    if (plen > 0 && !uart1_tx_buf(params, plen)) return false;
    uint8_t err = 0;
    if (!fuji_get_error(&err)) return false;
    return (err == FUJI_STAT_OK);
}

bool fuji_mos_probe(fuji_state_t *fs) {
    memset(fs, 0, sizeof(*fs));
    fuji_mos_init_uart1();
    uart1_flush_rx();

    /* Fast probe: 2 quick polls (~200 ms max) so offline local-SD boot is instant */
    uint8_t req[2] = { OP_FUJI, FUJI_CMD_POLL };
    uint8_t st = 0;
    bool alive = false;
    for (int i = 0; i < 2; i++) {
        if (uart1_tx_buf(req, 2) && uart1_rx_byte(&st, 150) && st == FUJI_STAT_OK) {
            alive = true;
            break;
        }
    }
    if (!alive) {
        fs->online = false;
        return false;
    }
    fs->online = true;

    /* 1. Adapter Config (140 bytes) */
    uint8_t cfg[140];
    if (fuji_cmd_send(FUJI_CMD_GET_ADAPTER, NULL, 0) && fuji_get_response(cfg, 140)) {
        memcpy(fs->ssid, &cfg[0], 33);       fs->ssid[33] = '\0';
        memcpy(fs->hostname, &cfg[33], 64);  fs->hostname[64] = '\0';
        memcpy(fs->ip, &cfg[97], 4);
        memcpy(fs->firmware, &cfg[125], 15); fs->firmware[15] = '\0';
    }

    /* 2. Host Slots (8 x 32 = 256 bytes) */
    uint8_t htab[256];
    if (fuji_cmd_send(FUJI_CMD_GET_HOST_SLOTS, NULL, 0) && fuji_get_response(htab, 256)) {
        for (int i = 0; i < 8; i++) {
            memcpy(fs->hosts[i], &htab[i * 32], 32);
            fs->hosts[i][32] = '\0';
        }
    }

    /* 3. Device Slots (Stage 1: 4 x 38 = 152 bytes) */
    uint8_t dtab[152];
    if (fuji_cmd_send(FUJI_CMD_GET_DEV_SLOTS, NULL, 0) && fuji_get_response(dtab, 152)) {
        for (int i = 0; i < 4; i++) {
            fs->devs[i].host_slot = dtab[i * 38 + 0];
            fs->devs[i].mode      = dtab[i * 38 + 1];
            memcpy(fs->devs[i].file, &dtab[i * 38 + 2], 36);
            fs->devs[i].file[36] = '\0';
        }
    }

    return true;
}

bool fuji_mos_mount_host(uint8_t slot) {
    return fuji_cmd_send(FUJI_CMD_MOUNT_HOST, &slot, 1);
}

bool fuji_mos_open_dir(uint8_t host_slot, const char *path) {
    uint8_t buf[257];
    memset(buf, 0, sizeof(buf));
    buf[0] = host_slot;
    strncpy((char *)&buf[1], path ? path : "/", 255);
    return fuji_cmd_send(FUJI_CMD_OPEN_DIR, buf, 257);
}

bool fuji_mos_read_dir_entry(uint8_t max_len, char *out_name) {
    uint8_t p[2] = { max_len, 0 };
    if (!fuji_cmd_send(FUJI_CMD_READ_DIR_ENT, p, 2)) return false;
    if (!fuji_get_response((uint8_t *)out_name, max_len)) return false;
    out_name[max_len - 1] = '\0';
    return ((uint8_t)out_name[0] != 0x7F && out_name[0] != '\0');
}

void fuji_mos_close_dir(void) {
    (void)fuji_cmd_send(FUJI_CMD_CLOSE_DIR, NULL, 0);
}

bool fuji_mos_set_dev_file(uint8_t dev_slot, uint8_t host_slot, uint8_t mode, const char *path) {
    uint8_t pkt[259];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = dev_slot;
    pkt[1] = host_slot;
    pkt[2] = mode;
    strncpy((char *)&pkt[3], path, 255);
    return fuji_cmd_send(FUJI_CMD_SET_DEV_FILE, pkt, 259);
}

bool fuji_mos_mount_image(uint8_t dev_slot, uint8_t mode) {
    uint8_t p[2] = { dev_slot, mode };
    return fuji_cmd_send(FUJI_CMD_MOUNT_IMG, p, 2);
}

#else /* HOST_TEST stubs */

void fuji_mos_init_uart1(void) {}
bool fuji_mos_probe(fuji_state_t *fs) {
    memset(fs, 0, sizeof(*fs));
    fs->online = false;
    return false;
}
bool fuji_mos_mount_host(uint8_t slot) { (void)slot; return false; }
bool fuji_mos_open_dir(uint8_t host_slot, const char *path) { (void)host_slot; (void)path; return false; }
bool fuji_mos_read_dir_entry(uint8_t max_len, char *out_name) { (void)max_len; (void)out_name; return false; }
void fuji_mos_close_dir(void) {}
bool fuji_mos_set_dev_file(uint8_t d, uint8_t h, uint8_t m, const char *p) { (void)d; (void)h; (void)m; (void)p; return false; }
bool fuji_mos_mount_image(uint8_t d, uint8_t m) { (void)d; (void)m; return false; }

#endif
