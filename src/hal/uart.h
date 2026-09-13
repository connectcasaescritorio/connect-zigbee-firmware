#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>

typedef void (*hal_uart_rx_cb_t)(const uint8_t *bytes, uint16_t len);

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_cb);
void hal_uart_send(const uint8_t *bytes, uint16_t len);

#endif
