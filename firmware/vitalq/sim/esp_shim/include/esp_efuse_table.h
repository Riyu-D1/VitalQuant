/* esp_efuse_table.h — host-sim eFuse field table (ESP32-S3 layout).
 *
 * Exposes the same `const esp_efuse_desc_t *ESP_EFUSE_<FIELD>[]` arrays
 * the firmware references, with descriptor positions copied verbatim
 * from the real esp32s3 eFuse table.  The backing bit storage lives in
 * rom/efuse_mac.c.
 */
#pragma once

#include "esp_efuse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------ BLK0 : REPEAT_DATA ----------------------------- */
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS[];                    /* block 0, bits 0-31   */
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_RD_DIS[];
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_DIS_USB_JTAG[];
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_DIS_USB_SERIAL_JTAG[];
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_STRAP_JTAG_SEL[];
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_VDD_SPI_XPD[];
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_VDD_SPI_TIEH[];
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_VDD_SPI_FORCE[];
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_SPI_BOOT_CRYPT_CNT[];
extern const esp_efuse_desc_t* ESP_EFUSE_WR_DIS_SECURE_BOOT_EN[];

extern const esp_efuse_desc_t* ESP_EFUSE_RD_DIS[];
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_ICACHE[];
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_DCACHE[];
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_DOWNLOAD_ICACHE[];
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_DOWNLOAD_DCACHE[];
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_FORCE_DOWNLOAD[];
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_USB_OTG[];               /* bit 45 */
#define ESP_EFUSE_DIS_USB ESP_EFUSE_DIS_USB_OTG
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_TWAI[];
#define ESP_EFUSE_DIS_CAN ESP_EFUSE_DIS_TWAI
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_APP_CPU[];
extern const esp_efuse_desc_t* ESP_EFUSE_SOFT_DIS_JTAG[];             /* bits 48-50 */
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_PAD_JTAG[];              /* bit 51 */
#define ESP_EFUSE_HARD_DIS_JTAG ESP_EFUSE_DIS_PAD_JTAG
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_DOWNLOAD_MANUAL_ENCRYPT[];
extern const esp_efuse_desc_t* ESP_EFUSE_USB_EXCHG_PINS[];
extern const esp_efuse_desc_t* ESP_EFUSE_USB_EXT_PHY_ENABLE[];
extern const esp_efuse_desc_t* ESP_EFUSE_VDD_SPI_XPD[];               /* bit 68 */
extern const esp_efuse_desc_t* ESP_EFUSE_VDD_SPI_TIEH[];              /* bit 69 */
extern const esp_efuse_desc_t* ESP_EFUSE_VDD_SPI_FORCE[];             /* bit 70 */
extern const esp_efuse_desc_t* ESP_EFUSE_WDT_DELAY_SEL[];             /* bits 80-81 */
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_BOOT_CRYPT_CNT[];        /* bits 82-84 */
extern const esp_efuse_desc_t* ESP_EFUSE_SECURE_BOOT_KEY_REVOKE0[];
extern const esp_efuse_desc_t* ESP_EFUSE_SECURE_BOOT_KEY_REVOKE1[];
extern const esp_efuse_desc_t* ESP_EFUSE_SECURE_BOOT_KEY_REVOKE2[];
extern const esp_efuse_desc_t* ESP_EFUSE_SECURE_BOOT_EN[];            /* bit 116 */
extern const esp_efuse_desc_t* ESP_EFUSE_SECURE_BOOT_AGGRESSIVE_REVOKE[];
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_USB_JTAG[];              /* bit 118 */
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_USB_SERIAL_JTAG[];       /* bit 119 */
#define ESP_EFUSE_DIS_USB_DEVICE ESP_EFUSE_DIS_USB_SERIAL_JTAG
extern const esp_efuse_desc_t* ESP_EFUSE_STRAP_JTAG_SEL[];            /* bit 120 */
extern const esp_efuse_desc_t* ESP_EFUSE_USB_PHY_SEL[];               /* bit 121 */
extern const esp_efuse_desc_t* ESP_EFUSE_FLASH_TPUW[];                /* bits 124-127 */
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_DOWNLOAD_MODE[];         /* bit 128 */
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_DIRECT_BOOT[];           /* bit 129 */
#define ESP_EFUSE_DIS_LEGACY_SPI_BOOT ESP_EFUSE_DIS_DIRECT_BOOT
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_USB_SERIAL_JTAG_ROM_PRINT[]; /* bit 130 */
#define ESP_EFUSE_UART_PRINT_CHANNEL ESP_EFUSE_DIS_USB_SERIAL_JTAG_ROM_PRINT
extern const esp_efuse_desc_t* ESP_EFUSE_FLASH_ECC_MODE[];            /* bit 131 */
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE[]; /* bit 132 */
#define ESP_EFUSE_DIS_USB_DOWNLOAD_MODE ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE
extern const esp_efuse_desc_t* ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD[];  /* bit 133 */
extern const esp_efuse_desc_t* ESP_EFUSE_UART_PRINT_CONTROL[];        /* bits 134-135 */
extern const esp_efuse_desc_t* ESP_EFUSE_PIN_POWER_SELECTION[];       /* bit 136 */
extern const esp_efuse_desc_t* ESP_EFUSE_FLASH_TYPE[];
extern const esp_efuse_desc_t* ESP_EFUSE_FLASH_PAGE_SIZE[];
extern const esp_efuse_desc_t* ESP_EFUSE_FLASH_ECC_EN[];
extern const esp_efuse_desc_t* ESP_EFUSE_FORCE_SEND_RESUME[];
extern const esp_efuse_desc_t* ESP_EFUSE_SECURE_VERSION[];
extern const esp_efuse_desc_t* ESP_EFUSE_DIS_USB_OTG_DOWNLOAD_MODE[];
extern const esp_efuse_desc_t* ESP_EFUSE_DISABLE_WAFER_VERSION_MAJOR[];
extern const esp_efuse_desc_t* ESP_EFUSE_DISABLE_BLK_VERSION_MAJOR[];

/* ------------------ BLK1 : MAC_SPI_8M_SYS -------------------------- */
extern const esp_efuse_desc_t* ESP_EFUSE_MAC[];                       /* 48-bit factory MAC */
#define ESP_EFUSE_MAC_FACTORY ESP_EFUSE_MAC
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_CLK[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_Q[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_D[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_CS[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_HD[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_WP[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_DQS[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_D4[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_D5[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_D6[];
extern const esp_efuse_desc_t* ESP_EFUSE_SPI_PAD_CONFIG_D7[];

/* ------------------ BLK3 : USER_DATA ------------------------------- */
extern const esp_efuse_desc_t* ESP_EFUSE_USER_DATA[];
extern const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_MAC_CUSTOM[];      /* 48-bit custom MAC */
#define ESP_EFUSE_MAC_CUSTOM ESP_EFUSE_USER_DATA_MAC_CUSTOM
#define ESP_EFUSE_CUSTOM_MAC ESP_EFUSE_USER_DATA_MAC_CUSTOM

#ifdef __cplusplus
}
#endif
