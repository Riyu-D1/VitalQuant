/* vq_rv3028.c — Micro Crystal RV-3028-C7 RTC (U24) @ VQ_ADDR_RV3028.
 *
 * fix_v22 hardware context (hardware/pcb/pin_audit_r6/U24_RV3028.md):
 *   - /INT goes to TP28 ONLY — no interrupt route exists to the ESP —
 *     so this driver never touches the chip's interrupt enables and
 *     "status/flag poll" is purely an I2C read of STATUS (0x0E).
 *   - VBACKUP is grounded through R115 (no backup cell, by design):
 *     every +3V3 power-up is a POR → PORF latches → wall time is only
 *     trustworthy after a set. It survives a WARM ESP reset (VDD stays).
 *   - EVI is grounded (no timestamp events); CLKOUT pin is NC — EEPROM
 *     default may leave a ~32 kHz square wave on it, so init() parks it
 *     LOW (EEPROM CLKOUT FD field = 0b111) per the pin-audit µA note.
 *   - FITTED per fix_v22 BOM (C2829066) but registered probe-optional:
 *     descriptor required=false — absent degrades to SNTP-only wall time.
 *
 * Clock resource, not a sampler: channels=NULL / n_channels=0 / rate_hz=0
 * is INTENTIONAL — the pipeline keeps it for probe/init/health
 * bookkeeping and must not schedule reads from it. Calibration reads go
 * through the exported helpers below (clock_service / console use them).
 *
 * Register/bit references: RV-3028-C7 Application Manual (Micro Crystal),
 * §3.x register pages, §4.6 EEPROM protocol (EEADDR 25h / EEDATA 26h /
 * EECMD 27h; single-byte write cmd 0x21, read cmd 0x22, arm write 0x00 —
 * the 0x11 bulk "Update All" is deliberately avoided so the password
 * mirror bytes 0x30-0x34/0x36-0x37 are never committed by accident).
 */
#include "vq_aux_priv.h"
#include "vq_drv_aux.h"
#include "board_pins.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "vq_rv3028";

/* ---- register map --------------------------------------------------- */
#define R_REG_SECONDS   0x00u   /* BCD 00-59                            */
#define R_REG_MINUTES   0x01u   /* BCD 00-59                            */
#define R_REG_HOURS     0x02u   /* BCD 00-23 in 24 h mode               */
#define R_REG_WEEKDAY   0x03u   /* 0-6, free-running, user-assigned     */
#define R_REG_DATE      0x04u   /* BCD 01-31                            */
#define R_REG_MONTH     0x05u   /* BCD 01-12                            */
#define R_REG_YEAR      0x06u   /* BCD 00-99 → 2000-2099                */
#define R_REG_STATUS    0x0Eu   /* VQ_RTC_ST_* flags                    */
#define R_REG_CTRL1     0x0Fu   /* EERD lives here (bit3)               */
#define R_REG_CTRL2     0x10u   /* 12_24 select + INT enables (unused)  */
#define R_REG_EE_ADDR   0x25u   /* EEPROM address register              */
#define R_REG_EE_DATA   0x26u   /* EEPROM data register                 */
#define R_REG_EE_CMD    0x27u   /* EEPROM command register              */

#define R_CTRL1_EERD    0x08u   /* pause EEPROM auto-refresh during RMW */
#define R_CTRL2_12_24   0x02u   /* bit1: 0 = 24 h (forced), 1 = 12 h    */

/* EEPROM command protocol (AppMan §4.6): arm with 0x00, then issue. */
#define R_EE_CMD_FIRST  0x00u
#define R_EE_CMD_WRITE  0x21u   /* commit ONE byte (EEADDR→EEDATA)      */
#define R_EE_CMD_READ   0x22u   /* fetch ONE byte (EEADDR→EEDATA)       */
/* 0x11 "Update All" exists but is never used — see file header.       */

#define R_EE_CLKOUT_ADDR 0x35u  /* EEPROM: programmable clock output    */
#define R_EE_CLKOUT_FD   0x07u  /* FD[2:0] field mask                   */
#define R_EE_CLKOUT_LOW  0x07u  /* FD=111 → CLKOUT static LOW (off)     */

#define R_EE_WAIT_MS    15u     /* >10 ms pre-poll for commit (p.56)    */
#define R_EE_RD_WAIT_MS 2u      /* >1 ms pre-poll for read              */
#define R_EE_POLL_MAX   30u     /* ~150 ms bound on EEBUSY              */
#define R_EE_POLL_MS    5u

/* ---- private state -------------------------------------------------- */

typedef struct {
    bool    present;            /* probe saw the part                  */
    bool    time_valid;         /* set since last PORF                 */
    bool    cold_start;         /* PORF latched at init                */
    uint8_t last_status;        /* most recent STATUS byte (diag)      */
} rtc_priv_t;

static rtc_priv_t s_rtc;

/* ---- BCD + civil/unix conversion (C99, no lib calls) ---------------- */

static uint8_t bcd2bin(uint8_t b)  { return (uint8_t)((b & 0x0F) + (((b >> 4) & 0x0F) * 10)); }
static uint8_t bin2bcd(uint8_t v)  { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static bool fields_in_range(const vq_rtc_time_t *t)
{
    return t->second <= 59 && t->minute <= 59 && t->hour <= 23 &&
           t->weekday <= 6 && t->day >= 1 && t->day <= 31 &&
           t->month >= 1 && t->month <= 12 &&
           t->year >= 2000 && t->year <= 2099;
}

/* Howard Hinnant days_from_civil — days since 1970-01-01. */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= (m <= 2);
    int      era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)(153 * (m + (m > 2 ? -3 : 9)) + 2) / 5u
                   + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

uint32_t vq_rtc_to_unix(const vq_rtc_time_t *t)
{
    if (t == NULL || !fields_in_range(t)) {
        return 0;
    }
    int64_t days = days_from_civil((int)t->year, t->month, t->day);
    int64_t s = days * 86400LL + (int64_t)t->hour * 3600 +
                (int64_t)t->minute * 60 + t->second;
    return (s < 0) ? 0 : (uint32_t)s;
}

void vq_rtc_from_unix(uint32_t unix_s, vq_rtc_time_t *t)
{
    int64_t z    = unix_s / 86400u;
    unsigned sod = (unsigned)(unix_s % 86400u);
    t->hour   = (uint8_t)(sod / 3600u);
    t->minute = (uint8_t)((sod % 3600u) / 60u);
    t->second = (uint8_t)(sod % 60u);
    /* firmware convention: weekday 0=Sunday; epoch (days=0) is a
     * Thursday → 4. Purely cosmetic — the chip counter is user-mapped. */
    t->weekday = (uint8_t)((z + 4) % 7);

    int64_t  zz  = z + 719468;
    int64_t  era = zz / 146097;
    uint32_t doe = (uint32_t)(zz - era * 146097);
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int      y   = (int)yoe + (int)era * 400;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp  = (5u * doy + 2u) / 153u;
    t->day   = (uint8_t)(doy - (153u * mp + 2u) / 5u + 1u);
    int      mon = (int)mp + (mp < 10u ? 3 : -9);   /* mp 0=Mar → 3..14 */
    t->month = (uint8_t)mon;
    t->year  = (uint16_t)(y + (mon <= 2 ? 1 : 0));
}

/* ---- bus helpers ----------------------------------------------------- */

static esp_err_t r_rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return vq_i2c_read_reg(VQ_ADDR_RV3028, reg, buf, n);
}

static esp_err_t r_wr(uint8_t reg, const uint8_t *buf, size_t n)
{
    return vq_i2c_write_reg(VQ_ADDR_RV3028, reg, buf, n);
}

static esp_err_t r_wr8(uint8_t reg, uint8_t v)
{
    return r_wr(reg, &v, 1);
}

/* STATUS flags are write-0-to-clear; bit7 EEBUSY is read-only.
 * Clear exactly `flags`, leave every other flag latched: write 1s to
 * the untouched bits (a 1 write is ignored by the flag logic). */
esp_err_t vq_rtc_clear_flags(uint8_t flags)
{
    return r_wr8(R_REG_STATUS, (uint8_t)(VQ_RTC_ST_FLAGS & ~flags));
}

esp_err_t vq_rtc_status(uint8_t *flags)
{
    if (flags == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t e = r_rd(R_REG_STATUS, flags, 1);
    if (e == ESP_OK) {
        s_rtc.last_status = *flags;
    }
    return e;
}

/* ---- EEPROM access (for CLKOUT park-low only) ------------------------ */

static esp_err_t ee_wait_idle(void)
{
    for (int i = 0; i < R_EE_POLL_MAX; i++) {
        uint8_t st = 0;
        esp_err_t e = r_rd(R_REG_STATUS, &st, 1);
        if (e != ESP_OK) {
            return e;
        }
        if ((st & VQ_RTC_ST_EEBUSY) == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(R_EE_POLL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t ee_read_byte(uint8_t ee_addr, uint8_t *out)
{
    esp_err_t e = ee_wait_idle();
    if (e != ESP_OK) return e;
    if ((e = r_wr8(R_REG_EE_ADDR, ee_addr)) != ESP_OK) return e;
    if ((e = r_wr8(R_REG_EE_CMD, R_EE_CMD_FIRST)) != ESP_OK) return e;
    if ((e = r_wr8(R_REG_EE_CMD, R_EE_CMD_READ)) != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(R_EE_RD_WAIT_MS));
    if ((e = ee_wait_idle()) != ESP_OK) return e;
    return r_rd(R_REG_EE_DATA, out, 1);
}

static esp_err_t ee_write_byte(uint8_t ee_addr, uint8_t data)
{
    esp_err_t e = ee_wait_idle();
    if (e != ESP_OK) return e;
    if ((e = r_wr8(R_REG_EE_ADDR, ee_addr)) != ESP_OK) return e;
    if ((e = r_wr8(R_REG_EE_DATA, data)) != ESP_OK) return e;
    if ((e = r_wr8(R_REG_EE_CMD, R_EE_CMD_FIRST)) != ESP_OK) return e;
    if ((e = r_wr8(R_REG_EE_CMD, R_EE_CMD_WRITE)) != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(R_EE_WAIT_MS));
    return ee_wait_idle();
}

/* Park the NC CLKOUT pin LOW. Best-effort: every failure path is safe
 * (pin is unconnected; the only cost of a still-running 32 kHz square
 * wave is ~µA). TODO(datasheet): FD[2:0]=111→LOW is per AppMan Rev1.x
 * "Programmable Clock Output" EEPROM table at 0x35 — verify on silicon;
 * a mismatched decode here only costs that µA, nothing else.          */
static esp_err_t rtc_park_clkout(void)
{
    uint8_t ctrl1 = 0;
    esp_err_t e = r_rd(R_REG_CTRL1, &ctrl1, 1);
    if (e != ESP_OK) return e;

    bool armed_eerd = false;
    if ((ctrl1 & R_CTRL1_EERD) == 0) {
        /* pause auto-refresh so the mirror RMW can't be clobbered mid
         * sequence — restore CTRL1 on the way out (see below).         */
        e = r_wr8(R_REG_CTRL1, (uint8_t)(ctrl1 | R_CTRL1_EERD));
        if (e != ESP_OK) return e;
        armed_eerd = true;
    }

    uint8_t mirror = 0, ee = 0;
    if ((e = ee_wait_idle()) == ESP_OK &&
        (e = r_rd(R_EE_CLKOUT_ADDR, &mirror, 1)) == ESP_OK) {
        uint8_t want = (uint8_t)((mirror & ~R_EE_CLKOUT_FD) | R_EE_CLKOUT_LOW);
        if (want != mirror) {
            e = r_wr8(R_EE_CLKOUT_ADDR, want);
        }
        /* compare against the EEPROM *backing* byte (not the mirror):
         * a committed value survives refreshes; a mirror-only write is
         * silently reverted by the next auto-refresh. The read costs no
         * endurance, the commit runs only on a real diff — nCYCLE is
         * finite (p.98) and this runs on every boot.                   */
        if (e == ESP_OK &&
            (e = ee_read_byte(R_EE_CLKOUT_ADDR, &ee)) == ESP_OK &&
            ee != want) {
            e = ee_write_byte(R_EE_CLKOUT_ADDR, want);
        }
    }

    if (armed_eerd && e != ESP_ERR_TIMEOUT) {
        /* don't re-arm refresh over a cycle that may still be in flight */
        esp_err_t r = r_wr8(R_REG_CTRL1, ctrl1);
        if (e == ESP_OK) {
            e = r;
        }
    }
    return e;
}

/* ---- time read / write ------------------------------------------------ */

/* Burst-read 0x00-0x06 (single transaction — coherent snapshot) then
 * STATUS. Fills decoded + raw status; caller owns PORF handling.      */
static esp_err_t rtc_read_block(vq_rtc_time_t *out, uint8_t *status_out)
{
    uint8_t b[7] = {0};
    esp_err_t e = r_rd(R_REG_SECONDS, b, sizeof(b));
    if (e != ESP_OK) {
        return e;
    }
    uint8_t st = 0;
    e = r_rd(R_REG_STATUS, &st, 1);
    if (e != ESP_OK) {
        return e;
    }
    s_rtc.last_status = st;

    /* NOTE: a transaction stalled >~950 ms trips the part's internal
     * bus timeout and a subsequent read returns all-0xFF — which decodes
     * to a well-formed-looking garbage time (e.g. sec=85). Reject
     * out-of-range fields instead of handing bogus time upstream.      */
    vq_rtc_time_t d = {
        .second  = bcd2bin(b[0] & 0x7F),
        .minute  = bcd2bin(b[1] & 0x7F),
        .hour    = bcd2bin(b[2] & 0x3F),   /* 24 h mode forced at init */
        .weekday = (uint8_t)(b[3] & 0x07),
        .day     = bcd2bin(b[4] & 0x3F),
        .month   = bcd2bin(b[5] & 0x1F),
        .year    = (uint16_t)(2000 + bcd2bin(b[6])),
    };
    if (!fields_in_range(&d)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = d;
    if (status_out != NULL) {
        *status_out = st;
    }
    return ESP_OK;
}

esp_err_t vq_rtc_read_time(vq_rtc_time_t *t, bool *valid)
{
    if (t == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    vq_rtc_time_t d;
    uint8_t st = 0;
    esp_err_t e = rtc_read_block(&d, &st);
    if (e != ESP_OK) {
        if (valid != NULL) {
            *valid = false;
        }
        return e;
    }
    if (st & VQ_RTC_ST_PORF) {
        /* power was lost since the flag was last cleared → wall time
         * is untrusted (no backup cell). Clear it so a later power dip
         * re-latches and is caught again.                              */
        s_rtc.time_valid = false;
        s_rtc.cold_start = true;
        (void)vq_rtc_clear_flags(VQ_RTC_ST_PORF);
    }
    *t = d;
    if (valid != NULL) {
        *valid = s_rtc.time_valid;
    }
    return ESP_OK;
}

esp_err_t vq_rtc_set_time(const vq_rtc_time_t *t)
{
    if (t == NULL || !fields_in_range(t)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* force 24 h mode — CTRL2 bit1 12_24 = 0 (AppMan p.24: 0=24h)      */
    uint8_t ctrl2 = 0;
    esp_err_t e = r_rd(R_REG_CTRL2, &ctrl2, 1);
    if (e != ESP_OK) {
        return e;
    }
    if (ctrl2 & R_CTRL2_12_24) {
        e = r_wr8(R_REG_CTRL2, (uint8_t)(ctrl2 & ~R_CTRL2_12_24));
        if (e != ESP_OK) {
            return e;
        }
    }
    uint8_t b[7] = {
        bin2bcd(t->second),
        bin2bcd(t->minute),
        bin2bcd(t->hour),               /* 24 h encoding, bit5=0        */
        (uint8_t)(t->weekday & 0x07),
        bin2bcd(t->day),
        bin2bcd(t->month),
        bin2bcd((uint8_t)(t->year - 2000)),
    };
    e = r_wr(R_REG_SECONDS, b, sizeof(b));
    if (e != ESP_OK) {
        return e;
    }
    /* time is now explicitly set — the PORF "untrusted" marker is stale */
    s_rtc.time_valid = true;
    s_rtc.present    = true;
    (void)vq_rtc_clear_flags(VQ_RTC_ST_PORF);
    return ESP_OK;
}

esp_err_t vq_rtc_read_unix(uint32_t *unix_s, bool *valid)
{
    if (unix_s == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    vq_rtc_time_t t;
    esp_err_t e = vq_rtc_read_time(&t, valid);
    if (e == ESP_OK) {
        *unix_s = vq_rtc_to_unix(&t);
    }
    return e;
}

esp_err_t vq_rtc_set_unix(uint32_t unix_s)
{
    vq_rtc_time_t t;
    vq_rtc_from_unix(unix_s, &t);
    return vq_rtc_set_time(&t);
}

bool vq_rtc_present(void)     { return s_rtc.present; }
bool vq_rtc_time_valid(void)  { return s_rtc.time_valid; }

/* ------------------------------------------------------------------ */
/* vq_sensor_ops — clock resource: no channels, nothing to stream       */

static esp_err_t rtc_probe(vq_sensor_t *s)
{
    (void)s;
    if (vq_i2c_probe(VQ_ADDR_RV3028) != ESP_OK) {
        s_rtc.present = false;
        return ESP_ERR_NOT_FOUND;       /* absent → tolerated           */
    }
    uint8_t st = 0;                     /* real transaction sanity      */
    esp_err_t e = r_rd(R_REG_STATUS, &st, 1);
    if (e != ESP_OK) {
        s_rtc.present = false;
        return ESP_ERR_NOT_FOUND;
    }
    s_rtc.last_status = st;
    s_rtc.present     = true;
    return ESP_OK;
}

static esp_err_t rtc_init(vq_sensor_t *s)
{
    (void)s;
    esp_err_t e = rtc_probe(NULL);
    if (e != ESP_OK) {
        return e;
    }
    uint8_t st = s_rtc.last_status;
    s_rtc.cold_start = (st & VQ_RTC_ST_PORF) != 0;

    /* Clean flag baseline: this board consumes no RTC interrupt (INT →
     * TP28 only), so nothing latched can be serviced — drop everything
     * so vq_rtc_status() polls start meaningful. PORF we latch in
     * cold_start first (it is the "time untrusted" record).            */
    e = r_wr8(R_REG_STATUS, 0x00);      /* write-0-to-clear, all flags  */
    if (e != ESP_OK) {
        return e;
    }

    /* force 24 h mode — CTRL2 bit1 */
    uint8_t ctrl2 = 0;
    if ((e = r_rd(R_REG_CTRL2, &ctrl2, 1)) == ESP_OK &&
        (ctrl2 & R_CTRL2_12_24)) {
        e = r_wr8(R_REG_CTRL2, (uint8_t)(ctrl2 & ~R_CTRL2_12_24));
    }
    if (e != ESP_OK) {
        return e;
    }

    /* best-effort CLKOUT park-low; failure is logged, never fatal      */
    e = rtc_park_clkout();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "clkout park-low failed: %s (NC pin — cosmetic)",
                 esp_err_to_name(e));
    }

    /* validate whatever is ticking now; a cold start or garbage decode
     * leaves time_valid=false until vq_rtc_set_time()/set_unix().      */
    vq_rtc_time_t t = {0};
    uint8_t st2 = 0;
    e = rtc_read_block(&t, &st2);
    if (e != ESP_OK && e != ESP_ERR_INVALID_RESPONSE) {
        return e;                       /* real bus fault — report up   */
    }
    /* a decode-range failure (post-POR defaults or bus-timeout garbage)
     * is tolerated: time_valid stays false until an explicit set.      */
    s_rtc.time_valid = (e == ESP_OK) && !s_rtc.cold_start &&
                       !(st2 & VQ_RTC_ST_PORF);
    s_rtc.present    = true;
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "rv3028 up: %04u-%02u-%02u %02u:%02u:%02u %s%s",
                 (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
                 (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second,
                 s_rtc.time_valid ? "valid" : "INVALID (needs set)",
                 s_rtc.cold_start ? " cold-start" : "");
    } else {
        ESP_LOGW(TAG, "rv3028 time decode rejected (post-POR defaults?)"
                 " — UNSET");
    }
    return ESP_OK;
}

static esp_err_t rtc_start(vq_sensor_t *s)
{
    /* No stream: this descriptor produces zero channels — it exists for
     * probe/health bookkeeping. Calendar reads go through the public
     * vq_rtc_* helpers (clock_service/console), not the sample sink.   */
    (void)s;
    return ESP_OK;
}

static esp_err_t rtc_stop(vq_sensor_t *s)
{
    (void)s;
    return ESP_OK;
}

static esp_err_t rtc_sleep(vq_sensor_t *s)
{
    /* Documented no-op: the RTC rides the +3V3 rail that stays up in
     * deep sleep, it is the wall-clock anchor, and /INT is not wired —
     * there is nothing to arm or release. It keeps ticking; on wake the
     * next read reports PORF if the rail ever dipped.                  */
    (void)s;
    return ESP_OK;
}

static bool rtc_healthy(vq_sensor_t *s)
{
    (void)s;
    if (!s_rtc.present) {
        return false;
    }
    return vq_i2c_probe(VQ_ADDR_RV3028) == ESP_OK;
}

static const vq_sensor_ops_t rtc_ops = {
    .name       = "rv3028",
    .probe      = rtc_probe,
    .init       = rtc_init,
    .start      = rtc_start,
    .stop       = rtc_stop,
    .sleep      = rtc_sleep,
    .healthy    = rtc_healthy,
    .channels   = NULL,     /* clock resource — no sample channels     */
    .n_channels = 0,
    .rate_hz    = 0.0f,     /* nothing to schedule                     */
    .required   = false,    /* probe-optional (BOM-fitted but tolerate) */
};

vq_sensor_t vq_aux_rtc_sensor = {
    .ops  = &rtc_ops,
    .priv = &s_rtc,
};

/* ------------------------------------------------------------------ */
/* self-test fragment                                                   */

esp_err_t vq_aux_rtc_selftest(char *buf, size_t len)
{
    if (vq_i2c_probe(VQ_ADDR_RV3028) != ESP_OK) {
        snprintf(buf, len, "no ACK @0x52");
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t st = 0;
    esp_err_t e = vq_rtc_status(&st);
    if (e != ESP_OK) {
        snprintf(buf, len, "status read err 0x%x", e);
        return e;
    }

    /* Seconds-advance check on RAW seconds — works even when the
     * calendar block still holds post-POR defaults that fail range
     * decode (oscillator tick is the hardware fact being verified).    */
    uint8_t s1 = 0, s2 = 0;
    e = r_rd(R_REG_SECONDS, &s1, 1);
    if (e != ESP_OK) {
        snprintf(buf, len, "seconds read err 0x%x", e);
        return e;
    }
    vTaskDelay(pdMS_TO_TICKS(1200));
    e = r_rd(R_REG_SECONDS, &s2, 1);
    if (e != ESP_OK) {
        snprintf(buf, len, "seconds re-read err 0x%x", e);
        return e;
    }
    int delta = ((int)bcd2bin(s2 & 0x7F) - (int)bcd2bin(s1 & 0x7F) + 60) % 60;

    /* full calendar decode for the report — may legitimately fail on a
     * never-set cold start; that is the "UNSET" note, not a fault.     */
    vq_rtc_time_t t;
    bool valid = false;
    esp_err_t de = vq_rtc_read_time(&t, &valid);
    if (de == ESP_OK) {
        uint32_t ux = vq_rtc_to_unix(&t);
        snprintf(buf, len,
                 "%04u-%02u-%02uT%02u:%02u:%02uZ unix=%lu sec+=%ds %s "
                 "st=0x%02X%s",
                 (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
                 (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second,
                 (unsigned long)ux, delta,
                 valid ? "valid" : "UNSET", st,
                 (st & VQ_RTC_ST_PORF) ? " porf!" : "");
    } else {
        snprintf(buf, len, "sec+=%ds decode-rejected (UNSET) st=0x%02X",
                 delta, st);
    }
    return (delta >= 1 && delta <= 2) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
