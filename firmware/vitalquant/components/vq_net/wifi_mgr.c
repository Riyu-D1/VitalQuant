/* wifi_mgr.c — vq_net component: STA Wi-Fi manager + LAN status endpoint
 * for firmware v2 (fix_v22, ESP-IDF 5.4). Implements wifi_mgr.h.
 *
 * Power / coexistence contract (hw brief §4.2/§8 — see wifi_mgr.h):
 *  - Wi-Fi TX bursts pull ~350-500 mA off the shared 3V3 rail. Any large
 *    POST/OTA window must clear vq_wifi_burst_wait() — which asks
 *    vq_power_wifi_burst_ok() — first, so bursts never land inside LED
 *    windows or charge-heavy states. The caller should bracket its
 *    transfer with vq_power_burst_begin()/vq_power_burst_end(); this gate
 *    only sleeps/waits.
 *  - BT stays enabled in sdkconfig (NimBLE peripheral + SW coexistence
 *    CONFIG_ESP_COEX_SW_COEXIST_ENABLE): BLE is the preferred streaming
 *    path; Wi-Fi here is STA-only — never AP mode.
 *  - TX power stays at the driver default: the antenna is a fixed PCB
 *    keep-out and RF workarounds are forbidden — esp_wifi_set_max_tx_power()
 *    is never called. Modem sleep also stays at the default (best W/bit
 *    on battery for a monitor that mostly listens).
 *
 * LAN monitoring: on IP_EVENT_STA_GOT_IP the module auto-starts a tiny
 * httpd — GET /status (JSON via the registered renderer), GET /healthz
 * ("ok") — and announces mDNS "vitalquant" / _http._tcp:80 so lab tooling can
 * find it as vitalquant.local. The SNTP/clock kick is NOT done here: the clock
 * and cloud services subscribe via vq_wifi_on_change() and re-arm their
 * own sync/upload machinery. All endpoint wording stays engineering-grade:
 * "research prototype", no clinical claims.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_bit_defs.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mdns.h"

#include "wifi_mgr.h"
#include "power_mgr.h"

static const char *TAG = "vq_wifi";

/* ------------------------------------------------------------------ */
/* Tunables                                                            */
/* ------------------------------------------------------------------ */

#define VQ_NVS_NS              "vitalquant"      /* shared app namespace     */
#define VQ_NVS_KEY_SSID        "wifi_ssid"
#define VQ_NVS_KEY_PASS        "wifi_pass"

#define VQ_SSID_MAX            32            /* IEEE 802.11 SSID bound   */
#define VQ_PASS_MAX            64            /* WPA2 passphrase bound    */

#define VQ_BACKOFF_MIN_MS      1000u         /* exp reconnect 1 s …      */
#define VQ_BACKOFF_MAX_MS      60000u        /* … capped at 60 s         */
#define VQ_JITTER_BASE_PC      75            /* ±25 % jitter window:     */
#define VQ_JITTER_SPAN_PC      51            /* delay ∈ [0.75,1.25]×base */
#define VQ_RECONN_FLOOR_MS     250u          /* never tighter than this  */

#define VQ_CONNECT_WAIT_MS     30000u        /* vq_wifi_connect() block  */
#define VQ_SETCFG_RETRIES      5             /* set_config while assoc'd */
#define VQ_SETCFG_RETRY_MS     50

#define VQ_WIFI_CB_MAX         8             /* on_change subscribers    */
#define VQ_STATUS_BUF_LEN      768           /* /status scratch on stack */

#define VQ_BIT_UP              BIT0          /* event group: got IP      */
#define VQ_BIT_FAIL            BIT1          /* event group: fatal auth  */

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static esp_netif_t   *s_sta_netif;
static httpd_handle_t s_httpd;
static EventGroupHandle_t s_eg;
static SemaphoreHandle_t  s_lock;
static esp_timer_handle_t s_reconn_timer;

static volatile bool s_inited;
static volatile bool s_started;            /* esp_wifi_start() issued    */
static volatile bool s_up;                 /* IP_EVENT_STA_GOT_IP seen   */
static volatile bool s_net_down;           /* user-requested disconnect  */
static volatile uint32_t s_backoff_ms = VQ_BACKOFF_MIN_MS;

static char s_ssid[VQ_SSID_MAX + 1];
static char s_pass[VQ_PASS_MAX + 1];

static vq_status_render_t s_renderer;      /* NULL → default renderer    */

static struct {
    vq_wifi_cb_t cb;
    void        *ctx;
} s_cbs[VQ_WIFI_CB_MAX];
static int s_cb_count;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* Fan-out to on_change subscribers. Called on the sys event task; the
 * list is copied under the lock so callbacks may re-enter this module
 * (cloud subscribe, SNTP kick — keep them short either way).           */
static void notify_cbs(bool up)
{
    vq_wifi_cb_t cbs[VQ_WIFI_CB_MAX];
    void        *ctxs[VQ_WIFI_CB_MAX];
    int          n = 0;

    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < s_cb_count; i++) {
            cbs[i]  = s_cbs[i].cb;
            ctxs[i] = s_cbs[i].ctx;
        }
        n = s_cb_count;
        xSemaphoreGive(s_lock);
    }
    for (int i = 0; i < n; i++) {
        cbs[i](up, ctxs[i]);
    }
}

/* Push s_ssid/s_pass into the driver. The driver rejects set_config while
 * associated (ESP_ERR_WIFI_CONN) — callers retry or apply on reconnect. */
static esp_err_t apply_sta_config(void)
{
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    size_t sl = strlen(s_ssid);
    if (sl > sizeof(cfg.sta.ssid)) sl = sizeof(cfg.sta.ssid);
    memcpy(cfg.sta.ssid, s_ssid, sl);

    size_t pl = strlen(s_pass);
    if (pl > sizeof(cfg.sta.password)) pl = sizeof(cfg.sta.password);
    memcpy(cfg.sta.password, s_pass, pl);

    cfg.sta.threshold.authmode =
        (s_pass[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

/* Ensure the STA is started and a connect attempt is in flight.
 * No-op without creds or before init.                                   */
static void start_sta(void)
{
    if (!s_inited || !vq_wifi_have_creds()) return;

    esp_err_t err = apply_sta_config();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "sta config: %s", esp_err_to_name(err));
    }

    if (!s_started) {
        err = esp_wifi_start();
        if (err == ESP_OK || err == ESP_ERR_WIFI_CONN) {
            s_started = true;              /* WIFI_EVENT_STA_START → connect */
        } else {
            ESP_LOGE(TAG, "wifi start: %s", esp_err_to_name(err));
        }
        return;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
    }
}

/* ------------------------------------------------------------------ */
/* Reconnect: exponential backoff 1 s → 60 s cap, ±25 % jitter           */
/* ------------------------------------------------------------------ */

static void schedule_reconnect(void)
{
    if (s_net_down || !s_inited || !vq_wifi_have_creds()) return;

    uint32_t jitter = VQ_JITTER_BASE_PC + (esp_random() % VQ_JITTER_SPAN_PC);
    uint32_t delay  = (s_backoff_ms * jitter) / 100u;
    if (delay < VQ_RECONN_FLOOR_MS) delay = VQ_RECONN_FLOOR_MS;

    ESP_LOGI(TAG, "reconnect in %lu ms (backoff base %lu ms)",
             (unsigned long)delay, (unsigned long)s_backoff_ms);
    esp_timer_start_once(s_reconn_timer, (uint64_t)delay * 1000ULL);

    s_backoff_ms *= 2u;
    if (s_backoff_ms > VQ_BACKOFF_MAX_MS) s_backoff_ms = VQ_BACKOFF_MAX_MS;
}

static void reconn_timer_cb(void *arg)
{
    (void)arg;
    if (s_net_down || !s_inited || s_up || !vq_wifi_have_creds()) return;
    /* start_sta() re-applies the config first, so credentials updated
     * via vq_wifi_set_creds() mid-association still take effect here
     * even if an earlier set_config() bounced off the old link.        */
    start_sta();
}

/* ------------------------------------------------------------------ */
/* Event handlers (sys event task context)                             */
/* ------------------------------------------------------------------ */

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg; (void)base;

    switch (id) {
    case WIFI_EVENT_STA_START:
        s_backoff_ms = VQ_BACKOFF_MIN_MS;
        if (vq_wifi_have_creds() && !s_net_down) {
            esp_wifi_connect();
        }
        break;

    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "associated — awaiting IP");
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "disconnected (reason %d)%s",
                 d ? (int)d->reason : -1, s_net_down ? " — manual" : "");
        xEventGroupClearBits(s_eg, VQ_BIT_UP);
        /* Permanent-looking failures (bad creds / no AP) unblock a
         * waiting vq_wifi_connect() early; retries still continue.      */
        if (d && (d->reason == WIFI_REASON_AUTH_FAIL ||
                  d->reason == WIFI_REASON_NO_AP_FOUND ||
                  d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                  d->reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY)) {
            xEventGroupSetBits(s_eg, VQ_BIT_FAIL);
        }
        if (s_up) {
            s_up = false;
            notify_cbs(false);
        }
        schedule_reconnect();
        break;
    }

    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;

    const ip_event_got_ip_t *e = data;
    s_up         = true;
    s_backoff_ms = VQ_BACKOFF_MIN_MS;
    xEventGroupClearBits(s_eg, VQ_BIT_FAIL);
    xEventGroupSetBits(s_eg, VQ_BIT_UP);

    if (e != NULL) {
        ESP_LOGI(TAG, "up — ip " IPSTR, IP2STR(&e->ip_info.ip));
    } else {
        ESP_LOGI(TAG, "up");
    }

    /* Subscriber kick: cloud reconnect, SNTP re-arm, etc. The SNTP kick
     * itself is owned by the clock service — not done here.             */
    notify_cbs(true);

    /* LAN monitor auto-start (idempotent — also callable from app).     */
    vq_wifi_status_httpd_start();
}

/* ------------------------------------------------------------------ */
/* NVS credentials — ns "vitalquant", keys wifi_ssid / wifi_pass             */
/* ------------------------------------------------------------------ */

static void load_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(VQ_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    size_t n = sizeof(s_ssid);
    nvs_get_str(h, VQ_NVS_KEY_SSID, s_ssid, &n);
    s_ssid[sizeof(s_ssid) - 1] = '\0';

    n = sizeof(s_pass);
    nvs_get_str(h, VQ_NVS_KEY_PASS, s_pass, &n);
    s_pass[sizeof(s_pass) - 1] = '\0';

    nvs_close(h);
}

/* ------------------------------------------------------------------ */
/* Status httpd + mDNS                                                 */
/* ------------------------------------------------------------------ */

/* Default /status body — engineering wording only ("research prototype").
 * A richer renderer (device, power, last decision, health per the header)
 * is installed by the app via vq_wifi_set_status_renderer().           */
static void default_status_render(char *buf, size_t len)
{
    char ip[16] = "0.0.0.0";
    if (s_up && s_sta_netif != NULL) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK) {
            esp_ip4addr_ntoa(&info.ip, ip, sizeof(ip));
        }
    }

    snprintf(buf, len,
        "{\"device\":\"vitalquant-fw2\",\"uptime_s\":%llu,"
        "\"heap\":%lu,\"psram\":%lu,"
        "\"wifi\":{\"ip\":\"%s\",\"rssi\":%d},"
        "\"research_prototype\":true}",
        (unsigned long long)(esp_timer_get_time() / 1000000ULL),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        ip, (int)vq_wifi_rssi());
}

static esp_err_t h_status(httpd_req_t *req)
{
    char buf[VQ_STATUS_BUF_LEN];
    vq_status_render_t render = s_renderer ? s_renderer : default_status_render;
    render(buf, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_healthz(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

static void mdns_announce(void)
{
    static bool s_mdns;
    if (s_mdns) return;

    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mdns init failed — httpd still reachable by IP");
        return;
    }
    s_mdns = true;

    mdns_hostname_set("vitalquant");           /* → vitalquant.local            */
    mdns_instance_name_set("vitalquant-fw2 research prototype");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}

vq_err_t vq_wifi_status_httpd_start(void)
{
    if (s_httpd != NULL) return ESP_OK;    /* idempotent auto-start     */

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable  = true;
    cfg.max_uri_handlers  = 8;

    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd start: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t status_uri = {
        .uri = "/status", .method = HTTP_GET,
        .handler = h_status, .user_ctx = NULL,
    };
    static const httpd_uri_t health_uri = {
        .uri = "/healthz", .method = HTTP_GET,
        .handler = h_healthz, .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &status_uri);
    httpd_register_uri_handler(s_httpd, &health_uri);

    mdns_announce();
    ESP_LOGI(TAG, "status httpd on :80 — /status /healthz, mdns vitalquant.local");
    return ESP_OK;
}

void vq_wifi_set_status_renderer(vq_status_render_t fn)
{
    s_renderer = fn;                       /* NULL restores the default */
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

vq_err_t vq_wifi_init(void)
{
    if (s_inited) return ESP_OK;

    /* NVS is shared app-wide (console `wifi set`, cloud keys, …). The
     * call is a no-op when the app already initialised the partition.   */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs init %s — erase + retry", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs unavailable (%s) — creds won't persist",
                 esp_err_to_name(err));
    }
    load_creds();

    err = esp_netif_init();                /* idempotent on IDF 5.x     */
    if (err != ESP_OK) return err;

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;                        /* app may have made it first */
    }

    s_eg   = xEventGroupCreate();
    s_lock = xSemaphoreCreateMutex();
    if (s_eg == NULL || s_lock == NULL) return ESP_ERR_NO_MEM;

    const esp_timer_create_args_t targs = {
        .callback              = reconn_timer_cb,
        .arg                   = NULL,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "vq_wifi_reconn",
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&targs, &s_reconn_timer);
    if (err != ESP_OK) return err;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) return ESP_FAIL;

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wcfg);
    if (err != ESP_OK) return err;

    /* Credentials live in our NVS namespace — driver uses RAM storage.  */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;

    err = esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL, NULL);
    if (err != ESP_OK) return err;

    /* STA only — no AP mode. TX power stays default (antenna keep-out). */
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;

    s_inited   = true;
    s_net_down = false;

    if (vq_wifi_have_creds()) {
        ESP_LOGI(TAG, "starting STA — ssid \"%s\"", s_ssid);
        start_sta();                       /* STA_START handler connects */
    } else {
        ESP_LOGI(TAG, "no wifi creds — console: wifi set <ssid> <pass>");
    }
    return ESP_OK;
}

vq_err_t vq_wifi_set_creds(const char *ssid, const char *pass)
{
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > VQ_SSID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pass == NULL) pass = "";
    if (strlen(pass) > VQ_PASS_MAX) return ESP_ERR_INVALID_ARG;

    /* Persist — same "vitalquant" namespace the console `wifi set` writes.   */
    nvs_handle_t h;
    esp_err_t err = nvs_open(VQ_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, VQ_NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, VQ_NVS_KEY_PASS, pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;

    if (s_lock != NULL) xSemaphoreTake(s_lock, portMAX_DELAY);
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_pass, pass, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';
    if (s_lock != NULL) xSemaphoreGive(s_lock);

    if (!s_inited) return ESP_OK;

    s_net_down   = false;
    s_backoff_ms = VQ_BACKOFF_MIN_MS;
    esp_timer_stop(s_reconn_timer);

    if (!s_started) {
        start_sta();                       /* first STA start          */
        return ESP_OK;
    }

    /* Apply live: drop the current association, push the new config,
     * and reconnect. The DISCONNECTED event also arms the backoff timer
     * — that retry is harmless (esp_wifi_connect while connecting is a
     * no-op) and covers the case where set_config needed a retry loop.  */
    if (s_up) esp_wifi_disconnect();

    err = ESP_ERR_WIFI_CONN;
    for (int i = 0; i < VQ_SETCFG_RETRIES && err == ESP_ERR_WIFI_CONN; i++) {
        err = apply_sta_config();
        if (err == ESP_ERR_WIFI_CONN) {
            vTaskDelay(pdMS_TO_TICKS(VQ_SETCFG_RETRY_MS));
        }
    }
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) return err;

    err = esp_wifi_connect();
    if (err == ESP_OK || err == ESP_ERR_WIFI_CONN) return ESP_OK;
    return err;
}

bool vq_wifi_have_creds(void)
{
    return s_ssid[0] != '\0';
}

vq_err_t vq_wifi_connect(void)
{
    if (!s_inited || !vq_wifi_have_creds()) return ESP_ERR_INVALID_STATE;
    if (s_up) return ESP_OK;

    s_net_down   = false;
    s_backoff_ms = VQ_BACKOFF_MIN_MS;
    esp_timer_stop(s_reconn_timer);        /* immediate attempt, not delayed */

    xEventGroupClearBits(s_eg, VQ_BIT_UP | VQ_BIT_FAIL);
    start_sta();

    EventBits_t b = xEventGroupWaitBits(s_eg, VQ_BIT_UP | VQ_BIT_FAIL,
                                      pdFALSE, pdFALSE,
                                      pdMS_TO_TICKS(VQ_CONNECT_WAIT_MS));
    if (b & VQ_BIT_UP)   return ESP_OK;
    if (b & VQ_BIT_FAIL) return ESP_FAIL;
    return ESP_ERR_TIMEOUT;
}

void vq_wifi_disconnect(void)
{
    s_net_down = true;
    if (s_reconn_timer != NULL) esp_timer_stop(s_reconn_timer);
    if (s_started) esp_wifi_disconnect();
    if (s_up) {
        s_up = false;
        notify_cbs(false);
    }
}

bool vq_wifi_up(void)
{
    return s_up;
}

vq_err_t vq_wifi_ip(char *buf, size_t len)
{
    if (buf == NULL || len == 0) return ESP_ERR_INVALID_ARG;
    buf[0] = '\0';
    if (!s_up || s_sta_netif == NULL) return ESP_ERR_INVALID_STATE;

    esp_netif_ip_info_t info;
    esp_err_t err = esp_netif_get_ip_info(s_sta_netif, &info);
    if (err != ESP_OK) return err;
    if (esp_ip4addr_ntoa(&info.ip, buf, (int)len) == NULL) {
        return ESP_ERR_INVALID_SIZE;       /* needs >= 16 bytes         */
    }
    return ESP_OK;
}

int8_t vq_wifi_rssi(void)
{
    int rssi = 0;
    if (!s_up || esp_wifi_sta_get_rssi(&rssi) != ESP_OK) return 0;
    return (int8_t)rssi;
}

/* TX-burst gate: ask the power FSM before any large POST/OTA window.
 * Polls until the rail is clear or the timeout expires — sleeps inside
 * per the header contract, so callers just block on it.                */
vq_err_t vq_wifi_burst_wait(uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t limit = pdMS_TO_TICKS(timeout_ms);
    const TickType_t poll  = pdMS_TO_TICKS(50);

    while (!vq_power_wifi_burst_ok()) {
        TickType_t waited = xTaskGetTickCount() - start;
        if (waited >= limit) return ESP_ERR_TIMEOUT;
        TickType_t rem = limit - waited;   /* never oversleep the budget */
        vTaskDelay(rem < poll ? rem : poll);
    }
    return ESP_OK;
}

vq_err_t vq_wifi_on_change(vq_wifi_cb_t cb, void *ctx)
{
    if (cb == NULL) return ESP_ERR_INVALID_ARG;
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_cb_count >= VQ_WIFI_CB_MAX) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    s_cbs[s_cb_count].cb  = cb;
    s_cbs[s_cb_count].ctx = ctx;
    s_cb_count++;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
