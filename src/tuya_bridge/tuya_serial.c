#include "tuya_serial.h"
#include <string.h>

static tuya_frame_cb_t g_on_frame = 0;

static enum {
    ST_H1, ST_H2, ST_VER, ST_SEQ_HI, ST_SEQ_LO, ST_CMD,
    ST_LEN_HI, ST_LEN_LO, ST_DATA, ST_CHK
} g_state = ST_H1;

static tuya_frame_t g_frame;
static uint16_t g_data_pos = 0;
static uint8_t  g_sum = 0;

void tuya_serial_init(tuya_frame_cb_t on_frame) {
    g_on_frame = on_frame;
    g_state = ST_H1;
    g_data_pos = 0;
    g_sum = 0;
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
            g_frame.version = b; g_sum += b; g_state = ST_SEQ_HI;
            break;
        case ST_SEQ_HI:
            g_frame.seq = ((uint16_t)b) << 8; g_sum += b; g_state = ST_SEQ_LO;
            break;
        case ST_SEQ_LO:
            g_frame.seq |= b; g_sum += b; g_state = ST_CMD;
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

uint16_t tuya_serial_build(uint8_t *out, uint16_t seq, uint8_t command,
                           const uint8_t *data, uint16_t data_len) {
    uint16_t n = 0;
    out[n++] = 0x55;
    out[n++] = 0xAA;
    out[n++] = TUYA_ZB_VER;
    out[n++] = (uint8_t)(seq >> 8);
    out[n++] = (uint8_t)(seq & 0xFF);
    out[n++] = command;
    out[n++] = (uint8_t)(data_len >> 8);
    out[n++] = (uint8_t)(data_len & 0xFF);
    for (uint16_t i = 0; i < data_len; i++) out[n++] = data[i];
    uint8_t sum = 0;
    for (uint16_t i = 0; i < n; i++) sum += out[i];
    out[n++] = sum;
    return n;
}

int tuya_dp_next_from(const tuya_frame_t *frame, uint16_t start_offset,
                      uint16_t *offset, tuya_dp_t *dp) {
    uint16_t off = (*offset < start_offset) ? start_offset : *offset;
    if (off + 4 > frame->data_len) return 0;
    dp->id   = frame->data[off];
    dp->type = frame->data[off + 1];
    dp->len  = ((uint16_t)frame->data[off + 2] << 8) | frame->data[off + 3];
    if (off + 4 + dp->len > frame->data_len) return 0;
    dp->data = &frame->data[off + 4];
    *offset = off + 4 + dp->len;
    return 1;
}
