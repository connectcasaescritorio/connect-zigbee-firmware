#include "bridge_core.h"
#include <string.h>

// ============================================================
// Nucleo da ponte - PROTOCOLO ZIGBEE OFICIAL (v0x02):
// boot -> 0x01 product query -> MCU responde JSON
//      -> 0x02 status "conectado" -> MCU ACKa
//      -> 0x07 sync -> MCU reporta DPs via 0x09 (ACKamos cada)
// MCU 5s: 0x03 data 1 -> ACK diplomatico + status conectado
// ============================================================

#define QUERY_RETRY_TICKS       10    // 1s
#define STATUS_REFRESH_TICKS    600   // 60s re-afirma conectado

static bridge_tx_t    g_tx = 0;
static bridge_dp_cb_t g_on_dp = 0;
static bridge_state_t g_state = BR_ST_BOOT;
static uint16_t g_tick = 0;
static uint16_t g_last_action_tick = 0;
static uint16_t g_seq = 1;
static uint16_t g_rx_frames = 0;
static uint8_t g_last_rx_cmd = 0;
static char g_product[96];

#define TXQ_SIZE 384
static uint8_t g_txq[TXQ_SIZE];
static uint16_t g_txq_len = 0;

static void send_cmd_seq(uint16_t seq, uint8_t cmd,
                         const uint8_t *data, uint16_t len) {
    uint8_t buf[TUYA_MAX_FRAME + 12];
    uint16_t n = tuya_serial_build(buf, seq, cmd, data, len);
    if (g_txq_len + n <= TXQ_SIZE) {
        for (uint16_t i = 0; i < n; i++) g_txq[g_txq_len++] = buf[i];
    }
}

static void send_cmd(uint8_t cmd, const uint8_t *data, uint16_t len) {
    if (++g_seq > 0xFFF0) g_seq = 1;
    send_cmd_seq(g_seq, cmd, data, len);
}

static void txq_flush(void) {
    if (g_txq_len && g_tx) {
        g_tx(g_txq, g_txq_len);
        g_txq_len = 0;
    }
}

static void assert_connected(void) {
    uint8_t st = 0x01;  // conectado ao gateway
    send_cmd(TUYA_CMD_NET_STATUS, &st, 1);
}

static void parse_dp_frame(const tuya_frame_t *f) {
    // Tolerante: tenta DPs direto (offset 0) e pulando endereco (offset 2)
    uint16_t off = 0;
    tuya_dp_t dp;
    uint16_t start = 0;
    if (!tuya_dp_next_from(f, 0, &off, &dp) ||
        (dp.type > TUYA_DP_TYPE_ENUM + 1)) {
        off = 0;
        start = 2;
        if (!tuya_dp_next_from(f, 2, &off, &dp)) return;
    }
    do {
        if (g_on_dp) g_on_dp(&dp);
    } while (tuya_dp_next_from(f, start, &off, &dp));
}

static void on_frame(const tuya_frame_t *f) {
    g_rx_frames++;
    g_last_rx_cmd = f->command;
    switch (f->command) {
    case TUYA_CMD_PRODUCT_QUERY: {
        uint16_t n = f->data_len < sizeof(g_product) - 1
                   ? f->data_len : sizeof(g_product) - 1;
        memcpy(g_product, f->data, n);
        g_product[n] = 0;
        if (g_state <= BR_ST_WAIT_PRODUCT) {
            g_state = BR_ST_WAIT_CONF;
            assert_connected();
        }
        break;
    }
    case TUYA_CMD_NET_STATUS:
        // ACK da MCU ao nosso status
        if (g_state == BR_ST_WAIT_CONF) {
            g_state = BR_ST_OPERATIONAL;
            send_cmd(TUYA_CMD_SYNC_NOTIFY, 0, 0);
        }
        break;
    case TUYA_CMD_MCU_RESET_REQ: {
        // Segurar 5s: ACK diplomatico + seguimos conectados
        send_cmd_seq(f->seq, TUYA_CMD_MCU_RESET_REQ, 0, 0);
        assert_connected();
        break;
    }
    case TUYA_CMD_DP_REPORT: {
        // ACK obrigatorio (mesma seq, resultado 0x00)
        uint8_t ok = 0x00;
        send_cmd_seq(f->seq, TUYA_CMD_DP_REPORT, &ok, 1);
        parse_dp_frame(f);
        break;
    }
    case TUYA_CMD_SYNC_NOTIFY:
        break;  // ACK do sync
    default:
        break;
    }
}

void bridge_init(bridge_tx_t tx, bridge_dp_cb_t on_dp) {
    g_tx = tx;
    g_on_dp = on_dp;
    g_state = BR_ST_BOOT;
    g_tick = 0;
    g_last_action_tick = 0;
    g_txq_len = 0;
    g_product[0] = 0;
    tuya_serial_init(on_frame);
}

void bridge_rx(const uint8_t *bytes, uint16_t len) {
    tuya_serial_feed(bytes, len);
}

void bridge_tick_100ms(void) {
    g_tick++;
    txq_flush();
    switch (g_state) {
    case BR_ST_BOOT:
        g_state = BR_ST_WAIT_PRODUCT;
        send_cmd(TUYA_CMD_PRODUCT_QUERY, 0, 0);
        g_last_action_tick = g_tick;
        break;
    case BR_ST_WAIT_PRODUCT:
    case BR_ST_WAIT_CONF:
        if (g_tick - g_last_action_tick >= QUERY_RETRY_TICKS) {
            // Abertura multipla: alterna os verbos ate ela responder
            static uint8_t opener = 0;
            if (opener == 0) {
                send_cmd(TUYA_CMD_PRODUCT_QUERY, 0, 0);
            } else if (opener == 1) {
                assert_connected();
            } else {
                send_cmd(TUYA_CMD_SYNC_NOTIFY, 0, 0);
            }
            opener = (opener + 1) % 3;
            g_last_action_tick = g_tick;
        }
        break;
    case BR_ST_WAIT_HEARTBEAT:  // legado (nao usado no v2)
    case BR_ST_OPERATIONAL:
        if (g_tick - g_last_action_tick >= STATUS_REFRESH_TICKS) {
            assert_connected();
            g_last_action_tick = g_tick;
        }
        break;
    }
}

bridge_state_t bridge_state(void) { return g_state; }
uint16_t bridge_rx_frame_count(void) { return g_rx_frames; }
uint8_t bridge_last_rx_cmd(void) { return g_last_rx_cmd; }
const char *bridge_product_info(void) { return g_product; }

static void queue_dp(uint8_t dp_id, uint8_t type,
                     const uint8_t *val, uint16_t vlen) {
    uint8_t d[12];
    uint16_t n = 0;
    d[n++] = dp_id;
    d[n++] = type;
    d[n++] = (uint8_t)(vlen >> 8);
    d[n++] = (uint8_t)(vlen & 0xFF);
    for (uint16_t i = 0; i < vlen; i++) d[n++] = val[i];
    send_cmd(TUYA_CMD_DP_COMMAND, d, n);
}

void bridge_set_dp_bool(uint8_t dp_id, uint8_t value) {
    uint8_t v = value ? 1 : 0;
    queue_dp(dp_id, TUYA_DP_TYPE_BOOL, &v, 1);
}

void bridge_set_dp_enum(uint8_t dp_id, uint8_t value) {
    queue_dp(dp_id, TUYA_DP_TYPE_ENUM, &value, 1);
}

void bridge_set_dp_value(uint8_t dp_id, uint32_t value) {
    uint8_t v[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                    (uint8_t)(value >> 8), (uint8_t)(value)};
    queue_dp(dp_id, TUYA_DP_TYPE_VALUE, v, 4);
}

void bridge_query_all_dps(void) {
    send_cmd(TUYA_CMD_SYNC_NOTIFY, 0, 0);
}
