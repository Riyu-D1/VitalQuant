/* st_priv.h — module-private helpers for the vq_selftest component.
 *
 * Report contract (selftest.h / hw brief §9): every test fills `report`
 * with a first line "<name>: PASS|FAIL|SKIP — <measured values>" and may
 * append indented detail lines.  SKIP is used where the probe target is
 * absent-by-design (optional BOM) or the driver hook is not linked —
 * never for a device that should be there.
 */
#pragma once
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "vq_types.h"
#include "selftest.h"

/* verdict → vq_err_t mapping used by every test */
#define ST_RC_PASS  ESP_OK
#define ST_RC_FAIL  ESP_FAIL
#define ST_RC_SKIP  ESP_ERR_NOT_SUPPORTED

typedef enum { ST_V_PASS = 0, ST_V_FAIL, ST_V_SKIP } st_verdict_t;

/* ---- append-safe scratch buffer for detail text ----------------------- */
typedef struct {
    char  *p;
    size_t cap;
    size_t len;
} st_buf_t;

static inline void st_buf_init(st_buf_t *b, char *p, size_t cap)
{
    b->p = p; b->cap = cap; b->len = 0;
    if (cap) p[0] = '\0';
}

/* printf-append with clamping; never overflows, always NUL-terminated */
void st_printf(st_buf_t *b, const char *fmt, ...);

/* write the canonical first line and glue the detail block behind it */
void st_done(char *report, size_t len, const char *name,
             st_verdict_t v, const st_buf_t *det);

/* map verdict to the run() return code */
static inline vq_err_t st_rc(st_verdict_t v)
{
    return v == ST_V_PASS ? ST_RC_PASS :
           v == ST_V_FAIL ? ST_RC_FAIL : ST_RC_SKIP;
}

/* optional argv tail for `selftest <name> <arg>` (e.g. "commit") */
const char *st_arg(void);
void        st_set_arg(const char *arg);

/* shared I2C helper: 16-bit big-endian register read */
vq_err_t st_rd16be(uint8_t addr, uint8_t reg, uint16_t *out);

/* shared analytic check over the led_sched trace: counts overlapping
 * slot pairs; returns overlap count (0 = interlock honoured). */
uint32_t st_led_trace_overlap(char *detail, size_t len);

/* -----------------------------------------------------------------------
 * Driver-owned selftest/id hooks — declared weak so the suite links and
 * degrades to a SKIP note until the vq_drv_* components land.  When a
 * driver's public header exists these prototypes must be reconciled
 * with it (keep signatures identical or delete the local decl).
 * --------------------------------------------------------------------- */
extern vq_err_t vq_ads1292_read_id(uint8_t *id_out)      __attribute__((weak));
extern vq_err_t vq_ads1292_selftest(char *report, size_t len) __attribute__((weak));
extern vq_err_t vq_afe4900_diag_read(uint8_t reg, uint32_t *val) __attribute__((weak));
extern vq_err_t vq_afe4900_selftest(char *report, size_t len)  __attribute__((weak));
extern vq_err_t vq_max86141_read_partid(uint8_t *id_out) __attribute__((weak));
extern vq_err_t vq_max86141_selftest(char *report, size_t len) __attribute__((weak));
extern vq_err_t vq_ad5940_selftest(char *report, size_t len)   __attribute__((weak));
extern vq_err_t vq_ad5940_run_rcal(float *ohm)          __attribute__((weak));
extern vq_err_t vq_ad5940_nir_gate(bool on)              __attribute__((weak));
/* capture one dark/light spectral frame: counts = f1..f8,clear,nir,flicker */
extern vq_err_t vq_as7341_capture(uint16_t counts[11], bool illum_on) __attribute__((weak));
extern vq_err_t vq_mic_selftest(char *report, size_t len) __attribute__((weak));
extern vq_err_t vq_fsr_selftest(char *report, size_t len) __attribute__((weak));

/* ---- per-test entry points (defined in st_core.c / st_sys.c) ---------- */
vq_err_t st_rails_boot(char *r, size_t n);
vq_err_t st_i2c_scan(char *r, size_t n);
vq_err_t st_expander_walk(char *r, size_t n);
vq_err_t st_spi_ids(char *r, size_t n);
vq_err_t st_miso_contention(char *r, size_t n);
vq_err_t st_irq_latency(char *r, size_t n);
vq_err_t st_power_paths(char *r, size_t n);
vq_err_t st_usb_cdc(char *r, size_t n);
vq_err_t st_sleep_current(char *r, size_t n);
vq_err_t st_optical(char *r, size_t n);
vq_err_t st_ecg_bioz(char *r, size_t n);
vq_err_t st_mic_pdm(char *r, size_t n);
vq_err_t st_fsr_adc(char *r, size_t n);
vq_err_t st_model_infer(char *r, size_t n);
vq_err_t st_led_interlock(char *r, size_t n);
vq_err_t st_decision_loop(char *r, size_t n);
