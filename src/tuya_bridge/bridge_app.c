#include "bridge_app.h"
#include "bridge_core.h"
#include "hal/uart.h"
#include "hal/tasks.h"
#include "hal/printf_selector.h"
#include "zigbee/relay_cluster.h"
#include "zigbee/switch_cluster.h"
#include "zigbee/basic_cluster.h"

// ============================================================
// ConnectCasa tuya_mcu_bridge - aplicacao da ponte
// Painel 3ch+3cenas (_TZE284_3kjidznp) - mapa DP confirmado:
//   Reles:  DP 24/25/26  <-> relay_clusters[0..2]
//   Botoes: DP 4/5/6 (0=simples,1=duplo,2=segurar) -> actions teclas 1..3
//   Cenas:  DP 1/2/3 (canal em modo cena) -> action single da tecla
//   Backlight: DP 36 (on/off), DP 101 (brilho 0-99)
// ============================================================

#define DP_RELAY_1      24
#define DP_RELAY_2      25
#define DP_RELAY_3      26
#define DP_BTN_1        4
#define DP_BTN_2        5
#define DP_BTN_3        6
#define DP_SCENE_CH_1   1
#define DP_SCENE_CH_2   2
#define DP_SCENE_CH_3   3

// Valores multistate (mesma tabela do switch_cluster/converters)
#define MS_LONG_PRESS   2
#define MS_SINGLE       5
#define MS_DOUBLE       6

extern zigbee_relay_cluster relay_clusters[];
extern zigbee_switch_cluster switch_clusters[];

static uint8_t g_active = 0;
static uint8_t g_suppress_dp_tx = 0;  // evita eco DP<->cluster

static hal_task_t g_tick_task;

// Auto-baud: cicla as velocidades Tuya ate o handshake fechar
static const uint32_t BAUDS[] = {115200, 9600}; // spec: 115200 fixo
static uint8_t g_baud_idx = 0;
static uint16_t g_ticks_in_state = 0;
#define BAUD_SWITCH_TICKS 40   // 4s sem handshake -> proxima velocidade

static void on_mcu_dp(const tuya_dp_t *dp);

static void uart_rx(const uint8_t *bytes, uint16_t len) {
    bridge_rx(bytes, len);
    // ACKs nao podem esperar o tick de 100ms: a MCU tem timeout curto.
    // Reagenda o tick para JA (drena a fila em ~1ms, fora da IRQ).
    hal_tasks_schedule(&g_tick_task, 1);
}

static void bridge_uart_tx(const uint8_t *bytes, uint16_t len) {
    hal_uart_send(bytes, len);
}

static void tick(void *arg) {
    hal_uart_process();
    bridge_tick_100ms();

    // Auto-baud: sem operacao apos 4s, tenta a proxima velocidade
    if (bridge_state() != BR_ST_OPERATIONAL) {
        g_ticks_in_state++;
        if (g_ticks_in_state >= BAUD_SWITCH_TICKS) {
            g_ticks_in_state = 0;
            g_baud_idx = (g_baud_idx + 1) % 2;
            printf("bridge: auto-baud -> %d\r\n", (int)BAUDS[g_baud_idx]);
            hal_uart_init(BAUDS[g_baud_idx], uart_rx);
            bridge_init(bridge_uart_tx, on_mcu_dp);
        }
    } else {
        g_ticks_in_state = 0;
    }

    // Diagnostico: nibble alto do estado = indice do baud atual
    basic_cluster_update_bridge_diag(
        (uint8_t)((g_baud_idx << 4) | (uint8_t)bridge_state()),
        bridge_rx_frame_count());
    basic_cluster_update_bridge_last_cmd(bridge_last_rx_cmd());
    hal_tasks_schedule(&g_tick_task, 100);
}

static void emit_action(uint8_t key_idx, uint8_t ms_value) {
    if (key_idx > 2) return;
    switch_cluster_emit_action(&switch_clusters[key_idx], ms_value);
}

static void set_relay_from_dp(uint8_t idx, uint8_t on) {
    if (idx > 2) return;
    g_suppress_dp_tx = 1;
    if (on) relay_cluster_on(&relay_clusters[idx]);
    else    relay_cluster_off(&relay_clusters[idx]);
    g_suppress_dp_tx = 0;
}

static void on_mcu_dp(const tuya_dp_t *dp) {
    uint8_t v = dp->len > 0 ? dp->data[dp->len - 1] : 0;
    switch (dp->id) {
    case DP_RELAY_1: set_relay_from_dp(0, v); break;
    case DP_RELAY_2: set_relay_from_dp(1, v); break;
    case DP_RELAY_3: set_relay_from_dp(2, v); break;
    case DP_BTN_1:
    case DP_BTN_2:
    case DP_BTN_3: {
        uint8_t key = dp->id - DP_BTN_1;
        uint8_t ms = (v == 0) ? MS_SINGLE : (v == 1) ? MS_DOUBLE : MS_LONG_PRESS;
        emit_action(key, ms);
        break;
    }
    case DP_SCENE_CH_1:
    case DP_SCENE_CH_2:
    case DP_SCENE_CH_3:
        emit_action(dp->id - DP_SCENE_CH_1, MS_SINGLE);
        break;
    default:
        printf("bridge: DP %d desconhecido (type %d len %d)\r\n",
               dp->id, dp->type, dp->len);
        break;
    }
}

// Chamado pelo relay_cluster quando o rele muda (comando Zigbee/binding)
void bridge_on_relay_change(uint8_t relay_index, uint8_t state) {
    if (!g_active || g_suppress_dp_tx) return;
    if (relay_index > 2) return;
    bridge_set_dp_bool(DP_RELAY_1 + relay_index, state ? 1 : 0);
}

uint8_t bridge_app_active(void) { return g_active; }

// ===== Modo RADAR (modelo -RD): descoberta de fiacao =====
static hal_task_t g_radar_task;
static void radar_tick(void *arg) {
    radar_step();
    basic_cluster_update_bridge_diag(radar_status(), radar_result());
    hal_tasks_schedule(&g_radar_task, 3000);
}

void radar_app_init(void) {
    g_radar_task.handler = radar_tick;
    g_radar_task.arg = 0;
    hal_tasks_init(&g_radar_task);
    hal_tasks_schedule(&g_radar_task, 5000);
    printf("RADAR: varredura de UART iniciada\r\n");
}

void bridge_app_init(void) {
    g_active = 1;
    hal_uart_init(9600, uart_rx);
    bridge_init(bridge_uart_tx, on_mcu_dp);
    g_tick_task.handler = tick;
    g_tick_task.arg = 0;
    hal_tasks_init(&g_tick_task);
    hal_tasks_schedule(&g_tick_task, 100);
    printf("bridge_app: iniciado (UART 9600, mapa 3ch+3cenas)\r\n");
}
