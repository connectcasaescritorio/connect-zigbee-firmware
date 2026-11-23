#include "config_nv.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "nvm_items.h"
#include <string.h>

#ifdef HAL_SILABS
#include "silabs_config.h"
#endif

#ifndef STRINGIFY
#define _STRINGIFY(x)    #x
#define STRINGIFY(x)     _STRINGIFY(x)
#endif

#ifndef DEFAULT_CONFIG
const char default_config_data[] = "unknown;TS0012-CUSTOM;";
#else
const char default_config_data[] = STRINGIFY(DEFAULT_CONFIG);
#endif

device_config_str_t device_config_str;

void device_config_write_to_nv() {
    printf("Writing config to nv: %s\r\n", device_config_str.data);
    hal_nvm_status_t st = 0;

    printf("Size: %d\r\n", (int)sizeof(device_config_str));
    st = hal_nvm_write(NV_ITEM_DEVICE_CONFIG, sizeof(device_config_str),
                       (uint8_t *)&device_config_str);

    if (st != HAL_NVM_SUCCESS) {
        printf(
            "Failed to write DEVICE_CONFIG_DATA to NV, status: %d. (bytes: %d)\r\n",
            st, device_config_str.size);
    } else {
        printf("success!\r\n");
    }
}

void device_config_read_from_nv() {
    hal_nvm_status_t st = 0;

    // Se a config COMPILADA for do dimmer (-BD) ou do touch 4 canais (-B4),
    // ignora a NVM e usa SEMPRE a compilada. Contorna NVM presa com config
    // antiga (ex.: peca de 4 canais que veio gravada como 6 canais).
    {
        const char *d = (const char *)default_config_data;
        const char *p = d;
        int force_compiled = 0;
        while (*p) {
            if (p[0]=='-' && p[1]=='B' && (p[2]=='D' || p[2]=='4')) {
                force_compiled = 1;
                break;
            }
            if (p[0]=='C' && p[1]=='T' && p[2]=='4') {
                // TS0004-CT4 (touch 4 canais SoC): NVM nao grava nessa peca,
                // entao forca a config compilada pra ela SEMPRE aplicar.
                force_compiled = 1;
                break;
            }
            if (p[0]=='C' && p[1]=='C' && p[2]=='2') {
                // TS0002-CC2 (modulo 2ch ZT2S gcagsb9r): NVM nao grava nessa
                // peca, entao forca a config compilada pra SEMPRE aplicar.
                force_compiled = 1;
                break;
            }
            p++;
        }
        if (force_compiled) {
            // SEMPRE usa a compilada. Cada build vale.
            // (mecanismo comprovado no dimmer na 1.9.32)
            memcpy(device_config_str.data, default_config_data,
                   sizeof(default_config_data));
            device_config_str.size = strlen(d);
            printf("Config compilada forcada: %s\r\n", d);
            return;
        }
    }

    st = hal_nvm_read(NV_ITEM_DEVICE_CONFIG, sizeof(device_config_str),
                      (uint8_t *)&device_config_str);

    if (st != HAL_NVM_SUCCESS) {
        printf("Failed to read NV_ITEM_DEVICE_CONFIG, using default config "
               "instead, status: %d. (bytes: %d)\r\n",
               st, device_config_str.size);
        memcpy(device_config_str.data, default_config_data,
               sizeof(default_config_data));
        device_config_str.size = strlen((const char *)default_config_data);
    }

    printf("Using config: %d chars from\r\n%s\r\n", device_config_str.size,
           device_config_str.data);
}
