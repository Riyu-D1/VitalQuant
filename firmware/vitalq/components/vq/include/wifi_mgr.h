/* wifi_mgr.h — STA Wi-Fi + LAN monitoring interface.
 *
 * Constraints (hw brief §4.2/§8): Wi-Fi TX bursts ~350-500 mA hit the
 * shared 3V3 rail → never burst during LED windows or charging-heavy
 * states (asks power_mgr); BLE preferred for streaming. TX power stays
 * at default — antenna is a fixed PCB keep-out; no RF workarounds.
 *
 * Monitoring: mDNS "vitalq" + tiny HTTP server: GET /status (JSON),
 * GET /healthz. Credentials via NVS (console `wifi set <ssid> <pass>`).
 */
#pragma once
#include "vq_types.h"

vq_err_t vq_wifi_init(void);            /* netif, event loop            */
vq_err_t vq_wifi_set_creds(const char *ssid, const char *pass); /* NVS */
bool     vq_wifi_have_creds(void);
vq_err_t vq_wifi_connect(void);         /* blocks until up/fail         */
void     vq_wifi_disconnect(void);
bool     vq_wifi_up(void);
vq_err_t vq_wifi_ip(char *buf, size_t len);
int8_t   vq_wifi_rssi(void);

/* TX-burst gate — net code calls before any large POST/OTA window;
 * sleeps/waits inside if the power manager says no.                  */
vq_err_t vq_wifi_burst_wait(uint32_t timeout_ms);

/* Monitoring server (LAN): starts once IP is up.                     */
vq_err_t vq_wifi_status_httpd_start(void);
/* JSON rendered by status.c — device, power, last decision, health.  */
typedef void (*vq_status_render_t)(char *buf, size_t len);
void     vq_wifi_set_status_renderer(vq_status_render_t fn);

/* Event callback on connect/disconnect (cloud client, SNTP hook).    */
typedef void (*vq_wifi_cb_t)(bool up, void *ctx);
vq_err_t vq_wifi_on_change(vq_wifi_cb_t cb, void *ctx);
