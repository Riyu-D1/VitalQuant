/* Host-sim esp_chip_info.h — chip model/feature types.  The sim
 * presents an ESP32-S3 (the fix_v22 board's ESP32-S3-WROOM-1-N16R8). */
#ifndef ESP_CHIP_INFO_H
#define ESP_CHIP_INFO_H

#include <stdint.h>
#include "esp_bit_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHIP_ESP32         = 1,   /* ESP32                                   */
    CHIP_ESP32S2       = 2,   /* ESP32-S2                                */
    CHIP_ESP32S3       = 9,   /* ESP32-S3                                */
    CHIP_ESP32C3       = 5,   /* ESP32-C3                                */
    CHIP_ESP32C2       = 12,  /* ESP32-C2                                */
    CHIP_ESP32C6       = 13,  /* ESP32-C6                                */
    CHIP_ESP32H2       = 16,  /* ESP32-H2                                */
    CHIP_ESP32P4       = 18,  /* ESP32-P4                                */
    CHIP_ESP32C5       = 23,  /* ESP32-C5                                */
    CHIP_POSIX_LINUX   = 999, /* code is running on POSIX/Linux (sim!)   */
} esp_chip_model_t;

typedef enum {
    CHIP_FEATURE_EMB_FLASH      = BIT(0), /* embedded flash memory        */
    CHIP_FEATURE_WIFI_BGN       = BIT(1), /* 2.4 GHz Wi-Fi (802.11 b/g/n) */
    CHIP_FEATURE_BLE            = BIT(4), /* Bluetooth LE                 */
    CHIP_FEATURE_BT             = BIT(5), /* Bluetooth Classic            */
    CHIP_FEATURE_IEEE802154     = BIT(6), /* IEEE 802.15.4                */
    CHIP_FEATURE_EMB_PSRAM      = BIT(7), /* embedded PSRAM               */
} esp_chip_feature_t;

typedef struct {
    esp_chip_model_t model;         /* chip model, one of esp_chip_model_t   */
    uint32_t         features;      /* bit mask of CHIP_FEATURE_x flags      */
    uint16_t         revision_full; /* chip revision (MXXYY format)          */
    uint8_t          cores;         /* number of CPU cores                   */
    uint8_t          revision;      /* chip revision number (deprecated)     */
} esp_chip_info_t;

/* esp_chip_info() itself is declared in esp_system.h; declare it here
 * as well so files that include only this header still see it. */
void esp_chip_info(esp_chip_info_t *out_chip_info);

#ifdef __cplusplus
}
#endif

#endif /* ESP_CHIP_INFO_H */
