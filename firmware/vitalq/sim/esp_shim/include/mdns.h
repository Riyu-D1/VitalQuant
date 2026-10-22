/* mdns.h — host-sim shim of the espressif/mdns component API.
 *
 * Records hostname/instance/service announcements so scenarios can
 * observe what the firmware published; no real multicast traffic.
 * Implemented by sim/esp_shim/net/mdns_sim.c.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief mDNS TXT item (key/value pair)
 */
typedef struct {
    const char *key;                        /*!< item key name */
    const char *value;                      /*!< item value string */
} mdns_txt_item_t;

/**
 * @brief mDNS TXT item with explicit value length
 */
typedef struct {
    const char *key;                        /*!< item key name */
    const char *value;                      /*!< item value string */
    uint8_t value_len;                      /*!< item value length */
} mdns_txt_item_with_value_len_t;

/**
 * @brief mDNS IP address structure
 */
typedef struct {
    esp_ip_addr_t addr;                     /*!< IP address */
    struct mdns_ip_addr_s *next;            /*!< next IP address */
} mdns_ip_addr_t;

/**
 * @brief mDNS basic result structure
 */
typedef struct mdns_result_s {
    struct mdns_result_s *next;             /*!< next result */
    esp_netif_t *esp_netif;                 /*!< network interface */
    mdns_ip_addr_t *ip_addr;                /*!< linked list of IP addresses */
    mdns_ip_addr_t *addrv6;                 /*!< linked list of IPv6 addresses */
    const char *instance_name;              /*!< instance name */
    const char *txt;                        /*!< TXT data */
    const char *hostname;                   /*!< hostname */
    uint16_t port;                          /*!< service port */
    size_t txt_count;                       /*!< number of TXT items */
    const uint8_t *txt_value_len;           /*!< array of TXT value lengths */
} mdns_result_t;

typedef enum {
    MDNS_PROTO_TCP = 0,
    MDNS_PROTO_UDP = 1,
} mdns_sd_proto_t;

typedef enum {
    MDNS_EVENT_IP6_REVERSE_LOOKUP = (1 << 1),
    MDNS_EVENT_IP4_REVERSE_LOOKUP = (1 << 2),
    MDNS_EVENT_IP6_REVERSE_LOOKUP_SET = (1 << 3),
    MDNS_EVENT_IP4_REVERSE_LOOKUP_SET = (1 << 4),
    MDNS_EVENT_ANNOUNCE = (1 << 5),
    MDNS_EVENT_IP6_REVERSE_LOOKUP_FAILED = (1 << 6),
    MDNS_EVENT_IP4_REVERSE_LOOKUP_FAILED = (1 << 7),
} mdns_event_actions_t;

/**
 * @brief Initialize mDNS — records state in sim.
 */
esp_err_t mdns_init(void);

/**
 * @brief Deinitialize mDNS.
 */
void mdns_free(void);

/**
 * @brief Set hostname (→ <hostname>.local).
 */
esp_err_t mdns_hostname_set(const char *hostname);

/**
 * @brief Get hostname.
 */
esp_err_t mdns_hostname_get(char *hostname);

/**
 * @brief Set the default instance name.
 */
esp_err_t mdns_instance_name_set(const char *instance_name);

/**
 * @brief Add a service announcement (sim: recorded).
 */
esp_err_t mdns_service_add(const char *instance_name, const char *service_type,
                           const char *proto, uint16_t port,
                           mdns_txt_item_t txt[], size_t num_items);

esp_err_t mdns_service_add_for_host(const char *instance_name, const char *service_type,
                                    const char *proto, const char *hostname,
                                    uint16_t port, mdns_txt_item_t txt[],
                                    size_t num_items);

esp_err_t mdns_service_add_with_explicit_txt_item_value_len(
        const char *instance_name, const char *service_type, const char *proto,
        uint16_t port, mdns_txt_item_with_value_len_t txt[], size_t num_items);

esp_err_t mdns_service_add_for_host_with_explicit_txt_item_value_len(
        const char *instance_name, const char *service_type, const char *proto,
        const char *hostname, uint16_t port,
        mdns_txt_item_with_value_len_t txt[], size_t num_items);

/**
 * @brief Remove a service.
 */
esp_err_t mdns_service_remove(const char *service_type, const char *proto);
esp_err_t mdns_service_remove_for_host(const char *instance_name,
                                       const char *service_type, const char *proto,
                                       const char *hostname);

/**
 * @brief Remove all services.
 */
esp_err_t mdns_service_remove_all(void);

/**
 * @brief Change service port.
 */
esp_err_t mdns_service_port_set(const char *service_type, const char *proto, uint16_t port);
esp_err_t mdns_service_port_set_for_host(const char *instance_name,
                                         const char *service_type, const char *proto,
                                         const char *hostname, uint16_t port);

/**
 * @brief Set service instance name.
 */
esp_err_t mdns_service_instance_name_set(const char *service_type, const char *proto,
                                         const char *instance_name);
esp_err_t mdns_service_instance_name_set_for_host(const char *instance_name,
                                                  const char *service_type,
                                                  const char *proto, const char *hostname,
                                                  const char *service_instance_name);

/**
 * @brief Set TXT items of a service.
 */
esp_err_t mdns_service_txt_set(const char *service_type, const char *proto,
                               mdns_txt_item_t txt[], uint8_t num_items);
esp_err_t mdns_service_txt_set_for_host(const char *instance_name,
                                        const char *service_type, const char *proto,
                                        const char *hostname,
                                        mdns_txt_item_t txt[], uint8_t num_items);

/**
 * @brief Set a single TXT item of a service.
 */
esp_err_t mdns_service_txt_item_set(const char *service_type, const char *proto,
                                    const char *key, const char *value);
esp_err_t mdns_service_txt_item_set_for_host(const char *instance_name,
                                             const char *service_type, const char *proto,
                                             const char *hostname,
                                             const char *key, const char *value);

esp_err_t mdns_service_txt_item_set_with_explicit_value_len(
        const char *service_type, const char *proto, const char *key,
        const char *value, uint8_t value_len);

/**
 * @brief Remove a TXT item of a service.
 */
esp_err_t mdns_service_txt_item_remove(const char *service_type, const char *proto,
                                       const char *key);
esp_err_t mdns_service_txt_item_remove_for_host(const char *instance_name,
                                                const char *service_type,
                                                const char *proto, const char *hostname,
                                                const char *key);

/**
 * @brief Enable/disable mDNS on a netif (recorded).
 */
esp_err_t mdns_register_netif(esp_netif_t *esp_netif);
esp_err_t mdns_unregister_netif(esp_netif_t *esp_netif);
esp_err_t mdns_netif_action(esp_netif_t *esp_netif, mdns_event_actions_t event_action);

/**
 * @brief Browse/query — not modelled in sim; return ESP_ERR_NOT_SUPPORTED.
 */
esp_err_t mdns_query_a(const char *host_name, uint32_t timeout, esp_ip4_addr_t *addr);
esp_err_t mdns_query_aaaa(const char *host_name, uint32_t timeout, esp_ip6_addr_t *addr);
esp_err_t mdns_query_txt(const char *instance_name, const char *service_type,
                         const char *proto, uint32_t timeout, mdns_result_t **result);
esp_err_t mdns_query_srv(const char *instance_name, const char *service_type,
                         const char *proto, uint32_t timeout, mdns_result_t **result);
esp_err_t mdns_query_ptr(const char *service_type, const char *proto,
                         uint32_t timeout, uint8_t max_results, mdns_result_t **results);
esp_err_t mdns_query_async_delete(void);
void mdns_query_results_free(mdns_result_t *results);
esp_err_t mdns_query_end(void);

/**
 * @brief Delegate hostname/self-announce helpers.
 */
esp_err_t mdns_delegate_hostname_add(const char *hostname, mdns_ip_addr_t *address_set);
esp_err_t mdns_delegate_hostname_remove(const char *hostname);
bool mdns_delegate_hostname_exists(const char *hostname);

/**
 * @brief Manual announce (recorded).
 */
esp_err_t mdns_announce_pcb(esp_netif_t *tcpip_if, esp_netif_t *ip_protocol,
                            const char *services[], size_t num_services,
                            bool include_ip, bool include_ipv6);

/**
 * @brief Custom-subtype helpers — stubs.
 */
esp_err_t mdns_service_subtype_set_for_host(const char *instance_name,
                                            const char *service_type, const char *proto,
                                            const char *hostname, const char *subtype);

/**
 * @brief Browse for services — not modelled.
 */
esp_err_t mdns_browse_new(const char *service_type, const char *proto, void *notifier);
esp_err_t mdns_browse_delete(void *notifier);

/**
 * @brief Name of default mDNS service prefix (target-internal; harmless stub).
 */
esp_err_t mdns_service_discovery_timeout_set(uint32_t timeout);

#ifdef __cplusplus
}
#endif
