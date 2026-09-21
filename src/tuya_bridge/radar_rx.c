#include "bridge_app.h"
#include "hal/printf_selector.h"

#ifdef MCU_CORE_8258
#include "tl_common.h"

// ============================================================
// RADAR SO-RX: escuta TODOS os pinos (sem transmitir) e conta
// transicoes. O pino com atividade serial (a MCU falando sozinha
// no boot/handshake) acumula muitas bordas. Passivo e seguro.
// ============================================================

typedef struct { u32 pin; const char *name; } rxpin_t;

static const rxpin_t RXCAND[] = {
    {GPIO_PB1, "B1"}, {GPIO_PB7, "B7"}, {GPIO_PA0, "A0"},
    {GPIO_PD4, "D4"}, {GPIO_PC0, "C0"}, {GPIO_PC1, "C1"},
    {GPIO_PC2, "C2"}, {GPIO_PC3, "C3"}, {GPIO_PD2, "D2"},
    {GPIO_PD3, "D3"}, {GPIO_PD7, "D7"}, {GPIO_PB6, "B6"},
    {GPIO_PB4, "B4"}, {GPIO_PB5, "B5"}, {GPIO_PC4, "C4"},
    {GPIO_PA1, "A1"}, {GPIO_PA2, "A2"}, {GPIO_PA3, "A3"},
    {GPIO_PA4, "A4"}, {GPIO_PB0, "B0"}, {GPIO_PB2, "B2"},
    {GPIO_PB3, "B3"}, {GPIO_PD5, "D5"}, {GPIO_PD6, "D6"},
};
#define NRX (sizeof(RXCAND)/sizeof(RXCAND[0]))

static u16 g_edge_count[NRX];
static u8  g_rx_best = 0xFF;
static u16 g_rx_best_edges = 0;

unsigned char radar_rx_best_pin(void) { return g_rx_best; }
unsigned short radar_rx_best_edges(void) { return g_rx_best_edges; }

static void rx_all_inputs(void) {
    for (u8 i = 0; i < NRX; i++) {
        gpio_set_func(RXCAND[i].pin, AS_GPIO);
        gpio_set_output_en(RXCAND[i].pin, 0);
        gpio_set_input_en(RXCAND[i].pin, 1);
        gpio_setup_up_down_resistor(RXCAND[i].pin, PM_PIN_PULLUP_10K);
    }
}

// Uma janela de escuta: amostra todos os pinos por ~200ms e conta
// as transicoes de cada um. Chamado periodicamente.
void radar_rx_step(void) {
    rx_all_inputs();
    u8 last[NRX];
    for (u8 i = 0; i < NRX; i++) {
        last[i] = gpio_read(RXCAND[i].pin) ? 1 : 0;
        g_edge_count[i] = 0;
    }
    // amostra por ~200ms (a 8us/amostra = 25000 amostras)
    u8 r = irq_disable();
    for (u32 t = 0; t < 25000; t++) {
        for (u8 i = 0; i < NRX; i++) {
            u8 v = gpio_read(RXCAND[i].pin) ? 1 : 0;
            if (v != last[i]) { g_edge_count[i]++; last[i] = v; }
        }
        sleep_us(8);
    }
    irq_restore(r);
    // acha o pino com mais bordas (atividade serial)
    for (u8 i = 0; i < NRX; i++) {
        if (g_edge_count[i] > g_rx_best_edges) {
            g_rx_best_edges = g_edge_count[i];
            g_rx_best = i;
        }
    }
    if (g_rx_best != 0xFF) {
        printf("RADAR-RX: pino %s com %d bordas\r\n",
               RXCAND[g_rx_best].name, g_rx_best_edges);
    }
}

#else
unsigned char radar_rx_best_pin(void) { return 0xFF; }
unsigned short radar_rx_best_edges(void) { return 0; }
void radar_rx_step(void) {}
#endif
