#ifndef _BASIC_CLUSTER_H_
#define _BASIC_CLUSTER_H_

#include "hal/zigbee.h"

#include <stddef.h>

typedef struct {
    uint8_t              deviceEnable;
    char                 manuName[32];
    char                 modelId[32];
    hal_zigbee_attribute attr_infos[31];
} zigbee_basic_cluster;

void basic_cluster_update_bridge_diag(uint8_t state, uint16_t rx_frames);
void basic_cluster_update_bridge_last_cmd(uint8_t cmd);
void basic_cluster_update_bridge_frame(const char *hex);
void basic_cluster_update_bridge_tx(uint16_t counts);
void basic_cluster_update_bridge_backlight(uint8_t on);
void basic_cluster_update_bridge_brightness(uint8_t pct);
void basic_cluster_update_bridge_mode(uint8_t ch, uint8_t m);
void basic_cluster_update_bridge_unknown(const char *txt);
void basic_cluster_add_to_endpoint(zigbee_basic_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint);

void basic_cluster_callback_attr_write_trampoline(uint16_t attribute_id);

#endif
