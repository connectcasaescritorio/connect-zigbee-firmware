#include "hal/uart.h"
#include <string.h>

// Stub UART: guarda TX para inspecao; RX injetavel em testes futuros
static hal_uart_rx_cb_t g_rx_cb = 0;
uint8_t stub_uart_tx_buf[512];
uint16_t stub_uart_tx_len = 0;

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_cb) {
    (void)baudrate;
    g_rx_cb = rx_cb;
    stub_uart_tx_len = 0;
}

void hal_uart_send(const uint8_t *bytes, uint16_t len) {
    if (stub_uart_tx_len + len <= sizeof(stub_uart_tx_buf)) {
        memcpy(stub_uart_tx_buf + stub_uart_tx_len, bytes, len);
        stub_uart_tx_len += len;
    }
}

void stub_uart_inject_rx(const uint8_t *bytes, uint16_t len) {
    if (g_rx_cb) g_rx_cb(bytes, len);
}

void hal_uart_process(void) {}
