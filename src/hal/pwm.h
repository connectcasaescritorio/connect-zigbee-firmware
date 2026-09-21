#ifndef HAL_PWM_H
#define HAL_PWM_H
#include <stdint.h>
// Inicia PWM num pino, frequencia fixa (~1kHz). duty 0..255.
void hal_pwm_init(uint32_t pin);
void hal_pwm_set_duty(uint32_t pin, uint8_t duty);  // 0=apagado, 255=cheio
#endif
