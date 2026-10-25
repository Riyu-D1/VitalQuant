/* esp_netif_ip_addr.h — host-sim shim of lwIP-style address types.
 *
 * esp_ip4_addr_t stores the address as a uint32 in *network byte order*
 * (matching lwIP on the target, and esp_netif_htonl below), so IP2STR /
 * esp_ip4_addr_get_byte / esp_ip4addr_ntoa all agree.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define esp_netif_htonl(x) ((((x) & (uint32_t)0x000000ffUL) << 24) | \
                            (((x) & (uint32_t)0x0000ff00UL) <<  8) | \
                            (((x) & (uint32_t)0x00ff0000UL) >>  8) | \
                            (((x) & (uint32_t)0xff000000UL) >> 24))
#else
#define esp_netif_htonl(x) ((uint32_t)(x))
#endif

#define esp_netif_htons(x) ((uint16_t)((((x) & (uint16_t)0x00ffU) << 8) | \
                                       (((x) & (uint16_t)0xff00U) >> 8)))
#define esp_netif_ntohs(x) esp_netif_htons(x)
#define esp_netif_ntohl(x) esp_netif_htonl(x)

/* IPv4 address — one uint32, network byte order */
struct esp_ip4_addr {
    uint32_t addr;  /*!< IPv4 address */
};
typedef struct esp_ip4_addr esp_ip4_addr_t;

/* IPv6 address */
struct esp_ip6_addr {
    uint32_t addr[4]; /*!< IPv6 address */
    uint8_t zone;     /*!< zone ID */
};
typedef struct esp_ip6_addr esp_ip6_addr_t;

/* Generic IP address (v4 or v6) */
typedef struct _ip_addr {
    union {
        esp_ip6_addr_t ip6; /*!< IPv6 address */
        esp_ip4_addr_t ip4; /*!< IPv4 address */
    } u_addr;               /*!< IP address union */
    uint8_t type;           /*!< address type (ESP_IPADDR_TYPE_*) */
} esp_ip_addr_t;

#define ESP_IPADDR_TYPE_V4  0U
#define ESP_IPADDR_TYPE_V6  6U
#define ESP_IPADDR_TYPE_ANY 46U

/* Byte accessors — operate on the stored (network order) value */
#define esp_ip4_addr_get_byte(ipaddr, idx) (((const uint8_t*)(&(ipaddr)->addr))[idx])
#define esp_ip4_addr1(ipaddr)   esp_ip4_addr_get_byte(ipaddr, 0)
#define esp_ip4_addr2(ipaddr)   esp_ip4_addr_get_byte(ipaddr, 1)
#define esp_ip4_addr3(ipaddr)   esp_ip4_addr_get_byte(ipaddr, 2)
#define esp_ip4_addr4(ipaddr)   esp_ip4_addr_get_byte(ipaddr, 3)

#define esp_ip4_addr1_16(ipaddr) ((uint16_t)esp_ip4_addr1(ipaddr))
#define esp_ip4_addr2_16(ipaddr) ((uint16_t)esp_ip4_addr2(ipaddr))
#define esp_ip4_addr3_16(ipaddr) ((uint16_t)esp_ip4_addr3(ipaddr))
#define esp_ip4_addr4_16(ipaddr) ((uint16_t)esp_ip4_addr4(ipaddr))

#define IPSTR "%d.%d.%d.%d"
#define IP2STR(ipaddr) esp_ip4_addr1_16(ipaddr), \
                       esp_ip4_addr2_16(ipaddr), \
                       esp_ip4_addr3_16(ipaddr), \
                       esp_ip4_addr4_16(ipaddr)

#define ESP_IP4TOUINT32(a,b,c,d) (((uint32_t)((a) & 0xffU) << 24) | \
                                  ((uint32_t)((b) & 0xffU) << 16) | \
                                  ((uint32_t)((c) & 0xffU) <<  8) | \
                                   (uint32_t)((d) & 0xffU))
#define ESP_IP4TOADDR(a,b,c,d)  esp_netif_htonl(ESP_IP4TOUINT32(a, b, c, d))

#define ESP_IP4ADDR_INIT(a, b, c, d) \
    { .u_addr = { .ip4 = { .addr = ESP_IP4TOADDR(a, b, c, d) } }, .type = ESP_IPADDR_TYPE_V4 }

#ifndef IP4ADDR_STRLEN_MAX
#define IP4ADDR_STRLEN_MAX  16
#endif

#define ESP_IP6_ADDR_BLOCK1(ip6addr) ((uint16_t)((esp_netif_htonl((ip6addr)->addr[0]) >> 16) & 0xffff))
#define ESP_IP6_ADDR_BLOCK2(ip6addr) ((uint16_t)((esp_netif_htonl((ip6addr)->addr[0])) & 0xffff))
#define ESP_IP6_ADDR_BLOCK3(ip6addr) ((uint16_t)((esp_netif_htonl((ip6addr)->addr[1]) >> 16) & 0xffff))
#define ESP_IP6_ADDR_BLOCK4(ip6addr) ((uint16_t)((esp_netif_htonl((ip6addr)->addr[1])) & 0xffff))
#define ESP_IP6_ADDR_BLOCK5(ip6addr) ((uint16_t)((esp_netif_htonl((ip6addr)->addr[2]) >> 16) & 0xffff))
#define ESP_IP6_ADDR_BLOCK6(ip6addr) ((uint16_t)((esp_netif_htonl((ip6addr)->addr[2])) & 0xffff))
#define ESP_IP6_ADDR_BLOCK7(ip6addr) ((uint16_t)((esp_netif_htonl((ip6addr)->addr[3]) >> 16) & 0xffff))
#define ESP_IP6_ADDR_BLOCK8(ip6addr) ((uint16_t)((esp_netif_htonl((ip6addr)->addr[3])) & 0xffff))

#define IPV6STR "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x"
#define IPV62STR(ipaddr) ESP_IP6_ADDR_BLOCK1(&(ipaddr)),  \
                         ESP_IP6_ADDR_BLOCK2(&(ipaddr)),  \
                         ESP_IP6_ADDR_BLOCK3(&(ipaddr)),  \
                         ESP_IP6_ADDR_BLOCK4(&(ipaddr)),  \
                         ESP_IP6_ADDR_BLOCK5(&(ipaddr)),  \
                         ESP_IP6_ADDR_BLOCK6(&(ipaddr)),  \
                         ESP_IP6_ADDR_BLOCK7(&(ipaddr)),  \
                         ESP_IP6_ADDR_BLOCK8(&(ipaddr))

typedef enum {
    ESP_IP6_ADDR_IS_UNKNOWN,
    ESP_IP6_ADDR_IS_GLOBAL,
    ESP_IP6_ADDR_IS_LINK_LOCAL,
    ESP_IP6_ADDR_IS_SITE_LOCAL,
    ESP_IP6_ADDR_IS_UNIQUE_LOCAL,
    ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6
} esp_ip6_addr_type_t;

#ifdef __cplusplus
}
#endif
