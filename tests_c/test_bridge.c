#include "bridge_core.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

// MCU simulada: captura o que a ponte envia e responde como a Tuya faria
static uint8_t mcu_rx[256]; static int mcu_rx_len = 0;
static tuya_dp_t last_dp; static int dps = 0;

static void fake_tx(const uint8_t *b, uint16_t n) {
    memcpy(mcu_rx + mcu_rx_len, b, n); mcu_rx_len += n;
}
static void on_dp(const tuya_dp_t *dp) { last_dp = *dp; dps++; }

static void mcu_reply(uint8_t cmd, const uint8_t *d, uint16_t n) {
    uint8_t buf[128];
    uint16_t len = tuya_serial_build(buf, 0x00, cmd, d, n);
    bridge_rx(buf, len);
}

int main(void) {
    bridge_init(fake_tx, on_dp);

    // Tick 1: ponte manda heartbeat
    bridge_tick_100ms();
    assert(bridge_state() == BR_ST_WAIT_HEARTBEAT && mcu_rx_len > 0);
    assert(mcu_rx[3] == TUYA_CMD_HEARTBEAT);

    // MCU responde heartbeat -> ponte pede product info
    mcu_rx_len = 0;
    uint8_t hb = 0x01;
    mcu_reply(TUYA_CMD_HEARTBEAT, &hb, 1);
    assert(bridge_state() == BR_ST_WAIT_PRODUCT);
    assert(mcu_rx[3] == TUYA_CMD_PRODUCT_INFO);

    // MCU manda product info -> ponte pede conf
    mcu_rx_len = 0;
    const char *pi = "{\"p\":\"3kjidznp\",\"v\":\"1.0.0\"}";
    mcu_reply(TUYA_CMD_PRODUCT_INFO, (const uint8_t*)pi, strlen(pi));
    assert(bridge_state() == BR_ST_WAIT_CONF);
    assert(strstr(bridge_product_info(), "3kjidznp"));

    // MCU responde conf -> ponte: wifi_state(4) + query DPs, OPERACIONAL
    mcu_rx_len = 0;
    mcu_reply(TUYA_CMD_MCU_CONF, 0, 0);
    assert(bridge_state() == BR_ST_OPERATIONAL);
    assert(mcu_rx[3] == TUYA_CMD_WIFI_STATE && mcu_rx[6] == 0x04);

    // MCU reporta: rele 1 ligou (DP 24) + cena tecla 1 single (DP 1 enum 0)
    uint8_t rep[] = {24, 0x01, 0x00, 0x01, 0x01,
                     1, 0x04, 0x00, 0x01, 0x00};
    mcu_reply(TUYA_CMD_DP_REPORT, rep, sizeof(rep));
    assert(dps == 2 && last_dp.id == 1 && last_dp.type == TUYA_DP_TYPE_ENUM);

    // Ponte comanda: liga rele 2 (DP 25)
    mcu_rx_len = 0;
    bridge_set_dp_bool(25, 1);
    assert(mcu_rx[3] == TUYA_CMD_DP_CMD && mcu_rx[6] == 25 && mcu_rx[10] == 1);

    // Heartbeat periodico em operacao (150 ticks)
    mcu_rx_len = 0;
    for (int i = 0; i < 151; i++) bridge_tick_100ms();
    assert(mcu_rx_len > 0 && mcu_rx[3] == TUYA_CMD_HEARTBEAT);

    printf("bridge_core: 7/7 cenarios OK (handshake completo + DPs + heartbeat)\n");
    return 0;
}
