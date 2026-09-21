#include "bridge_app.h"
#include "hal/printf_selector.h"

#ifdef MCU_CORE_8258
#include "tl_common.h"

// ============================================================
// RADAR ConnectCasa (v1.3.0) - descoberta automatica da UART
// Para cada pino candidato a TX: bit-banga 2 heartbeats Tuya
// (9600) e conta transicoes em todos os demais candidatos.
// O par que "responder" e a fiacao real MCU<->ZTU.
// Diagnostico: 0xff05 = (found<<7)|tx_idx ; 0xff06 = melhor par
//   (tx_idx<<8)|rx_idx quando encontrado; senao maior contagem.
// ============================================================

typedef struct { u32 pin; const char *name; } radar_pin_t;

static const radar_pin_t CAND[] = {
    {GPIO_PB1, "B1"}, {GPIO_PB7, "B7"}, {GPIO_PA0, "A0"},
    {GPIO_PD4, "D4"}, {GPIO_PC0, "C0"}, {GPIO_PC1, "C1"},
    {GPIO_PC2, "C2"}, {GPIO_PC3, "C3"}, {GPIO_PD2, "D2"},
    {GPIO_PD3, "D3"}, {GPIO_PD7, "D7"}, {GPIO_PB6, "B6"},
    {GPIO_PB4, "B4"}, {GPIO_PB5, "B5"}, {GPIO_PC4, "C4"},
    {GPIO_PA1, "A1"}, {GPIO_PA2, "A2"}, {GPIO_PA3, "A3"},
    {GPIO_PA4, "A4"}, {GPIO_PB0, "B0"}, {GPIO_PB2, "B2"},
    {GPIO_PB3, "B3"}, {GPIO_PD5, "D5"}, {GPIO_PD6, "D6"},
};
#define NCAND (sizeof(CAND)/sizeof(CAND[0]))

// Product query v0x02 seq=1: 55 AA 02 00 01 01 00 00 chk(=0x03... calc)
static const u8 HEARTBEAT[] = {0x55, 0xAA, 0x02, 0x00, 0x01, 0x01,
                               0x00, 0x00, 0x03};
#define BIT_US 104  // 9600 na sonda (bit-bang preciso; 115200 fica p/ ponte)

static u8 g_tx_idx = 0;
static u8 g_found = 0;
static u16 g_best = 0;

u8 radar_status(void) { return (g_found ? 0x80 : 0) | g_tx_idx; }
u16 radar_result(void) { return g_best; }

static void all_inputs(void) {
    for (u8 i = 0; i < NCAND; i++) {
        gpio_set_func(CAND[i].pin, AS_GPIO);
        gpio_set_output_en(CAND[i].pin, 0);
        gpio_set_input_en(CAND[i].pin, 1);
        gpio_setup_up_down_resistor(CAND[i].pin, PM_PIN_PULLUP_10K);
    }
}

static void tx_byte(u32 pin, u8 b) {
    gpio_write(pin, 0); sleep_us(BIT_US);
    for (u8 i = 0; i < 8; i++) {
        gpio_write(pin, (b >> i) & 1); sleep_us(BIT_US);
    }
    gpio_write(pin, 1); sleep_us(BIT_US);
}

// Uma rodada: TX no candidato atual + escuta janelada nos demais
void radar_step(void) {
    if (g_found) return;

    u8 t = g_tx_idx;
    all_inputs();
    gpio_set_input_en(CAND[t].pin, 0);
    gpio_set_output_en(CAND[t].pin, 1);
    gpio_write(CAND[t].pin, 1);
    sleep_us(2000);

    u8 last[NCAND];
    u16 edges[NCAND];
    for (u8 i = 0; i < NCAND; i++) {
        last[i] = gpio_read(CAND[i].pin) ? 1 : 0;
        edges[i] = 0;
    }

    u8 r = irq_disable();
    for (u8 rep = 0; rep < 2; rep++) {
        for (u8 k = 0; k < sizeof(HEARTBEAT); k++) {
            // transmite o byte e amostra os outros pinos entre bits
            u8 b = HEARTBEAT[k];
            gpio_write(CAND[t].pin, 0);
            for (u16 us = 0; us < BIT_US; us += 8) {
                for (u8 i = 0; i < NCAND; i++) {
                    if (i == t) continue;
                    u8 v = gpio_read(CAND[i].pin) ? 1 : 0;
                    if (v != last[i]) { edges[i]++; last[i] = v; }
                }
                sleep_us(8);
            }
            for (u8 bit = 0; bit < 8; bit++) {
                gpio_write(CAND[t].pin, (b >> bit) & 1);
                for (u16 us = 0; us < BIT_US; us += 8) {
                    for (u8 i = 0; i < NCAND; i++) {
                        if (i == t) continue;
                        u8 v = gpio_read(CAND[i].pin) ? 1 : 0;
                        if (v != last[i]) { edges[i]++; last[i] = v; }
                    }
                    sleep_us(8);
                }
            }
            gpio_write(CAND[t].pin, 1);
            sleep_us(BIT_US);
        }
        // janela de resposta pos-frame: ~20ms de escuta
        for (u16 w = 0; w < 2500; w++) {
            for (u8 i = 0; i < NCAND; i++) {
                if (i == t) continue;
                u8 v = gpio_read(CAND[i].pin) ? 1 : 0;
                if (v != edges[i] % 2 && v != last[i]) { edges[i]++; last[i] = v; }
                else if (v != last[i]) { edges[i]++; last[i] = v; }
            }
            sleep_us(8);
        }
    }
    irq_restore(r);

    // melhor RX desta rodada
    u16 best_e = 0; u8 best_i = 0xFF;
    for (u8 i = 0; i < NCAND; i++) {
        if (i == t) continue;
        if (edges[i] > best_e) { best_e = edges[i]; best_i = i; }
    }
    if (best_e >= 10) {
        g_found = 1;
        g_best = ((u16)t << 8) | best_i;
        printf("RADAR: TX=%s RX=%s edges=%d\r\n",
               CAND[t].name, CAND[best_i].name, best_e);
    } else {
        if (best_e > (g_best & 0xFF)) {
            g_best = ((u16)t << 8) | (best_e & 0xFF);
        }
        g_tx_idx = (g_tx_idx + 1) % NCAND;
    }
}

#else
// Stub: radar inerte
unsigned char radar_status(void) { return 0; }
unsigned short radar_result(void) { return 0; }
void radar_step(void) {}
#endif
