/* esp_mac.h — host-sim shim of MAC-address services.
 *
 * The sim exposes one stable Espressif-OUI base MAC; derived addresses
 * follow the real bit-twiddling so firmware code paths (mDNS hostname,
 * cloud client-id) see realistic values.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(__has_include)
#  if __has_include("esp_err.h")
#    include "esp_err.h"
#  endif
#endif

#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#endif

#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG 0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif
#ifndef ESP_ERR_INVALID_MAC
#define ESP_ERR_INVALID_MAC 0x10B
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_MAC_WIFI_STA,      /**< MAC for WiFi Station (6 bytes) */
    ESP_MAC_WIFI_SOFTAP,   /**< MAC for WiFi Soft-AP (6 bytes) */
    ESP_MAC_BT,            /**< MAC for Bluetooth (6 bytes) */
    ESP_MAC_ETH,           /**< MAC for Ethernet (6 bytes) */
    ESP_MAC_IEEE802154,    /**< MAC for IEEE802154 (8 bytes) */
    ESP_MAC_BASE,          /**< Base MAC for that used for other MAC types (6 bytes) */
    ESP_MAC_EFUSE_FACTORY, /**< MAC_FACTORY eFuse which was burned by Espressif in production (6 bytes) */
    ESP_MAC_EFUSE_CUSTOM,  /**< MAC_CUSTOM eFuse which was can be burned by customer (6 bytes) */
    ESP_MAC_EFUSE_EXT,     /**< MAC_EXT eFuse which is used as an extender for IEEE802154 MAC (2 bytes) */
} esp_mac_type_t;

/** @cond */
#define ONE_UNIVERSAL_MAC_ADDR 1
#define TWO_UNIVERSAL_MAC_ADDR 2
#define FOUR_UNIVERSAL_MAC_ADDR 4
#ifndef CONFIG_ESP_MAC_UNIVERSAL_MAC_ADDRESSES
#define CONFIG_ESP_MAC_UNIVERSAL_MAC_ADDRESSES FOUR_UNIVERSAL_MAC_ADDR
#endif
#define UNIVERSAL_MAC_ADDR_NUM CONFIG_ESP_MAC_UNIVERSAL_MAC_ADDRESSES
/** @endcond */

/**
 * @brief Set base MAC address with the MAC address which is stored in BLK3 of EFUSE or
 *        external storage e.g. flash and EEPROM.
 */
esp_err_t esp_base_mac_addr_set(const uint8_t *mac);

/**
 * @brief Return base MAC address which is set with esp_base_mac_addr_set.
 */
esp_err_t esp_base_mac_addr_get(uint8_t *mac);

/**
 * @brief Return base MAC address which was previously written to BLK3 of EFUSE.
 */
esp_err_t esp_efuse_mac_get_custom(uint8_t *mac);

/**
 * @brief Return base MAC address which is factory-programmed by Espressif in BLK0 of EFUSE.
 */
esp_err_t esp_efuse_mac_get_default(uint8_t *mac);

/**
 * @brief Read base MAC address and set the MAC addresses for each interface.
 */
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);

/**
 * @brief Derive local MAC address from universal MAC address.
 */
esp_err_t esp_derive_local_mac(uint8_t *local_mac, const uint8_t *universal_mac);

/**
 * @brief Set custom MAC for a specific interface (esp_read_mac returns it).
 */
esp_err_t esp_iface_mac_addr_set(const uint8_t *mac, esp_mac_type_t type);

/**
 * @brief Returns the length of the MAC address for a given type.
 */
size_t esp_mac_addr_len_get(esp_mac_type_t type);

/**
 * @brief Legacy alias kept for IDF4-era call sites.
 */
int esp_mac_get_len(esp_mac_type_t type);

/** @cond */
#define MAC_ADDR_UNINITIALIZED  {0, 0, 0, 0, 0, 0}
/** @endcond */

/**
 * @brief Returns whether the MAC address is uninitialized.
 */
bool esp_mac_is_uninitialized(const uint8_t *mac);

#ifdef __cplusplus
}
#endif
