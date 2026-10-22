/* wifi_sim.c — sim Wi-Fi driver (STA mode only).
 *
 * Connection model: esp_wifi_connect() spawns a worker that waits ~30 ms
 * on the vboard clock, then either
 *   - posts WIFI_EVENT_STA_CONNECTED, fabricates a DHCP lease on the
 *     default STA netif (192.168.7.42/24, gw 192.168.7.1) and posts
 *     IP_EVENT_STA_GOT_IP, or
 *   - posts WIFI_EVENT_STA_DISCONNECTED (reason NO_AP_FOUND) when
 *     g_vb_faults.wifi_link_down is set.
 *
 * esp_wifi_disconnect() (or stop/deinit) bumps a generation counter so an
 * in-flight worker abandons its post — the same "cancelled connect"
 * behaviour the real driver has.
 *
 * All delays use vboard_clock_sleep_us() → scenario time-accel applies.
 */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_err.h"
#include "vboard.h"
#include "sim_net_priv.h"

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define SIM_WIFI_CONNECT_DELAY_US   (30 * 1000)    /* ~30 ms assoc+dhcp    */
#define SIM_WIFI_SCAN_DELAY_US      (10 * 1000)    /* ~10 ms scan          */

#define SIM_STA_IP    ESP_IP4TOADDR(192, 168, 7, 42)
#define SIM_STA_MASK  ESP_IP4TOADDR(255, 255, 255, 0)
#define SIM_STA_GW    ESP_IP4TOADDR(192, 168, 7, 1)

#define SIM_BSSID     { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 }  /* fake AP    */
#define SIM_CHANNEL   6
#define SIM_RSSI_OK   (-58)
#define SIM_RSSI_BAD  (-95)

/* TX power in 0.25 dBm units; 80 = 20 dBm = driver default/max. The hw
 * brief forbids RF tuning — anything above is logged. */
#define SIM_TXPWR_MAX_UNITS  80

static const uint8_t  SIM_MAC_STA[6]  = { 0x24, 0x6F, 0x28, 0x51, 0x1A, 0x2B };
static const uint8_t  SIM_MAC_AP[6]   = { 0x24, 0x6F, 0x28, 0x51, 0x1A, 0x2C };
static const uint8_t  SIM_MAC_BT[6]   = { 0x24, 0x6F, 0x28, 0x51, 0x1A, 0x2D };
static const uint8_t  SIM_MAC_ETH[6]  = { 0x24, 0x6F, 0x28, 0x51, 0x1A, 0x2E };

ESP_EVENT_DEFINE_BASE(WIFI_EVENT);

#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 0x105
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    bool              inited;
    bool              started;
    bool              wifi_ready_sent;
    wifi_mode_t       mode;
    bool              mode_set;

    wifi_config_t     sta_cfg;
    bool              sta_cfg_set;

    bool              connecting;     /* connect attempt in flight     */
    bool              connected;      /* associated to the sim AP      */
    bool              ip_up;          /* fabricated lease active       */
    unsigned long     gen;            /* bump cancels pending workers  */

    wifi_storage_t    storage;
    wifi_ps_type_t    ps;
    uint8_t           proto_bitmap[WIFI_IF_MAX];
    wifi_bandwidth_t  bw[WIFI_IF_MAX];
    uint8_t           primary_chan;
    wifi_second_chan_t second_chan;
    int8_t            tx_power;       /* 0.25 dBm units                */
    bool              auto_connect;
    bool              promiscuous;
    uint8_t           sta_mac[6];
    uint8_t           ap_mac[6];
    uint16_t          ap_num;         /* fabricated scan results       */
} sim_wifi_t;

static pthread_mutex_t s_mtx = PTHREAD_MUTEX_INITIALIZER;
static sim_wifi_t      s_wifi;

static void sim_log(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
static void sim_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("VB-WIFI ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/* Event fabrication                                                  */
/* ------------------------------------------------------------------ */

static void post_sta_connected(void)
{
    wifi_event_sta_connected_t ev;
    memset(&ev, 0, sizeof(ev));
    pthread_mutex_lock(&s_mtx);
    size_t sl = strlen((const char *)s_wifi.sta_cfg.sta.ssid);
    if (sl > sizeof(ev.ssid)) sl = sizeof(ev.ssid);
    memcpy(ev.ssid, s_wifi.sta_cfg.sta.ssid, sl);
    ev.ssid_len = (uint8_t)sl;
    const uint8_t bssid[6] = SIM_BSSID;
    memcpy(ev.bssid, bssid, 6);
    ev.channel  = SIM_CHANNEL;
    ev.authmode = s_wifi.sta_cfg.sta.threshold.authmode;
    ev.aid      = 1;
    pthread_mutex_unlock(&s_mtx);

    esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                   &ev, sizeof(ev), 0);
}

/* Assigns the fabricated lease to the default STA netif, then posts
 * IP_EVENT_STA_GOT_IP. Must run *after* STA_CONNECTED is queued. */
static void post_got_ip(void)
{
    ip_event_got_ip_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.esp_netif           = sim_netif_sta();
    ev.ip_info.ip.addr     = SIM_STA_IP;
    ev.ip_info.netmask.addr = SIM_STA_MASK;
    ev.ip_info.gw.addr     = SIM_STA_GW;
    ev.ip_changed          = true;

    sim_netif_assign_ip(ev.esp_netif, &ev.ip_info);
    pthread_mutex_lock(&s_mtx);
    s_wifi.ip_up = true;            /* set before posting: a handler may    */
    pthread_mutex_unlock(&s_mtx);   /* call esp_wifi_disconnect immediately */
    esp_event_post(IP_EVENT, IP_EVENT_STA_GOT_IP, &ev, sizeof(ev), 0);
}

static void post_sta_disconnected(uint8_t reason)
{
    wifi_event_sta_disconnected_t ev;
    memset(&ev, 0, sizeof(ev));
    pthread_mutex_lock(&s_mtx);
    size_t sl = strlen((const char *)s_wifi.sta_cfg.sta.ssid);
    if (sl > sizeof(ev.ssid)) sl = sizeof(ev.ssid);
    memcpy(ev.ssid, s_wifi.sta_cfg.sta.ssid, sl);
    ev.ssid_len = (uint8_t)sl;
    const uint8_t bssid[6] = SIM_BSSID;
    memcpy(ev.bssid, bssid, 6);
    bool had_ip = s_wifi.ip_up;
    s_wifi.ip_up = false;
    pthread_mutex_unlock(&s_mtx);

    /* If a lease was held, it goes away first (netif + LOST_IP). */
    if (had_ip) {
        sim_netif_clear_ip(sim_netif_sta());
        ip_event_got_ip_t lev;
        memset(&lev, 0, sizeof(lev));
        lev.esp_netif  = sim_netif_sta();
        lev.ip_changed = false;
        esp_event_post(IP_EVENT, IP_EVENT_STA_LOST_IP, &lev, sizeof(lev), 0);
    }
    ev.reason = reason;
    ev.rssi   = SIM_RSSI_BAD;
    esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                   &ev, sizeof(ev), 0);
}

/* ------------------------------------------------------------------ */
/* Async workers                                                      */
/* ------------------------------------------------------------------ */

static void *connect_worker(void *arg)
{
    const unsigned long gen = (unsigned long)arg;

    vboard_clock_sleep_us(SIM_WIFI_CONNECT_DELAY_US);

    pthread_mutex_lock(&s_mtx);
    bool live = s_wifi.connecting && s_wifi.gen == gen;
    bool link_down = g_vb_faults.wifi_link_down;
    pthread_mutex_unlock(&s_mtx);
    if (!live) return NULL;                       /* cancelled/disconnected */

    if (link_down) {
        pthread_mutex_lock(&s_mtx);
        s_wifi.connecting = false;
        pthread_mutex_unlock(&s_mtx);
        post_sta_disconnected(WIFI_REASON_NO_AP_FOUND);
        return NULL;
    }

    pthread_mutex_lock(&s_mtx);
    s_wifi.connecting = false;
    s_wifi.connected  = true;
    pthread_mutex_unlock(&s_mtx);

    post_sta_connected();
    post_got_ip();
    return NULL;
}

static void *scan_worker(void *arg)
{
    const unsigned long gen = (unsigned long)arg;

    vboard_clock_sleep_us(SIM_WIFI_SCAN_DELAY_US);

    pthread_mutex_lock(&s_mtx);
    bool live = s_wifi.started && s_wifi.gen == gen;
    pthread_mutex_unlock(&s_mtx);
    if (!live) return NULL;

    wifi_event_sta_scan_done_t ev = {
        .status  = 0,
        .number  = 1,
        .scan_id = 0,
    };
    esp_event_post(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &ev, sizeof(ev), 0);
    return NULL;
}

static void spawn_worker(void *(*fn)(void *), unsigned long gen)
{
    pthread_t t;
    if (pthread_create(&t, NULL, fn, (void *)gen) == 0) {
        pthread_detach(t);
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

esp_err_t esp_wifi_init(const wifi_init_config_t *config)
{
    if (config == NULL || config->magic != WIFI_INIT_CONFIG_MAGIC) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    memset(&s_wifi, 0, sizeof(s_wifi));
    s_wifi.inited      = true;
    s_wifi.storage     = WIFI_STORAGE_FLASH;
    s_wifi.ps          = WIFI_PS_MIN_MODEM;
    s_wifi.tx_power    = SIM_TXPWR_MAX_UNITS;
    s_wifi.proto_bitmap[WIFI_IF_STA] = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                                     WIFI_PROTOCOL_11N;
    s_wifi.proto_bitmap[WIFI_IF_AP]  = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                                     WIFI_PROTOCOL_11N;
    s_wifi.bw[WIFI_IF_STA] = WIFI_BW_HT20;
    s_wifi.bw[WIFI_IF_AP]  = WIFI_BW_HT20;
    s_wifi.primary_chan  = 1;
    memcpy(s_wifi.sta_mac, SIM_MAC_STA, 6);
    memcpy(s_wifi.ap_mac,  SIM_MAC_AP,  6);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_deinit(void)
{
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_WIFI_NOT_INIT;
    }
    unsigned long gen = ++s_wifi.gen;      /* cancel in-flight workers */
    memset(&s_wifi, 0, sizeof(s_wifi));
    s_wifi.gen = gen;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode)
{
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    if (mode <= WIFI_MODE_NULL || mode >= WIFI_MODE_MAX) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_WIFI_MODE;
    }
    if (mode != WIFI_MODE_STA) {
        sim_log("set_mode %d — sim is STA-only; recorded", (int)mode);
    }
    s_wifi.mode     = mode;
    s_wifi.mode_set = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_get_mode(wifi_mode_t *mode)
{
    if (mode == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    *mode = s_wifi.mode_set ? s_wifi.mode : WIFI_MODE_NULL;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_start(void)
{
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    if (s_wifi.started) { pthread_mutex_unlock(&s_mtx); return ESP_OK; }
    s_wifi.started = true;
    bool first_ready = !s_wifi.wifi_ready_sent;
    s_wifi.wifi_ready_sent = true;
    pthread_mutex_unlock(&s_mtx);

    if (first_ready) {
        esp_event_post(WIFI_EVENT, WIFI_EVENT_WIFI_READY, NULL, 0, 0);
    }
    esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_START, NULL, 0, 0);
    return ESP_OK;
}

esp_err_t esp_wifi_stop(void)
{
    bool was_conn;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    if (!s_wifi.started) { pthread_mutex_unlock(&s_mtx); return ESP_OK; }
    s_wifi.started    = false;
    s_wifi.connecting = false;
    s_wifi.gen++;
    was_conn          = s_wifi.connected;
    s_wifi.connected  = false;
    pthread_mutex_unlock(&s_mtx);

    if (was_conn) post_sta_disconnected(WIFI_REASON_ASSOC_LEAVE);
    esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_STOP, NULL, 0, 0);
    return ESP_OK;
}

esp_err_t esp_wifi_restore(void)
{
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    s_wifi.connecting = false;
    bool was_conn = s_wifi.connected;
    s_wifi.connected  = false;
    s_wifi.gen++;
    memset(&s_wifi.sta_cfg, 0, sizeof(s_wifi.sta_cfg));
    s_wifi.sta_cfg_set = false;
    pthread_mutex_unlock(&s_mtx);

    if (was_conn) post_sta_disconnected(WIFI_REASON_ASSOC_LEAVE);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Connect / disconnect                                               */
/* ------------------------------------------------------------------ */

esp_err_t esp_wifi_connect(void)
{
    unsigned long gen;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    if (!s_wifi.started) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_STARTED; }
    if (!s_wifi.sta_cfg_set || s_wifi.sta_cfg.sta.ssid[0] == '\0') {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_WIFI_SSID;
    }
    if (s_wifi.connected || s_wifi.connecting) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_WIFI_CONN;      /* attempt already in flight    */
    }
    s_wifi.connecting = true;
    gen = s_wifi.gen;
    pthread_mutex_unlock(&s_mtx);

    spawn_worker(connect_worker, gen);
    return ESP_OK;
}

esp_err_t esp_wifi_disconnect(void)
{
    bool was_conn, was_ip;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    if (!s_wifi.connected && !s_wifi.connecting) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    s_wifi.connecting = false;
    s_wifi.gen++;                      /* cancel pending connect worker */
    was_conn = s_wifi.connected;
    was_ip   = s_wifi.ip_up;
    s_wifi.connected = false;
    pthread_mutex_unlock(&s_mtx);

    (void)was_ip;   /* LOST_IP handling lives in post_sta_disconnected */
    post_sta_disconnected(was_conn ? WIFI_REASON_ASSOC_LEAVE
                                   : WIFI_REASON_UNSPECIFIED);
    return ESP_OK;
}

esp_err_t esp_wifi_clear_fast_connect(void)
{
    return ESP_OK;
}

esp_err_t esp_wifi_deauth_sta(uint16_t aid)
{
    (void)aid;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Scan                                                               */
/* ------------------------------------------------------------------ */

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block)
{
    (void)config;
    unsigned long gen;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    if (!s_wifi.started) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_STARTED; }
    s_wifi.ap_num = 1;
    gen = s_wifi.gen;
    pthread_mutex_unlock(&s_mtx);

    if (block) {
        vboard_clock_sleep_us(SIM_WIFI_SCAN_DELAY_US);
        wifi_event_sta_scan_done_t ev = { .status = 0, .number = 1, .scan_id = 0 };
        esp_event_post(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &ev, sizeof(ev), 0);
    } else {
        spawn_worker(scan_worker, gen);
    }
    return ESP_OK;
}

esp_err_t esp_wifi_set_scan_parameters(const wifi_scan_default_params_t *config)
{
    (void)config;
    return ESP_OK;
}

esp_err_t esp_wifi_get_scan_parameters(wifi_scan_default_params_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    config->scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config->scan_time.active.min = 0;
    config->scan_time.active.max = 120;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_stop(void)
{
    pthread_mutex_lock(&s_mtx);
    s_wifi.gen++;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number)
{
    if (number == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    *number = s_wifi.ap_num;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records)
{
    if (number == NULL || ap_records == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    uint16_t cap = *number;
    if (cap > s_wifi.ap_num) cap = (uint16_t)s_wifi.ap_num;
    for (uint16_t i = 0; i < cap; i++) {
        memset(&ap_records[i], 0, sizeof(ap_records[i]));
        const uint8_t bssid[6] = SIM_BSSID;
        memcpy(ap_records[i].bssid, bssid, 6);
        size_t sl = strlen((const char *)s_wifi.sta_cfg.sta.ssid);
        if (sl > 32) sl = 32;
        memcpy(ap_records[i].ssid, s_wifi.sta_cfg.sta.ssid, sl);
        ap_records[i].primary = SIM_CHANNEL;
        ap_records[i].rssi    = g_vb_faults.wifi_link_down ? SIM_RSSI_BAD : SIM_RSSI_OK;
        ap_records[i].authmode = s_wifi.sta_cfg.sta.threshold.authmode;
        ap_records[i].band    = WIFI_BAND_2GHZ;
        ap_records[i].phy_11n = 1;
    }
    *number = cap;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *ap_record)
{
    if (ap_record == NULL) return ESP_ERR_INVALID_ARG;
    uint16_t n = 1;
    return esp_wifi_scan_get_ap_records(&n, ap_record);
}

esp_err_t esp_wifi_clear_ap_list(void)
{
    pthread_mutex_lock(&s_mtx);
    s_wifi.ap_num = 0;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info)
{
    if (ap_info == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.connected) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_ASSOC; }
    memset(ap_info, 0, sizeof(*ap_info));
    const uint8_t bssid[6] = SIM_BSSID;
    memcpy(ap_info->bssid, bssid, 6);
    size_t sl = strlen((const char *)s_wifi.sta_cfg.sta.ssid);
    if (sl > 32) sl = 32;
    memcpy(ap_info->ssid, s_wifi.sta_cfg.sta.ssid, sl);
    ap_info->primary  = SIM_CHANNEL;
    ap_info->rssi     = g_vb_faults.wifi_link_down ? SIM_RSSI_BAD : SIM_RSSI_OK;
    ap_info->authmode = s_wifi.sta_cfg.sta.threshold.authmode;
    ap_info->band     = WIFI_BAND_2GHZ;
    ap_info->phy_11n  = 1;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Configuration                                                      */
/* ------------------------------------------------------------------ */

esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
    if (conf == NULL || interface >= WIFI_IF_MAX) return ESP_ERR_WIFI_IF;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    if (interface != WIFI_IF_STA) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_OK;                   /* AP cfg accepted, unmodelled */
    }
    if (s_wifi.connected || s_wifi.connecting) {
        /* Same as the target driver: rejected while associated — the
         * firmware's retry/disconnect path relies on this code. */
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_WIFI_CONN;
    }
    s_wifi.sta_cfg     = *conf;
    s_wifi.sta_cfg_set = true;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf)
{
    if (conf == NULL || interface >= WIFI_IF_MAX) return ESP_ERR_WIFI_IF;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.inited) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_INIT; }
    if (interface == WIFI_IF_STA) {
        *conf = s_wifi.sta_cfg;
    } else {
        memset(conf, 0, sizeof(*conf));
    }
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_ap_config(wifi_config_t *ap_config)
{
    return esp_wifi_get_config(WIFI_IF_STA, ap_config);
}

esp_err_t esp_wifi_sta_get_ap_list(wifi_config_t *ap_list)
{
    if (ap_list == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (s_wifi.sta_cfg_set) {
        ap_list[0] = s_wifi.sta_cfg;
    } else {
        memset(&ap_list[0], 0, sizeof(ap_list[0]));
    }
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *sta)
{
    if (sta == NULL) return ESP_ERR_INVALID_ARG;
    memset(sta, 0, sizeof(*sta));
    return ESP_OK;
}

esp_err_t esp_wifi_ap_get_sta_aid(const uint8_t mac[6], uint16_t *aid)
{
    (void)mac;
    if (aid != NULL) *aid = 0;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t esp_wifi_set_storage(wifi_storage_t storage)
{
    if (storage != WIFI_STORAGE_FLASH && storage != WIFI_STORAGE_RAM) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    s_wifi.storage = storage;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_auto_connect(bool en)
{
    pthread_mutex_lock(&s_mtx);
    s_wifi.auto_connect = en;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_get_auto_connect(bool *en)
{
    if (en == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    *en = s_wifi.auto_connect;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Radio knobs — all recorded, none modelled                          */
/* ------------------------------------------------------------------ */

esp_err_t esp_wifi_set_ps(wifi_ps_type_t type)
{
    pthread_mutex_lock(&s_mtx);
    s_wifi.ps = type;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_get_ps(wifi_ps_type_t *type)
{
    if (type == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    *type = s_wifi.ps;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap)
{
    if (ifx >= WIFI_IF_MAX) return ESP_ERR_WIFI_IF;
    pthread_mutex_lock(&s_mtx);
    s_wifi.proto_bitmap[ifx] = protocol_bitmap;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_get_protocol(wifi_interface_t ifx, uint8_t *protocol_bitmap)
{
    if (ifx >= WIFI_IF_MAX || protocol_bitmap == NULL) return ESP_ERR_WIFI_IF;
    pthread_mutex_lock(&s_mtx);
    *protocol_bitmap = s_wifi.proto_bitmap[ifx];
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t bw)
{
    if (ifx >= WIFI_IF_MAX) return ESP_ERR_WIFI_IF;
    pthread_mutex_lock(&s_mtx);
    s_wifi.bw[ifx] = bw;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_get_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t *bw)
{
    if (ifx >= WIFI_IF_MAX || bw == NULL) return ESP_ERR_WIFI_IF;
    pthread_mutex_lock(&s_mtx);
    *bw = s_wifi.bw[ifx];
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second)
{
    pthread_mutex_lock(&s_mtx);
    s_wifi.primary_chan = primary;
    s_wifi.second_chan  = second;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second)
{
    pthread_mutex_lock(&s_mtx);
    if (primary != NULL) *primary = s_wifi.primary_chan;
    if (second  != NULL) *second  = s_wifi.second_chan;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_country(const wifi_country_t *country)
{
    (void)country;
    return ESP_OK;
}

esp_err_t esp_wifi_get_country(wifi_country_t *country)
{
    if (country == NULL) return ESP_ERR_INVALID_ARG;
    memset(country, 0, sizeof(*country));
    memcpy(country->cc, "01", 2);
    country->schan  = 1;
    country->nchan  = 11;
    country->policy = WIFI_COUNTRY_POLICY_AUTO;
    country->wifi_band = WIFI_BAND_2GHZ;
    return ESP_OK;
}

esp_err_t esp_wifi_set_mac(wifi_interface_t ifx, const uint8_t mac[6])
{
    if (ifx >= WIFI_IF_MAX || mac == NULL) return ESP_ERR_WIFI_IF;
    pthread_mutex_lock(&s_mtx);
    if (ifx == WIFI_IF_STA) memcpy(s_wifi.sta_mac, mac, 6);
    else                    memcpy(s_wifi.ap_mac,  mac, 6);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
    if (ifx >= WIFI_IF_MAX || mac == NULL) return ESP_ERR_WIFI_IF;
    pthread_mutex_lock(&s_mtx);
    if (ifx == WIFI_IF_STA) memcpy(mac, s_wifi.sta_mac, 6);
    else                    memcpy(mac, s_wifi.ap_mac,  6);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_promiscuous(bool en)
{
    pthread_mutex_lock(&s_mtx);
    s_wifi.promiscuous = en;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_get_promiscuous(bool *en)
{
    if (en == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    *en = s_wifi.promiscuous;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_promiscuous_rx_cb(void *cb)            { (void)cb; return ESP_OK; }
esp_err_t esp_wifi_set_promiscuous_filter(const void *f)      { (void)f;  return ESP_OK; }
esp_err_t esp_wifi_get_promiscuous_filter(void *f)            { (void)f;  return ESP_OK; }
esp_err_t esp_wifi_set_promiscuous_ctrl_filter(const void *f) { (void)f;  return ESP_OK; }
esp_err_t esp_wifi_get_promiscuous_ctrl_filter(void *f)       { (void)f;  return ESP_OK; }

esp_err_t esp_wifi_set_vendor_ie(bool enable, uint32_t vnd_ie_type,
                                 uint32_t vendor_ie_index, const void *vnd_ie)
{
    (void)enable; (void)vnd_ie_type; (void)vendor_ie_index; (void)vnd_ie;
    return ESP_OK;
}

esp_err_t esp_wifi_set_vendor_ie_cb(esp_vendor_ie_cb_t cb, void *ctx)
{
    (void)cb; (void)ctx;
    return ESP_OK;
}

esp_err_t esp_wifi_set_max_tx_power(int8_t power)
{
    if (power < 8 || power > 84) return ESP_ERR_INVALID_ARG; /* 2..21 dBm */
    pthread_mutex_lock(&s_mtx);
    s_wifi.tx_power = power;
    pthread_mutex_unlock(&s_mtx);
    if (power > SIM_TXPWR_MAX_UNITS) {
        /* hw brief: RF tuning is forbidden — surface it in the sim log
         * so a scenario can grep for "VB-WIFI-TXPWR". */
        sim_log("VB-WIFI-TXPWR: max_tx_power set to %d (0.25dBm) > %d default",
                (int)power, SIM_TXPWR_MAX_UNITS);
    }
    return ESP_OK;
}

esp_err_t esp_wifi_get_max_tx_power(int8_t *power)
{
    if (power == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    *power = s_wifi.tx_power;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_csi(const void *config)            { (void)config; return ESP_OK; }
esp_err_t esp_wifi_set_csi_rx_cb(void *cb, void *ctx)     { (void)cb; (void)ctx; return ESP_OK; }
esp_err_t esp_wifi_set_csi_config(const void *config)     { (void)config; return ESP_OK; }

esp_err_t esp_wifi_set_ant(const void *config)        { (void)config; return ESP_OK; }
esp_err_t esp_wifi_get_ant(void *config)              { (void)config; return ESP_OK; }
esp_err_t esp_wifi_set_ant_gpio(const void *config)   { (void)config; return ESP_OK; }
esp_err_t esp_wifi_get_ant_gpio(void *config)         { (void)config; return ESP_OK; }

int64_t esp_wifi_get_tsf_time(wifi_interface_t interface)
{
    (void)interface;
    return vboard_now_us();
}

esp_err_t esp_wifi_set_inactive_time(wifi_interface_t ifx, uint16_t sec)
{
    (void)ifx; (void)sec;
    return ESP_OK;
}

esp_err_t esp_wifi_get_inactive_time(wifi_interface_t ifx, uint16_t *sec)
{
    (void)ifx;
    if (sec != NULL) *sec = 0;
    return ESP_OK;
}

esp_err_t esp_wifi_statis_dump(uint32_t modules)
{
    (void)modules;
    pthread_mutex_lock(&s_mtx);
    sim_log("stats: inited=%d started=%d connecting=%d connected=%d ip_up=%d "
            "storage=%d ps=%d txpwr=%d",
            s_wifi.inited, s_wifi.started, s_wifi.connecting, s_wifi.connected,
            s_wifi.ip_up, s_wifi.storage, s_wifi.ps, s_wifi.tx_power);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_set_rssi_threshold(int32_t rssi)
{
    (void)rssi;
    return ESP_OK;
}

esp_err_t esp_wifi_get_wake_reason(int *reason)
{
    if (reason != NULL) *reason = 0;
    return ESP_OK;
}

esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer,
                            int len, bool en_sys_seq)
{
    (void)ifx; (void)buffer; (void)len; (void)en_sys_seq;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_wifi_sta_get_negotiated_phymode(wifi_phy_mode_t *phymode)
{
    if (phymode == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.connected) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_ASSOC; }
    *phymode = WIFI_PHY_MODE_HT20;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_register_txrx_csi_callback(void *cb, void *ctx)
{
    (void)cb; (void)ctx;
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_rssi(int *rssi)
{
    if (rssi == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    if (!s_wifi.connected) { pthread_mutex_unlock(&s_mtx); return ESP_ERR_WIFI_NOT_ASSOC; }
    *rssi = g_vb_faults.wifi_link_down ? SIM_RSSI_BAD : SIM_RSSI_OK;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_wifi_config_80211_tx_rate(wifi_interface_t ifx, uint32_t rate)
{
    (void)ifx; (void)rate;
    return ESP_OK;
}

esp_err_t esp_wifi_set_espnow_rate(wifi_interface_t ifx, uint32_t rate)
{
    (void)ifx; (void)rate;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* esp_wifi_default.h — wifi/netif glue                               */
/* ------------------------------------------------------------------ */

esp_err_t esp_netif_attach_wifi_station(esp_netif_t *esp_netif)
{
    return esp_netif_attach(esp_netif, (void *)0x57494649uL /* "WIFI" */);
}

esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *esp_netif)
{
    return esp_netif_attach(esp_netif, (void *)0x41504946uL /* "APIF" */);
}

esp_err_t esp_wifi_set_default_wifi_sta_handlers(void)  { return ESP_OK; }
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void)   { return ESP_OK; }
esp_err_t esp_wifi_set_default_wifi_nan_handlers(void)  { return ESP_OK; }

esp_err_t esp_wifi_clear_default_wifi_driver_and_data(void *esp_netif)
{
    (void)esp_netif;
    return ESP_OK;
}

esp_err_t esp_wifi_register_default_wifi_sta_handlers(void) { return ESP_OK; }
esp_err_t esp_wifi_register_default_wifi_ap_handlers(void)  { return ESP_OK; }
esp_err_t esp_wifi_register_default_wifi_nan_handlers(void) { return ESP_OK; }
esp_err_t esp_wifi_unregister_default_wifi_handlers(void)   { return ESP_OK; }

/* esp_mac.h public functions (esp_base_mac_addr_*, esp_efuse_mac_*,
 * esp_read_mac, esp_derive_local_mac, esp_mac_get_len,
 * esp_mac_is_uninitialized) are provided by rom/efuse_mac.c — the
 * eFuse-derived factory MAC is authoritative here. */
