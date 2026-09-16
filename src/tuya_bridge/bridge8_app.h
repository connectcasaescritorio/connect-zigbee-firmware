#ifndef BRIDGE8_APP_H
#define BRIDGE8_APP_H
#include <stdint.h>
void bridge8_app_init(void);
uint8_t bridge8_app_active(void);
void bridge8_on_relay_change(uint8_t relay_index, uint8_t state);
void bridge8_set_backlight(uint8_t mode);
const char *bridge8_dplog(void);
#endif
