#ifndef _DIMMER_CLUSTER_H_
#define _DIMMER_CLUSTER_H_

#include "hal/zigbee.h"
#include <stdint.h>

// Callback que aplica o brilho no hardware (corte de fase / PWM).
// level: 1..254 (0 = desligado). Implementado pelo firmware/HAL.
typedef void (*dimmer_apply_t)(uint8_t level);

typedef struct {
    uint8_t              endpoint;
    uint8_t              on;            // genOnOff
    uint8_t              current_level; // genLevelCtrl (1..254)
    uint8_t              startup_level;
    hal_zigbee_attribute onoff_attrs[2];
    hal_zigbee_attribute level_attrs[4];
    dimmer_apply_t       apply;         // aplica no hardware
} zigbee_dimmer_cluster;

void dimmer_cluster_add_to_endpoint(zigbee_dimmer_cluster *cluster,
                                    hal_zigbee_endpoint *endpoint);
void dimmer_cluster_set_level(zigbee_dimmer_cluster *cluster, uint8_t level);
void dimmer_cluster_on(zigbee_dimmer_cluster *cluster);
void dimmer_cluster_off(zigbee_dimmer_cluster *cluster);
void dimmer_cluster_report(zigbee_dimmer_cluster *cluster);

#endif
