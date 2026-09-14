#include "bridge_core.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static uint8_t mcu_rx[512]; static int mcu_rx_len = 0;
static tuya_dp_t last_dp; static int dps = 0;

static void fake_tx(const uint8_t *b, uint16_t n) {
    memcpy(mcu_rx + mcu_rx_len, b, n); mcu_rx_len += n;
}
static void on_dp(const tuya_dp_t *dp) { last_dp = *dp; dps++; }

static void mcu_reply(uint16_t seq, uint8_t cmd, const uint8_t *d, uint16_t n) {
    uint8_t buf[160];
    uint16_t len = tuya_serial_build(buf, seq, cmd, d, n);
    bridge_rx(buf, len);
}

int main(void) {
    bridge_init(fake_tx, on_dp);

    // Tick 1-2: enfileira e envia PRODUCT QUERY (55 AA 02 seq 01 ...)
    bridge_tick_100ms(); bridge_tick_100ms();
    assert(mcu_rx_len >= 9);
    assert(mcu_rx[0]==0x55 && mcu_rx[1]==0xAA && mcu_rx[2]==0x02);
    assert(mcu_rx[5]==0x01);  // cmd product query
    printf("1. product query no fio (v2+seq) OK\n");

    // MCU responde produto -> ponte manda status conectado
    mcu_rx_len = 0;
    const char *pi = "{\"p\":\"3kjidznp\",\"v\":\"1.0.0\"}";
    mcu_reply(1, 0x01, (const uint8_t*)pi, strlen(pi));
    bridge_tick_100ms();
    assert(bridge_state() == BR_ST_WAIT_CONF);
    assert(mcu_rx[5]==0x02 && mcu_rx[8]==0x01);  // status=conectado
    assert(strstr(bridge_product_info(), "3kjidznp"));
    printf("2. produto + status conectado OK\n");

    // MCU ACKa o status -> OPERACIONAL + sync
    mcu_rx_len = 0;
    mcu_reply(2, 0x02, 0, 0);
    bridge_tick_100ms();
    assert(bridge_state() == BR_ST_OPERATIONAL);
    assert(mcu_rx[5]==0x07);  // sync notify
    printf("3. operacional + sync OK\n");

    // MCU reporta DPs (0x09): rele1 on + acao tecla; ponte ACKa e entrega
    mcu_rx_len = 0;
    uint8_t rep[] = {24, 0x01, 0x00, 0x01, 0x01,
                     5, 0x04, 0x00, 0x01, 0x00};
    mcu_reply(7, 0x09, rep, sizeof(rep));
    bridge_tick_100ms();
    assert(dps == 2 && last_dp.id == 5);
    assert(mcu_rx[5]==0x09 && mcu_rx[3]==0 && mcu_rx[4]==7);  // ACK mesma seq
    printf("4. DP report + ACK OK\n");

    // Comando de rele: cmd 0x08 com unidade DP correta
    mcu_rx_len = 0;
    bridge_set_dp_bool(25, 1);
    bridge_tick_100ms();
    // Spray: 0x06 sai primeiro, sempre
    assert(mcu_rx[5]==0x06 && mcu_rx[8]==25 && mcu_rx[12]==1);
    int g2 = -1;
    for (int i = 13; i < mcu_rx_len - 1; i++)
        if (mcu_rx[i]==0x55 && mcu_rx[i+1]==0xAA) { g2 = i; break; }
    assert(g2 > 0 && mcu_rx[g2+5]==0x07 && mcu_rx[g2+8]==25);  // 2o do spray
    printf("5. DP command espelhando verbo aprendido OK\n");

    // Reset 5s da MCU (0x03 data 1): ACK diplomatico + status conectado
    mcu_rx_len = 0;
    uint8_t one = 1;
    mcu_reply(9, 0x03, &one, 1);
    bridge_tick_100ms();
    assert(mcu_rx[5]==0x03 && mcu_rx[3]==0 && mcu_rx[4]==9);  // ACK seq 9
    int f2 = -1;
    for (int i = 9; i < mcu_rx_len - 1; i++)
        if (mcu_rx[i]==0x55 && mcu_rx[i+1]==0xAA) { f2 = i; break; }
    assert(f2 > 0 && mcu_rx[f2+5]==0x02 && mcu_rx[f2+8]==0x01);
    printf("6. reset diplomatico v2 OK\n");

    // DPs em formato three-level (endereco 2B antes): parser tolerante
    uint8_t rep2[] = {0x00, 0x00, 26, 0x01, 0x00, 0x01, 0x01};
    mcu_reply(11, 0x09, rep2, sizeof(rep2));
    assert(dps == 3 && last_dp.id == 26);
    printf("7. DP com endereco (tolerancia) OK\n");

    // MCU real: reporta DP em 0x06 -> parse + ACK 0x06 mesma seq
    bridge_tick_100ms();  // drena ACK pendente do teste 7
    mcu_rx_len = 0;
    uint8_t rep06[] = {24, 0x01, 0x00, 0x01, 0x00};
    mcu_reply(13, 0x06, rep06, sizeof(rep06));
    bridge_tick_100ms();
    assert(dps == 4 && last_dp.id == 24);
    assert(mcu_rx[5]==0x06 && mcu_rx[4]==13);
    printf("8. report em 0x06 (dialeto real) OK\n");

    // Aprendizado: report dela COM endereco 0x0102 em 0x06 ->
    // proximo comando nosso espelha verbo E endereco
    bridge_tick_100ms(); mcu_rx_len = 0;
    uint8_t repA[] = {0x01, 0x02, 24, 0x01, 0x00, 0x01, 0x01};
    mcu_reply(21, 0x06, repA, sizeof(repA));
    bridge_tick_100ms();
    mcu_rx_len = 0;
    bridge_set_dp_bool(24, 0);
    bridge_tick_100ms();
    // Spray: primeiro frame 0x06 sem endereco, DP 24
    assert(mcu_rx[5]==0x06 && mcu_rx[8]==24 && mcu_rx[12]==0);
    printf("9. comando em spray (0x06 primeiro) OK\n");

    printf("bridge v2: 9/9 com aprendizado de dialeto\n");
    return 0;
}
