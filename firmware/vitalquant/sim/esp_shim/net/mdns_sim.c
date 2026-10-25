/* mdns_sim.c — sim mDNS responder.
 *
 * No multicast traffic on the host; the module records hostname,
 * instance name and announced services so scenarios can assert on the
 * log ("VB-MDNS") and the firmware exercises real call ordering.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdns.h"
#include "esp_err.h"

#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 0x105
#endif

#define SIM_MDNS_MAX_SVC   8
#define SIM_MDNS_NAME_LEN  64

typedef struct {
    bool     used;
    char     instance[SIM_MDNS_NAME_LEN];
    char     type[SIM_MDNS_NAME_LEN];
    char     proto[8];
    char     hostname[SIM_MDNS_NAME_LEN];
    uint16_t port;
} sim_mdns_svc_t;

static pthread_mutex_t  s_mtx = PTHREAD_MUTEX_INITIALIZER;
static bool             s_inited;
static char             s_hostname[SIM_MDNS_NAME_LEN];
static char             s_instance[SIM_MDNS_NAME_LEN];
static sim_mdns_svc_t   s_svc[SIM_MDNS_MAX_SVC];

/* ------------------------------------------------------------------ */

esp_err_t mdns_init(void)
{
    pthread_mutex_lock(&s_mtx);
    bool first = !s_inited;
    s_inited = true;
    pthread_mutex_unlock(&s_mtx);
    if (first) fprintf(stderr, "VB-MDNS init\n");
    return ESP_OK;
}

void mdns_free(void)
{
    pthread_mutex_lock(&s_mtx);
    s_inited        = false;
    s_hostname[0]   = '\0';
    s_instance[0]   = '\0';
    memset(s_svc, 0, sizeof(s_svc));
    pthread_mutex_unlock(&s_mtx);
}

esp_err_t mdns_hostname_set(const char *hostname)
{
    if (hostname == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    snprintf(s_hostname, sizeof(s_hostname), "%s", hostname);
    pthread_mutex_unlock(&s_mtx);
    fprintf(stderr, "VB-MDNS hostname '%s' → %s.local\n", hostname, hostname);
    return ESP_OK;
}

esp_err_t mdns_hostname_get(char *hostname)
{
    if (hostname == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    snprintf(hostname, SIM_MDNS_NAME_LEN, "%s", s_hostname);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t mdns_instance_name_set(const char *instance_name)
{
    if (instance_name == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    snprintf(s_instance, sizeof(s_instance), "%s", instance_name);
    pthread_mutex_unlock(&s_mtx);
    fprintf(stderr, "VB-MDNS instance '%s'\n", instance_name);
    return ESP_OK;
}

static esp_err_t svc_add_locked(const char *instance_name,
                                const char *service_type,
                                const char *proto,
                                const char *hostname,
                                uint16_t port)
{
    for (int i = 0; i < SIM_MDNS_MAX_SVC; i++) {
        if (s_svc[i].used &&
            strcmp(s_svc[i].type, service_type ? service_type : "") == 0 &&
            strcmp(s_svc[i].proto, proto ? proto : "") == 0) {
            /* update port/instance of an existing announcement */
            s_svc[i].port = port;
            snprintf(s_svc[i].instance, sizeof(s_svc[i].instance),
                     "%s", instance_name ? instance_name : "");
            return ESP_OK;
        }
    }
    for (int i = 0; i < SIM_MDNS_MAX_SVC; i++) {
        if (!s_svc[i].used) {
            s_svc[i].used = true;
            snprintf(s_svc[i].instance, sizeof(s_svc[i].instance),
                     "%s", instance_name ? instance_name : "");
            snprintf(s_svc[i].type, sizeof(s_svc[i].type),
                     "%s", service_type ? service_type : "");
            snprintf(s_svc[i].proto, sizeof(s_svc[i].proto),
                     "%s", proto ? proto : "");
            snprintf(s_svc[i].hostname, sizeof(s_svc[i].hostname),
                     "%s", hostname ? hostname : s_hostname);
            s_svc[i].port = port;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t mdns_service_add_for_host(const char *instance_name,
                                    const char *service_type,
                                    const char *proto,
                                    const char *hostname,
                                    uint16_t port,
                                    mdns_txt_item_t txt[],
                                    size_t num_items)
{
    (void)txt; (void)num_items;
    pthread_mutex_lock(&s_mtx);
    esp_err_t r = svc_add_locked(instance_name, service_type, proto,
                                 hostname, port);
    pthread_mutex_unlock(&s_mtx);
    if (r == ESP_OK) {
        fprintf(stderr, "VB-MDNS svc %s.%s:%u inst '%s' host '%s'\n",
                service_type ? service_type : "?",
                proto ? proto : "?",
                (unsigned)port,
                instance_name ? instance_name : "-",
                hostname ? hostname : s_hostname);
    }
    return r;
}

esp_err_t mdns_service_add(const char *instance_name,
                           const char *service_type,
                           const char *proto,
                           uint16_t port,
                           mdns_txt_item_t txt[],
                           size_t num_items)
{
    return mdns_service_add_for_host(instance_name, service_type, proto,
                                     NULL, port, txt, num_items);
}

esp_err_t mdns_service_add_with_explicit_txt_item_value_len(
        const char *instance_name, const char *service_type, const char *proto,
        uint16_t port, mdns_txt_item_with_value_len_t txt[], size_t num_items)
{
    (void)txt; (void)num_items;
    return mdns_service_add_for_host(instance_name, service_type, proto,
                                     NULL, port, NULL, 0);
}

esp_err_t mdns_service_add_for_host_with_explicit_txt_item_value_len(
        const char *instance_name, const char *service_type, const char *proto,
        const char *hostname, uint16_t port,
        mdns_txt_item_with_value_len_t txt[], size_t num_items)
{
    (void)txt; (void)num_items;
    return mdns_service_add_for_host(instance_name, service_type, proto,
                                     hostname, port, NULL, 0);
}

static esp_err_t svc_remove_locked(const char *service_type, const char *proto)
{
    for (int i = 0; i < SIM_MDNS_MAX_SVC; i++) {
        if (s_svc[i].used &&
            strcmp(s_svc[i].type, service_type ? service_type : "") == 0 &&
            strcmp(s_svc[i].proto, proto ? proto : "") == 0) {
            s_svc[i].used = false;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t mdns_service_remove(const char *service_type, const char *proto)
{
    pthread_mutex_lock(&s_mtx);
    esp_err_t r = svc_remove_locked(service_type, proto);
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_err_t mdns_service_remove_for_host(const char *instance_name,
                                       const char *service_type,
                                       const char *proto,
                                       const char *hostname)
{
    (void)instance_name; (void)hostname;
    return mdns_service_remove(service_type, proto);
}

esp_err_t mdns_service_remove_all(void)
{
    pthread_mutex_lock(&s_mtx);
    memset(s_svc, 0, sizeof(s_svc));
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t mdns_service_port_set(const char *service_type, const char *proto,
                                uint16_t port)
{
    pthread_mutex_lock(&s_mtx);
    esp_err_t r = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < SIM_MDNS_MAX_SVC; i++) {
        if (s_svc[i].used &&
            strcmp(s_svc[i].type, service_type ? service_type : "") == 0 &&
            strcmp(s_svc[i].proto, proto ? proto : "") == 0) {
            s_svc[i].port = port;
            r = ESP_OK;
        }
    }
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_err_t mdns_service_port_set_for_host(const char *instance_name,
                                         const char *service_type,
                                         const char *proto,
                                         const char *hostname,
                                         uint16_t port)
{
    (void)instance_name; (void)hostname;
    return mdns_service_port_set(service_type, proto, port);
}

esp_err_t mdns_service_instance_name_set(const char *service_type,
                                         const char *proto,
                                         const char *instance_name)
{
    pthread_mutex_lock(&s_mtx);
    esp_err_t r = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < SIM_MDNS_MAX_SVC; i++) {
        if (s_svc[i].used &&
            strcmp(s_svc[i].type, service_type ? service_type : "") == 0 &&
            strcmp(s_svc[i].proto, proto ? proto : "") == 0) {
            snprintf(s_svc[i].instance, sizeof(s_svc[i].instance),
                     "%s", instance_name ? instance_name : "");
            r = ESP_OK;
        }
    }
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_err_t mdns_service_instance_name_set_for_host(const char *instance_name,
                                                  const char *service_type,
                                                  const char *proto,
                                                  const char *hostname,
                                                  const char *service_instance_name)
{
    (void)instance_name; (void)hostname;
    return mdns_service_instance_name_set(service_type, proto,
                                          service_instance_name);
}

esp_err_t mdns_service_txt_set(const char *service_type, const char *proto,
                               mdns_txt_item_t txt[], uint8_t num_items)
{
    (void)service_type; (void)proto; (void)txt; (void)num_items;
    return ESP_OK;
}

esp_err_t mdns_service_txt_set_for_host(const char *instance_name,
                                        const char *service_type,
                                        const char *proto,
                                        const char *hostname,
                                        mdns_txt_item_t txt[],
                                        uint8_t num_items)
{
    (void)instance_name; (void)hostname;
    return mdns_service_txt_set(service_type, proto, txt, num_items);
}

esp_err_t mdns_service_txt_item_set(const char *service_type, const char *proto,
                                    const char *key, const char *value)
{
    (void)service_type; (void)proto; (void)key; (void)value;
    return ESP_OK;
}

esp_err_t mdns_service_txt_item_set_for_host(const char *instance_name,
                                             const char *service_type,
                                             const char *proto,
                                             const char *hostname,
                                             const char *key, const char *value)
{
    (void)instance_name; (void)hostname;
    return mdns_service_txt_item_set(service_type, proto, key, value);
}

esp_err_t mdns_service_txt_item_set_with_explicit_value_len(
        const char *service_type, const char *proto, const char *key,
        const char *value, uint8_t value_len)
{
    (void)value_len;
    return mdns_service_txt_item_set(service_type, proto, key, value);
}

esp_err_t mdns_service_txt_item_remove(const char *service_type,
                                       const char *proto, const char *key)
{
    (void)service_type; (void)proto; (void)key;
    return ESP_OK;
}

esp_err_t mdns_service_txt_item_remove_for_host(const char *instance_name,
                                                const char *service_type,
                                                const char *proto,
                                                const char *hostname,
                                                const char *key)
{
    (void)instance_name; (void)hostname;
    return mdns_service_txt_item_remove(service_type, proto, key);
}

esp_err_t mdns_register_netif(esp_netif_t *esp_netif)   { (void)esp_netif; return ESP_OK; }
esp_err_t mdns_unregister_netif(esp_netif_t *esp_netif) { (void)esp_netif; return ESP_OK; }
esp_err_t mdns_netif_action(esp_netif_t *esp_netif, mdns_event_actions_t a)
{
    (void)esp_netif; (void)a;
    return ESP_OK;
}

esp_err_t mdns_query_a(const char *host_name, uint32_t timeout, esp_ip4_addr_t *addr)
{
    (void)host_name; (void)timeout; (void)addr;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mdns_query_aaaa(const char *host_name, uint32_t timeout, esp_ip6_addr_t *addr)
{
    (void)host_name; (void)timeout; (void)addr;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mdns_query_txt(const char *instance_name, const char *service_type,
                         const char *proto, uint32_t timeout, mdns_result_t **result)
{
    (void)instance_name; (void)service_type; (void)proto; (void)timeout;
    (void)result;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mdns_query_srv(const char *instance_name, const char *service_type,
                         const char *proto, uint32_t timeout, mdns_result_t **result)
{
    (void)instance_name; (void)service_type; (void)proto; (void)timeout;
    (void)result;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mdns_query_ptr(const char *service_type, const char *proto,
                         uint32_t timeout, uint8_t max_results,
                         mdns_result_t **results)
{
    (void)service_type; (void)proto; (void)timeout; (void)max_results;
    (void)results;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mdns_query_async_delete(void) { return ESP_OK; }
void mdns_query_results_free(mdns_result_t *results) { (void)results; }
esp_err_t mdns_query_end(void) { return ESP_OK; }

esp_err_t mdns_delegate_hostname_add(const char *hostname, mdns_ip_addr_t *address_set)
{
    (void)hostname; (void)address_set;
    return ESP_OK;
}

esp_err_t mdns_delegate_hostname_remove(const char *hostname)
{
    (void)hostname;
    return ESP_OK;
}

bool mdns_delegate_hostname_exists(const char *hostname)
{
    (void)hostname;
    return false;
}

esp_err_t mdns_announce_pcb(esp_netif_t *tcpip_if, esp_netif_t *ip_protocol,
                            const char *services[], size_t num_services,
                            bool include_ip, bool include_ipv6)
{
    (void)tcpip_if; (void)ip_protocol; (void)services; (void)num_services;
    (void)include_ip; (void)include_ipv6;
    return ESP_OK;
}

esp_err_t mdns_service_subtype_set_for_host(const char *instance_name,
                                            const char *service_type,
                                            const char *proto,
                                            const char *hostname,
                                            const char *subtype)
{
    (void)instance_name; (void)service_type; (void)proto; (void)hostname;
    (void)subtype;
    return ESP_OK;
}

esp_err_t mdns_browse_new(const char *service_type, const char *proto, void *notifier)
{
    (void)service_type; (void)proto; (void)notifier;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mdns_browse_delete(void *notifier)
{
    (void)notifier;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mdns_service_discovery_timeout_set(uint32_t timeout)
{
    (void)timeout;
    return ESP_OK;
}
