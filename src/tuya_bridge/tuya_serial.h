#ifndef TUYA_SERIAL_H
#define TUYA_SERIAL_H

#include <stdint.h>

// ============================================================
// ConnectCasa tuya_mcu_bridge - parser do protocolo serial Tuya
// Frames: 55 AA <ver> <cmd> <len_hi> <len_lo> <data...> <chk>
// chk = soma de todos os bytes anteriores, mod 256
// ============================================================

#define TUYA_CMD_HEARTBEAT       0x00
#define TUYA_CMD_PRODUCT_INFO    0x01
#define TUYA_CMD_MCU_CONF        0x02
#define TUYA_CMD_WIFI_STATE      0x03
#define TUYA_CMD_RESET           0x04
#define TUYA_CMD_DP_CMD          0x06  // nos -> MCU (comando de DP)
#define TUYA_CMD_DP_REPORT       0x07  // MCU -> nos (estado de DP)
#define TUYA_CMD_QUERY_DPS       0x08

#define TUYA_DP_TYPE_RAW         0x00
#define TUYA_DP_TYPE_BOOL        0x01
#define TUYA_DP_TYPE_VALUE       0x02
#define TUYA_DP_TYPE_STRING      0x03
#define TUYA_DP_TYPE_ENUM        0x04

#define TUYA_MAX_FRAME           128

typedef struct {
    uint8_t version;
    uint8_t command;
    uint16_t data_len;
    uint8_t data[TUYA_MAX_FRAME];
} tuya_frame_t;

// Callback chamado a cada frame valido recebido
typedef void (*tuya_frame_cb_t)(const tuya_frame_t *frame);

void tuya_serial_init(tuya_frame_cb_t on_frame);

// Alimenta o parser com bytes recebidos da UART (qualquer fragmentacao)
void tuya_serial_feed(const uint8_t *bytes, uint16_t len);

// Monta um frame no buffer out (retorna o tamanho total)
uint16_t tuya_serial_build(uint8_t *out, uint8_t version, uint8_t command,
                           const uint8_t *data, uint16_t data_len);

// Atalho: monta um comando de DP (cmd 0x06)
uint16_t tuya_serial_build_dp_bool(uint8_t *out, uint8_t dp_id, uint8_t value);
uint16_t tuya_serial_build_dp_enum(uint8_t *out, uint8_t dp_id, uint8_t value);
uint16_t tuya_serial_build_dp_value(uint8_t *out, uint8_t dp_id, uint32_t value);

// Helpers de leitura de um DP dentro de um frame DP_REPORT
// (um frame pode conter varios DPs em sequencia)
typedef struct {
    uint8_t id;
    uint8_t type;
    uint16_t len;
    const uint8_t *data;
} tuya_dp_t;

// Itera DPs: *offset comeca em 0; retorna 1 enquanto extrair um DP valido
int tuya_dp_next(const tuya_frame_t *frame, uint16_t *offset, tuya_dp_t *dp);

#endif
