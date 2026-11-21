#include "basic_cluster.h"
#include "base_components/network_indicator.h"
#include "build_date.h"
#include "cluster_common.h"
#include "consts.h"
#include "hal/system.h"
#include "device_config/config_nv.h"
#include "device_config/config_parser.h"
#include "device_config/device_params_nv.h"
#include "device_config/nvm_items.h"
#include "device_config/reset.h"
#include "relay_cluster.h"
#include "hal/gpio.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "hal/tasks.h"
#include <stddef.h>

#ifdef HAL_SILABS
#include "silabs_config.h"
#endif

const uint8_t zclVersion   = 0x03;
const uint8_t appVersion   = 0x03;
const uint8_t stackVersion = 0x02;
const uint8_t hwVersion    = 0x00;

// Power source - set at runtime based on battery config
uint8_t powerSource = POWER_SOURCE_MAINS_1_PHASE; // 0x01 default

const uint16_t cluster_revision = 0x01;
DEF_STR(STRINGIFY_VALUE(VERSION_STR), swBuildId);
extern network_indicator_t network_indicator;

void basic_cluster_store_attrs_to_nv();
void basic_cluster_load_attrs_from_nv();

static uint8_t g_factory_wipe = 0;
static uint8_t g_reset_now = 0;
static uint8_t g_test_net_status = 0;
static uint8_t g_gpio_test = 0;
static uint8_t g_gpio_scan_result = 0;

// ---- Bring-up: teste de GPIO em runtime (saidas) + scan de entradas ----
extern hal_gpio_pin_t relay_pins_used[];
extern uint8_t relay_pins_used_cnt;
static uint8_t g_scan_active = 0;
static hal_task_t g_scan_task;

static uint8_t gpio_is_relay(hal_gpio_pin_t pin) {
    for (uint8_t i = 0; i < relay_pins_used_cnt; i++)
        if (relay_pins_used[i] == pin) return 1;
    return 0;
}

// Varre A0..D7 (pulando os pinos de rele p/ nao glitchar) e guarda em
// g_gpio_scan_result o codigo do pino que estiver em LOW (botao pressionado).
static void gpio_scan_task(void *arg) {
    if (!g_scan_active) return;
    for (uint8_t port = 1; port <= 4; port++) {
        for (uint8_t bit = 0; bit <= 7; bit++) {
            char buf[3] = {(char)('A' + port - 1), (char)('0' + bit), 0};
            hal_gpio_pin_t pin = hal_gpio_parse_pin(buf);
            if (pin == HAL_INVALID_PIN || gpio_is_relay(pin)) continue;
            if (hal_gpio_read(pin) == 0) {
                g_gpio_scan_result = port * 10 + bit;
                printf("GPIO_SCAN: %s LOW (code %d)\r\n", buf,
                       g_gpio_scan_result);
            }
        }
    }
    hal_tasks_schedule(&g_scan_task, 150);
}

static void gpio_scan_start(void) {
    // configura todos os pinos nao-rele como entrada com pull-up
    for (uint8_t port = 1; port <= 4; port++) {
        for (uint8_t bit = 0; bit <= 7; bit++) {
            char buf[3] = {(char)('A' + port - 1), (char)('0' + bit), 0};
            hal_gpio_pin_t pin = hal_gpio_parse_pin(buf);
            if (pin == HAL_INVALID_PIN || gpio_is_relay(pin)) continue;
            hal_gpio_init(pin, 1, HAL_GPIO_PULL_UP);
        }
    }
    g_gpio_scan_result = 0;
    g_scan_active = 1;
    g_scan_task.handler = gpio_scan_task;
    g_scan_task.arg = 0;
    hal_tasks_init(&g_scan_task);
    hal_tasks_schedule(&g_scan_task, 150);
    printf("GPIO_SCAN: iniciado (pressione cada botao e leia gpio_scan_result)\r\n");
}
static uint8_t g_br_backlight = 1;
static uint8_t g_br_brightness = 100;
static uint8_t g_br_mode[3] = {0, 0, 0};

static uint16_t g_hid_frames = 0;
static uint8_t g_hid_frame_pascal[66];

void basic_cluster_update_bridge_hidden(uint16_t frames, const char *hex) {
    g_hid_frames = frames;
    uint8_t n = 0;
    while (hex[n] && n < 64) {
        g_hid_frame_pascal[1 + n] = (uint8_t)hex[n];
        n++;
    }
    g_hid_frame_pascal[0] = n;
}

void basic_cluster_request_factory_wipe(void) {
    printf("FACTORY WIPE via ritual da ponte\r\n");
    schedule_full_reset(500);
}

void basic_cluster_update_bridge_backlight(uint8_t on) { g_br_backlight = on; }
void basic_cluster_update_bridge_brightness(uint8_t pct) { g_br_brightness = pct; }
void basic_cluster_update_bridge_mode(uint8_t ch, uint8_t m) {
    if (ch < 3) g_br_mode[ch] = m;
}

void basic_cluster_callback_attr_write_trampoline(uint16_t attribute_id) {
    basic_cluster_store_attrs_to_nv();
    if (attribute_id == ZCL_ATTR_BASIC_DEVICE_CONFIG) {
        void schedule_reboot(uint16_t delay_ms);
        device_config_str.data[device_config_str.size] =
            0;              // NULL terminate the string
        device_config_write_to_nv();
        schedule_reboot(3000); // 3s: garante que a NVM gravou antes de reiniciar
    }
    if (attribute_id == ZCL_ATTR_BASIC_STATUS_LED_STATE) {
        network_indicator_from_manual_state(&network_indicator);
    }
    if (attribute_id == ZCL_ATTR_BASIC_MULTI_PRESS_RESET_COUNT) {
        device_params_set_multi_press_reset_count(g_multi_press_reset_count);
    }
    if (attribute_id == ZCL_ATTR_BASIC_BACKLIGHT_MODE) {
        device_params_set_backlight_mode(g_backlight_mode);
        update_relay_clusters(); // re-aplica o backlight nos LEDs
    }
    if (attribute_id == ZCL_ATTR_BASIC_BRIDGE_BACKLIGHT) {
        void bridge_ui_backlight(uint8_t on);
        void bridge8_set_backlight(uint8_t mode);
        bridge_ui_backlight(g_br_backlight);
        bridge8_set_backlight(g_br_backlight);  // 0=off,1=normal,2=inverted
    }
    if (attribute_id == ZCL_ATTR_BASIC_BRIDGE_BRIGHTNESS) {
        void bridge_ui_brightness(uint8_t pct);
        bridge_ui_brightness(g_br_brightness);
    }
    if (attribute_id == ZCL_ATTR_BASIC_BRIDGE_MODE_CH1) {
        void bridge_ui_mode(uint8_t ch, uint8_t m);
        bridge_ui_mode(0, g_br_mode[0]);
    }
    if (attribute_id == ZCL_ATTR_BASIC_BRIDGE_MODE_CH2) {
        void bridge_ui_mode(uint8_t ch, uint8_t m);
        bridge_ui_mode(1, g_br_mode[1]);
    }
    if (attribute_id == ZCL_ATTR_BASIC_BRIDGE_MODE_CH3) {
        void bridge_ui_mode(uint8_t ch, uint8_t m);
        bridge_ui_mode(2, g_br_mode[2]);
    }
    if (attribute_id == ZCL_ATTR_BASIC_FACTORY_WIPE) {
        printf("FACTORY WIPE recebido, valor=%d\r\n", g_factory_wipe);
        if (g_factory_wipe == FACTORY_WIPE_MAGIC) {
            printf("FACTORY WIPE: disparando schedule_full_reset\r\n");
            schedule_full_reset(500);
        }
        g_factory_wipe = 0;
    }
    if (attribute_id == ZCL_ATTR_BASIC_TEST_NET_STATUS) {
        void bridge_force_net_status(uint8_t status);
        void bridge8_poke_dp(uint8_t dp_id, uint8_t value);
        uint8_t val = g_test_net_status;
        void bridge8_scan_start(uint8_t on);
        if (val == 200) { bridge8_scan_start(1); }
        else if (val == 201) { bridge8_scan_start(0); }
        else if (val >= 100) {
            // 100+N: manda DP N pra MCU com valor 1 (caca o pisca por DP)
            bridge8_poke_dp(val - 100, 1);
        } else if (val >= 50) {
            // 50+N: manda DP N com valor 0
            bridge8_poke_dp(val - 50, 0);
        } else {
            // 0-9: testa net_status
            bridge_force_net_status(val);
        }
    }
    if (attribute_id == ZCL_ATTR_BASIC_GPIO_TEST) {
        // Teste de GPIO em runtime (bring-up sem regravar).
        // Codigo = porta*10 + bit, porta A=1,B=2,C=3,D=4.
        //   HIGH:  A0=10 ... B5=25 ... C3=33 ... D7=47
        //   LOW:   codigo + 100 (ex.: C4 em LOW = 134)
        //   200:   inicia SCAN de entradas (acha botoes); le gpio_scan_result
        //   201:   para o scan
        //   0:     libera o pino de teste de saida
        // Saidas (backlight: dirige LOW e ve qual apaga; LED pareamento: HIGH).
        static hal_gpio_pin_t test_pin = HAL_INVALID_PIN;
        uint8_t v = g_gpio_test;
        if (v == 200) {
            gpio_scan_start();
        } else if (v == 201) {
            g_scan_active = 0;
            printf("GPIO_SCAN: parado\r\n");
        } else {
            if (test_pin != HAL_INVALID_PIN) {
                hal_gpio_init(test_pin, 1, HAL_GPIO_PULL_UP);  // libera anterior
                test_pin = HAL_INVALID_PIN;
            }
            uint8_t level = 1;
            uint8_t code  = v;
            if (v >= 100) { level = 0; code = v - 100; }
            uint8_t port = code / 10;   // 1=A 2=B 3=C 4=D
            uint8_t bit  = code % 10;
            if (v != 0 && port >= 1 && port <= 4 && bit <= 7) {
                char buf[3] = {(char)('A' + port - 1), (char)('0' + bit), 0};
                hal_gpio_pin_t pin = hal_gpio_parse_pin(buf);
                if (pin != HAL_INVALID_PIN) {
                    hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);  // saida
                    hal_gpio_write(pin, level);
                    test_pin = pin;
                    printf("GPIO_TEST: %s = %d\r\n", buf, level);
                }
            } else {
                printf("GPIO_TEST: liberado\r\n");
            }
        }
    }
    if (attribute_id == ZCL_ATTR_BASIC_RESET_NOW) {
        // Adiado 500ms: deixa o write Zigbee receber o ACK antes de resetar.
        // Antes rodava inline (clear_all + factory_reset) e o device saia da
        // rede no meio do handler, sem responder -> o Z2M acusava erro/timeout
        // e o reset "nao pegava". Agora agenda e responde o write normalmente.
        printf("RESET_NOW: agendando factory reset com leave (500ms)\r\n");
        schedule_reset_now(500);
    }
}

void basic_cluster_add_to_endpoint(zigbee_basic_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint) {
    // Set power source based on runtime battery configuration
    if (battery.pin != HAL_INVALID_PIN) {
        powerSource = POWER_SOURCE_BATTERY;
    }

    // Initialize build date buffer
    zb_build_date_init(ZB_BUILD_DATE_YYYYMMDD);

    // Fill Attrs

    SETUP_ATTR(0, ZCL_ATTR_BASIC_ZCL_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               zclVersion);

    SETUP_ATTR(1, ZCL_ATTR_BASIC_APP_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               appVersion);
    SETUP_ATTR(2, ZCL_ATTR_BASIC_STACK_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               stackVersion);
    SETUP_ATTR(3, ZCL_ATTR_BASIC_HW_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               hwVersion);
    SETUP_ATTR(4, ZCL_ATTR_BASIC_MFR_NAME, ZCL_DATA_TYPE_CHAR_STR, ATTR_READONLY,
               cluster->manuName);
    SETUP_ATTR(5, ZCL_ATTR_BASIC_MODEL_ID, ZCL_DATA_TYPE_CHAR_STR, ATTR_READONLY,
               cluster->modelId);
    SETUP_ATTR(6, ZCL_ATTR_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8, ATTR_READONLY,
               powerSource);
    SETUP_ATTR(7, ZCL_ATTR_BASIC_DEV_ENABLED, ZCL_DATA_TYPE_BOOLEAN,
               ATTR_WRITABLE, cluster->deviceEnable);
    SETUP_ATTR(8, ZCL_ATTR_BASIC_SW_BUILD_ID, ZCL_DATA_TYPE_CHAR_STR,
               ATTR_READONLY, swBuildId);
    SETUP_ATTR(9, ZCL_ATTR_BASIC_DATE_CODE, ZCL_DATA_TYPE_CHAR_STR, ATTR_READONLY,
               ZB_BUILD_DATE_YYYYMMDD);
    SETUP_ATTR(10, ZCL_ATTR_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
               ATTR_READONLY, cluster_revision);
    SETUP_ATTR(11, ZCL_ATTR_BASIC_DEVICE_CONFIG, ZCL_DATA_TYPE_LONG_CHAR_STR,
               ATTR_WRITABLE, device_config_str);
    SETUP_ATTR(12, ZCL_ATTR_BASIC_MULTI_PRESS_RESET_COUNT, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_multi_press_reset_count);
    SETUP_ATTR(13, ZCL_ATTR_BASIC_BACKLIGHT_MODE, ZCL_DATA_TYPE_ENUM8,
               ATTR_WRITABLE, g_backlight_mode);
    SETUP_ATTR(14, ZCL_ATTR_BASIC_FACTORY_WIPE, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_factory_wipe);
    SETUP_ATTR(15, ZCL_ATTR_BASIC_BRIDGE_BACKLIGHT, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_br_backlight);
    SETUP_ATTR(16, ZCL_ATTR_BASIC_BRIDGE_BRIGHTNESS, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_br_brightness);
    SETUP_ATTR(17, ZCL_ATTR_BASIC_BRIDGE_MODE_CH1, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_br_mode[0]);
    SETUP_ATTR(18, ZCL_ATTR_BASIC_BRIDGE_MODE_CH2, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_br_mode[1]);
    SETUP_ATTR(19, ZCL_ATTR_BASIC_BRIDGE_MODE_CH3, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_br_mode[2]);
    SETUP_ATTR(20, ZCL_ATTR_BASIC_BRIDGE_RX_FRAMES, ZCL_DATA_TYPE_UINT16,
               0, g_hid_frames);
    SETUP_ATTR(21, ZCL_ATTR_BASIC_BRIDGE_LAST_FRAME, ZCL_DATA_TYPE_CHAR_STR,
               0, g_hid_frame_pascal);
    SETUP_ATTR(22, ZCL_ATTR_BASIC_RESET_NOW, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_reset_now);
    SETUP_ATTR(23, ZCL_ATTR_BASIC_TEST_NET_STATUS, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_test_net_status);
    SETUP_ATTR(24, ZCL_ATTR_BASIC_GPIO_TEST, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_gpio_test);
    SETUP_ATTR(25, ZCL_ATTR_BASIC_GPIO_SCAN_RESULT, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_gpio_scan_result);
    if (network_indicator.has_dedicated_led) {
        SETUP_ATTR(26, ZCL_ATTR_BASIC_STATUS_LED_STATE, ZCL_DATA_TYPE_BOOLEAN,
                   ATTR_WRITABLE, network_indicator.manual_state_when_connected);
    }

    endpoint->clusters[endpoint->cluster_count].cluster_id      = ZCL_CLUSTER_BASIC;
    endpoint->clusters[endpoint->cluster_count].attribute_count =
        network_indicator.has_dedicated_led ? 27 : 26;
    endpoint->clusters[endpoint->cluster_count].attributes = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server  = 1;
    endpoint->cluster_count++;

    device_params_load_from_nv();
    basic_cluster_load_attrs_from_nv();
    if (hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED &&
        network_indicator.has_dedicated_led) {
        network_indicator_from_manual_state(&network_indicator);
    }
}

typedef struct {
    uint8_t network_led_on;
} zigbee_basic_cluster_config;

static zigbee_basic_cluster_config nv_config_buffer;

void basic_cluster_store_attrs_to_nv() {
    nv_config_buffer.network_led_on =
        network_indicator.manual_state_when_connected;

    hal_nvm_write(NV_ITEM_BASIC_CLUSTER_DATA, sizeof(zigbee_basic_cluster_config),
                  (uint8_t *)&nv_config_buffer);
}

void basic_cluster_load_attrs_from_nv() {
    hal_nvm_status_t st = hal_nvm_read(NV_ITEM_BASIC_CLUSTER_DATA,
                                       sizeof(zigbee_basic_cluster_config),
                                       (uint8_t *)&nv_config_buffer);

    if (st != HAL_NVM_SUCCESS) {
        return;
    }
    network_indicator.manual_state_when_connected =
        nv_config_buffer.network_led_on;
}
