#ifndef DEVICE_CONFIG_DEVICE_PARAMS_NV_H_
#define DEVICE_CONFIG_DEVICE_PARAMS_NV_H_

#include <stdint.h>

/*
 * Global device parameter: multi-press reset count.
 *   0  => multi-press factory reset disabled
 *   N  => factory-reset after N consecutive presses
 * Default: 10, persisted in NVM.
 */

extern uint8_t g_multi_press_reset_count;

// Backlight das teclas: 0 = desligado, 1 = ligado/tradicional (azul no
// repouso, apaga quando o rele arma), 2 = rosa (azul sempre aceso)
#define BACKLIGHT_MODE_OFF          0
#define BACKLIGHT_MODE_TRADITIONAL  1
#define BACKLIGHT_MODE_ROSA         2
extern uint8_t g_backlight_mode;
void device_params_set_backlight_mode(uint8_t value);

void device_params_load_from_nv(void);
void device_params_set_multi_press_reset_count(uint8_t value);

#endif /* DEVICE_CONFIG_DEVICE_PARAMS_NV_H_ */
