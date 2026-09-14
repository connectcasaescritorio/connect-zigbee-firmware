#ifndef BRIDGE_CORE_H
#define BRIDGE_CORE_H

#include <stdint.h>
#include "tuya_serial.h"

// ============================================================
// ConnectCasa tuya_mcu_bridge - nucleo da ponte ZTU <-> MCU
// Handshake: HEARTBEAT -> PRODUCT_INFO -> MCU_CONF -> WIFI_STATE
//            -> QUERY_DPS -> OPERACIONAL
// ============================================================

typedef enum {
    BR_ST_BOOT = 0,
    BR_ST_WAIT_HEARTBEAT,
    BR_ST_WAIT_PRODUCT,
    BR_ST_WAIT_CONF,
    BR_ST_OPERATIONAL,
} bridge_state_t;

// Saida de bytes para a UART (implementada pelo firmware/teste)
typedef void (*bridge_tx_t)(const uint8_t *bytes, uint16_t len);
// Notificacao de DP recebido da MCU (relatorio de estado/evento)
typedef void (*bridge_dp_cb_t)(const tuya_dp_t *dp);

void bridge_init(bridge_tx_t tx, bridge_dp_cb_t on_dp);
// Alimentar com bytes vindos da UART
void bridge_rx(const uint8_t *bytes, uint16_t len);
// Chamar periodicamente (a cada ~100ms); envia heartbeat/avanca handshake
void bridge_tick_100ms(void);

bridge_state_t bridge_state(void);
uint16_t bridge_rx_frame_count(void);
uint8_t bridge_last_rx_cmd(void);
const char *bridge_last_frame_hex(void);
void bridge_set_cmd_verb(uint8_t v);
uint8_t bridge_get_cmd_verb(void);
const char *bridge_product_info(void);  // JSON da MCU (apos handshake)

// Comandos de saida (ponte -> MCU)
void bridge_set_dp_bool(uint8_t dp_id, uint8_t value);
void bridge_set_dp_enum(uint8_t dp_id, uint8_t value);
void bridge_set_dp_value(uint8_t dp_id, uint32_t value);
void bridge_query_all_dps(void);

#endif
