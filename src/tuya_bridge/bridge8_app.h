#ifndef BRIDGE8_APP_H
#define BRIDGE8_APP_H
#include <stdint.h>
void bridge8_app_init(void);
uint8_t bridge8_app_active(void);
void bridge8_on_relay_change(uint8_t relay_index, uint8_t state);
void bridge8_set_backlight(uint8_t mode);
void bridge8_poke_dp(uint8_t dp_id, uint8_t value);
void bridge8_scan_start(uint8_t on);
uint8_t bridge8_scan_current_dp(void);
const char *bridge8_dplog(void);
#endif
