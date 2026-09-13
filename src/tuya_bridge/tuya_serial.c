#include "tuya_serial.h"
#include <string.h>

static tuya_frame_cb_t g_on_frame = 0;

// Maquina de estados do parser
static enum {
    ST_H1, ST_H2, ST_VER, ST_CMD, ST_LEN_HI, ST_LEN_LO, ST_DATA, ST_CHK
} g_state = ST_H1;

static tuya_frame_t g_frame;
static uint16_t g_data_pos = 0;
static uint8_t  g_sum = 0;

void tuya_serial_init(tuya_frame_cb_t on_frame) {
    g_on_frame = on_frame;
    g_state = ST_H1;
}

static void reset_parser(void) {
    g_state = ST_H1;
    g_data_pos = 0;
    g_sum = 0;
}

void tuya_serial_feed(const uint8_t *bytes, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        uint8_t b = bytes[i];
        switch (g_state) {
        case ST_H1:
            if (b == 0x55) { g_sum = 0x55; g_state = ST_H2; }
            break;
        case ST_H2:
            if (b == 0xAA) { g_sum += b; g_state = ST_VER; }
            else reset_parser();
            break;
        case ST_VER:
            g_frame.version = b; g_sum += b; g_state = ST_CMD;
            break;
        case ST_CMD:
            g_frame.command = b; g_sum += b; g_state = ST_LEN_HI;
            break;
        case ST_LEN_HI:
            g_frame.data_len = ((uint16_t)b) << 8; g_sum += b;
            g_state = ST_LEN_LO;
            break;
        case ST_LEN_LO:
            g_frame.data_len |= b; g_sum += b;
            if (g_frame.data_len > TUYA_MAX_FRAME) { reset_parser(); break; }
            g_data_pos = 0;
            g_state = (g_frame.data_len == 0) ? ST_CHK : ST_DATA;
            break;
        case ST_DATA:
            g_frame.data[g_data_pos++] = b; g_sum += b;
            if (g_data_pos >= g_frame.data_len) g_state = ST_CHK;
            break;
        case ST_CHK:
            if (b == g_sum && g_on_frame) {
                g_on_frame(&g_frame);
            }
            reset_parser();
            break;
        }
    }
}

uint16_t tuya_serial_build(uint8_t *out, uint8_t version, uint8_t command,
                           const uint8_t *data, uint16_t data_len) {
    uint16_t n = 0;
    out[n++] = 0x55;
    out[n++] = 0xAA;
    out[n++] = version;
    out[n++] = command;
    out[n++] = (uint8_t)(data_len >> 8);
    out[n++] = (uint8_t)(data_len & 0xFF);
    for (uint16_t i = 0; i < data_len; i++) out[n++] = data[i];
    uint8_t sum = 0;
    for (uint16_t i = 0; i < n; i++) sum += out[i];
    out[n++] = sum;
    return n;
}

uint16_t tuya_serial_build_dp_bool(uint8_t *out, uint8_t dp_id, uint8_t value) {
    uint8_t d[5] = {dp_id, TUYA_DP_TYPE_BOOL, 0x00, 0x01, value ? 1 : 0};
    return tuya_serial_build(out, 0x00, TUYA_CMD_DP_CMD, d, 5);
}

uint16_t tuya_serial_build_dp_enum(uint8_t *out, uint8_t dp_id, uint8_t value) {
    uint8_t d[5] = {dp_id, TUYA_DP_TYPE_ENUM, 0x00, 0x01, value};
    return tuya_serial_build(out, 0x00, TUYA_CMD_DP_CMD, d, 5);
}

uint16_t tuya_serial_build_dp_value(uint8_t *out, uint8_t dp_id, uint32_t value) {
    uint8_t d[8] = {dp_id, TUYA_DP_TYPE_VALUE, 0x00, 0x04,
                    (uint8_t)(value >> 24), (uint8_t)(value >> 16),
                    (uint8_t)(value >> 8), (uint8_t)(value)};
    return tuya_serial_build(out, 0x00, TUYA_CMD_DP_CMD, d, 8);
}

int tuya_dp_next(const tuya_frame_t *frame, uint16_t *offset, tuya_dp_t *dp) {
    uint16_t off = *offset;
    if (off + 4 > frame->data_len) return 0;
    dp->id   = frame->data[off];
    dp->type = frame->data[off + 1];
    dp->len  = ((uint16_t)frame->data[off + 2] << 8) | frame->data[off + 3];
    if (off + 4 + dp->len > frame->data_len) return 0;
    dp->data = &frame->data[off + 4];
    *offset = off + 4 + dp->len;
    return 1;
}
