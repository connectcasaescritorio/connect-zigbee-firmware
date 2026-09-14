#ifndef BRIDGE_APP_H
#define BRIDGE_APP_H

#include <stdint.h>

void bridge_app_init(void);
void radar_app_init(void);
unsigned char radar_status(void);
unsigned short radar_result(void);
void radar_step(void);
uint8_t bridge_app_active(void);
// Hook chamado pelo relay_cluster (sempre linkado; inerte fora do modo ponte)
void bridge_on_relay_change(uint8_t relay_index, uint8_t state);
void bridge_on_mcu_reset_request(void);
void bridge_ui_backlight(unsigned char on);
void bridge_ui_brightness(unsigned char pct);
void bridge_ui_mode(unsigned char ch, unsigned char mode);

#endif
