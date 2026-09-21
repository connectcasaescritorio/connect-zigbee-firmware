#include "hal/pwm.h"
#ifdef MCU_CORE_8258
#include "tl_common.h"

// TLSR8258: PWM0..5. Mapeamos o pino pro canal PWM correspondente.
// Frequencia ~1kHz: com clock de 24MHz, max_tick = 24000 (24MHz/1kHz).
#define PWM_MAX_TICK 24000

static int pin_to_pwm_id(uint32_t pin) {
    // Mapa comum dos pinos PWM do 8258 (ajustavel).
    switch (pin) {
    case GPIO_PD2: return 0;  // PWM0
    case GPIO_PD3: return 1;  // PWM1
    case GPIO_PD4: return 2;  // PWM2
    case GPIO_PD5: return 3;  // PWM3
    case GPIO_PD6: return 4;  // PWM4
    case GPIO_PD7: return 5;  // PWM5
    case GPIO_PB4: return 0;
    case GPIO_PB5: return 1;
    case GPIO_PC0: return 2;
    case GPIO_PC1: return 3;
    case GPIO_PC2: return 4;
    case GPIO_PC3: return 5;
    default:       return 0;
    }
}

void hal_pwm_init(uint32_t pin) {
    int id = pin_to_pwm_id(pin);
    gpio_set_func(pin, AS_PWM0_N + id);  // pino como saida PWM
    pwm_set_clk(CLOCK_SYS_CLOCK_HZ, CLOCK_SYS_CLOCK_HZ);
    pwm_set_cycle_and_duty(id, PWM_MAX_TICK, 0);
    pwm_start(id);
}

void hal_pwm_set_duty(uint32_t pin, uint8_t duty) {
    int id = pin_to_pwm_id(pin);
    uint32_t cmp = ((uint32_t)duty * PWM_MAX_TICK) / 255;
    pwm_set_cycle_and_duty(id, PWM_MAX_TICK, (uint16_t)cmp);
}
#else
void hal_pwm_init(uint32_t pin) { (void)pin; }
void hal_pwm_set_duty(uint32_t pin, uint8_t duty) { (void)pin; (void)duty; }
#endif
