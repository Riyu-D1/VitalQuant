/* vq_console.c — esp_console REPL on USB-Serial/JTAG (native USB, GPIO19/20).
 *
 * Implements vq_console.h. Every command handler is a thin wrapper over the
 * contract APIs in components/vq/include — no driver work happens here.
 * Handlers run on the low-priority REPL task that esp_console spawns, so a
 * slow command (i2c scan, selftests, blocking wifi connect) can never
 * starve the sensing pipeline.
 *
 * Safety invariants honoured here (hw brief + vq_console.h):
 *  - recovery path stays: console is the LAST thing app_main starts and it
 *    never touches GPIO19/20 as GPIO, never burns eFuses, never writes
 *    straps. GPIO0/45/46 are read-never-driven by everyone.
 *  - every handler survives its service being absent/not-yet-init'd —
 *    friendly error, no crash, no ESP_ERROR_CHECK on contract calls.
 *  - research prototype: wording stays engineering-grade, no clinical claims.
 */
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "vq_console.h"
#include "board_init.h"
#include "board_pins.h"
#include "cloud_client.h"
#include "decision.h"
#include "i2c_bus.h"
#include "led_sched.h"
#include "log_store.h"
#include "power_mgr.h"
#include "selftest.h"
#include "vq_model.h"
#include "vq_sensor.h"
#include "wifi_mgr.h"

static const char *TAG = "vq_console";

/* ------------------------------------------------------------------ */
/* Console-owned state                                                */
/* ------------------------------------------------------------------ */

#define VQ_CONSOLE_STACK_BYTES   8192    /* handlers call into selftests etc. */
#define VQ_CONSOLE_TASK_PRIO     3       /* low — pipeline must always win    */
#define VQ_CONSOLE_PROMPT        "vitalq> "
#define VQ_CONSOLE_CMDLINE_LEN   256

static esp_console_repl_t *s_repl;
static bool              s_started;

/* ------------------------------------------------------------------ */
/* Event ring — vq_console_log_event feeds logtail + ESP_LOGI mirror. */
/* Internal RAM only (DRAM_ATTR), mutexed; callable from any task.    */
/* ------------------------------------------------------------------ */

#define VQ_EVT_RING_CAP   64
#define VQ_EVT_STR_LEN    128

static DRAM_ATTR char      s_evt_ring[VQ_EVT_RING_CAP][VQ_EVT_STR_LEN];
static volatile uint32_t   s_evt_head;      /* next write index (mod CAP) */
static volatile uint32_t   s_evt_total;     /* events ever logged         */
static SemaphoreHandle_t   s_evt_mutex;
static portMUX_TYPE        s_evt_mux_init = portMUX_INITIALIZER_UNLOCKED;

static const char *const VQ_EVT_NAMES[] = {
    [VQ_EVT_BOOT]               = "BOOT",
    [VQ_EVT_SENSOR_FAULT]       = "SENSOR_FAULT",
    [VQ_EVT_I2C_ERR]            = "I2C_ERR",
    [VQ_EVT_SPI_ERR]            = "SPI_ERR",
    [VQ_EVT_LOW_BATTERY]        = "LOW_BATTERY",
    [VQ_EVT_EMERGENCY_SHUTDOWN] = "EMERGENCY_SHUTDOWN",
    [VQ_EVT_VBUS_IN]            = "VBUS_IN",
    [VQ_EVT_VBUS_OUT]           = "VBUS_OUT",
    [VQ_EVT_CHARGE_STATE]       = "CHARGE_STATE",
    [VQ_EVT_MODEL_SCORED]       = "MODEL_SCORED",
    [VQ_EVT_ESCALATION_SENT]    = "ESCALATION_SENT",
    [VQ_EVT_ESCALATION_RESULT]  = "ESCALATION_RESULT",
    [VQ_EVT_UPLOAD_OK]          = "UPLOAD_OK",
    [VQ_EVT_UPLOAD_FAIL]        = "UPLOAD_FAIL",
    [VQ_EVT_DEEP_SLEEP_ENTER]   = "DEEP_SLEEP_ENTER",
    [VQ_EVT_WATCHDOG_WARN]      = "WATCHDOG_WARN",
};

static const char *vq_evt_name(vq_event_id_t id)
{
    if ((unsigned)id < sizeof(VQ_EVT_NAMES) / sizeof(VQ_EVT_NAMES[0]) &&
        VQ_EVT_NAMES[id]) {
        return VQ_EVT_NAMES[id];
    }
    return "EVT?";
}

static void evt_mutex_ensure(void)
{
    if (s_evt_mutex) {
        return;
    }
    /* Allocate outside the critical section; claim the slot atomically. */
    SemaphoreHandle_t fresh = xSemaphoreCreateMutex();
    portENTER_CRITICAL(&s_evt_mux_init);
    if (!s_evt_mutex) {
        s_evt_mutex = fresh;
        fresh = NULL;
    }
    portEXIT_CRITICAL(&s_evt_mux_init);
    if (fresh) {
        vSemaphoreDelete(fresh);   /* lost the race — free the spare */
    }
}

void vq_console_log_event(vq_event_id_t id, const char *fmt, ...)
{
    char body[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt ? fmt : "", ap);
    va_end(ap);

    /* Mirror first — visible even if the ring ever fails. */
    ESP_LOGI(TAG, "evt %s: %s", vq_evt_name(id), body);

    if (xPortInIsrContext()) {
        return;              /* ring is mutexed — skip store in ISR ctx */
    }
    evt_mutex_ensure();
    if (!s_evt_mutex) {
        return;                              /* OOM — log still went out */
    }
    if (xSemaphoreTake(s_evt_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        uint32_t idx = s_evt_head % VQ_EVT_RING_CAP;
        int64_t  ms  = esp_timer_get_time() / 1000;
        snprintf(s_evt_ring[idx], VQ_EVT_STR_LEN, "[%lld.%03lld] %s: %s",
                 (long long)(ms / 1000), (long long)(ms % 1000),
                 vq_evt_name(id), body);
        s_evt_head++;
        s_evt_total++;
        xSemaphoreGive(s_evt_mutex);
    }
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static bool parse_u32(const char *s, uint32_t *out)
{
    if (!s || !*s) {
        return false;
    }
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (!end || *end != '\0') {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static const char *pwr_state_name(vq_pwr_state_t s)
{
    switch (s) {
    case VQ_PWR_NORMAL:       return "NORMAL";
    case VQ_PWR_CONSERVE:     return "CONSERVE";
    case VQ_PWR_LOW_CUTOFF:   return "LOW_CUTOFF";
    case VQ_PWR_EMERGENCY:    return "EMERGENCY";
    default:                  return "?";
    }
}

static const char *reset_reason_name(int r)
{
    switch ((esp_reset_reason_t)r) {
    case ESP_RST_POWERON:     return "power-on";
    case ESP_RST_EXT:         return "ext-pin";
    case ESP_RST_SW:          return "software";
    case ESP_RST_PANIC:       return "panic";
    case ESP_RST_INT_WDT:     return "int-wdt";
    case ESP_RST_TASK_WDT:    return "task-wdt";
    case ESP_RST_WDT:         return "wdt";
    case ESP_RST_DEEPSLEEP:   return "deep-sleep";
    case ESP_RST_BROWNOUT:    return "brownout";
    case ESP_RST_SDIO:        return "sdio";
    case ESP_RST_USB:         return "usb";
    case ESP_RST_JTAG:        return "jtag";
    case ESP_RST_EFUSE:       return "efuse";
    case ESP_RST_PWR_GLITCH:  return "pwr-glitch";
    case ESP_RST_CPU_LOCKUP:  return "cpu-lockup";
    case ESP_RST_UNKNOWN:
    default:                  return "unknown";
    }
}

/* Shared report buffer — handlers run on the single REPL task, so one
 * static buffer is safe (no re-entrancy) and costs no heap.            */
static DRAM_ATTR char s_report[6144];

/* ------------------------------------------------------------------ */
/* selftest [name|all|list]                                           */
/* ------------------------------------------------------------------ */

static int cmd_selftest(int argc, char **argv)
{
    const char *name = (argc >= 2) ? argv[1] : "all";

    if (strcmp(name, "list") == 0) {
        size_t n = 0;
        const vq_test_t *t = vq_selftests(&n);
        if (!t || n == 0) {
            printf("no self-tests registered (selftest service not up?)\n");
            return 1;
        }
        printf("%u self-tests:\n", (unsigned)n);
        for (size_t i = 0; i < n; i++) {
            printf("  %s\n", t[i].name ? t[i].name : "?");
        }
        return 0;
    }

    s_report[0] = '\0';
    vq_err_t err = (strcmp(name, "all") == 0)
                   ? vq_selftest_run_all(s_report, sizeof(s_report))
                   : vq_selftest_run(name, s_report, sizeof(s_report));
    if (err == ESP_ERR_NOT_FOUND) {
        printf("unknown test '%s' — try 'selftest list'\n", name);
        return 1;
    }
    printf("%s\n", s_report[0] ? s_report : "(empty report)");
    if (err != ESP_OK) {
        printf("selftest '%s' returned %s\n", name, esp_err_to_name(err));
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* status                                                             */
/* ------------------------------------------------------------------ */

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("=== vitalq status (uptime %lld s) ===\n",
           (long long)(esp_timer_get_time() / 1000000));

    /* --- boot --- */
    const vq_boot_info_t *bi = vq_board_info();
    if (bi) {
        printf("boot      : reset=%s flash=%lu B psram=%lu B%s vdd_spi=%s\n",
               reset_reason_name(bi->reset_reason),
               (unsigned long)bi->flash_bytes,
               (unsigned long)bi->psram_bytes,
               bi->psram_octal ? " (octal)" : "",
               bi->vdd_spi_3v3 ? "3V3" : "!!CHECK IO45!!");
    } else {
        printf("boot      : board info unavailable (vq_board_init not run)\n");
    }

    /* --- power --- */
    vq_power_status_t pst;
    memset(&pst, 0, sizeof(pst));
    vq_power_status(&pst);
    printf("power     : vcell=%.2f V soc=%.0f%% chg_rate=%.2f vbus=%d charging=%d"
           " inhibited=%d t_board=%.1f C state=%s\n",
           (double)pst.vcell_v, (double)pst.soc_pct, (double)pst.charge_rate,
           (int)pst.vbus, (int)pst.charging, (int)pst.charge_inhibited,
           (double)pst.board_temp_c, pwr_state_name(pst.state));

    /* --- wifi --- */
    if (vq_wifi_up()) {
        char ip[24] = "?";
        (void)vq_wifi_ip(ip, sizeof(ip));
        printf("wifi      : up  ip=%s rssi=%d dBm\n", ip, (int)vq_wifi_rssi());
    } else {
        printf("wifi      : down%s\n",
               vq_wifi_have_creds() ? " (creds stored)" : " (no creds — 'wifi set <ssid> <pass>')");
    }

    /* --- last decision --- */
    vq_decision_t d;
    memset(&d, 0, sizeof(d));
    vq_decision_last(&d);
    if (d.latency_us == 0 && d.prob == 0.0f && d.threshold == 0.0f) {
        printf("decision  : none yet\n");
    } else {
        printf("decision  : p=%.3f thr=%.2f esc=%d unc=%d lat=%lld us",
               (double)d.prob, (double)d.threshold, (int)d.escalated,
               (int)d.uncertain, (long long)d.latency_us);
        if (d.cloud_prob >= 0.0f) {
            printf(" cloud=%.3f (%s)", (double)d.cloud_prob, d.cloud_model);
        }
        printf("\n");
    }

    /* --- log store --- */
    if (vq_log_flash_present()) {
        uint32_t used = 0, total = 0;
        vq_log_space(&used, &total);
        printf("log_store : %lu/%lu B used (%.1f%%)\n",
               (unsigned long)used, (unsigned long)total,
               total ? 100.0 * used / total : 0.0);
    } else {
        printf("log_store : flash absent — RAM ring only\n");
    }

    printf("events    : %lu logged ('logtail <n>' to view)\n",
           (unsigned long)s_evt_total);
    printf("heap      : %u B internal free\n",
           (unsigned)esp_get_free_internal_heap_size());
    return 0;
}

/* ------------------------------------------------------------------ */
/* sensors                                                            */
/* ------------------------------------------------------------------ */

static int cmd_sensors(int argc, char **argv)
{
    (void)argc; (void)argv;
    size_t n = 0;
    vq_sensor_t *s = vq_sensors(&n);
    if (!s || n == 0) {
        printf("no sensors registered — pipeline not started?\n");
        return 1;
    }
    printf("%-14s %8s %10s %6s %14s %s\n",
           "name", "rate_hz", "samples", "err", "last_t_ms", "health");
    int64_t now = esp_timer_get_time();
    for (size_t i = 0; i < n; i++) {
        const vq_sensor_ops_t *o = s[i].ops;
        const char *nm = (o && o->name) ? o->name : "?";
        const char *health = "-";
        if (o && o->healthy) {
            health = o->healthy(&s[i]) ? "ok" : "FAULT";
        }
        if (s[i].last_t_us) {
            printf("%-14s %8.2f %10lu %6lu %14lld %s\n", nm,
                   o ? (double)o->rate_hz : 0.0,
                   (unsigned long)s[i].sample_count,
                   (unsigned long)s[i].err_count,
                   (long long)((now - s[i].last_t_us) / 1000), health);
        } else {
            printf("%-14s %8.2f %10lu %6lu %14s %s\n", nm,
                   o ? (double)o->rate_hz : 0.0,
                   (unsigned long)s[i].sample_count,
                   (unsigned long)s[i].err_count, "(never)", health);
        }
    }
    printf("last_t_ms = ms since last stamped sample (acquisition time)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* scan — I2C bus walk                                                */
/* ------------------------------------------------------------------ */

static const struct { uint8_t addr; const char *who; bool optional; } I2C_KNOWN[] = {
    { VQ_ADDR_TCA6408,     "tca6408 io-expander",        false },
    { VQ_ADDR_MAX17048,    "max17048 fuel gauge",        false },
    { VQ_ADDR_AS7341,      "as7341 spectral (1V8)",      false },
    { VQ_ADDR_MLX90632,    "mlx90632 ir-therm (1V8)",    false },
    { VQ_ADDR_SHT45,       "sht45 rh/temp",              false },
    { VQ_ADDR_TMP117_SKIN, "tmp117 skin",                false },
    { VQ_ADDR_TMP117_DOME, "tmp117 dome",                false },
    { VQ_ADDR_TMP117_TAIL, "tmp117 tail (J9)",           true  },
    { VQ_ADDR_RV3028,      "rv3028 rtc",                 false },
    { VQ_ADDR_LSM6DSV,     "lsm6dsv imu",                false },
    { VQ_ADDR_BME280,      "bme280 env",                 false },
};
#define I2C_KNOWN_N (sizeof(I2C_KNOWN) / sizeof(I2C_KNOWN[0]))

static const char *i2c_who(uint8_t a, bool *optional)
{
    for (size_t i = 0; i < I2C_KNOWN_N; i++) {
        if (I2C_KNOWN[i].addr == a) {
            if (optional) *optional = I2C_KNOWN[i].optional;
            return I2C_KNOWN[i].who;
        }
    }
    if (optional) *optional = false;
    return NULL;
}

static int cmd_scan(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint8_t addrs[112];                    /* 0x08..0x77 max */
    size_t n = vq_i2c_scan(addrs, sizeof(addrs));
    if (n == 0) {
        printf("no I2C devices ACKed — bus down, unpowered, or held busy\n");
        return 1;
    }
    printf("%u device(s) on I2C0 @%d Hz:\n", (unsigned)n, VQ_I2C_FREQ_HZ);
    for (size_t i = 0; i < n; i++) {
        bool opt = false;
        const char *who = i2c_who(addrs[i], &opt);
        printf("  0x%02X  %s%s\n", addrs[i], who ? who : "unknown",
               opt ? " [optional]" : "");
    }
    /* expected-set check */
    printf("expected missing:");
    size_t missing = 0;
    for (size_t i = 0; i < I2C_KNOWN_N; i++) {
        if (I2C_KNOWN[i].optional) continue;      /* tail TMP117 may be DNP */
        bool found = false;
        for (size_t j = 0; j < n; j++) {
            if (addrs[j] == I2C_KNOWN[i].addr) { found = true; break; }
        }
        if (!found) {
            printf(" 0x%02X(%s)", I2C_KNOWN[i].addr, I2C_KNOWN[i].who);
            missing++;
        }
    }
    fputs(missing ? "\n" : " none\n", stdout);
    return 0;
}

/* ------------------------------------------------------------------ */
/* wifi                                                               */
/* ------------------------------------------------------------------ */

static int cmd_wifi(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        printf("wifi: %s, creds=%s\n",
               vq_wifi_up() ? "up" : "down",
               vq_wifi_have_creds() ? "stored" : "none");
        if (vq_wifi_up()) {
            char ip[24] = "?";
            if (vq_wifi_ip(ip, sizeof(ip)) == ESP_OK) {
                printf("  ip=%s rssi=%d dBm\n", ip, (int)vq_wifi_rssi());
            }
        }
        return 0;
    }
    if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            printf("usage: wifi set <ssid> <pass>\n");
            return 1;
        }
        vq_err_t err = vq_wifi_set_creds(argv[2], argv[3]);
        if (err != ESP_OK) {
            printf("wifi set failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("credentials stored in NVS — run 'wifi connect'\n");
        return 0;
    }
    if (strcmp(argv[1], "connect") == 0) {
        if (!vq_wifi_have_creds()) {
            printf("no credentials — 'wifi set <ssid> <pass>' first\n");
            return 1;
        }
        printf("connecting (blocks until up/fail)...\n");
        int64_t t0 = esp_timer_get_time();
        vq_err_t err = vq_wifi_connect();
        printf("... %s after %lld ms\n", esp_err_to_name(err),
               (long long)((esp_timer_get_time() - t0) / 1000));
        if (err == ESP_OK) {
            char ip[24] = "?";
            (void)vq_wifi_ip(ip, sizeof(ip));
            printf("up: ip=%s rssi=%d dBm\n", ip, (int)vq_wifi_rssi());
            return 0;
        }
        return 1;
    }
    if (strcmp(argv[1], "disconnect") == 0) {
        vq_wifi_disconnect();
        printf("disconnected\n");
        return 0;
    }
    printf("usage: wifi [status] | wifi set <ssid> <pass> | wifi connect | wifi disconnect\n");
    return 1;
}

/* ------------------------------------------------------------------ */
/* cloud                                                              */
/* ------------------------------------------------------------------ */

static int cmd_cloud(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: cloud set <url> <key> | cloud flush [n] | "
               "cloud heartbeat | cloud stats\n");
        printf("configured=%s wifi=%s\n",
               vq_cloud_configured() ? "yes" : "no",
               vq_wifi_up() ? "up" : "down");
        return 1;
    }
    if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            printf("usage: cloud set <base_url> <device_key>\n");
            return 1;
        }
        vq_err_t err = vq_cloud_config(argv[2], argv[3]);
        if (err != ESP_OK) {
            printf("cloud config failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("cloud endpoint configured (TLS unless http:// — lab only)\n");
        return 0;
    }
    if (strcmp(argv[1], "stats") == 0) {
        uint64_t ok = 0, fail = 0;
        uint32_t rtt = 0;
        vq_cloud_stats(&ok, &fail, &rtt);
        printf("cloud stats: ok=%" PRIu64 " fail=%" PRIu64 " rtt=%lu ms\n",
               ok, fail, (unsigned long)rtt);
        return 0;
    }
    if (strcmp(argv[1], "flush") == 0) {
        if (!vq_cloud_configured()) {
            printf("cloud not configured — 'cloud set <url> <key>' first\n");
            return 1;
        }
        size_t maxr = 32;
        if (argc >= 3) {
            uint32_t v;
            if (!parse_u32(argv[2], &v) || v == 0 || v > 512) {
                printf("usage: cloud flush [1..512]\n");
                return 1;
            }
            maxr = v;
        }
        if (!vq_wifi_up()) {
            printf("wifi down — records stay queued in log_store\n");
        }
        vq_err_t err = vq_cloud_flush(maxr);
        printf("flush(%u) -> %s\n", (unsigned)maxr, esp_err_to_name(err));
        return (err == ESP_OK) ? 0 : 1;
    }
    if (strcmp(argv[1], "heartbeat") == 0) {
        if (!vq_cloud_configured()) {
            printf("cloud not configured — 'cloud set <url> <key>' first\n");
            return 1;
        }
        vq_err_t err = vq_cloud_heartbeat();
        printf("heartbeat -> %s\n", esp_err_to_name(err));
        return (err == ESP_OK) ? 0 : 1;
    }
    printf("usage: cloud set <url> <key> | cloud flush [n] | "
           "cloud heartbeat | cloud stats\n");
    return 1;
}

/* ------------------------------------------------------------------ */
/* model info                                                         */
/* ------------------------------------------------------------------ */

static int cmd_model(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "info") != 0) {
        printf("usage: model [info]\n");
        return 1;
    }
    vq_model_t *m = NULL;
    vq_err_t err = vq_model_load(&m);
    if (err != ESP_OK || !m) {
        printf("model blob load: %s — 'model' partition absent/corrupt;\n"
               "decision layer falls back to the compiled-in stub model\n",
               esp_err_to_name(err));
        return 1;
    }
    printf("model: inputs=%u outputs=%u params=%lu arena=%lu B\n",
           (unsigned)vq_model_n_inputs(m),
           (unsigned)vq_model_n_outputs(m),
           (unsigned long)vq_model_param_count(m),
           (unsigned long)vq_model_arena_used(m));
    char rpt[768];
    rpt[0] = '\0';
    vq_model_selftest_report(m, rpt, sizeof(rpt));
    if (rpt[0]) {
        printf("%s\n", rpt);
    }
    vq_model_free(m);
    return 0;
}

/* ------------------------------------------------------------------ */
/* score — one decision cycle                                         */
/* ------------------------------------------------------------------ */

static void print_esc_reasons(uint32_t r)
{
    if (r == 0) {
        printf("none");
        return;
    }
    struct { uint32_t bit; const char *s; } bits[] = {
        { VQ_ESC_R_PROB,      "PROB"      },
        { VQ_ESC_R_UNCERTAIN, "UNCERTAIN" },
        { VQ_ESC_R_MODEL_ERR, "MODEL_ERR" },
        { VQ_ESC_R_MANUAL,    "MANUAL"    },
        { VQ_ESC_R_QUALITY,   "QUALITY"   },
    };
    bool first = true;
    for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
        if (r & bits[i].bit) {
            printf("%s%s", first ? "" : "|", bits[i].s);
            first = false;
        }
    }
}

static int cmd_score(int argc, char **argv)
{
    (void)argc; (void)argv;
    vq_decision_t d;
    memset(&d, 0, sizeof(d));
    printf("running one feature->model score cycle (engineering proxy "
           "score, advisory only)...\n");
    vq_err_t err = vq_decision_run_once(&d);
    if (err != ESP_OK) {
        printf("decision run failed: %s — decision loop or pipeline not up?\n",
               esp_err_to_name(err));
        return 1;
    }
    printf("prob=%.3f  threshold=%.3f  escalated=%d  uncertain=%d\n",
           (double)d.prob, (double)d.threshold,
           (int)d.escalated, (int)d.uncertain);
    printf("latency=%lld us  reasons=0x%02lx(",
           (long long)d.latency_us, (unsigned long)d.reasons);
    print_esc_reasons(d.reasons);
    printf(")\n");
    if (d.cloud_prob >= 0.0f) {
        printf("cloud advisory: prob=%.3f model=%s\n",
               (double)d.cloud_prob, d.cloud_model);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* led <chain> <ms> — one-shot scheduler test pulse                   */
/* ------------------------------------------------------------------ */

typedef struct {
    const char      *name;
    const char      *alias;
    vq_led_chain_t   chain;
    uint32_t         min_mA;     /* documented minimum test current class */
} led_chain_desc_t;

static const led_chain_desc_t LED_CHAINS[] = {
    { "afe4900",  "afe",  VQ_LED_AFE4900,  200 }, /* SFH7072 ~200 mA class */
    { "max86141", "max",  VQ_LED_MAX86141, 200 }, /* LED1-3 ~200 mA class  */
    { "ir_gate",  "ir",   VQ_LED_IR_GATE,  200 }, /* Q1 low-side switch    */
    { "nir730",   "nir",  VQ_LED_NIR730,    50 }, /* via AD5940 GPIO2      */
    { "as7341",   "spec", VQ_LED_AS7341,    91 }, /* on-chip LDR ~91 mA    */
};
#define LED_CHAINS_N (sizeof(LED_CHAINS) / sizeof(LED_CHAINS[0]))

static volatile bool    s_led_done;
static volatile int64_t s_led_on_us;
static volatile int64_t s_led_off_us;

static void led_status_gpio_ensure(void)
{
    static bool done;
    if (done) {
        return;
    }
    /* IO17 is the active-high status LED via R88 — safe slot indicator. */
    (void)gpio_reset_pin(VQ_STATUS_LED_GPIO);
    (void)gpio_set_direction(VQ_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    (void)gpio_set_level(VQ_STATUS_LED_GPIO, 0);
    done = true;
}

static void led_cb_on(void *ctx)
{
    (void)ctx;
    s_led_on_us = esp_timer_get_time();
    (void)gpio_set_level(VQ_STATUS_LED_GPIO, 1);   /* visible slot marker */
}

static void led_cb_off(void *ctx)
{
    (void)ctx;
    (void)gpio_set_level(VQ_STATUS_LED_GPIO, 0);
    s_led_off_us = esp_timer_get_time();
    s_led_done   = true;
}

static int cmd_led(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: led <chain> <ms>  — one-shot scheduler test pulse\n");
        printf("chains (min test current used — chain drive itself is verified\n");
        printf("by 'selftest optical'; here we verify slot timing + non-overlap):\n");
        for (size_t i = 0; i < LED_CHAINS_N; i++) {
            printf("  %-8s (%s)  min %lu mA class\n", LED_CHAINS[i].name,
                   LED_CHAINS[i].alias, (unsigned long)LED_CHAINS[i].min_mA);
        }
        printf("status LED (IO%d) mirrors the granted slot for a scope check\n",
               (int)VQ_STATUS_LED_GPIO);
        return 1;
    }

    /* chain: name, alias, or 0..N-1 */
    int chain = -1;
    uint32_t idx;
    if (parse_u32(argv[1], &idx) && idx < LED_CHAINS_N) {
        chain = (int)idx;
    } else {
        for (size_t i = 0; i < LED_CHAINS_N; i++) {
            if (strcmp(argv[1], LED_CHAINS[i].name) == 0 ||
                strcmp(argv[1], LED_CHAINS[i].alias) == 0) {
                chain = (int)i;
                break;
            }
        }
    }
    if (chain < 0) {
        printf("unknown chain '%s' — run 'led' for the list\n", argv[1]);
        return 1;
    }

    uint32_t ms;
    if (!parse_u32(argv[2], &ms) || ms == 0 || ms > 10000) {
        printf("usage: led <chain> <ms>  (ms in 1..10000)\n");
        return 1;
    }

    const led_chain_desc_t *cd = &LED_CHAINS[chain];
    if (!vq_power_led_ok(cd->min_mA)) {
        printf("power manager refuses an LED window right now "
               "(state=%s) — try later\n", pwr_state_name(vq_power_state()));
        return 1;
    }

    led_status_gpio_ensure();
    s_led_done   = false;
    s_led_on_us  = 0;
    s_led_off_us = 0;

    vq_led_request_t req = {
        .chain     = cd->chain,
        .pulse_mA  = cd->min_mA,
        .on_us     = (uint32_t)ms * 1000,
        .period_us = 0,                       /* one-shot */
        .priority  = 0,
    };
    int64_t slot_start = 0;
    vq_err_t err = vq_led_submit(&req, led_cb_on, led_cb_off, NULL, &slot_start);
    if (err != ESP_OK) {
        printf("led_submit failed: %s (scheduler busy / duty limit?)\n",
               esp_err_to_name(err));
        return 1;
    }
    int64_t now = esp_timer_get_time();
    printf("%s: %lu mA class, on=%lu us, slot starts in %lld us (tx5=%d)\n",
           cd->name, (unsigned long)cd->min_mA, (unsigned long)req.on_us,
           (long long)(slot_start - now), (int)vq_led_tx5_on());

    /* Wait for the slot to complete: slot start + on-time + margin. */
    int64_t deadline = (slot_start > now ? slot_start : now)
                       + (int64_t)req.on_us + 5000000;
    while (!s_led_done && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!s_led_done) {
        (void)vq_led_cancel(cd->chain);
        printf("slot never fired — scheduler wedged? cancelled request\n");
        return 1;
    }
    printf("measured: on-time=%lld us, started %lld us %s granted slot\n",
           (long long)(s_led_off_us - s_led_on_us),
           (long long)(s_led_on_us - slot_start),
           (s_led_on_us >= slot_start) ? "after" : "before");
    printf("led duty (60 s window) = %.3f %%\n", (double)vq_led_duty_pct());
    return 0;
}

/* ------------------------------------------------------------------ */
/* power [status|prepare|sleep]                                       */
/* ------------------------------------------------------------------ */

/* The deep-sleep sequence itself — shared by 'power sleep' and 'sleep'.
 * No confirm prompt BY DESIGN: this IS the sleep-path test command; the
 * operator types it deliberately. Warns loudly that the console dies. */
static void do_power_sleep(void)
{
    printf("!!! entering deep sleep NOW — this console does NOT return !!!\n");
    printf("USB CDC drops; wake via GAUGE_ALRT(IO%d)/EXP_INT(IO%d)/"
           "LSM6(IO%d)/VBUS per power_mgr\n",
           (int)VQ_GAUGE_ALRT_GPIO, (int)VQ_EXP_INT_GPIO,
           (int)VQ_LSM6_INT1_GPIO);
    printf("running the sec 4.4 park checklist, then entering...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(400));              /* let the USB frame out */
    vq_err_t err = vq_power_prepare_sleep();
    printf("prepare_sleep -> %s (entering regardless — this IS the test)\n",
           esp_err_to_name(err));
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));
    vq_console_log_event(VQ_EVT_DEEP_SLEEP_ENTER,
                         "console-forced deep sleep");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    vq_power_enter_deep_sleep();                 /* noreturn */
}

static int cmd_power(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        vq_power_status_t st;
        memset(&st, 0, sizeof(st));
        vq_power_status(&st);
        printf("power: state=%s\n", pwr_state_name(st.state));
        printf("  vcell=%.2f V  soc=%.1f %%  chg_rate=%.2f\n",
               (double)st.vcell_v, (double)st.soc_pct, (double)st.charge_rate);
        printf("  vbus=%d charging=%d charge_inhibited=%d board_t=%.1f C\n",
               (int)st.vbus, (int)st.charging, (int)st.charge_inhibited,
               (double)st.board_temp_c);
        printf("  led_ok=%d  wifi_burst_ok=%d\n",
               (int)vq_power_led_ok(200), (int)vq_power_wifi_burst_ok());
        return 0;
    }
    if (strcmp(argv[1], "prepare") == 0) {
        vq_err_t err = vq_power_prepare_sleep();
        printf("prepare_sleep (sec 4.4 checklist) -> %s\n",
               esp_err_to_name(err));
        return (err == ESP_OK) ? 0 : 1;
    }
    if (strcmp(argv[1], "sleep") == 0) {
        do_power_sleep();                       /* noreturn */
    }
    printf("usage: power [status] | power prepare | power sleep\n");
    return 1;
}

/* Top-level 'sleep' alias — same no-confirm test path as 'power sleep'
 * (both listed in the vq_console.h command roster). */
static int cmd_sleep(int argc, char **argv)
{
    (void)argc; (void)argv;
    do_power_sleep();                           /* noreturn */
    return 0;
}

/* ------------------------------------------------------------------ */
/* logtail [n] [flash|store]                                          */
/* ------------------------------------------------------------------ */

/* Small rolling capture for the flash path: last n VQ_LOG_EVENT records. */
typedef struct { int64_t t_us; uint32_t len; char text[96]; } tail_rec_t;
typedef struct { tail_rec_t *recs; int cap; int wr; uint32_t total; } tail_ctx_t;

static bool tail_collect_cb(const vq_log_hdr_t *h, const uint8_t *payload,
                            void *vctx)
{
    tail_ctx_t *c = (tail_ctx_t *)vctx;
    tail_rec_t *r = &c->recs[c->wr % c->cap];
    r->t_us = h->t_us;
    r->len  = h->len;
    r->text[0] = '\0';
    if (payload && h->len) {
        /* printable payload -> string preview; else hex dump of head bytes */
        size_t chk = h->len < 24 ? h->len : 24;
        bool printable = true;
        for (size_t i = 0; i < chk; i++) {
            if (payload[i] < 0x20 || payload[i] > 0x7E) {
                printable = false;
                break;
            }
        }
        if (printable) {
            size_t cplen = h->len < (sizeof(r->text) - 1)
                           ? h->len : (sizeof(r->text) - 1);
            memcpy(r->text, payload, cplen);
            r->text[cplen] = '\0';
        } else {
            size_t w = 0;
            for (size_t i = 0; i < chk && w + 4 < sizeof(r->text); i++) {
                w += snprintf(r->text + w, sizeof(r->text) - w,
                              "%02X ", payload[i]);
            }
            if (h->len > chk && w + 5 < sizeof(r->text)) {
                snprintf(r->text + w, sizeof(r->text) - w, "...");
            }
        }
    }
    c->wr++;
    c->total++;
    return true;                             /* continue to newest */
}

static int logtail_flash(int n)
{
    if (!vq_log_flash_present()) {
        printf("log flash absent — RAM ring only ('logtail <n>')\n");
        return 1;
    }
    if (n <= 0) {
        n = 16;
    }
    if (n > 32) {
        n = 32;                                 /* small buffer on purpose */
    }
    tail_rec_t *recs = heap_caps_calloc(n, sizeof(tail_rec_t), MALLOC_CAP_INTERNAL);
    if (!recs) {
        printf("out of internal RAM\n");
        return 1;
    }
    tail_ctx_t ctx = { .recs = recs, .cap = n, .wr = 0, .total = 0 };
    vq_err_t err = vq_log_iterate(VQ_LOG_EVENT, tail_collect_cb, &ctx);
    if (err != ESP_OK) {
        printf("log iterate failed: %s\n", esp_err_to_name(err));
        free(recs);
        return 1;
    }
    int count = (int)(ctx.total < (uint32_t)ctx.cap ? ctx.total : ctx.cap);
    if (count == 0) {
        printf("no stored events in log flash\n");
        free(recs);
        return 0;
    }
    printf("last %d of %lu stored events (oldest->newest):\n",
           count, (unsigned long)ctx.total);
    for (int i = 0; i < count; i++) {
        int idx = (ctx.wr - count + i) % ctx.cap;
        printf("  [t=%lld ms] (%lu B) %s\n",
               (long long)(recs[idx].t_us / 1000),
               (unsigned long)recs[idx].len,
               recs[idx].text[0] ? recs[idx].text : "(binary payload)");
    }
    free(recs);
    return 0;
}

static int cmd_logtail(int argc, char **argv)
{
    int n = 16;
    bool from_flash = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "flash") == 0 || strcmp(argv[i], "store") == 0 ||
            strcmp(argv[i], "-f") == 0) {
            from_flash = true;
        } else if (isdigit((unsigned char)argv[i][0])) {
            n = atoi(argv[i]);
        } else {
            printf("usage: logtail [n] [flash]\n");
            return 1;
        }
    }
    if (from_flash) {
        return logtail_flash(n);
    }
    if (n <= 0) {
        n = 16;
    }
    if (n > VQ_EVT_RING_CAP) {
        n = VQ_EVT_RING_CAP;                    /* ring holds at most 64 */
    }
    evt_mutex_ensure();
    if (!s_evt_mutex) {
        printf("event ring unavailable\n");
        return 1;
    }
    /* Snapshot n entries under the lock, print after (UART is slow). */
    char *scratch = heap_caps_malloc(n * VQ_EVT_STR_LEN, MALLOC_CAP_INTERNAL);
    if (!scratch) {
        printf("out of internal RAM\n");
        return 1;
    }
    int shown = 0;
    if (xSemaphoreTake(s_evt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t count = s_evt_total < VQ_EVT_RING_CAP
                         ? s_evt_total : VQ_EVT_RING_CAP;
        if ((uint32_t)n > count) {
            n = (int)count;
        }
        for (int i = 0; i < n; i++) {
            uint32_t idx = (s_evt_head + VQ_EVT_RING_CAP - (uint32_t)n + i)
                           % VQ_EVT_RING_CAP;
            memcpy(scratch + i * VQ_EVT_STR_LEN,
                   s_evt_ring[idx], VQ_EVT_STR_LEN);
        }
        shown = n;
        xSemaphoreGive(s_evt_mutex);
    }
    printf("last %d of %lu console events:\n", shown, (unsigned long)s_evt_total);
    for (int i = 0; i < shown; i++) {
        printf("  %s\n", scratch + i * VQ_EVT_STR_LEN);
    }
    free(scratch);
    return 0;
}

/* ------------------------------------------------------------------ */
/* fsr / mic — thin wrappers onto the named driver self-tests         */
/* ------------------------------------------------------------------ */

static int run_named_selftest(const char *name)
{
    s_report[0] = '\0';
    vq_err_t err = vq_selftest_run(name, s_report, sizeof(s_report));
    if (err == ESP_ERR_NOT_FOUND) {
        printf("selftest '%s' not registered (driver not up?)\n", name);
        return 1;
    }
    printf("%s\n", s_report[0] ? s_report : "(empty report)");
    return (err == ESP_OK) ? 0 : 1;
}

static int cmd_fsr(int argc, char **argv)
{
    (void)argc; (void)argv;
    return run_named_selftest("fsr_adc");       /* rest vs pressed counts */
}

static int cmd_mic(int argc, char **argv)
{
    if (argc >= 2) {
        uint32_t s;
        if (!parse_u32(argv[1], &s) || s == 0 || s > 60) {
            printf("usage: mic [1..60 s] (capture window is fixed by the\n"
                   "mic_pdm selftest — value noted only)\n");
            return 1;
        }
        printf("requested %lu s — mic_pdm selftest runs its fixed capture\n",
               (unsigned long)s);
    }
    return run_named_selftest("mic_pdm");        /* 1 s capture -> RMS    */
}

/* ------------------------------------------------------------------ */
/* reboot / profile                                                   */
/* ------------------------------------------------------------------ */

static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("rebooting...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return 0;                                    /* unreachable */
}

static int cmd_profile(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_chip_info_t ci;
    esp_chip_info(&ci);

    printf("=== vitalq hardware profile ===\n");
    printf("hardware_revision : hw_v3 (fix_v22 board)\n");
    printf("module            : ESP32-S3-WROOM-1-N16R8 "
           "(16 MB QIO flash, 8 MB octal PSRAM)\n");
    printf("idf               : %s\n", esp_get_idf_version());
    printf("chip              : %d core(s), rev %d.%d\n",
           ci.cores, ci.revision / 100, ci.revision % 100);
    printf("console           : USB-Serial/JTAG on GPIO19/20 "
           "(recovery path intact)\n");
    printf("i2c0              : SDA=IO%d SCL=IO%d @ %d Hz (hard limit, "
           "2.2k pulls)\n",
           (int)VQ_I2C_SDA_GPIO, (int)VQ_I2C_SCL_GPIO, VQ_I2C_FREQ_HZ);
    printf("spi2              : MOSI=IO%d SCLK=IO%d MISO=IO%d; CS: "
           "ads1292=IO%d ad5940=IO%d flash=IO%d afe4900=IO%d max86141=IO%d\n",
           (int)VQ_SPI_MOSI_GPIO, (int)VQ_SPI_SCLK_GPIO, (int)VQ_SPI_MISO_GPIO,
           (int)VQ_CS_ADS1292_GPIO, (int)VQ_CS_AD5940_GPIO,
           (int)VQ_CS_FLASH_GPIO, (int)VQ_CS_AFE4900_GPIO,
           (int)VQ_CS_MAX86141_GPIO);
    printf("led chains        : %d, one active at a time; >=300 mA class "
           "<=1%% duty; TX_5V boost via expander P%d\n",
           (int)VQ_LED_CHAIN_COUNT, (int)VQ_EXP_TX5_EN);
    printf("battery limits    : cutoff %d mV, emergency %d mV, boost min %d mV, "
           "chg %d mA, usb budget %d mA\n",
           VQ_BATT_LOW_MV, VQ_BATT_EMERG_MV, VQ_BATT_BOOST_MIN_MV,
           VQ_CHG_CURRENT_MA, VQ_USB_BUDGET_MA);
    printf("model contract    : features=%d dims, esc_ch<=%d, input<=%d floats\n",
           VQ_FEATURE_DIM, VQ_ESC_CHANNELS_MAX, VQ_MODEL_INPUT_MAX);
    printf("status            : research prototype — engineering signals only\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Command registration                                               */
/* ------------------------------------------------------------------ */

static void reg_cmd(const char *name, const char *help, const char *hint,
                    esp_console_cmd_func_t fn)
{
    const esp_console_cmd_t c = {
        .command = name,
        .help    = help,
        .hint    = hint,
        .func    = fn,
    };
    esp_err_t err = esp_console_cmd_register(&c);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cmd '%s' register failed: %s", name, esp_err_to_name(err));
    }
}

static void register_commands(void)
{
    reg_cmd("selftest",
            "run bring-up self-test(s); prints pass/fail report",
            "[all|<name>|list]", cmd_selftest);
    reg_cmd("status",
            "boot info + power + wifi + last decision + log space",
            NULL, cmd_status);
    reg_cmd("sensors",
            "registered sensor drivers: name/rate/samples/err/last_t",
            NULL, cmd_sensors);
    reg_cmd("scan",
            "I2C bus scan 0x08-0x77 with expected-device check",
            NULL, cmd_scan);
    reg_cmd("wifi",
            "wifi set <ssid> <pass> | wifi connect | wifi status | wifi disconnect",
            "[set <ssid> <pass>|connect|status|disconnect]", cmd_wifi);
    reg_cmd("cloud",
            "cloud set <url> <key> | cloud flush [n] | cloud heartbeat | cloud stats",
            "[set <url> <key>|flush [n]|heartbeat|stats]", cmd_cloud);
    reg_cmd("model",
            "model info: load 'model' partition blob, run selftest report, free",
            "[info]", cmd_model);
    reg_cmd("score",
            "run one decision cycle: prob/threshold/escalated/latency "
            "(engineering score, advisory only)",
            NULL, cmd_score);
    reg_cmd("led",
            "one-shot led_submit scheduler test pulse at min documented "
            "current; status LED mirrors the slot",
            "<chain> <ms>", cmd_led);
    reg_cmd("power",
            "power status | power prepare | power sleep (prepare+enter deep "
            "sleep — does NOT return over this console)",
            "[status|prepare|sleep]", cmd_power);
    reg_cmd("sleep",
            "alias for 'power sleep': park everything per sec 4.4 and enter "
            "deep sleep NOW — does NOT return over this console",
            NULL, cmd_sleep);
    reg_cmd("logtail",
            "last n console events from the RAM ring; 'logtail <n> flash' "
            "reads stored events from log flash",
            "[n] [flash]", cmd_logtail);
    reg_cmd("fsr",
            "FSR ADC selftest (rest vs pressed raw counts)",
            NULL, cmd_fsr);
    reg_cmd("mic",
            "mic_pdm selftest: short PDM capture -> RMS level",
            "[s]", cmd_mic);
    reg_cmd("reboot",
            "esp_restart()", NULL, cmd_reboot);
    reg_cmd("profile",
            "hardware_revision hw_v3 + key board facts",
            NULL, cmd_profile);
}

/* ------------------------------------------------------------------ */
/* REPL bring-up                                                      */
/* ------------------------------------------------------------------ */

/* Fallback for builds where the console isn't a REPL-capable device:
 * a plain stdin/stdout reader loop in our own low-priority task.      */
static void stdin_repl_task(void *arg)
{
    (void)arg;
    char *line = heap_caps_malloc(VQ_CONSOLE_CMDLINE_LEN, MALLOC_CAP_INTERNAL);
    if (!line) {
        ESP_LOGE(TAG, "no memory for stdin repl");
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        fputs(VQ_CONSOLE_PROMPT, stdout);
        fflush(stdout);
        if (!fgets(line, VQ_CONSOLE_CMDLINE_LEN, stdin)) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) {
            continue;
        }
        int cmd_ret = 0;
        esp_err_t err = esp_console_run(line, &cmd_ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("unrecognized command — try 'help'\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            /* empty line */
        } else if (err != ESP_OK) {
            printf("console run error: %s\n", esp_err_to_name(err));
        } else if (cmd_ret != 0) {
            printf("command exited with code %d\n", cmd_ret);
        }
    }
}

vq_err_t vq_console_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    evt_mutex_ensure();

    esp_console_config_t ccfg = ESP_CONSOLE_CONFIG_DEFAULT();
    ccfg.max_cmdline_length = VQ_CONSOLE_CMDLINE_LEN;
    ccfg.max_cmdline_args   = 16;
    esp_err_t err = esp_console_init(&ccfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    register_commands();
    (void)esp_console_register_help_command();
    (void)esp_console_set_help_verbose_level(ESP_CONSOLE_HELP_VERBOSE_LEVEL_1);

    esp_console_repl_config_t rcfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rcfg.task_stack_size    = VQ_CONSOLE_STACK_BYTES;
    rcfg.task_priority      = VQ_CONSOLE_TASK_PRIO;
    rcfg.prompt             = VQ_CONSOLE_PROMPT;
    rcfg.max_history_len    = 32;
    rcfg.history_save_path  = NULL;
    rcfg.max_cmdline_length = VQ_CONSOLE_CMDLINE_LEN;

    err = ESP_ERR_NOT_SUPPORTED;
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    {
        esp_console_dev_usb_serial_jtag_config_t dcfg =
            ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
        err = esp_console_new_repl_usb_serial_jtag(&dcfg, &rcfg, &s_repl);
    }
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    {
        esp_console_dev_usb_cdc_config_t dcfg =
            ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
        err = esp_console_new_repl_usb_cdc(&dcfg, &rcfg, &s_repl);
    }
#elif defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    {
        esp_console_dev_uart_config_t dcfg =
            ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
        err = esp_console_new_repl_uart(&dcfg, &rcfg, &s_repl);
    }
#endif

    if (err == ESP_OK && s_repl) {
        err = esp_console_start_repl(s_repl);
    } else {
        /* No REPL-capable console device configured: own stdin/stdout task.
         * NOTE: esp_console_new_repl_* calls esp_console_deinit() on its
         * internal failure path, so the registry may have been torn down —
         * re-init and re-register before spawning the reader task. */
        ESP_LOGW(TAG, "no REPL device (%s) — falling back to stdin task",
                 esp_err_to_name(err));
        s_repl = NULL;
        /* esp_console_init returns INVALID_STATE iff the module is still
         * up (e.g. start_repl failed); ESP_OK means the failed new_repl
         * tore it down and we just re-inited — commands must be re-added. */
        esp_err_t rein = esp_console_init(&ccfg);
        if (rein == ESP_OK) {
            register_commands();
            (void)esp_console_register_help_command();
        } else if (rein != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "console re-init failed: %s", esp_err_to_name(rein));
            return rein;
        }
        if (xTaskCreate(stdin_repl_task, "vq_console", VQ_CONSOLE_STACK_BYTES,
                        NULL, VQ_CONSOLE_TASK_PRIO, NULL) == pdPASS) {
            err = ESP_OK;
        } else {
            err = ESP_ERR_NO_MEM;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_started = true;

    printf("\n");
    printf("=================================================\n");
    printf(" vitalq fw2 console — research prototype\n");
    printf(" engineering signals only, no clinical claims\n");
    printf(" 'help' lists commands, 'selftest list' the tests\n");
    printf("=================================================\n");
    vq_console_log_event(VQ_EVT_BOOT, "console started");
    return ESP_OK;
}
