/* sim_net_priv.h — internal plumbing shared between the net/ shim units.
 * Not part of the ESP-IDF surface; sim-only.
 */
#pragma once

#include <stdbool.h>
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* netif subsystem init flag (set by esp_netif_init). */
bool         sim_netif_inited(void);

/* The default STA netif, if firmware created one. */
esp_netif_t *sim_netif_sta(void);

/* Assign/clear an IPv4 config on a netif object (used by wifi_sim to
 * fabricate the DHCP lease before posting IP_EVENT_STA_GOT_IP). */
void         sim_netif_assign_ip(esp_netif_t *netif, const esp_netif_ip_info_t *ip);
void         sim_netif_clear_ip(esp_netif_t *netif);

#ifdef __cplusplus
}
#endif
