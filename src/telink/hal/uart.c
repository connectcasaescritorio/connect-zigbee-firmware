#include "hal/uart.h"
#include "tl_common.h"
#include "drivers/drv_uart.h"

// ============================================================
// UART Telink (TLSR8258/ZTU) - 9600 8N1 nos pinos do modulo:
// TX = PB1, RX = PB7 (o barramento DP para a MCU Tuya)
// Formato DMA do SDK: rxBuf[0..3] = tamanho recebido (LE), dados em [4..]
// ============================================================

static hal_uart_rx_cb_t g_rx_cb = 0;
static u8 g_rx_dma_buf[144] __attribute__((aligned(4)));

static void uart_rx_irq_handler(void) {
    u32 len = g_rx_dma_buf[0] | (g_rx_dma_buf[1] << 8)
            | (g_rx_dma_buf[2] << 16) | (g_rx_dma_buf[3] << 24);
    if (len > 0 && len <= sizeof(g_rx_dma_buf) - 4 && g_rx_cb) {
        g_rx_cb(&g_rx_dma_buf[4], (uint16_t)len);
    }
}

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_cb) {
    g_rx_cb = rx_cb;
    drv_uart_pin_set(UART_TX_PB1, UART_RX_PB7);
    drv_uart_init(baudrate, g_rx_dma_buf, sizeof(g_rx_dma_buf),
                  uart_rx_irq_handler);
}

static u8 g_tx_buf[160] __attribute__((aligned(4)));

void hal_uart_send(const uint8_t *bytes, uint16_t len) {
    if (len > sizeof(g_tx_buf)) return;
    memcpy(g_tx_buf, bytes, len);
    drv_uart_tx_start(g_tx_buf, len);
}

void hal_uart_process(void) {
    drv_uart_exceptionProcess();
}
