#include "reset.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "hal/system.h"
#include "device_config/nvm_items.h"
#include "hal/tasks.h"
#include <stdint.h>

static hal_task_t reset_task;

__attribute__((noreturn)) void reset_all() {
    printf("RESET ALL!\r\n");
    hal_nvm_delete(NV_ITEM_DEVICE_CONFIG);  // apaga a config EXPLICITAMENTE
    hal_nvm_clear_all();
    hal_factory_reset();
    hal_system_reset();
}

void reset_all_handler(void *arg) {
    reset_all();
}

void reboot_handler(void *arg) {
    hal_system_reset();
}

void schedule_full_reset(uint16_t delay_ms) {
    reset_task.handler = reset_all_handler;
    hal_tasks_init(&reset_task);
    hal_tasks_schedule(&reset_task,
                       delay_ms != 0 ? delay_ms : DEFAULT_RESET_DELAY_MS);
}

void schedule_reboot(uint16_t delay_ms) {
    reset_task.handler = reboot_handler;
    hal_tasks_init(&reset_task);
    hal_tasks_schedule(&reset_task,
                       delay_ms != 0 ? delay_ms : DEFAULT_RESET_DELAY_MS);
}

void reset_now_handler(void *arg) {
    // RESET_NOW adiado: limpa a NVM de rede e sai da rede (hal_factory_reset
    // faz o leave e reinicia sozinho). NAO chamar system_reset depois - isso
    // cortava o leave antes de completar. NAO apaga a device_config, pra peca
    // reler a string ao reparear.
    printf("RESET NOW (agendado): clear_all + factory_reset com leave\r\n");
    hal_nvm_clear_all();
    hal_factory_reset();
}

void schedule_reset_now(uint16_t delay_ms) {
    reset_task.handler = reset_now_handler;
    hal_tasks_init(&reset_task);
    hal_tasks_schedule(&reset_task,
                       delay_ms != 0 ? delay_ms : DEFAULT_RESET_DELAY_MS);
}
