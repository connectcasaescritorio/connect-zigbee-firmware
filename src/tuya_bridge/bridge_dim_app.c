#include "bridge_dim_app.h"
#include "bridge_core.h"
#include "hal/uart.h"
#include "hal/tasks.h"
#include "hal/printf_selector.h"
#include "zigbee/dimmer_cluster.h"

// ============================================================
// PONTE DIMMER - dimmer TS110E via MCU HK32F030 (DP serial)
// DPs padrao do dimmer Tuya:
//   DP 1 = on/off (bool)
//   DP 2 = brilho (value, 0..1000)
// A MCU faz o corte de fase; nos so mandamos o nivel via DP.
// ============================================================

#define DP_DIM_ONOFF  1
#define DP_DIM_LEVEL  2
#define DP_LEVEL_MAX  1000   // escala Tuya

extern zigbee_dimmer_cluster dimmer_clusters[];

static uint8_t g_active = 0;
static uint8_t g_suppress = 0;
static hal_task_t g_tick_task;
static const uint32_t BAUDS[] = {115200, 9600};
static uint8_t g_baud_idx = 0;
static uint16_t g_ticks_in_state = 0;

static void on_mcu_dp(const tuya_dp_t *dp);
static void bridge_uart_tx(const uint8_t *b, uint16_t n) { hal_uart_send(b, n); }

static void uart_rx(const uint8_t *bytes, uint16_t len) {
    bridge_rx(bytes, len);
    hal_tasks_schedule(&g_tick_task, 1);
}

static void tick(void *arg) {
    hal_uart_process();
    bridge_tick_100ms();
    if (bridge_state() != BR_ST_OPERATIONAL) {
        g_ticks_in_state++;
        if (g_ticks_in_state >= 40) {
            g_ticks_in_state = 0;
            g_baud_idx = (g_baud_idx + 1) % 2;
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

// DP da MCU -> atualiza o cluster (feedback)
static void on_mcu_dp(const tuya_dp_t *dp) {
    uint8_t v = dp->len > 0 ? dp->data[dp->len - 1] : 0;
    g_suppress = 1;
    if (dp->id == DP_DIM_ONOFF) {
        if (v) dimmer_cluster_on(&dimmer_clusters[0]);
        else   dimmer_cluster_off(&dimmer_clusters[0]);
    } else if (dp->id == DP_DIM_LEVEL) {
        // valor Tuya (0..1000) -> nivel zigbee (0..254)
        uint32_t val = 0;
        for (uint16_t i = 0; i < dp->len; i++) val = (val << 8) | dp->data[i];
        uint8_t lvl = (uint8_t)((val * 254) / DP_LEVEL_MAX);
        dimmer_cluster_set_level(&dimmer_clusters[0], lvl);
    }
    g_suppress = 0;
}

// Comando do cluster (UI) -> manda DP pra MCU
static void dim_apply(uint8_t level) {
    if (!g_active || g_suppress) return;
    if (level == 0) {
        bridge_set_dp_bool(DP_DIM_ONOFF, 0);
    } else {
        bridge_set_dp_bool(DP_DIM_ONOFF, 1);
        uint32_t tuya_val = ((uint32_t)level * DP_LEVEL_MAX) / 254;
        bridge_set_dp_value(DP_DIM_LEVEL, tuya_val);
    }
}

uint8_t bridge_dim_active(void) { return g_active; }

void bridge_dim_app_init(void) {
    g_active = 1;
    dimmer_clusters[0].apply = dim_apply;
    dimmer_clusters[0].current_level = 254;
    hal_uart_init(BAUDS[0], uart_rx);
    bridge_init(bridge_uart_tx, on_mcu_dp);
    g_tick_task.handler = tick;
    g_tick_task.arg = 0;
    hal_tasks_init(&g_tick_task);
    hal_tasks_schedule(&g_tick_task, 100);
    printf("bridge_dim: iniciado (DP1=onoff, DP2=brilho)\r\n");
}
