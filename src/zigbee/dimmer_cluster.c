#include "dimmer_cluster.h"
#include "consts.h"
#include "cluster_common.h"
#include "hal/printf_selector.h"
#include <stddef.h>

static zigbee_dimmer_cluster *dimmer_by_endpoint[16] = {0};

// Aplica o nível no hardware (se houver callback) e mantém o on/off coerente
static void dimmer_apply_level(zigbee_dimmer_cluster *cluster) {
    if (cluster->apply) {
        cluster->apply(cluster->on ? cluster->current_level : 0);
    }
    printf("dimmer ep%d: on=%d level=%d\r\n",
           cluster->endpoint, cluster->on, cluster->current_level);
}

void dimmer_cluster_set_level(zigbee_dimmer_cluster *cluster, uint8_t level) {
    if (level == 0) {
        cluster->on = 0;
    } else {
        cluster->current_level = level;
        cluster->on = 1;
    }
    dimmer_apply_level(cluster);
    dimmer_cluster_report(cluster);
}

void dimmer_cluster_on(zigbee_dimmer_cluster *cluster) {
    cluster->on = 1;
    if (cluster->current_level == 0) cluster->current_level = 254;
    dimmer_apply_level(cluster);
    dimmer_cluster_report(cluster);
}

void dimmer_cluster_off(zigbee_dimmer_cluster *cluster) {
    cluster->on = 0;
    dimmer_apply_level(cluster);
    dimmer_cluster_report(cluster);
}

void dimmer_cluster_report(zigbee_dimmer_cluster *cluster) {
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF);
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_LEVEL_CONTROL,
                                        ZCL_ATTR_CURRENT_LEVEL);
}

// ===== Callbacks de comando =====
static hal_zigbee_cmd_result_t onoff_cb(zigbee_dimmer_cluster *cluster,
                                        uint8_t command_id) {
    switch (command_id) {
    case 0x00: dimmer_cluster_off(cluster); break;   // Off
    case 0x01: dimmer_cluster_on(cluster);  break;   // On
    case 0x02:                                        // Toggle
        if (cluster->on) dimmer_cluster_off(cluster);
        else dimmer_cluster_on(cluster);
        break;
    default: return HAL_ZIGBEE_CMD_SKIPPED;
    }
    return HAL_ZIGBEE_CMD_PROCESSED;
}

static hal_zigbee_cmd_result_t level_cb(zigbee_dimmer_cluster *cluster,
                                        uint8_t command_id,
                                        void *payload, uint16_t len) {
    switch (command_id) {
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF:
        if (payload == NULL || len < 1) return HAL_ZIGBEE_MALFORMED_COMMAND;
        dimmer_cluster_set_level(cluster, *(uint8_t *)payload);
        break;
    default:
        printf("dimmer: level cmd %d ignorado\r\n", command_id);
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    return HAL_ZIGBEE_CMD_PROCESSED;
}

static hal_zigbee_cmd_result_t onoff_trampoline(uint8_t endpoint, uint16_t cid,
                                                uint8_t command_id, void *p,
                                                uint16_t len) {
    (void)cid; (void)p; (void)len;
    return onoff_cb(dimmer_by_endpoint[endpoint], command_id);
}
static hal_zigbee_cmd_result_t level_trampoline(uint8_t endpoint, uint16_t cid,
                                                uint8_t command_id, void *p,
                                                uint16_t len) {
    (void)cid;
    return level_cb(dimmer_by_endpoint[endpoint], command_id, p, len);
}

void dimmer_cluster_add_to_endpoint(zigbee_dimmer_cluster *cluster,
                                    hal_zigbee_endpoint *endpoint) {
    dimmer_by_endpoint[endpoint->endpoint] = cluster;
    cluster->endpoint = endpoint->endpoint;
    if (cluster->current_level == 0) cluster->current_level = 254;

    // Cluster On/Off (0x0006)
    SETUP_ATTR_FOR_TABLE(cluster->onoff_attrs, 0, ZCL_ATTR_ONOFF,
                         ZCL_DATA_TYPE_BOOLEAN, ATTR_READONLY, cluster->on);
    SETUP_ATTR_FOR_TABLE(cluster->onoff_attrs, 1, ZCL_ATTR_START_UP_ONOFF,
                         ZCL_DATA_TYPE_ENUM8, ATTR_WRITABLE, cluster->on);
    endpoint->clusters[endpoint->cluster_count].cluster_id      = ZCL_CLUSTER_ON_OFF;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 2;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->onoff_attrs;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->clusters[endpoint->cluster_count].cmd_callback    = onoff_trampoline;
    endpoint->cluster_count++;

    // Cluster Level Control (0x0008)
    SETUP_ATTR_FOR_TABLE(cluster->level_attrs, 0, ZCL_ATTR_CURRENT_LEVEL,
                         ZCL_DATA_TYPE_UINT8, ATTR_READONLY, cluster->current_level);
    SETUP_ATTR_FOR_TABLE(cluster->level_attrs, 1, ZCL_ATTR_LEVEL_MIN,
                         ZCL_DATA_TYPE_UINT8, ATTR_READONLY, cluster->current_level);
    SETUP_ATTR_FOR_TABLE(cluster->level_attrs, 2, ZCL_ATTR_LEVEL_MAX,
                         ZCL_DATA_TYPE_UINT8, ATTR_READONLY, cluster->current_level);
    SETUP_ATTR_FOR_TABLE(cluster->level_attrs, 3, ZCL_ATTR_LEVEL_STARTUP,
                         ZCL_DATA_TYPE_UINT8, ATTR_WRITABLE, cluster->startup_level);
    endpoint->clusters[endpoint->cluster_count].cluster_id      = ZCL_CLUSTER_LEVEL_CONTROL;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 4;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->level_attrs;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->clusters[endpoint->cluster_count].cmd_callback    = level_trampoline;
    endpoint->cluster_count++;
}
