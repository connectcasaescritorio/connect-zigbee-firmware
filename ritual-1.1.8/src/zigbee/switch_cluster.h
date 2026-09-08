#ifndef _SWITCH_CLUSTER_H_
#define _SWITCH_CLUSTER_H_

#include "base_components/button.h"
#include "base_components/led.h"
#include "hal/zigbee.h"
#include <stdint.h>

typedef struct {
    uint8_t  mode;
    uint8_t  action;
    uint8_t  relay_mode;
    uint8_t  relay_index;
    uint16_t button_long_press_duration;
    uint8_t  level_move_rate;
    uint8_t  binded_mode;
    uint8_t  multi_click;
} zigbee_switch_cluster_config;

typedef struct {
    uint8_t              switch_idx;
    uint8_t              endpoint;
    uint8_t              mode;
    uint8_t              action;
    uint8_t              relay_mode;
    uint8_t              relay_index;
    uint8_t              binded_mode;
    uint8_t              multi_click;
    button_t *           button;
    hal_zigbee_attribute attr_infos[9];
    uint16_t             multistate_state;
    hal_zigbee_attribute multistate_attr_infos[4];
    uint8_t              level_move_rate;
    uint8_t              level_move_direction;
    led_t *              indicator_led;
} zigbee_switch_cluster;

// Apply a new switch mode (toggle/momentary/momentary_nc) with all side
// effects (multistate sync, button polarity) and persist it.
void switch_cluster_apply_mode(zigbee_switch_cluster *cluster, uint8_t mode);

void switch_cluster_add_to_endpoint(zigbee_switch_cluster *cluster,
                                    hal_zigbee_endpoint *endpoint);

void switch_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                   uint16_t attribute_id);

void update_switch_clusters(void);

#endif
