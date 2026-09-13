#include "tuya_serial.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static tuya_frame_t last;
static int frames = 0;
static void on_frame(const tuya_frame_t *f) { last = *f; frames++; }

int main(void) {
    tuya_serial_init(on_frame);

    // 1. Heartbeat resposta da MCU: 55 AA 00 00 00 01 01 chk(01+55+AA=0x01... )
    uint8_t hb[] = {0x55, 0xAA, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01};
    tuya_serial_feed(hb, sizeof(hb));
    assert(frames == 1 && last.command == 0x00 && last.data_len == 1);

    // 2. DP report do rele 1 ligado (DP 24 bool=1), alimentado byte a byte
    uint8_t rep[] = {0x55, 0xAA, 0x00, 0x07, 0x00, 0x05,
                     24, 0x01, 0x00, 0x01, 0x01, 0x26};
    for (unsigned i = 0; i < sizeof(rep); i++) tuya_serial_feed(&rep[i], 1);
    assert(frames == 2 && last.command == TUYA_CMD_DP_REPORT);
    uint16_t off = 0; tuya_dp_t dp;
    assert(tuya_dp_next(&last, &off, &dp) == 1);
    assert(dp.id == 24 && dp.type == TUYA_DP_TYPE_BOOL && dp.data[0] == 1);
    assert(tuya_dp_next(&last, &off, &dp) == 0);

    // 3. Checksum invalido = frame descartado
    uint8_t bad[] = {0x55, 0xAA, 0x00, 0x07, 0x00, 0x05,
                     24, 0x01, 0x00, 0x01, 0x01, 0xFF};
    tuya_serial_feed(bad, sizeof(bad));
    assert(frames == 2);

    // 4. Lixo antes do frame = resincroniza
    uint8_t noise[] = {0x13, 0x37, 0x55, 0x11};
    tuya_serial_feed(noise, sizeof(noise));
    tuya_serial_feed(hb, sizeof(hb));
    assert(frames == 3);

    // 5. Build de comando: ligar rele 2 (DP 25) e re-parsear
    uint8_t out[32];
    uint16_t n = tuya_serial_build_dp_bool(out, 25, 1);
    tuya_serial_feed(out, n);
    assert(frames == 4 && last.command == TUYA_CMD_DP_CMD);
    off = 0;
    assert(tuya_dp_next(&last, &off, &dp) == 1 && dp.id == 25 && dp.data[0] == 1);

    // 6. Frame com MULTIPLOS DPs (relatorio de estado completo)
    uint8_t multi_data[] = {24, 0x01, 0x00, 0x01, 0x01,
                            25, 0x01, 0x00, 0x01, 0x00,
                            101, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 50};
    uint8_t multi[64];
    n = tuya_serial_build(multi, 0x00, TUYA_CMD_DP_REPORT, multi_data, sizeof(multi_data));
    tuya_serial_feed(multi, n);
    assert(frames == 5);
    off = 0;
    int count = 0; uint8_t ids[3];
    while (tuya_dp_next(&last, &off, &dp)) ids[count++] = dp.id;
    assert(count == 3 && ids[0] == 24 && ids[1] == 25 && ids[2] == 101);

    printf("tuya_serial: 6/6 testes OK\n");
    return 0;
}
