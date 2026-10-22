/* netif_sim.c — sim esp-netif: heap objects carrying {ip_info, hostname,
 * dhcp/dns state}. The wifi shim fabricates DHCP results by poking
 * sim_netif_assign_ip() before posting IP_EVENT_STA_GOT_IP.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_netif.h"
#include "esp_err.h"
#include "sim_net_priv.h"

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

#define SIM_NETIF_MAGIC  0x4E455449u  /* 'NETI' */
#define SIM_HOSTNAME_LEN 32

struct esp_netif_obj {
    uint32_t              magic;
    char                  ifkey[8];
    bool                  is_sta;
    esp_netif_ip_info_t   ip;
    bool                  ip_set;
    char                  hostname[SIM_HOSTNAME_LEN];
    esp_netif_flags_t     flags;
    esp_netif_dhcp_status_t dhcpc;
    esp_netif_dns_info_t  dns[ESP_NETIF_DNS_MAX];
    void                 *driver;
    esp_netif_t          *next;
};

static pthread_mutex_t  s_mtx = PTHREAD_MUTEX_INITIALIZER;
static bool             s_inited;
static esp_netif_t     *s_list;
static esp_netif_t     *s_sta_netif;
static esp_netif_t     *s_ap_netif;

ESP_EVENT_DEFINE_BASE(IP_EVENT);

/* ------------------------------------------------------------------ */
/* Internal hooks (sim_net_priv.h)                                    */
/* ------------------------------------------------------------------ */

bool sim_netif_inited(void)
{
    pthread_mutex_lock(&s_mtx);
    bool r = s_inited;
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_netif_t *sim_netif_sta(void)
{
    pthread_mutex_lock(&s_mtx);
    esp_netif_t *r = s_sta_netif;
    pthread_mutex_unlock(&s_mtx);
    return r;
}

void sim_netif_assign_ip(esp_netif_t *netif, const esp_netif_ip_info_t *ip)
{
    if (netif == NULL || ip == NULL) return;
    pthread_mutex_lock(&s_mtx);
    if (netif->magic == SIM_NETIF_MAGIC) {
        netif->ip     = *ip;
        netif->ip_set = true;
    }
    pthread_mutex_unlock(&s_mtx);
}

void sim_netif_clear_ip(esp_netif_t *netif)
{
    if (netif == NULL) return;
    pthread_mutex_lock(&s_mtx);
    if (netif->magic == SIM_NETIF_MAGIC) {
        memset(&netif->ip, 0, sizeof(netif->ip));
        netif->ip_set = false;
    }
    pthread_mutex_unlock(&s_mtx);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

esp_err_t esp_netif_init(void)
{
    pthread_mutex_lock(&s_mtx);
    s_inited = true;                        /* idempotent, like IDF 5.x */
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_deinit(void)
{
    pthread_mutex_lock(&s_mtx);
    s_inited = false;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

static esp_netif_t *netif_alloc(const char *ifkey, bool is_sta,
                                esp_netif_flags_t flags)
{
    if (!s_inited) {
        /* IDF creates netifs only after esp_netif_init; mirror that. */
        return NULL;
    }
    esp_netif_t *n = calloc(1, sizeof(*n));
    if (n == NULL) return NULL;
    n->magic  = SIM_NETIF_MAGIC;
    n->is_sta = is_sta;
    n->flags  = flags;
    snprintf(n->ifkey, sizeof(n->ifkey), "%s", ifkey);
    n->dhcpc  = ESP_NETIF_DHCP_INIT;
    n->next   = s_list;
    s_list    = n;
    return n;
}

esp_netif_t *esp_netif_create_default_wifi_sta(void)
{
    pthread_mutex_lock(&s_mtx);
    if (s_sta_netif == NULL) {
        s_sta_netif = netif_alloc("WIFI_STA_DEF", true,
                                  ESP_NETIF_FLAG_DHCP_CLIENT |
                                  ESP_NETIF_FLAG_GARP |
                                  ESP_NETIF_FLAG_EVENT_IP_MODIFIED);
    }
    esp_netif_t *r = s_sta_netif;
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_netif_t *esp_netif_create_default_wifi_ap(void)
{
    pthread_mutex_lock(&s_mtx);
    if (s_ap_netif == NULL) {
        s_ap_netif = netif_alloc("WIFI_AP_DEF", false,
                                 ESP_NETIF_FLAG_DHCP_SERVER |
                                 ESP_NETIF_FLAG_AUTOUP);
        if (s_ap_netif != NULL) {
            /* default AP config 192.168.4.1/24 gw=self */
            s_ap_netif->ip.ip.addr     = ESP_IP4TOADDR(192, 168, 4, 1);
            s_ap_netif->ip.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
            s_ap_netif->ip.gw.addr     = ESP_IP4TOADDR(192, 168, 4, 1);
            s_ap_netif->ip_set         = true;
        }
    }
    esp_netif_t *r = s_ap_netif;
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg)
{
    (void)cfg;
    pthread_mutex_lock(&s_mtx);
    esp_netif_t *n = netif_alloc("CUSTOM", false, 0);
    pthread_mutex_unlock(&s_mtx);
    return n;
}

void esp_netif_destroy(esp_netif_t *esp_netif)
{
    if (esp_netif == NULL) return;
    pthread_mutex_lock(&s_mtx);
    esp_netif_t **pp = &s_list;
    while (*pp != NULL) {
        if (*pp == esp_netif) { *pp = esp_netif->next; break; }
        pp = &(*pp)->next;
    }
    if (s_sta_netif == esp_netif) s_sta_netif = NULL;
    if (s_ap_netif  == esp_netif) s_ap_netif  = NULL;
    esp_netif->magic = 0;
    pthread_mutex_unlock(&s_mtx);
    free(esp_netif);
}

static bool netif_ok(const esp_netif_t *n)
{
    return n != NULL && n->magic == SIM_NETIF_MAGIC;
}

/* ------------------------------------------------------------------ */
/* IP configuration                                                   */
/* ------------------------------------------------------------------ */

esp_err_t esp_netif_get_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info)
{
    if (ip_info == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    *ip_info = esp_netif->ip;      /* zeros when unset, like a down iface */
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_set_ip_info(esp_netif_t *esp_netif,
                                const esp_netif_ip_info_t *ip_info)
{
    if (ip_info == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    esp_netif->ip     = *ip_info;
    esp_netif->ip_set = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_set_ip4_addr(esp_netif_t *esp_netif, const esp_ip4_addr_t *addr)
{
    if (addr == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    esp_netif->ip.ip  = *addr;
    esp_netif->ip_set = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_get_ip4_addr(esp_netif_t *esp_netif, esp_ip4_addr_t *addr)
{
    if (addr == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    *addr = esp_netif->ip.ip;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_get_hostname(esp_netif_t *esp_netif, const char **hostname)
{
    if (hostname == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    *hostname = esp_netif->hostname[0] ? esp_netif->hostname : NULL;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_set_hostname(esp_netif_t *esp_netif, const char *hostname)
{
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(esp_netif->hostname, sizeof(esp_netif->hostname),
             "%s", hostname ? hostname : "");
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_attach(esp_netif_t *esp_netif,
                           esp_netif_iodriver_handle driver_handle)
{
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    esp_netif->driver = driver_handle;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_netif_flags_t esp_netif_get_flags(esp_netif_t *esp_netif)
{
    pthread_mutex_lock(&s_mtx);
    esp_netif_flags_t f = netif_ok(esp_netif) ? esp_netif->flags : 0;
    pthread_mutex_unlock(&s_mtx);
    return f;
}

const char *esp_netif_get_ifkey(esp_netif_t *esp_netif)
{
    return netif_ok(esp_netif) ? esp_netif->ifkey : "";
}

/* ------------------------------------------------------------------ */
/* DHCP / DNS                                                         */
/* ------------------------------------------------------------------ */

esp_err_t esp_netif_dhcpc_start(esp_netif_t *esp_netif)
{
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_INVALID_ARG; }
    esp_netif->dhcpc = ESP_NETIF_DHCP_STARTED;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_dhcpc_stop(esp_netif_t *esp_netif)
{
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_INVALID_ARG; }
    esp_netif->dhcpc = ESP_NETIF_DHCP_STOPPED;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_dhcpc_get_status(esp_netif_t *esp_netif,
                                     esp_netif_dhcp_status_t *status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_INVALID_ARG; }
    *status = esp_netif->dhcpc;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_dhcps_get_status(esp_netif_t *esp_netif,
                                     esp_netif_dhcp_status_t *status)
{
    return esp_netif_dhcpc_get_status(esp_netif, status);
}

esp_err_t esp_netif_dhcps_start(esp_netif_t *esp_netif)
{
    return esp_netif_dhcpc_start(esp_netif);
}

esp_err_t esp_netif_dhcps_stop(esp_netif_t *esp_netif)
{
    return esp_netif_dhcpc_stop(esp_netif);
}

esp_err_t esp_netif_set_dns_info(esp_netif_t *esp_netif,
                                 esp_netif_dns_type_t type,
                                 esp_netif_dns_info_t *dns)
{
    if (type >= ESP_NETIF_DNS_MAX || dns == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_INVALID_ARG; }
    esp_netif->dns[type] = *dns;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_netif_get_dns_info(esp_netif_t *esp_netif,
                                 esp_netif_dns_type_t type,
                                 esp_netif_dns_info_t *dns)
{
    if (type >= ESP_NETIF_DNS_MAX || dns == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!netif_ok(esp_netif)) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_INVALID_ARG; }
    *dns = esp_netif->dns[type];
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* tcpip_exec + iteration                                             */
/* ------------------------------------------------------------------ */

esp_err_t esp_netif_tcpip_exec(esp_netif_callback_fn fn, void *ctx)
{
    if (fn == NULL) return ESP_ERR_INVALID_ARG;
    if (!sim_netif_inited()) return ESP_ERR_INVALID_STATE;
    return fn(ctx);      /* sim: inline — no lwip thread to hop onto */
}

esp_netif_t *esp_netif_next_unsafe(esp_netif_t *esp_netif)
{
    pthread_mutex_lock(&s_mtx);
    esp_netif_t *r = (esp_netif == NULL) ? s_list : esp_netif->next;
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_netif_t *esp_netif_find_if(esp_netif_find_predicate_t fn, void *ctx)
{
    if (fn == NULL) return NULL;
    pthread_mutex_lock(&s_mtx);
    esp_netif_t *found = NULL;
    for (esp_netif_t *n = s_list; n != NULL; n = n->next) {
        if (fn(n, ctx)) { found = n; break; }
    }
    pthread_mutex_unlock(&s_mtx);
    return found;
}

/* ------------------------------------------------------------------ */
/* Address text helpers                                               */
/* ------------------------------------------------------------------ */

char *esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, int buflen)
{
    if (addr == NULL || buf == NULL || buflen <= 0) return NULL;
    const uint8_t *b = (const uint8_t *)&addr->addr;   /* network order */
    int need = snprintf(buf, (size_t)buflen, "%u.%u.%u.%u",
                        b[0], b[1], b[2], b[3]);
    if (need < 0 || need >= buflen) return NULL;
    return buf;
}

uint32_t esp_ip4addr_aton(const char *addr)
{
    if (addr == NULL) return 0;
    unsigned a, b, c, d;
    char tail;
    if (sscanf(addr, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255) return 0;
    return ESP_IP4TOADDR(a, b, c, d);
}

void esp_netif_to_ip4_addr(const esp_ip4_addr_t *esp_ip4, esp_ip4_addr_t *ip4)
{
    if (esp_ip4 != NULL && ip4 != NULL) *ip4 = *esp_ip4;
}
