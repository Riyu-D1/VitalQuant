/* vq_clock.c — clock_service.h implementation (fix_v22).
 *
 * One monotonic domain: vq_clock_us() is a thin esp_timer_get_time() wrapper;
 * every vq_sample_t.t_us stamp lives on it (FIFO TIMESTAMP CONTRACT).
 *
 * Wall clock is a {wall ↔ mono} anchor:
 *     wall_us(now) = wall_us_at_anchor + (now - mono_us_at_anchor)
 * exact to esp_timer drift (~tens of ppm) between anchor refreshes. Anchor
 * sources, newest wins:
 *   - SNTP (pool.ntp.org, re-sync 3600 s) — needs Wi-Fi/netif already up.
 *   - RV-3028-C7 RTC @0x52 — optional BOM item (DNP on fix_v22), probed at
 *     boot; refreshes the anchor hourly from a periodic esp_timer.
 *
 * RV-3028 notes: INT is NOT wired to the ESP, so status flags are polled in
 * the same hourly pass. No backup cell (R115 DNP) → PORF is expected after
 * every battery pull; logged + cleared, and the read is still adopted as the
 * anchor (SNTP overwrites it once Wi-Fi is up).
 *
 * Reliability: anchor valid AND (SNTP/RTC-anchored within max_unsynced_s
 * (7200 s) OR rtc_present). This file never blocks sample-rate paths.
 */
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"          /* lwip component — esp_sntp_* aliases   */
#include "esp_netif_sntp.h"    /* esp_netif component — esp_netif_sntp_* */

#include "clock_service.h"
#include "i2c_bus.h"
#include "board_pins.h"

static const char *TAG = "vq_clock";

/* ---- config (mirrors firmware/esp32/profiles/hw_v2.yaml `clock:`) ------- */
#define VQ_CLOCK_NTP_SERVER         "pool.ntp.org"
#define VQ_CLOCK_SNTP_INTERVAL_MS   (3600u * 1000u)         /* re-sync period */
#define VQ_CLOCK_MAX_UNSYNCED_US    (7200LL * 1000000LL)    /* max_unsynced_s */
#define VQ_CLOCK_RTC_REFRESH_US     (3600ULL * 1000000ULL)  /* hourly re-anchor + flag poll */

/* ---- RV-3028-C7 registers (I2C @0x52, auto-increment reads) ------------- */
#define VQ_RV3028_REG_SECONDS       0x00   /* BCD 00-59                      */
#define VQ_RV3028_REG_MINUTES       0x01   /* BCD 00-59                      */
#define VQ_RV3028_REG_HOURS         0x02   /* BCD 00-23 (24 h) / 1-12+AMPM   */
#define VQ_RV3028_REG_WEEKDAY       0x03   /* 0-6, user-assigned — ignored   */
#define VQ_RV3028_REG_DATE          0x04   /* BCD 01-31                      */
#define VQ_RV3028_REG_MONTH         0x05   /* BCD 01-12                      */
#define VQ_RV3028_REG_YEAR          0x06   /* BCD 00-99 (2000-2099)          */
#define VQ_RV3028_REG_STATUS        0x0E   /* flag bits below                */
#define VQ_RV3028_REG_CTRL1         0x0F   /* (unused here)                  */
#define VQ_RV3028_REG_CTRL2         0x10   /* bit1 12_24 selects hour format */
#define VQ_RV3028_CTRL2_12_24       0x02

/* Status 0x0E bits (datasheet: cleared by writing 0 to the bit) */
#define VQ_RV3028_ST_PORF           0x01   /* power-on reset — time untrusted */
#define VQ_RV3028_ST_EVF            0x02   /* external event                  */
#define VQ_RV3028_ST_AF             0x04   /* alarm                           */
#define VQ_RV3028_ST_TF             0x08   /* countdown timer                 */
#define VQ_RV3028_ST_UF             0x10   /* periodic time update            */
#define VQ_RV3028_ST_BSF            0x20   /* backup switchover               */
#define VQ_RV3028_ST_CLKF           0x40   /* clock output (CLKOUT) flag      */
#define VQ_RV3028_ST_EEBUSY         0x80   /* EEPROM busy (read-only)         */

#define VQ_RV3028_TIMEDATE_LEN      7      /* regs 0x00..0x06 in one burst   */

/* ---- state (spec: {wall_us_at_anchor, mono_us_at_anchor, last_sync_mono,
 *       reliable}) ------------------------------------------------------- */
static SemaphoreHandle_t  s_lock;               /* guards the fields below   */
static esp_timer_handle_t s_rtc_timer;

static int64_t  s_wall_us_at_anchor;   /* wall µs (Unix epoch) at anchor    */
static int64_t  s_mono_us_at_anchor;   /* esp_timer µs at anchor            */
static int64_t  s_last_sync_mono;      /* esp_timer µs of last anchor update */
static bool     s_reliable;            /* the stored anchor is trustworthy  */

static bool     s_rtc_present;         /* 0x52 ACK + sane BCD at boot       */
static bool     s_sntp_started;
static bool     s_initialized;

/* ---- helpers ---------------------------------------------------------- */

static bool bcd_ok(uint8_t v)                   /* both nibbles 0-9          */
{
    return (v & 0x0Fu) <= 9u && (v >> 4) <= 9u;
}

static unsigned bcd2bin(uint8_t v)
{
    return (unsigned)(v >> 4) * 10u + (unsigned)(v & 0x0Fu);
}

/* Howard Hinnant's days_from_civil — days since 1970-01-01. Deterministic,
 * avoids timegm/mktime + TZ entirely (RTC is treated as UTC). */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                    /* 0..399    */
    const unsigned mp  = (m + 9u) % 12u;                               /* Mar=0     */
    const unsigned doy = (153u * mp + 2u) / 5u + d - 1u;               /* 0..365    */
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;     /* 0..146096 */
    return era * 146097 + (int64_t)doe - 719468;
}

/* Decode the 0x00..0x06 burst into wall µs (UTC). Rejects illegal BCD so a
 * wedged/absent part can't poison the anchor with 0xFF garbage. */
static bool rtc_decode(const uint8_t r[VQ_RV3028_TIMEDATE_LEN], int64_t *wall_us)
{
    const uint8_t sec  = r[0] & 0x7Fu;   /* bit7 unused                       */
    const uint8_t min  = r[1] & 0x7Fu;
    const uint8_t hour = r[2] & 0x3Fu;   /* assumes 24 h mode (12_24=0 default);
                                          * TODO(datasheet): in 12 h mode bit5
                                          * is AM/PM, hours are 1-12 — read
                                          * CTRL2.12_24 first if ever needed. */
    const uint8_t date = r[4] & 0x3Fu;
    const uint8_t mon  = r[5] & 0x1Fu;
    const uint8_t year = r[6];           /* full byte, 00-99                  */

    if (!bcd_ok(sec)  || bcd2bin(sec)  > 59u) return false;
    if (!bcd_ok(min)  || bcd2bin(min)  > 59u) return false;
    if (!bcd_ok(hour) || bcd2bin(hour) > 23u) return false;
    if (!bcd_ok(date) || bcd2bin(date) < 1u || bcd2bin(date) > 31u) return false;
    if (!bcd_ok(mon)  || bcd2bin(mon)  < 1u || bcd2bin(mon)  > 12u) return false;
    if (!bcd_ok(year)) return false;

    const int64_t days = days_from_civil(2000 + (int)bcd2bin(year),
                                         bcd2bin(mon), bcd2bin(date));
    const int64_t secs = days * 86400LL + (int64_t)bcd2bin(hour) * 3600LL
                       + (int64_t)bcd2bin(min) * 60LL + (int64_t)bcd2bin(sec);
    *wall_us = secs * 1000000LL;
    return true;
}

/* Caller must hold s_lock. Spec: reliability = synced within max_unsynced_s
 * OR rtc_present — but only once some real source has anchored us. */
static bool clock_ok_locked(int64_t now)
{
    if (!s_reliable) return false;
    if (s_rtc_present) return true;
    return (now - s_last_sync_mono) <= VQ_CLOCK_MAX_UNSYNCED_US;
}

/* Take a fresh {wall,mono} correspondence (caller computes wall_us). */
static void anchor_adopt(int64_t wall_us, int64_t mono_us)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_wall_us_at_anchor = wall_us;
    s_mono_us_at_anchor = mono_us;
    s_last_sync_mono    = mono_us;
    s_reliable          = true;
    xSemaphoreGive(s_lock);
}

static int64_t timespec_to_us(const struct timespec *ts)
{
    return (int64_t)ts->tv_sec * 1000000LL + (int64_t)ts->tv_nsec / 1000LL;
}

/* ---- RV-3028-C7 --------------------------------------------------------- */

/* Cheap presence probe: no whoami on this part — ACK on the seconds reg
 * plus a legal BCD value counts as present. */
static bool rtc_probe(void)
{
    uint8_t sec = 0;
    if (vq_i2c_read_reg(VQ_ADDR_RV3028, VQ_RV3028_REG_SECONDS, &sec, 1) != ESP_OK) {
        return false;
    }
    sec &= 0x7Fu;
    return bcd_ok(sec) && bcd2bin(sec) <= 59u;
}

vq_err_t vq_clock_rtc_read(struct timespec *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_rtc_present) return ESP_ERR_INVALID_STATE;

    uint8_t raw[VQ_RV3028_TIMEDATE_LEN];
    const vq_err_t err = vq_i2c_read_reg(VQ_ADDR_RV3028, VQ_RV3028_REG_SECONDS,
                                         raw, sizeof raw);
    if (err != ESP_OK) return err;

    int64_t wall_us;
    if (!rtc_decode(raw, &wall_us)) {
        ESP_LOGW(TAG, "RTC BCD decode failed — power-loss garbage?");
        return ESP_ERR_INVALID_RESPONSE;
    }
    out->tv_sec  = (time_t)(wall_us / 1000000LL);
    out->tv_nsec = (long)((wall_us % 1000000LL) * 1000LL);
    return ESP_OK;
}

bool vq_clock_rtc_present(void)
{
    return s_rtc_present;
}

/* Poll + log + clear status flags (INT not wired — this is the only way we
 * ever see PORF/BSF/EVF). Called at boot and hourly. */
static void rtc_poll_flags(const char *when)
{
    uint8_t st = 0;
    if (vq_i2c_read_reg(VQ_ADDR_RV3028, VQ_RV3028_REG_STATUS, &st, 1) != ESP_OK) {
        ESP_LOGW(TAG, "RTC status read failed (%s)", when);
        return;
    }
    if (st == 0) return;

    ESP_LOGW(TAG, "RTC flags @%s: 0x%02x%s%s%s%s%s%s%s%s",
             when, st,
             (st & VQ_RV3028_ST_PORF)   ? " PORF(power-reset:time-untrusted)" : "",
             (st & VQ_RV3028_ST_EVF)    ? " EVF"   : "",
             (st & VQ_RV3028_ST_AF)     ? " AF"    : "",
             (st & VQ_RV3028_ST_TF)     ? " TF"    : "",
             (st & VQ_RV3028_ST_UF)     ? " UF"    : "",
             (st & VQ_RV3028_ST_BSF)    ? " BSF(backup-switch)" : "",
             (st & VQ_RV3028_ST_CLKF)   ? " CLKF"  : "",
             (st & VQ_RV3028_ST_EEBUSY) ? " EEbusy": "");

    /* Flags clear on write-0 per datasheet; EEbusy is read-only. */
    const uint8_t zero = 0;
    if (vq_i2c_write_reg(VQ_ADDR_RV3028, VQ_RV3028_REG_STATUS, &zero, 1) != ESP_OK) {
        ESP_LOGW(TAG, "RTC flag clear failed");
    }
    /* TODO(datasheet): confirm clear-on-write-0 semantics for BSF/CLKF and
     * whether clearing PORF needs the CONTROL-2 RESET bit handled first. */
}

/* Hourly esp_timer (task-dispatch context — mutexed I2C is legal here):
 * refresh the wall anchor and poll the flag register. */
static void rtc_housekeeping_cb(void *arg)
{
    (void)arg;
    struct timespec ts;
    if (vq_clock_rtc_read(&ts) == ESP_OK) {
        const int64_t wall = timespec_to_us(&ts);
        const int64_t mono = esp_timer_get_time();
        anchor_adopt(wall, mono);
        ESP_LOGD(TAG, "RTC re-anchor: wall=%lldus mono=%lldus",
                 (long long)wall, (long long)mono);
    } else {
        ESP_LOGW(TAG, "RTC hourly read failed");
    }
    rtc_poll_flags("hourly");
}

/* ---- SNTP --------------------------------------------------------------- */

/* Runs on the lwip tcpip task via esp_netif_sntp — keep it short. */
static void sntp_time_sync_cb(struct timeval *tv)
{
    const int64_t wall = (int64_t)tv->tv_sec * 1000000LL + (int64_t)tv->tv_usec;
    const int64_t mono = esp_timer_get_time();
    anchor_adopt(wall, mono);
    ESP_LOGI(TAG, "SNTP sync: wall=%lldus mono=%lldus",
             (long long)wall, (long long)mono);
}

vq_err_t vq_clock_sntp_start(void)
{
    if (s_sntp_started) return ESP_OK;
    if (!s_initialized) {
        const vq_err_t err = vq_clock_init();
        if (err != ESP_OK) return err;
    }

    /* NOTE: esp_netif_sntp_init runs esp_netif_tcpip_exec — call only after
     * esp_netif_init()/Wi-Fi (per header contract). */
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(VQ_CLOCK_NTP_SERVER);
    cfg.sync_cb = sntp_time_sync_cb;
    cfg.start   = false;        /* set the 1 h interval first, then start     */
    vq_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed (%s) — netif up yet?",
                 esp_err_to_name(err));
        return err;
    }
    esp_sntp_set_sync_interval(VQ_CLOCK_SNTP_INTERVAL_MS);  /* 3600 s         */
    err = esp_netif_sntp_start();
    if (err != ESP_OK) {
        esp_netif_sntp_deinit();
        return err;
    }
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started: %s, re-sync every %lu s",
             VQ_CLOCK_NTP_SERVER,
             (unsigned long)(VQ_CLOCK_SNTP_INTERVAL_MS / 1000u));
    return ESP_OK;
}

bool vq_clock_synced(void)
{
    if (!s_initialized) return false;
    const int64_t now = esp_timer_get_time();
    bool synced;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* "Synced" = a trusted anchor was taken within the unsynced window —
     * SNTP fixes and hourly RTC re-anchors both move last_sync_mono. */
    synced = s_reliable && (now - s_last_sync_mono) <= VQ_CLOCK_MAX_UNSYNCED_US;
    xSemaphoreGive(s_lock);
    return synced;
}

/* ---- public clock -------------------------------------------------------- */

vq_err_t vq_clock_init(void)
{
    if (s_initialized) return ESP_OK;

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    /* Boot anchor: epoch 0 ↔ now — wall interpolation still works pre-sync
     * (returns ≈ uptime flagged unreliable). Replaced by RTC/SNTP below. */
    const int64_t now = esp_timer_get_time();
    s_wall_us_at_anchor = 0;
    s_mono_us_at_anchor = now;
    s_last_sync_mono    = now;
    s_reliable          = false;

    /* Probe the optional RTC (DNP BOM item — presence is a BOM question). */
    s_rtc_present = rtc_probe();
    ESP_LOGI(TAG, "RV-3028-C7 RTC (0x%02x): %s",
             VQ_ADDR_RV3028, s_rtc_present ? "present" : "absent");

    if (s_rtc_present) {
        rtc_poll_flags("boot");   /* PORF expected after battery pull */
        struct timespec ts;
        if (vq_clock_rtc_read(&ts) == ESP_OK) {
            const int64_t wall = timespec_to_us(&ts);
            anchor_adopt(wall, esp_timer_get_time());
            ESP_LOGI(TAG, "RTC wall anchor: %lldus epoch", (long long)wall);
        } else {
            ESP_LOGW(TAG, "RTC present but read failed — SNTP will anchor");
        }

        /* Hourly re-anchor + flag poll — INT is not wired to the ESP. */
        const esp_timer_create_args_t tcfg = {
            .callback             = rtc_housekeeping_cb,
            .arg                  = NULL,
            .dispatch_method      = ESP_TIMER_TASK,
            .name                 = "vq_rtc",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&tcfg, &s_rtc_timer) == ESP_OK &&
            esp_timer_start_periodic(s_rtc_timer, VQ_CLOCK_RTC_REFRESH_US) == ESP_OK) {
            ESP_LOGD(TAG, "RTC housekeeping armed: every 3600 s");
        } else {
            ESP_LOGW(TAG, "RTC housekeeping timer failed to arm");
            s_rtc_timer = NULL;
        }
    }

    s_initialized = true;
    return ESP_OK;
}

int64_t vq_clock_us(void)
{
    return esp_timer_get_time();
}

bool vq_clock_wall(struct timespec *out)
{
    if (!s_initialized) return false;
    const int64_t now = esp_timer_get_time();
    int64_t wall_us;
    bool    ok;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    wall_us = s_wall_us_at_anchor + (now - s_mono_us_at_anchor);
    ok      = clock_ok_locked(now);
    xSemaphoreGive(s_lock);

    if (out) {   /* best estimate always filled; return value is the trust bit */
        out->tv_sec  = (time_t)(wall_us / 1000000LL);
        out->tv_nsec = (long)((wall_us % 1000000LL) * 1000LL);
    }
    return ok;
}

int64_t vq_clock_wall_us(bool *ok)
{
    if (!s_initialized) {
        if (ok) *ok = false;
        return -1;
    }
    const int64_t now = esp_timer_get_time();
    int64_t wall_us;
    bool    good;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    wall_us = s_wall_us_at_anchor + (now - s_mono_us_at_anchor);
    good    = clock_ok_locked(now);
    xSemaphoreGive(s_lock);

    if (ok) *ok = good;
    return good ? wall_us : -1;   /* "wall µs or -1" per header */
}

vq_clock_anchor_t vq_clock_anchor(void)
{
    vq_clock_anchor_t a = {0, 0, false};
    if (!s_initialized) return a;
    const int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    a.wall_us  = s_wall_us_at_anchor;
    a.mono_us  = s_mono_us_at_anchor;
    a.reliable = clock_ok_locked(now);
    xSemaphoreGive(s_lock);
    return a;
}
