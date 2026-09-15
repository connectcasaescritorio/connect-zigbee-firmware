#include "bridge_app.h"
#include <stdint.h>
#include "bridge_core.h"
#include "hal/uart.h"
#include "hal/tasks.h"
#include "hal/printf_selector.h"
#include "zigbee/relay_cluster.h"
#include "zigbee/switch_cluster.h"
#include "zigbee/basic_cluster.h"
#include "hal/system.h"
#include "hal/zigbee.h"

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
// Mapa do Marcello: 6 DPs de cena DISTINTOS = 6 teclas fisicas.
// DP 1..6 -> endpoints de action 1..6 (esquerda 1-3, direita 4-6)
#define DP_CENA_MIN     1
#define DP_CENA_MAX     6
#define DP_MODE_CH_1_v  18
#define DP_MODE_CH_1    18
#define DP_MODE_CH_2    19
#define DP_MODE_CH_3    20
#define DP_BACKLIGHT    36
#define DP_BRIGHTNESS   101

// Valores multistate (mesma tabela do switch_cluster/converters)
#define MS_LONG_PRESS   2
#define MS_SINGLE       5
#define MS_DOUBLE       6
#define MS_TRIPLE       7
#define MS_CENA_BASE    11  // 11/12/13 = cena single/double/triple

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

static uint32_t g_app_ticks = 0;
uint32_t bridge_app_ticks(void);

static void tick(void *arg) {
    g_app_ticks++;
    hal_uart_process();
    bridge_tick_100ms();
    bridge_set_network_joined(
        hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED);

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

    basic_cluster_update_bridge_hidden(bridge_rx_frame_count(),
                                       bridge_last_frame_hex());
    hal_tasks_schedule(&g_tick_task, 100);
}

static hal_task_t g_action_reset_task;
static uint8_t g_action_pending[6] = {0,0,0,0,0,0};

static void action_reset(void *arg) {
    for (uint8_t i = 0; i < 6; i++) {
        if (g_action_pending[i]) {
            g_action_pending[i] = 0;
            switch_cluster_emit_action(&switch_clusters[i], 0);
        }
    }
}

static void emit_action_ep(uint8_t src, uint8_t ms_value);

// ===== Multiclique fabricado na ponte (a MCU so manda toques v=0) =====
#define MC_WINDOW_MS 400
static uint8_t g_mc_count[6];
static hal_task_t g_mc_task[6];

static void mc_fire(void *arg) {
    uint8_t src = (uint8_t)(uintptr_t)arg;   // 0..5 = tecla fisica 1..6
    uint8_t n = g_mc_count[src];
    g_mc_count[src] = 0;
    if (n == 0) return;
    if (n > 3) n = 3;
    // Cada fonte tem SEU endpoint (1..6) e o gesto single/double/triple
    emit_action_ep(src, MS_SINGLE + (n - 1));
}

static void mc_press(uint8_t src) {
    if (src > 5) return;
    g_mc_count[src]++;
    g_mc_task[src].handler = mc_fire;
    g_mc_task[src].arg = (void *)(uintptr_t)src;
    hal_tasks_init(&g_mc_task[src]);
    hal_tasks_schedule(&g_mc_task[src], MC_WINDOW_MS);
}

static void emit_action_ep(uint8_t src, uint8_t ms_value) {
    if (src > 5) return;
    switch_cluster_emit_action(&switch_clusters[src], ms_value);
    g_action_pending[src] = 1;
    g_action_reset_task.handler = action_reset;
    hal_tasks_init(&g_action_reset_task);
    hal_tasks_schedule(&g_action_reset_task, 400);
}

static void set_relay_from_dp(uint8_t idx, uint8_t on) {
    if (idx > 2) return;
    g_suppress_dp_tx = 1;
    if (on) relay_cluster_on(&relay_clusters[idx]);
    else    relay_cluster_off(&relay_clusters[idx]);
    g_suppress_dp_tx = 0;
}

// Ritual de reset fisico: 7 toques rapidos + segurar (0x03 da MCU)
extern uint8_t g_multi_press_reset_count;
#define RITUAL_WINDOW_TICKS  140
static uint32_t g_press_ticks[16];
static uint8_t g_press_idx = 0;

static void ritual_note_press(void) {
    uint8_t need = g_multi_press_reset_count;
    if (need == 0 || need > 16) need = 10;
    g_press_ticks[g_press_idx % need] = bridge_app_ticks();
    g_press_idx++;
    if (g_press_idx >= need) {
        uint32_t now = bridge_app_ticks();
        uint8_t all_recent = 1;
        for (uint8_t i = 0; i < need; i++)
            if (now - g_press_ticks[i] > RITUAL_WINDOW_TICKS) all_recent = 0;
        if (all_recent) {
            printf("bridge: RITUAL %d toques - wipe\r\n", need);
            basic_cluster_request_factory_wipe();
            g_press_idx = 0;
        }
    }
}


static void on_mcu_dp(const tuya_dp_t *dp) {
    uint8_t v = dp->len > 0 ? dp->data[dp->len - 1] : 0;
    if (dp->id >= DP_CENA_MIN && dp->id <= DP_CENA_MAX) {
        ritual_note_press();
    }
    switch (dp->id) {
    case DP_RELAY_1: set_relay_from_dp(0, v); break;
    case DP_RELAY_2: set_relay_from_dp(1, v); break;
    case DP_RELAY_3: set_relay_from_dp(2, v); break;

    case 1: case 2: case 3: case 4: case 5: case 6:
        (void)v;
        mc_press(dp->id - 1);  // DP 1..6 -> fonte 0..5 (6 teclas distintas)
        break;
    case DP_BACKLIGHT:
        basic_cluster_update_bridge_backlight(v ? 1 : 0);
        break;
    case DP_BRIGHTNESS:
        basic_cluster_update_bridge_brightness(v);
        break;
    case DP_MODE_CH_1:
    case DP_MODE_CH_2:
    case DP_MODE_CH_3:
        basic_cluster_update_bridge_mode(dp->id - DP_MODE_CH_1, v);
        break;
    default:
        bridge_note_unknown_dp(dp->id, v);
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
uint32_t bridge_app_ticks(void) { return g_app_ticks; }

// ===== Modo RADAR (modelo -RD): descoberta de fiacao =====
static hal_task_t g_radar_task;
static void radar_tick(void *arg) {
    radar_step();
    hal_tasks_schedule(&g_radar_task, 3000);
}

void radar_app_init(void) {
    g_radar_task.handler = radar_tick;
    g_radar_task.arg = 0;
    hal_tasks_init(&g_radar_task);
    hal_tasks_schedule(&g_radar_task, 5000);
    printf("RADAR: varredura de UART iniciada\r\n");
}

void bridge_ui_backlight(uint8_t on) {
    if (g_active) bridge_set_dp_bool(DP_BACKLIGHT, on);
}
void bridge_ui_brightness(uint8_t pct) {
    if (g_active) bridge_set_dp_value(DP_BRIGHTNESS, pct);
}
void bridge_ui_mode(uint8_t ch, uint8_t mode) {
    if (g_active && ch < 3) bridge_set_dp_enum(DP_MODE_CH_1 + ch, mode);
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
