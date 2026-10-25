/* esp_netif_sntp.h — host-sim shim of the esp_netif SNTP service. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"   /* TickType_t */
#include "esp_err.h"
#include "esp_netif_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_LWIP_SNTP_MAX_SERVERS
#define CONFIG_LWIP_SNTP_MAX_SERVERS 3   /* sim default (target sdkconfig: 1) */
#endif

/**
 * @brief Time sync notification function
 */
typedef void (*esp_sntp_time_cb_t)(struct timeval *tv);

/**
 * @brief Utility macro for providing multiple servers in parentheses
 */
#define ESP_SNTP_SERVER_LIST(...)   { __VA_ARGS__ }

/**
 * @brief Default configuration to init SNTP with multiple servers
 */
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(servers_in_list, list_of_servers)   {   \
            .smooth_sync = false,                       \
            .server_from_dhcp = false,                  \
            .wait_for_sync = true,                      \
            .start = true,                              \
            .sync_cb = NULL,                            \
            .renew_servers_after_new_IP = false,        \
            .ip_event_to_renew = IP_EVENT_STA_GOT_IP,   \
            .index_of_first_server = 0,                 \
            .num_of_servers = (servers_in_list),        \
            .servers = list_of_servers,                 \
}

/**
 * @brief Default configuration with a single server
 */
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) \
            ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(1, {server})

/**
 * @brief SNTP configuration struct
 */
typedef struct esp_sntp_config {
    bool smooth_sync;              ///< set to true if smooth sync required
    bool server_from_dhcp;         ///< set to true to request NTP server config from DHCP
    bool wait_for_sync;            ///< if true, we create a semaphore to signal time sync event
    bool start;                    ///< set to true to automatically start the SNTP service
    esp_sntp_time_cb_t sync_cb;    ///< optionally sets callback function on time sync event
    bool renew_servers_after_new_IP; ///< refresh server list if NTP provided by DHCP
    ip_event_t ip_event_to_renew;  ///< set the IP event id on which we refresh server list
    size_t index_of_first_server;  ///< refresh server list after this server
    size_t num_of_servers;         ///< number of preconfigured NTP servers
    const char* servers[CONFIG_LWIP_SNTP_MAX_SERVERS]; ///< list of servers
} esp_sntp_config_t;

/**
 * @brief Initialize SNTP with supplied config struct.
 *        Requires esp_netif_init() first (ESP_ERR_INVALID_STATE otherwise),
 *        mirroring the real esp_netif_tcpip_exec dependency.
 */
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t * config);

/**
 * @brief Start SNTP service if it wasn't started during init
 *        (config.start = false), or restart it if already started.
 */
esp_err_t esp_netif_sntp_start(void);

/**
 * @brief Deinitialize esp_netif SNTP module.
 */
void esp_netif_sntp_deinit(void);

/**
 * @brief Wait for time sync event.
 * @param tout Specified timeout in RTOS ticks (sim treats ticks as ms).
 */
esp_err_t esp_netif_sntp_sync_wait(TickType_t tout);

/**
 * @brief Returns SNTP server's reachability shift register per RFC 5905.
 */
esp_err_t esp_netif_sntp_reachability(unsigned int index, unsigned int *reachability);

#ifdef __cplusplus
}
#endif
