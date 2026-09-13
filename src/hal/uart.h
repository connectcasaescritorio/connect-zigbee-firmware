#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>

typedef void (*hal_uart_rx_cb_t)(const uint8_t *bytes, uint16_t len);

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_cb);
void hal_uart_send(const uint8_t *bytes, uint16_t len);
void hal_uart_process(void);  // limpar excecoes de RX (chamar periodicamente)
unsigned short hal_uart_tx_hw_count(void);
unsigned short hal_uart_tx_fb_count(void);

#endif
