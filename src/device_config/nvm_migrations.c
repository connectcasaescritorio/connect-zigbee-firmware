#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "nvm_items.h"
#include "zigbee/switch_cluster.h"

#ifdef HAL_SILABS
#include "silabs_config.h"
#endif

#define UNKNOWN_VERSION    0
#define MAX_SWITCH_NV_ITEMS    10

// Layout of zigbee_switch_cluster_config before multi_click was added (v1)
typedef struct {
    uint8_t  mode;
    uint8_t  action;
    uint8_t  relay_mode;
    uint8_t  relay_index;
    uint16_t button_long_press_duration;
    uint8_t  level_move_rate;
    uint8_t  binded_mode;
} switch_cluster_config_v1;

// v2: switch config gained the multi_click field. Rewrite every stored
// switch item in the new layout, defaulting multi_click to 0 (classic).
static void migrate_to_v2() {
    switch_cluster_config_v1     old_cfg;
    zigbee_switch_cluster_config new_cfg;

    for (uint8_t i = 0; i < MAX_SWITCH_NV_ITEMS; i++) {
        hal_nvm_status_t st =
            hal_nvm_read(NV_ITEM_SWITCH_CLUSTER_DATA(i), sizeof(old_cfg),
                         (uint8_t *)&old_cfg);
        if (st != HAL_NVM_SUCCESS) {
            continue;
        }
        new_cfg.mode                       = old_cfg.mode;
        new_cfg.action                     = old_cfg.action;
        new_cfg.relay_mode                 = old_cfg.relay_mode;
        new_cfg.relay_index                = old_cfg.relay_index;
        new_cfg.button_long_press_duration = old_cfg.button_long_press_duration;
        new_cfg.level_move_rate            = old_cfg.level_move_rate;
        new_cfg.binded_mode                = old_cfg.binded_mode;
        new_cfg.multi_click                = 0;
        hal_nvm_write(NV_ITEM_SWITCH_CLUSTER_DATA(i), sizeof(new_cfg),
                      (uint8_t *)&new_cfg);
        printf("Migrated switch %d config to v2\r\n", i);
    }
}

uint16_t read_version_in_nv() {
    uint16_t version;

    hal_nvm_status_t res = hal_nvm_read(NV_ITEM_CURRENT_VERSION_IN_NV,
                                        sizeof(version), (uint8_t *)&version);

    if (res == HAL_NVM_SUCCESS) {
        printf("read version form new location\r\n");
        return version;
    }

    return UNKNOWN_VERSION;
}

void write_version_to_nv(uint16_t version) {
    hal_nvm_status_t res = hal_nvm_write(NV_ITEM_CURRENT_VERSION_IN_NV,
                                         sizeof(version), (uint8_t *)&version);

    if (res != HAL_NVM_SUCCESS) {
        printf("Failed to write lastSeenVersion to NV, st: %d\r\n", res);
    }
}

void handle_version_changes() {
    uint16_t oldVersion     = read_version_in_nv();
    uint16_t currentVersion = NVM_MIGRATIONS_VERSION;

    printf("Old version: %d\r\n", oldVersion);
    printf("Current version: %d\r\n", currentVersion);

    if (oldVersion == currentVersion) {
        // Same version, nothing to do
        return;
    }

    if (oldVersion == UNKNOWN_VERSION) {
        // Either old device or it first boot after re-flash, just store version
        write_version_to_nv(currentVersion);
        return;
    }

    // Handle migrations here
    if (oldVersion < 2) {
        migrate_to_v2();
    }

    write_version_to_nv(currentVersion);
}
