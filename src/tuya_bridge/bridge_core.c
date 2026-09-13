#include "bridge_core.h"
#include <string.h>

#define HEARTBEAT_PERIOD_TICKS   150   // 15s em ticks de 100ms
#define HANDSHAKE_RETRY_TICKS    10    // 1s

static bridge_tx_t    g_tx = 0;
static bridge_dp_cb_t g_on_dp = 0;
static bridge_state_t g_state = BR_ST_BOOT;
static uint16_t g_tick = 0;
static uint16_t g_last_hb_tick = 0;
static char g_product[96];

static void send_cmd(uint8_t cmd, const uint8_t *data, uint16_t len) {
    uint8_t buf[TUYA_MAX_FRAME + 8];
    uint16_t n = tuya_serial_build(buf, 0x00, cmd, data, len);
    if (g_tx) g_tx(buf, n);
}

static void on_frame(const tuya_frame_t *f) {
    switch (f->command) {
    case TUYA_CMD_HEARTBEAT:
        if (g_state == BR_ST_WAIT_HEARTBEAT) {
            g_state = BR_ST_WAIT_PRODUCT;
            send_cmd(TUYA_CMD_PRODUCT_INFO, 0, 0);
        }
        break;
    case TUYA_CMD_PRODUCT_INFO: {
        uint16_t n = f->data_len < sizeof(g_product) - 1
                   ? f->data_len : sizeof(g_product) - 1;
        memcpy(g_product, f->data, n);
        g_product[n] = 0;
        if (g_state == BR_ST_WAIT_PRODUCT) {
            g_state = BR_ST_WAIT_CONF;
            send_cmd(TUYA_CMD_MCU_CONF, 0, 0);
        }
        break;
    }
    case TUYA_CMD_MCU_CONF: {
        if (g_state == BR_ST_WAIT_CONF) {
            // Informa "conectado na nuvem" (estado 4) - MCU libera operacao
            uint8_t st = 0x04;
            send_cmd(TUYA_CMD_WIFI_STATE, &st, 1);
            g_state = BR_ST_OPERATIONAL;
            send_cmd(TUYA_CMD_QUERY_DPS, 0, 0);
        }
        break;
    }
    case TUYA_CMD_DP_REPORT: {
        uint16_t off = 0;
        tuya_dp_t dp;
        while (tuya_dp_next(f, &off, &dp)) {
            if (g_on_dp) g_on_dp(&dp);
        }
        break;
    }
    default:
        break;
    }
}

void bridge_init(bridge_tx_t tx, bridge_dp_cb_t on_dp) {
    g_tx = tx;
    g_on_dp = on_dp;
    g_state = BR_ST_BOOT;
    g_tick = 0;
    g_last_hb_tick = 0;
    g_product[0] = 0;
    tuya_serial_init(on_frame);
}

void bridge_rx(const uint8_t *bytes, uint16_t len) {
    tuya_serial_feed(bytes, len);
}

void bridge_tick_100ms(void) {
    g_tick++;
    switch (g_state) {
    case BR_ST_BOOT:
        g_state = BR_ST_WAIT_HEARTBEAT;
        send_cmd(TUYA_CMD_HEARTBEAT, 0, 0);
        g_last_hb_tick = g_tick;
        break;
    case BR_ST_WAIT_HEARTBEAT:
    case BR_ST_WAIT_PRODUCT:
    case BR_ST_WAIT_CONF:
        if (g_tick - g_last_hb_tick >= HANDSHAKE_RETRY_TICKS) {
            g_state = BR_ST_WAIT_HEARTBEAT;
            send_cmd(TUYA_CMD_HEARTBEAT, 0, 0);
            g_last_hb_tick = g_tick;
        }
        break;
    case BR_ST_OPERATIONAL:
        if (g_tick - g_last_hb_tick >= HEARTBEAT_PERIOD_TICKS) {
            send_cmd(TUYA_CMD_HEARTBEAT, 0, 0);
            g_last_hb_tick = g_tick;
        }
        break;
    }
}

bridge_state_t bridge_state(void) { return g_state; }
const char *bridge_product_info(void) { return g_product; }

void bridge_set_dp_bool(uint8_t dp_id, uint8_t value) {
    uint8_t buf[16];
    uint16_t n = tuya_serial_build_dp_bool(buf, dp_id, value);
    if (g_tx) g_tx(buf, n);
}

void bridge_set_dp_enum(uint8_t dp_id, uint8_t value) {
    uint8_t buf[16];
    uint16_t n = tuya_serial_build_dp_enum(buf, dp_id, value);
    if (g_tx) g_tx(buf, n);
}

void bridge_set_dp_value(uint8_t dp_id, uint32_t value) {
    uint8_t buf[16];
    uint16_t n = tuya_serial_build_dp_value(buf, dp_id, value);
    if (g_tx) g_tx(buf, n);
}

void bridge_query_all_dps(void) {
    send_cmd(TUYA_CMD_QUERY_DPS, 0, 0);
}
