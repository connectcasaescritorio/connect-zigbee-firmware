#include "hal/uart.h"
#include "tl_common.h"
#include "drivers/drv_uart.h"
#include <string.h>

// ============================================================
// UART hibrida ConnectCasa (TLSR8258/ZTU):
//   RX: periferico UART + DMA (drv_uart) no PB7
//   TX: BIT-BANG no PB1 (mesmo mecanismo do printf de debug -
//       comprovadamente funcional neste chip). Contorna toda a
//       cadeia DMA/IRQ de transmissao.
// ============================================================

#define BRIDGE_TX_PIN   GPIO_PB1

static hal_uart_rx_cb_t g_rx_cb = 0;
// 8258: a DMA RX entrega PACOTES inteiros (disparo por fim de recepcao)
// e o buffer precisa caber o maior pacote. 144 e o tamanho correto.
// (16 causou overflow em todo frame — regressao da 1.4.5, revertida.)
static u8 g_rx_dma_buf[144] __attribute__((aligned(4)));
static u32 g_bit_us = 104;  // 9600 -> ~104us por bit

static void uart_rx_irq_handler_cb(void) {
    u32 len = g_rx_dma_buf[0] | (g_rx_dma_buf[1] << 8)
            | (g_rx_dma_buf[2] << 16) | (g_rx_dma_buf[3] << 24);
    if (len > 0 && len <= sizeof(g_rx_dma_buf) - 4 && g_rx_cb) {
        g_rx_cb(&g_rx_dma_buf[4], (uint16_t)len);
    }
}

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_cb) {
    g_rx_cb = rx_cb;
    g_bit_us = (baudrate >= 115200) ? 8 : (1000000 / baudrate);

    // RX no periferico (so o pino de RX importa para ele)
    drv_uart_pin_set(UART_TX_PB1, UART_RX_PB7);
    drv_uart_init(baudrate, g_rx_dma_buf, sizeof(g_rx_dma_buf),
                  uart_rx_irq_handler_cb);

    // PB1 fica MUXADO NA UART (drv_uart_pin_set ja o plugou).
    // NUNCA tomar o pino como GPIO aqui: isso desconecta o periferico
    // e o DMA transmite para o vacuo (o fantasma do TX das 1.2.4-1.4.7).
}

static void tx_byte_bitbang(uint8_t b) {
    // Fallback raro: tomar o pino, transmitir, DEVOLVER ao periferico
    gpio_set_func(BRIDGE_TX_PIN, AS_GPIO);
    gpio_set_input_en(BRIDGE_TX_PIN, 0);
    gpio_set_output_en(BRIDGE_TX_PIN, 1);
    gpio_write(BRIDGE_TX_PIN, 1);
    u8 r = irq_disable();
    gpio_write(BRIDGE_TX_PIN, 0);            // start bit
    sleep_us(g_bit_us);
    for (u8 i = 0; i < 8; i++) {             // LSB first
        gpio_write(BRIDGE_TX_PIN, (b >> i) & 1);
        sleep_us(g_bit_us);
    }
    gpio_write(BRIDGE_TX_PIN, 1);            // stop bit
    sleep_us(g_bit_us);
    irq_restore(r);
    // Devolve o pino ao periferico UART
    drv_uart_pin_set(UART_TX_PB1, UART_RX_PB7);
}

// TX direto no periferico com buffer ESTATICO (formato DMA do 8258:
// 4 bytes de tamanho LE + dados). Sem alocador do stack, sem estado
// do driver — o caminho mais curto entre nos e a MCU.
static u8 g_hw_tx_buf[196] __attribute__((aligned(4)));
static u16 g_tx_hw_count = 0;
static u16 g_tx_fb_count = 0;

u16 hal_uart_tx_hw_count(void) { return g_tx_hw_count; }
u16 hal_uart_tx_fb_count(void) { return g_tx_fb_count; }

void hal_uart_send(const uint8_t *bytes, uint16_t len) {
    if (len > sizeof(g_hw_tx_buf) - 4) return;

    // Espera a transmissao anterior terminar (timeout ~20ms)
    u32 guard = 0;
    while (uart_tx_is_busy() && guard < 20000) {
        sleep_us(1);
        guard++;
    }
    if (uart_tx_is_busy()) {
        // Periferico travado: fallback bit-bang (timing 9600 apenas)
        g_tx_fb_count++;
        for (uint16_t i = 0; i < len; i++) tx_byte_bitbang(bytes[i]);
        return;
    }

    g_hw_tx_buf[0] = (u8)(len);
    g_hw_tx_buf[1] = (u8)(len >> 8);
    g_hw_tx_buf[2] = 0;
    g_hw_tx_buf[3] = 0;
    memcpy(g_hw_tx_buf + 4, bytes, len);
    uart_dma_send(g_hw_tx_buf);
    g_tx_hw_count++;
}

void hal_uart_process(void) {
    drv_uart_exceptionProcess();
}
