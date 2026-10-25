/* esp_netif_types.h — host-sim shim of esp-netif shared types. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

struct esp_netif_obj;
typedef struct esp_netif_obj esp_netif_t;

/** IP event declarations */
typedef enum {
    IP_EVENT_STA_GOT_IP,               /*!< station got IP from connected AP */
    IP_EVENT_STA_LOST_IP,              /*!< station lost IP and the IP is reset to 0 */
    IP_EVENT_AP_STAIPASSIGNED,         /*!< soft-AP assign an IP to a connected station */
    IP_EVENT_GOT_IP6,                  /*!< station or ap got IPv6 address */
    IP_EVENT_ETH_GOT_IP,               /*!< ethernet got IP from connected AP */
    IP_EVENT_ETH_LOST_IP,              /*!< ethernet lost IP */
    IP_EVENT_PPP_GOT_IP,               /*!< PPP interface got IP */
    IP_EVENT_PPP_LOST_IP,              /*!< PPP interface lost IP */
    IP_EVENT_TX_RX,                    /*!< transmitting/receiving data packet */
} ip_event_t;

/** Event base for all IP events */
ESP_EVENT_DECLARE_BASE(IP_EVENT);

/** IP address info (IPv4) — also usable for ESP_IPADDR_TYPE_V4 config */
typedef struct {
    esp_ip4_addr_t ip;      /*!< Interface IPV4 address */
    esp_ip4_addr_t netmask; /*!< Interface IPV4 netmask */
    esp_ip4_addr_t gw;      /*!< Interface IPV4 gateway address */
} esp_netif_ip_info_t;

/** IPV6 IP address information */
typedef struct {
    esp_ip6_addr_t ip;      /*!< Interface IPV6 address */
} esp_netif_ip6_info_t;

/** Event structure for IP_EVENT_STA_GOT_IP, IP_EVENT_ETH_GOT_IP events */
typedef struct {
    esp_netif_t *esp_netif;          /*!< Pointer to corresponding esp-netif object */
    esp_netif_ip_info_t ip_info;     /*!< IP address, netmask, gateway IP address */
    bool ip_changed;                 /*!< Whether the assigned IP has changed or not */
} ip_event_got_ip_t;

/** Event structure for IP_EVENT_GOT_IP6 event */
typedef struct {
    esp_netif_t *esp_netif;          /*!< Pointer to corresponding esp-netif object */
    esp_netif_ip6_info_t ip6_info;   /*!< IPv6 address of the interface */
    int ip_index;                    /*!< IPv6 address index */
} ip_event_got_ip6_t;

/** Event structure for IP_EVENT_STA_LOST_IP (same payload as got_ip) */
typedef ip_event_got_ip_t ip_event_lost_ip_t;

/** Event structure for ADD_IP6 event */
typedef struct {
    esp_ip6_addr_t addr;             /*!< The address to be added */
    bool preferred;                  /*!< The default preference of the address */
} ip_event_add_ip6_t;

/** Event structure for IP_EVENT_AP_STAIPASSIGNED event */
typedef struct {
    esp_netif_t *esp_netif;          /*!< Pointer to the associated netif handle */
    esp_ip4_addr_t ip;               /*!< IP address which was assigned to the station */
    uint8_t mac[6];                  /*!< MAC address of the connected client */
} ip_event_ap_staipassigned_t;

/**
 * @brief IP event action table (unused in sim, kept for signature parity)
 */
typedef esp_err_t (*esp_netif_action_t)(void *esp_netif, esp_event_base_t base,
                                        int32_t event_id, void *data);

typedef enum {
    ESP_NETIF_DNS_MAIN = 0,       /*!< DNS main server address */
    ESP_NETIF_DNS_BACKUP,         /*!< DNS backup server address */
    ESP_NETIF_DNS_FALLBACK,       /*!< DNS fallback server address */
    ESP_NETIF_DNS_MAX
} esp_netif_dns_type_t;

typedef struct {
    esp_ip_addr_t ip;             /*!< IPV4 address */
} esp_netif_dns_info_t;

typedef enum {
    ESP_NETIF_DHCP_INIT = 0,      /*!< DHCP started */
    ESP_NETIF_DHCP_STARTED,       /*!< DHCP started */
    ESP_NETIF_DHCP_STOPPED,       /*!< DHCP stopped */
    ESP_NETIF_DHCP_STATUS_MAX
} esp_netif_dhcp_status_t;

typedef enum {
    ESP_NETIF_OP_START = 0,
    ESP_NETIF_OP_SET,
    ESP_NETIF_OP_GET,
    ESP_NETIF_OP_MAX
} esp_netif_dhcp_option_mode_t;

typedef enum {
    ESP_NETIF_SUBNET_MASK = 1,
    ESP_NETIF_DOMAIN_NAME_SERVER = 6,
    ESP_NETIF_ROUTER_SOLICITATION_ADDRESS = 32,
    ESP_NETIF_REQUESTED_IP_ADDRESS = 50,
    ESP_NETIF_IP_ADDRESS_LEASE_TIME = 51,
    ESP_NETIF_IP_REQUEST_RETRY_TIME = 52,
} esp_netif_dhcp_option_id_t;

typedef enum {
    ESP_NETIF_FLAG_DHCP_CLIENT = 1 << 0,
    ESP_NETIF_FLAG_DHCP_SERVER = 1 << 1,
    ESP_NETIF_FLAG_AUTOUP = 1 << 2,
    ESP_NETIF_FLAG_GARP = 1 << 3,
    ESP_NETIF_FLAG_EVENT_IP_MODIFIED = 1 << 4,
    ESP_NETIF_FLAG_IS_PPP = 1 << 5,
    ESP_NETIF_FLAG_IS_BRIDGE = 1 << 6,
    ESP_NETIF_FLAG_MLDV6_REPORT = 1 << 7,
} esp_netif_flags_t;

typedef enum esp_netif_ip_event_type {
    ESP_NETIF_IP_EVENT_GOT_IP = 1,
    ESP_NETIF_IP_EVENT_LOST_IP = 2,
} esp_netif_ip_event_type_t;

/** Transmit/receive callback signature (unused in sim) */
typedef esp_err_t (*esp_netif_receive_t)(esp_netif_t *esp_netif, void *buffer,
                                         size_t len, void *eb);
typedef esp_err_t (*esp_netif_transmit_t)(esp_netif_t *esp_netif, void *buffer,
                                          size_t len);
typedef esp_err_t (*esp_netif_transmit_wrap_t)(esp_netif_t *esp_netif, void *buffer,
                                               size_t len, void *netstack_buffer);
typedef void (*esp_netif_free_rx_buffer_t)(void *h, void *buffer);

/** I/O driver handle (opaque in sim) */
typedef void *esp_netif_iodriver_handle;

/** esp-netif driver base glue (shape-parity; sim netifs are self-contained) */
typedef struct esp_netif_driver_base_s {
    esp_err_t (*post_attach)(esp_netif_t *netif, esp_netif_iodriver_handle h);
    esp_netif_t *netif;
} esp_netif_driver_base_t;

typedef struct esp_netif_config {
    const esp_netif_driver_base_t *base;
    const void *stack;
    const void *driver;
} esp_netif_config_t;

#ifdef __cplusplus
}
#endif
