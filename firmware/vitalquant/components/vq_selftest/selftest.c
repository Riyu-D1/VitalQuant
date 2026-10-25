/* selftest.c — §9 bring-up suite harness.
 *
 * The table order is the §9 order; `selftest all` runs it top to bottom
 * and appends a pass/fail/skip tally.  `vq_selftest_run` accepts
 * "<name>" or "<name> <arg>" — currently only "sleep_current commit"
 * consumes an arg.
 */
#include <string.h>
#include <stdio.h>
#include "st_priv.h"
#include "i2c_bus.h"
#include "led_sched.h"

vq_err_t st_rd16be(uint8_t addr, uint8_t reg, uint16_t *out)
{
    uint8_t b[2];
    vq_err_t e = vq_i2c_read_reg(addr, reg, b, 2);
    if (e == ESP_OK) *out = ((uint16_t)b[0] << 8) | b[1];
    return e;
}

void st_printf(st_buf_t *b, const char *fmt, ...)
{
    if (!b->cap || b->len >= b->cap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    b->len += ((size_t)w < b->cap - b->len) ? (size_t)w : b->cap - b->len - 1;
}

void st_done(char *report, size_t len, const char *name,
             st_verdict_t v, const st_buf_t *det)
{
    static const char *const vs[] = { "PASS", "FAIL", "SKIP" };
    if (!len) return;
    snprintf(report, len, "%s: %s — %s", name, vs[v],
             det && det->p ? det->p : "no detail");
}

static const char *s_arg;
const char *st_arg(void) { return s_arg ? s_arg : ""; }
void st_set_arg(const char *arg) { s_arg = arg; }

/* last-N slot trace → count overlapping [on_us,off_us) pairs */
uint32_t st_led_trace_overlap(char *detail, size_t len)
{
    vq_led_trace_t tr[VQ_LED_TRACE_N];
    size_t n = vq_led_trace(tr, VQ_LED_TRACE_N);
    /* insertion sort by on_us (<=32 entries) */
    for (size_t i = 1; i < n; i++) {
        vq_led_trace_t k = tr[i];
        size_t j = i;
        while (j && tr[j - 1].on_us > k.on_us) { tr[j] = tr[j - 1]; j--; }
        tr[j] = k;
    }
    uint32_t overlaps = 0;
    for (size_t i = 1; i < n; i++)
        if (tr[i].on_us < tr[i - 1].off_us) overlaps++;
    if (detail && len) {
        size_t off = 0;
        off += snprintf(detail + off, len - off, "trace n=%u", (unsigned)n);
        for (size_t i = 0; i < n && off < len; i++)
            off += snprintf(detail + off, len - off,
                            "\n  [%u] ch%d %lld..%lldus %umA",
                            (unsigned)i, (int)tr[i].chain,
                            (long long)tr[i].on_us, (long long)tr[i].off_us,
                            (unsigned)tr[i].mA);
    }
    return overlaps;
}

/* ---------------- the §9 table ---------------- */
static const vq_test_t s_tests[] = {
    { "rails_boot",       st_rails_boot       },
    { "i2c_scan",         st_i2c_scan         },
    { "expander_walk",    st_expander_walk    },
    { "spi_ids",          st_spi_ids          },
    { "miso_contention",  st_miso_contention  },
    { "irq_latency",      st_irq_latency      },
    { "power_paths",      st_power_paths      },
    { "usb_cdc",          st_usb_cdc          },
    { "sleep_current",    st_sleep_current    },
    { "optical",          st_optical          },
    { "ecg_bioz",         st_ecg_bioz         },
    { "mic_pdm",          st_mic_pdm          },
    { "fsr_adc",          st_fsr_adc          },
    { "model_infer",      st_model_infer      },
    { "led_interlock",    st_led_interlock    },
    { "decision_loop",    st_decision_loop    },
};

const vq_test_t *vq_selftests(size_t *count)
{
    if (count) *count = sizeof(s_tests) / sizeof(s_tests[0]);
    return s_tests;
}

vq_err_t vq_selftest_run(const char *name, char *report, size_t len)
{
    if (!name || !report || !len) return ESP_ERR_INVALID_ARG;

    /* split "<name> [arg]" — the arg tail is opt-in per test */
    char nm[24];
    const char *sp = strchr(name, ' ');
    size_t nl = sp ? (size_t)(sp - name) : strlen(name);
    if (nl == 0 || nl >= sizeof(nm)) {
        snprintf(report, len, "selftest: FAIL — bad name");
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(nm, name, nl);
    nm[nl] = '\0';
    st_set_arg(sp ? sp + 1 : "");

    size_t cnt;
    const vq_test_t *t = vq_selftests(&cnt);
    for (size_t i = 0; i < cnt; i++) {
        if (strcmp(t[i].name, nm) == 0) {
            vq_err_t rc = t[i].run(report, len);
            st_set_arg(NULL);
            return rc;
        }
    }
    snprintf(report, len, "%s: FAIL — unknown test (try `selftest all`)", nm);
    return ESP_ERR_NOT_FOUND;
}

vq_err_t vq_selftest_run_all(char *report, size_t len)
{
    if (!report || !len) return ESP_ERR_INVALID_ARG;
    size_t cnt, off = 0, pass = 0, fail = 0, skip = 0;
    const vq_test_t *t = vq_selftests(&cnt);
    char one[1600];

    for (size_t i = 0; i < cnt; i++) {
        vq_err_t rc = t[i].run(one, sizeof(one));
        if (rc == ESP_OK) pass++;
        else if (rc == ESP_ERR_NOT_SUPPORTED) skip++;
        else fail++;
        if (off < len) {
            int w = snprintf(report + off, len - off, "%s\n", one);
            if (w > 0) off += ((size_t)w < len - off) ? (size_t)w : len - off - 1;
        }
    }
    if (off < len)
        snprintf(report + off, len - off,
                 "selftest: %u/%u pass, %u fail, %u skip",
                 (unsigned)pass, (unsigned)cnt,
                 (unsigned)fail, (unsigned)skip);
    return fail ? ESP_FAIL : (pass == cnt ? ESP_OK : ESP_ERR_NOT_SUPPORTED);
}
