/* esp_wifi_default.h — host-sim shim: wifi <-> netif glue defaults. */
#ifndef _ESP_WIFI_DEFAULT_H
#define _ESP_WIFI_DEFAULT_H

#include "esp_netif.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Attaches wifi station interface to supplied netif
 */
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *esp_netif);

/**
 * @brief Attaches wifi soft AP interface to supplied netif
 */
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *esp_netif);

/**
 * @brief Sets default wifi event handlers for STA interface
 */
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void);

/**
 * @brief Sets default wifi event handlers for AP interface
 */
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void);

/**
 * @brief Sets default wifi event handlers for NAN interface
 */
esp_err_t esp_wifi_set_default_wifi_nan_handlers(void);

/**
 * @brief Clears default wifi event handlers for supplied network interface
 */
esp_err_t esp_wifi_clear_default_wifi_driver_and_data(void *esp_netif);

/**
 * @brief Registers wifi_default event handlers
 */
esp_err_t esp_wifi_register_default_wifi_sta_handlers(void);
esp_err_t esp_wifi_register_default_wifi_ap_handlers(void);
esp_err_t esp_wifi_register_default_wifi_nan_handlers(void);

/**
 * @brief Unregisters wifi_default event handlers
 */
esp_err_t esp_wifi_unregister_default_wifi_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* _ESP_WIFI_DEFAULT_H */
