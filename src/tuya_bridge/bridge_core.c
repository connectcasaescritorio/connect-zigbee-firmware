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

// Verbo de comando: 0 = spray (todos os candidatos); N = so o verbo N
static uint8_t g_cmd_verb = 0;
void bridge_set_cmd_verb(uint8_t v) { g_cmd_verb = v; }
uint8_t bridge_get_cmd_verb(void) { return g_cmd_verb; }

// Formato aprendido dos reports dela (espelhado nos comandos)
static uint8_t g_learned = 0;
static uint8_t g_dp_verb = 0x06;
static uint8_t g_dp_has_addr = 0;
static uint8_t g_mcu_addr[2] = {0, 0};

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

static uint8_t g_net_joined = 1;
static uint8_t g_unjoined_flip = 0;

static void assert_connected(void) {
    // Conectado: 0x01. Despareado: alterna 0x00/0x03 (dialeto da MCU
    // desconhecido para "pareando" - um dos dois acende o bale)
    uint8_t st;
    if (g_net_joined) {
        st = 0x01;
    } else {
        st = g_unjoined_flip ? 0x03 : 0x00;
        g_unjoined_flip ^= 1;
    }
    send_cmd(TUYA_CMD_NET_STATUS, &st, 1);
}

void bridge_set_network_joined(uint8_t joined) {
    joined = joined ? 1 : 0;
    if (joined != g_net_joined) {
        g_net_joined = joined;
        assert_connected();  // muda? avisa a MCU na hora (o balé liga/desliga)
    }
}

static int dp_units_valid(const tuya_frame_t *f, uint16_t start) {
    // Valida se a partir de start os DPs preenchem o frame exatamente
    uint16_t off = 0;
    tuya_dp_t dp;
    uint16_t consumed = start;
    while (tuya_dp_next_from(f, start, &off, &dp)) {
        if (dp.type > 0x05) return 0;
        consumed = off;
    }
    return consumed == f->data_len && consumed > start;
}

static void parse_dp_frame(const tuya_frame_t *f) {
    // Aprende o formato: DPs direto (0) ou com endereco 2B (2)?
    uint16_t start;
    if (dp_units_valid(f, 0)) {
        start = 0;
    } else if (dp_units_valid(f, 2)) {
        start = 2;
    } else {
        return;
    }
    // Registra o dialeto dela para espelhar nos comandos
    g_dp_verb = f->command;
    g_dp_has_addr = (start == 2);
    if (g_dp_has_addr) {
        g_mcu_addr[0] = f->data[0];
        g_mcu_addr[1] = f->data[1];
    }
    g_learned = 1;

    uint16_t off = 0;
    tuya_dp_t dp;
    while (tuya_dp_next_from(f, start, &off, &dp)) {
        if (g_on_dp) g_on_dp(&dp);
    }
}

static char g_last_hex[64];
static char g_unknown_dps[64];
static uint16_t g_unknown_len = 0;

void bridge_note_unknown_dp(uint8_t id, uint8_t value) {
    // acumula "id:val," sem duplicar o id
    char idbuf[8];
    uint8_t n = 0;
    uint8_t x = id;
    if (x >= 100) { idbuf[n++] = '0' + x / 100; x %= 100; }
    if (id >= 10)  { idbuf[n++] = '0' + x / 10; x %= 10; }
    idbuf[n++] = '0' + x;
    idbuf[n] = 0;
    // duplicado?
    for (uint16_t i = 0; i + n < g_unknown_len; i++) {
        uint8_t match = (i == 0 || g_unknown_dps[i-1] == ',');
        for (uint8_t k = 0; match && k < n; k++)
            if (g_unknown_dps[i+k] != idbuf[k]) match = 0;
        if (match && g_unknown_dps[i+n] == ':') return;
    }
    if (g_unknown_len + n + 5 >= sizeof(g_unknown_dps)) return;
    for (uint8_t k = 0; k < n; k++) g_unknown_dps[g_unknown_len++] = idbuf[k];
    g_unknown_dps[g_unknown_len++] = ':';
    g_unknown_dps[g_unknown_len++] = '0' + (value / 10) % 10;
    g_unknown_dps[g_unknown_len++] = '0' + value % 10;
    g_unknown_dps[g_unknown_len++] = ',';
    g_unknown_dps[g_unknown_len] = 0;
}

const char *bridge_unknown_dps(void) { return g_unknown_dps; }

static void record_hex(const tuya_frame_t *f) {
    static const char H[] = "0123456789ABCDEF";
    uint16_t n = 0;
    // ver seq cmd len + primeiros dados (ate caber)
    uint8_t hdr[6] = {f->version, (uint8_t)(f->seq >> 8), (uint8_t)f->seq,
                      f->command, (uint8_t)(f->data_len >> 8),
                      (uint8_t)f->data_len};
    for (uint8_t i = 0; i < 6 && n < sizeof(g_last_hex) - 3; i++) {
        g_last_hex[n++] = H[hdr[i] >> 4];
        g_last_hex[n++] = H[hdr[i] & 0xF];
    }
    for (uint16_t i = 0; i < f->data_len && n < sizeof(g_last_hex) - 3; i++) {
        g_last_hex[n++] = H[f->data[i] >> 4];
        g_last_hex[n++] = H[f->data[i] & 0xF];
    }
    g_last_hex[n] = 0;
}

const char *bridge_last_frame_hex(void) { return g_last_hex; }

static void on_frame(const tuya_frame_t *f) {
    g_rx_frames++;
    g_last_rx_cmd = f->command;
    record_hex(f);
    switch (f->command) {
    case 0x00:
        // Heartbeat respondido (dialeto classico): emenda o produto
        if (g_state < BR_ST_OPERATIONAL) {
            send_cmd(TUYA_CMD_PRODUCT_QUERY, 0, 0);
        }
        break;
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
    case 0x05:
    case 0x06:
    case TUYA_CMD_SYNC_NOTIFY:
    case TUYA_CMD_DP_REPORT: {
        // Poliglota: a MCU real usa 0x06 para reports (estilo classico).
        // Qualquer verbo de DP: ACK no MESMO verbo/seq + parse tolerante.
        if (f->data_len >= 4) {
            uint8_t ok = 0x00;
            send_cmd_seq(f->seq, f->command, &ok, 1);
            parse_dp_frame(f);
            // Ela fala: aproveita a atencao e corteja de novo
            if (g_state < BR_ST_OPERATIONAL) {
                send_cmd(TUYA_CMD_PRODUCT_QUERY, 0, 0);
            }
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
                send_cmd(0x00, 0, 0);  // heartbeat classico
            } else if (opener == 1) {
                send_cmd(TUYA_CMD_PRODUCT_QUERY, 0, 0);
            } else if (opener == 2) {
                assert_connected();
            } else {
                send_cmd(TUYA_CMD_SYNC_NOTIFY, 0, 0);
            }
            opener = (opener + 1) % 4;
            g_last_action_tick = g_tick;
        }
        break;
    case BR_ST_WAIT_HEARTBEAT:  // legado (nao usado no v2)
    case BR_ST_OPERATIONAL: {
        // Refresh: 60s conectado; 3s despareado (bale acende rapido)
        uint16_t period = g_net_joined ? STATUS_REFRESH_TICKS : 30;
        if (g_tick - g_last_action_tick >= period) {
            assert_connected();
            g_last_action_tick = g_tick;
        }
        break;
    }
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
    if (g_cmd_verb != 0) {
        // Seletor travado: SO o verbo escolhido (bisseccao ao vivo)
        send_cmd(g_cmd_verb, d, n);
        return;
    }
    // SPRAY: todos os verbos candidatos (bool/enum idempotentes -
    // apenas o verbo certo age; os demais ela ignora)
    send_cmd(0x06, d, n);
    send_cmd(0x07, d, n);
    send_cmd(0x04, d, n);
    send_cmd(0x05, d, n);
    send_cmd(TUYA_CMD_DP_COMMAND, d, n);          // 0x08 direto
    {
        uint8_t da[14];
        da[0] = 0x00; da[1] = 0x00;               // 0x08 three-level addr 0
        for (uint16_t i = 0; i < n; i++) da[2 + i] = d[i];
        send_cmd(TUYA_CMD_DP_COMMAND, da, n + 2);
    }
    send_cmd_seq(0x0000, 0x06, d, n);             // 0x06 com seq reservada 0
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
