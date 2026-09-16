#include "bridge8_app.h"
#include "bridge_core.h"
#include "hal/uart.h"
#include "hal/tasks.h"
#include "hal/printf_selector.h"
#include "zigbee/relay_cluster.h"
#include "zigbee/basic_cluster.h"

// ============================================================
// ConnectCasa - PONTE 8 RELÉS (placa 8gang-touch, _TZE204_wktrysab)
// Arquitetura: nosso fw no ZT3L <-UART DP-> MCU Tuya
// Mapa DP (do converter de fábrica):
//   DP 1..6  -> relés 1..6
//   DP 101   -> relé 7
//   DP 102   -> relé 8
// 8 DPs <-> 8 clusters onOff, bidirecional.
// ============================================================

// DP de cada relé, indexado por 0..7
static const uint16_t RELAY_DP[8] = {1, 2, 3, 4, 5, 6, 101, 102};

extern zigbee_relay_cluster relay_clusters[];

static uint8_t g_active = 0;
static uint8_t g_suppress_dp_tx = 0;
static hal_task_t g_tick_task;

// Auto-baud: cicla 115200 <-> 9600 ate o handshake fechar
static const uint32_t BAUDS[] = {115200, 9600};
static uint8_t g_baud_idx = 0;
static uint16_t g_ticks_in_state = 0;
#define BAUD_SWITCH_TICKS 40  // 4s sem handshake -> proxima velocidade

static void on_mcu_dp(const tuya_dp_t *dp);
static void bridge_uart_tx(const uint8_t *bytes, uint16_t len);

static void uart_rx(const uint8_t *bytes, uint16_t len) {
    bridge_rx(bytes, len);
    hal_tasks_schedule(&g_tick_task, 1);  // ACK rápido
}

static void bridge_uart_tx(const uint8_t *bytes, uint16_t len) {
    hal_uart_send(bytes, len);
}

static void tick(void *arg) {
    hal_uart_process();
    bridge_tick_100ms();

    if (bridge_state() != BR_ST_OPERATIONAL) {
        g_ticks_in_state++;
        if (g_ticks_in_state >= BAUD_SWITCH_TICKS) {
            g_ticks_in_state = 0;
            g_baud_idx = (g_baud_idx + 1) % 2;
            printf("bridge8: auto-baud -> %d\r\n", (int)BAUDS[g_baud_idx]);
            hal_uart_init(BAUDS[g_baud_idx], uart_rx);
            bridge_init(bridge_uart_tx, on_mcu_dp);
        }
    } else {
        g_ticks_in_state = 0;
    }

    // Expõe estado no atributo oculto: frames_rx (0xff06) e último frame (0xff08)
    basic_cluster_update_bridge_hidden(bridge_rx_frame_count(),
                                       bridge_last_frame_hex());
    hal_tasks_schedule(&g_tick_task, 100);
}

// DP -> índice de relé (0..7), ou 0xFF se não for relé
static uint8_t dp_to_relay(uint16_t dp) {
    for (uint8_t i = 0; i < 8; i++)
        if (RELAY_DP[i] == dp) return i;
    return 0xFF;
}

static void set_relay_from_dp(uint8_t idx, uint8_t on) {
    if (idx > 7) return;
    g_suppress_dp_tx = 1;
    if (on) relay_cluster_on(&relay_clusters[idx]);
    else    relay_cluster_off(&relay_clusters[idx]);
    g_suppress_dp_tx = 0;
}

static void on_mcu_dp(const tuya_dp_t *dp) {
    uint8_t v = dp->len > 0 ? dp->data[dp->len - 1] : 0;
    uint8_t idx = dp_to_relay(dp->id);
    if (idx != 0xFF) {
        set_relay_from_dp(idx, v);
    } else {
        printf("bridge8: DP %d desconhecido\r\n", dp->id);
    }
}

// Chamado pelo relay_cluster quando um relé muda (comando Zigbee)
void bridge8_on_relay_change(uint8_t relay_index, uint8_t state) {
    if (!g_active || g_suppress_dp_tx) return;
    if (relay_index > 7) return;
    bridge_set_dp_bool(RELAY_DP[relay_index], state ? 1 : 0);
}

uint8_t bridge8_app_active(void) { return g_active; }

void bridge8_app_init(void) {
    g_active = 1;
    hal_uart_init(BAUDS[0], uart_rx);
    bridge_init(bridge_uart_tx, on_mcu_dp);
    g_tick_task.handler = tick;
    g_tick_task.arg = 0;
    hal_tasks_init(&g_tick_task);
    hal_tasks_schedule(&g_tick_task, 100);
    printf("bridge8_app: iniciado (8 relés DP 1-6,101,102)\r\n");
}
