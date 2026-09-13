#ifndef TUYA_SERIAL_H
#define TUYA_SERIAL_H

#include <stdint.h>

// ============================================================
// Protocolo serial Tuya ZIGBEE (v0x02) - spec oficial:
// 55 AA | VER=02 | SEQ(2B BE) | CMD | LEN(2B BE) | DATA | CHK
// ============================================================

#define TUYA_ZB_VER              0x02

#define TUYA_CMD_PRODUCT_QUERY   0x01  // modulo->MCU abre a conversa
#define TUYA_CMD_NET_STATUS      0x02  // modulo->MCU (0=off,1=conectado,3=pareando)
#define TUYA_CMD_MCU_RESET_REQ   0x03  // MCU->modulo (data 1 = pedir pareamento)
#define TUYA_CMD_SYNC_NOTIFY     0x07  // modulo->MCU: reporte seus DPs
#define TUYA_CMD_DP_COMMAND      0x08  // modulo->MCU: comando de DP
#define TUYA_CMD_DP_REPORT       0x09  // MCU->modulo: estado de DP (EXIGE ACK)
#define TUYA_CMD_MCU_VERSION     0x0B

#define TUYA_DP_TYPE_RAW         0x00
#define TUYA_DP_TYPE_BOOL        0x01
#define TUYA_DP_TYPE_VALUE       0x02
#define TUYA_DP_TYPE_STRING      0x03
#define TUYA_DP_TYPE_ENUM        0x04

#define TUYA_MAX_FRAME           128

typedef struct {
    uint8_t version;
    uint16_t seq;
    uint8_t command;
    uint16_t data_len;
    uint8_t data[TUYA_MAX_FRAME];
} tuya_frame_t;

typedef void (*tuya_frame_cb_t)(const tuya_frame_t *frame);

void tuya_serial_init(tuya_frame_cb_t on_frame);
void tuya_serial_feed(const uint8_t *bytes, uint16_t len);

uint16_t tuya_serial_build(uint8_t *out, uint16_t seq, uint8_t command,
                           const uint8_t *data, uint16_t data_len);

typedef struct {
    uint8_t id;
    uint8_t type;
    uint16_t len;
    const uint8_t *data;
} tuya_dp_t;

// Itera DPs a partir de start_offset (0 direto; 2 pula endereco three-level)
int tuya_dp_next_from(const tuya_frame_t *frame, uint16_t start_offset,
                      uint16_t *offset, tuya_dp_t *dp);

#endif
