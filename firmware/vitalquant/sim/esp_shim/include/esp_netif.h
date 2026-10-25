/* esp_netif.h — host-sim shim of the esp-netif API.
 *
 * Sim netifs are tiny heap objects carrying {ip_info, hostname}; the
 * wifi shim pokes addresses in when it fabricates DHCP results.
 * Implemented by sim/esp_shim/net/netif_sim.c.
 */
#ifndef _ESP_NETIF_H_
#define _ESP_NETIF_H_

#include <stdint.h>
#include <stdbool.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the esp-netif subsystem (idempotent on IDF 5.x).
 */
esp_err_t esp_netif_init(void);

/**
 * @brief Deinitialize esp-netif.
 */
esp_err_t esp_netif_deinit(void);

/**
 * @brief Create the default Wi-Fi station netif.
 */
esp_netif_t *esp_netif_create_default_wifi_sta(void);

/**
 * @brief Create the default Wi-Fi soft-AP netif.
 */
esp_netif_t *esp_netif_create_default_wifi_ap(void);

/**
 * @brief Create a netif from an explicit config (sim: plain object).
 */
esp_netif_t *esp_netif_new(const esp_netif_config_t *esp_netif_config);

/**
 * @brief Destroy a netif.
 */
void esp_netif_destroy(esp_netif_t *esp_netif);

/**
 * @brief Get the interface's IPv4 info.
 */
esp_err_t esp_netif_get_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info);

/**
 * @brief Set the interface's IPv4 info (static configuration).
 */
esp_err_t esp_netif_set_ip_info(esp_netif_t *esp_netif, const esp_netif_ip_info_t *ip_info);

/**
 * @brief Set IPv4 address of a netif.
 */
esp_err_t esp_netif_set_ip4_addr(esp_netif_t *esp_netif, const esp_ip4_addr_t *addr);

/**
 * @brief Get IPv4 address of a netif.
 */
esp_err_t esp_netif_get_ip4_addr(esp_netif_t *esp_netif, esp_ip4_addr_t *addr);

/**
 * @brief Get interface hostname.
 */
esp_err_t esp_netif_get_hostname(esp_netif_t *esp_netif, const char **hostname);

/**
 * @brief Set interface hostname.
 */
esp_err_t esp_netif_set_hostname(esp_netif_t *esp_netif, const char *hostname);

/**
 * @brief Attach a driver glue layer to a netif (sim: records handle, ESP_OK).
 */
esp_err_t esp_netif_attach(esp_netif_t *esp_netif, esp_netif_iodriver_handle driver_handle);

/**
 * @brief Get the flags of a netif.
 */
esp_netif_flags_t esp_netif_get_flags(esp_netif_t *esp_netif);

/**
 * @brief Get the name of the netif (sim: "STA"/"AP").
 */
const char *esp_netif_get_ifkey(esp_netif_t *esp_netif);

/**
 * @brief DHCP client start/stop/status — recorded in sim.
 */
esp_err_t esp_netif_dhcpc_start(esp_netif_t *esp_netif);
esp_err_t esp_netif_dhcpc_stop(esp_netif_t *esp_netif);
esp_err_t esp_netif_dhcpc_get_status(esp_netif_t *esp_netif, esp_netif_dhcp_status_t *status);
esp_err_t esp_netif_dhcps_get_status(esp_netif_t *esp_netif, esp_netif_dhcp_status_t *status);
esp_err_t esp_netif_dhcps_start(esp_netif_t *esp_netif);
esp_err_t esp_netif_dhcps_stop(esp_netif_t *esp_netif);

/**
 * @brief DNS server info get/set — recorded in sim.
 */
esp_err_t esp_netif_set_dns_info(esp_netif_t *esp_netif, esp_netif_dns_type_t type,
                                 esp_netif_dns_info_t *dns);
esp_err_t esp_netif_get_dns_info(esp_netif_t *esp_netif, esp_netif_dns_type_t type,
                                 esp_netif_dns_info_t *dns);

/**  TCPIP thread safe callback used with esp_netif_tcpip_exec() */
typedef esp_err_t (*esp_netif_callback_fn)(void *ctx);

/**
 * @brief Run a callback on the tcpip thread. Sim: executed inline —
 *        the SNTP shim relies on this never blocking.
 */
esp_err_t esp_netif_tcpip_exec(esp_netif_callback_fn fn, void *ctx);

/**
 * @brief Iterate over netifs (sim: visits the registered set).
 */
esp_netif_t *esp_netif_next_unsafe(esp_netif_t *esp_netif);

/** predicate used with esp_netif_find_if() */
typedef bool (*esp_netif_find_predicate_t)(esp_netif_t *netif, void *ctx);
esp_netif_t *esp_netif_find_if(esp_netif_find_predicate_t fn, void *ctx);

/**
 * @brief esp_ip4addr_ntoa — network order uint32 → dotted text.
 *        Returns buf (NULL if buflen < needed).
 */
char *esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, int buflen);

/**
 * @brief esp_ip4addr_aton — dotted text → stored uint32 (0 on parse fail).
 */
uint32_t esp_ip4addr_aton(const char *addr);

/**
 * @brief Convert esp-netif ip4 to generic ip addr.
 */
void esp_netif_to_ip4_addr(const esp_ip4_addr_t *esp_ip4, esp_ip4_addr_t *ip4);

#ifdef __cplusplus
}
#endif

#endif /* _ESP_NETIF_H_ */
