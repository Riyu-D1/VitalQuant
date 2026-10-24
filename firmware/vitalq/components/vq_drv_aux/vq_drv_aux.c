/* vq_drv_aux.c — aux-group core: descriptor registration and the §9
 * self-test aggregation. See vq_drv_aux.h for the device roster and the
 * component's hardware notes.
 *
 * Registration inserts descriptors only — the pipeline probes later, so
 * this call generates zero bus traffic and is safe before vq_i2c_init().
 */
#include "vq_aux_priv.h"
#include "vq_drv_aux.h"
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "vq_aux";
static bool        s_registered;

/* ------------------------------------------------------------------ */

esp_err_t vq_aux_register(void)
{
    if (s_registered) {
        return ESP_OK;
    }
    vq_err_t first = vq_sensor_register(&vq_aux_gauge_sensor);  /* required */
    vq_err_t e     = vq_sensor_register(&vq_aux_rtc_sensor);    /* optional */
    if (first == ESP_OK) {
        first = e;
    }
    s_registered = true;
    ESP_LOGI(TAG, "registered aux devices: max17048 (required), rv3028"
             " (probe-optional)%s", first == ESP_OK ? "" : " — registry err");
    return first;
}

/* ------------------------------------------------------------------ */
/* report builder shared with the chip fragments                        */

int vq_aux_repf(char *r, size_t len, int off, const char *fmt, ...)
{
    if (off < 0 || (size_t)off >= len) {
        return (int)len;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(r + off, len - (size_t)off, fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return off;
    }
    off += n;
    return ((size_t)off >= len) ? (int)len - 1 : off;
}

/* ------------------------------------------------------------------ */

esp_err_t vq_aux_selftest(char *report, size_t len)
{
    if (report == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    report[0] = '\0';
    (void)vq_aux_register();            /* idempotent — devs must exist */

    int off = 0;
    int fails = 0;
    off = vq_aux_repf(report, len, off,
                      "aux-selftest: max17048 gauge (required) + rv3028 rtc"
                      " (probe-optional)\n");

    /* --- gauge ------------------------------------------------------ */
    {
        char sub[320];
        sub[0] = '\0';
        vq_err_t e = vq_aux_gauge_selftest(sub, sizeof(sub));
        off = vq_aux_repf(report, len, off, "%-8s %s — %s\n",
                          "max17048", sub,
                          e == ESP_OK ? "OK" : (e == ESP_ERR_NOT_FOUND
                              ? "ABSENT — REQUIRED, FAIL" : "FAIL"));
        if (e != ESP_OK) {
            fails++;                    /* gauge is the required device */
        }
    }

    /* --- rtc -------------------------------------------------------- */
    {
        char sub[320];
        sub[0] = '\0';
        vq_err_t e = vq_aux_rtc_selftest(sub, sizeof(sub));
        if (e == ESP_ERR_NOT_FOUND) {
            /* optional anchor absent — BOM-fitted part, so note loudly
             * but do not fail (clock_service falls back to SNTP).      */
            off = vq_aux_repf(report, len, off,
                              "%-8s %s — ABSENT (optional; expected U24)\n",
                              "rv3028", sub);
        } else {
            off = vq_aux_repf(report, len, off, "%-8s %s — %s\n",
                              "rv3028", sub, e == ESP_OK ? "OK" : "FAIL");
            if (e != ESP_OK) {
                fails++;
            }
        }
    }

    off = vq_aux_repf(report, len, off, "aux-selftest %s\n",
                      fails ? "FAIL" : "PASS");
    (void)off;
    return fails ? ESP_FAIL : ESP_OK;
}
