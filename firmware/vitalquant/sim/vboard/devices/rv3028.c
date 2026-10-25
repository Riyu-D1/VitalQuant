/* rv3028.c — virtual RV-3028-C7 RTC for the vboard host sim.
 *
 * Register model mirrors the firmware's usage
 * (components/vq_drv_aux/vq_rv3028.c, components/vq_clock/vq_clock.c,
 *  components/vq_selftest/st_core.c):
 *
 *   - Calendar block 0x00-0x06 (BCD seconds/minutes/hours, weekday
 *     counter, date/month/year) — free-running with real carries
 *     (incl. month lengths + Feb-29 on %4 years).  CTRL2.12_24 (0x10
 *     bit1) selects 12 h decode on read of HOURS (bit5 = PM).
 *   - STATUS 0x0E: flags are write-0-to-clear over bits [6:0]; bit7
 *     EEBUSY is read-only and reports the in-flight EEPROM command.
 *     PORF (bit0) is set at every power-on and stays until the
 *     firmware's init writes 0x00 — the exact check+clear path the
 *     tests observe.
 *   - UNIX_TIME 0x1B-0x1E: independent 32-bit counter, LE; ticks with
 *     vboard_now_us(), initialised to 1700000000 (2023-11-14T22:13:20Z)
 *     at power-on and after R/W.  (Independent of the calendar block —
 *     like silicon — though both start from the same epoch constant.)
 *   - Alarm block 0x07-0x09 (AE bit7 excludes the field; CTRL1.WADA
 *     selects weekday-vs-date compare) — a per-second match sets
 *     STATUS.AF.  Countdown timer: TIMERVAL 0x0A/0x0B (reload),
 *     TIMER_STATUS 0x0C/0x0D (live count, read-only), CTRL1.TE starts,
 *     TD[1:0] picks 4096/64/1/(1/60) Hz, TRPT (bit7) repeats; expiry
 *     sets STATUS.TF.  CTRL2.UIE gates the periodic-update UF flag
 *     (USEL: 0 = every second, 1 = every minute).  /INT is unwired
 *     (TP28 only) — flags are set, never signalled.
 *   - EEPROM protocol: EE_ADDR 0x25 / EE_DATA 0x26 / EE_CMD 0x27.
 *     Commands must be armed with 0x00 first; 0x21 commits
 *     EEDATA→EE[EEADDR] (~10 ms EEBUSY), 0x22 fetches EE[EEADDR]→EEDATA
 *     (~1 ms), 0x11 "Update All" / 0x12 "Refresh" move the whole
 *     mirror page 0x30-0x37.  CTRL1.EERD (bit3) pauses mirror refresh.
 *   - EEPROM mirror 0x30-0x37 (RAM shadow of config EEPROM): 0x35 is
 *     CLKOUT (FD[2:0], POR = 32.768 kHz — the driver parks FD=0b111
 *     LOW); 0x36 EEOffset, 0x37 Backup (TCE bit5 / TCR[1:0] trickle
 *     charge / BSM[3:2]).  Password region 0x30-0x34 reads 0x00
 *     (*WP convention; TODO(datasheet): exact map of 36h/37h access).
 *   - ID 0x28 reads 0x00 (TODO(datasheet): undocumented ID value).
 *
 * Power: NACKs while VB_RAIL_3V3 is down; rail off→on is a POR (PORF
 * set, calendar back to the epoch constant — VBACKUP is grounded per
 * fix_v22, nothing is retained).  reset() likewise restores POR state
 * per the vboard.h contract (warm-ESP-reset persistence is NOT
 * modelled — reset() means power-on state here).
 */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "vboard.h"
#include "imu_feed.h"

/* ------------------------------- reg map ------------------------------- */
enum {
    R_SEC       = 0x00,     /* BCD 00-59                              */
    R_MIN       = 0x01,     /* BCD 00-59                              */
    R_HRS       = 0x02,     /* BCD 00-23 (24 h) / 1-12+PM (12 h)        */
    R_WDAY      = 0x03,     /* 0-6, free-running user counter          */
    R_DATE      = 0x04,     /* BCD 01-31                              */
    R_MON       = 0x05,     /* BCD 01-12                              */
    R_YEAR      = 0x06,     /* BCD 00-99 → 2000-2099                   */
    R_ALM_MIN   = 0x07,     /* bit7 AE: field excluded from match      */
    R_ALM_HRS   = 0x08,
    R_ALM_WD    = 0x09,     /* weekday or date per CTRL1.WADA          */
    R_TIMEVAL0  = 0x0A,
    R_TIMEVAL1  = 0x0B,
    R_TIMESTAT0 = 0x0C,     /* live count, read-only                   */
    R_TIMESTAT1 = 0x0D,
    R_STATUS    = 0x0E,
    R_CTRL1     = 0x0F,
    R_CTRL2     = 0x10,
    R_GPBITS    = 0x11,
    R_INTMASK   = 0x12,
    R_EVCTRL    = 0x13,
    R_TS_COUNT  = 0x14,     /* 0x14-0x1A timestamp block               */
    R_UNIX0     = 0x1B,     /* 0x1B-0x1E, LE                           */
    R_UNIX3     = 0x1E,
    R_URAM1     = 0x1F,
    R_URAM2     = 0x20,
    R_PW0       = 0x21,     /* 0x21-0x24                                */
    R_EEADDR    = 0x25,
    R_EEDATA    = 0x26,
    R_EECMD     = 0x27,
    R_ID        = 0x28,
    R_MIR0      = 0x30,     /* EEPROM mirror 0x30-0x37                  */
    R_MIR_END   = 0x37,
    EE_CLKOUT   = 0x35,
    EE_OFFSET   = 0x36,
    EE_BACKUP   = 0x37,
    R_NREGS     = 0x40,
};

/* STATUS bits (vq_drv_aux.h VQ_RTC_ST_*) */
#define ST_PORF   0x01
#define ST_EVF    0x02
#define ST_AF     0x04
#define ST_TF     0x08
#define ST_UF     0x10
#define ST_BSF    0x20
#define ST_CLKF   0x40
#define ST_EEBUSY 0x80
#define ST_FLAGS  0x7F

/* CTRL1 */
#define C1_TRPT   0x80
#define C1_WADA   0x20
#define C1_USEL   0x10
#define C1_EERD   0x08
#define C1_TE     0x04
#define C1_TD_M   0x03

/* CTRL2 */
#define C2_TSE    0x80
#define C2_CLKIE  0x40
#define C2_UIE    0x20
#define C2_TIE    0x10
#define C2_AIE    0x08
#define C2_EIE    0x04
#define C2_12_24  0x02
#define C2_RESET  0x01

/* EE commands (must be preceded by the 0x00 arm write) */
#define EEC_ARM     0x00
#define EEC_UPDATE  0x11    /* mirror -> EEPROM                        */
#define EEC_REFRESH 0x12    /* EEPROM -> mirror                        */
#define EEC_WRITE1  0x21    /* EEDATA -> EE[EEADDR]                    */
#define EEC_READ1   0x22    /* EE[EEADDR] -> EEDATA                    */
#define EE_WR_US    10000   /* AppMan ~10 ms                            */
#define EE_RD_US    1000    /* ~1 ms                                    */

#define POR_UNIX    1700000000u   /* 2023-11-14T22:13:20Z               */

/* timer TD clock periods (µs): 00→4096 Hz, 01→64 Hz, 10→1 Hz, 11→1/60 Hz */
static const int64_t s_td_us[4] = { 1000000 / 4096, 15625, 1000000, 60000000 };

typedef struct {
    bool            powered;
    uint8_t         reg[R_NREGS];
    uint8_t         ee[R_NREGS];     /* EEPROM backing (user + cfg)    */
    uint8_t         regptr;

    int64_t         last_adv_us;     /* calendar sub-second anchor      */
    uint32_t        unix0_s;         /* unix counter base               */
    int64_t         unix0_us;

    /* EEPROM command engine */
    bool            ee_armed;
    uint8_t         ee_op;           /* pending op, 0 = idle            */
    uint8_t         ee_op_addr, ee_op_data;
    int64_t         ee_busy_until;

    /* countdown timer */
    uint16_t        tim_cnt;         /* live count → TIMERSTAT         */
    int64_t         tim_next_us;

    bool            prev_alm;        /* alarm match edge detect         */

    pthread_mutex_t lock;
} rv3028_t;

/* ------------------------------- helpers ------------------------------- */
static uint8_t bcd2bin(uint8_t b) { return (uint8_t)((b & 0x0F) + ((b >> 4) * 10)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static int mdays(int mon, int yr)
{
    static const int8_t d[12] =
        { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (mon < 1 || mon > 12) return 31;
    if (mon == 2 && (yr % 4) == 0) return 29;   /* 2000-2099: %4 rule ok */
    return d[mon - 1];
}

/* civil date from unix seconds — same Hinnant decomposition the
 * firmware uses (vq_rv3028.c vq_rtc_from_unix).                        */
static void civil_from_unix(uint32_t ux, int *yr, int *mo, int *dy,
                            int *wd, int *hr, int *mi, int *se)
{
    const int64_t  z   = ux / 86400u;
    const unsigned sod = ux % 86400u;
    *hr = (int)(sod / 3600u); *mi = (int)((sod % 3600u) / 60u);
    *se = (int)(sod % 60u);
    *wd = (int)((z + 4) % 7);
    const int64_t  zz  = z + 719468;
    const int64_t  era = zz / 146097;
    const uint32_t doe = (uint32_t)(zz - era * 146097);
    const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int      y   = (int)yoe + (int)era * 400;
    const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const uint32_t mp  = (5u * doy + 2u) / 153u;
    *dy = (int)(doy - (153u * mp + 2u) / 5u + 1u);
    const int mon = (int)mp + (mp < 10u ? 3 : -9);
    *mo = mon;
    *yr = y + (mon <= 2 ? 1 : 0);
}

/* ------------------------- calendar + flags ---------------------------- */
static void cal_store(rv3028_t *s, int se, int mi, int hr, int wd,
                      int dy, int mo, int yr)
{
    s->reg[R_SEC]  = bin2bcd((uint8_t)se);
    s->reg[R_MIN]  = bin2bcd((uint8_t)mi);
    s->reg[R_HRS]  = bin2bcd((uint8_t)hr);
    s->reg[R_WDAY] = (uint8_t)(wd & 7);
    s->reg[R_DATE] = bin2bcd((uint8_t)dy);
    s->reg[R_MON]  = bin2bcd((uint8_t)mo);
    s->reg[R_YEAR] = bin2bcd((uint8_t)yr);
}

/* decode current calendar to plain ints; out-of-range POR garbage is
 * clamped so the cascade stays sane until a set-time fixes it.        */
static void cal_load(const rv3028_t *s, int *se, int *mi, int *hr,
                     int *wd, int *dy, int *mo, int *yr)
{
    *se = bcd2bin(s->reg[R_SEC] & 0x7F);  if (*se > 59) *se = 0;
    *mi = bcd2bin(s->reg[R_MIN] & 0x7F);  if (*mi > 59) *mi = 0;
    *hr = bcd2bin(s->reg[R_HRS] & 0x3F);  if (*hr > 23) *hr = 0;
    *wd = s->reg[R_WDAY] & 7;
    *dy = bcd2bin(s->reg[R_DATE] & 0x3F); if (*dy > 31) *dy = 1;
    *mo = bcd2bin(s->reg[R_MON] & 0x1F);  if (*mo > 12) *mo = 0;
    *yr = bcd2bin(s->reg[R_YEAR]);
}

static void cal_add(rv3028_t *s, int64_t nsec)
{
    int se, mi, hr, wd, dy, mo, yr;
    cal_load(s, &se, &mi, &hr, &wd, &dy, &mo, &yr);
    int64_t t = se + nsec;
    se = (int)(t % 60); t /= 60;
    t += mi; mi = (int)(t % 60); t /= 60;
    t += hr; hr = (int)(t % 24); t /= 24;      /* t = whole days now    */
    wd = (int)((wd + t) % 7);
    while (t-- > 0) {
        if (++dy > mdays(mo, yr)) {
            dy = 1;
            if (++mo > 12) { mo = 1; yr = (yr + 1) % 100; }
        }
    }
    cal_store(s, se, mi, hr, wd, dy, mo, yr);
}

static bool alarm_match(const rv3028_t *s)
{
    int se, mi, hr, wd, dy, mo, yr;
    cal_load(s, &se, &mi, &hr, &wd, &dy, &mo, &yr);
    (void)se;
    const uint8_t am = s->reg[R_ALM_MIN], ah = s->reg[R_ALM_HRS],
                  aw = s->reg[R_ALM_WD];
    if (!(am & 0x80) && bcd2bin(am & 0x7F) != mi) return false;
    if (!(ah & 0x80) && bcd2bin(ah & 0x7F) != hr) return false;
    if (!(aw & 0x80)) {
        const int want = bcd2bin(aw & 0x3F);
        const int have = (s->reg[R_CTRL1] & C1_WADA) ? dy : wd;
        if (want != have) return false;
    }
    return true;
}

/* Push the model forward to `now`: EEPROM engine completion, countdown
 * timer, calendar advance, per-second flag/alarm evaluation.          */
static void rv_advance(rv3028_t *s, int64_t now)
{
    /* EEPROM op completes when its busy window elapses */
    if (s->ee_op && now >= s->ee_busy_until) {
        const uint8_t op = s->ee_op;
        s->ee_op = 0;
        switch (op) {
        case EEC_WRITE1:
            s->ee[s->ee_op_addr] = s->ee_op_data;
            if (!(s->reg[R_CTRL1] & C1_EERD) &&
                s->ee_op_addr >= R_MIR0 && s->ee_op_addr <= R_MIR_END)
                s->reg[s->ee_op_addr] = s->ee_op_data;
            break;
        case EEC_READ1:
            s->reg[R_EEDATA] = s->ee[s->ee_op_addr];
            break;
        case EEC_UPDATE:
            for (uint8_t a = R_MIR0; a <= R_MIR_END; a++) s->ee[a] = s->reg[a];
            break;
        case EEC_REFRESH:
            for (uint8_t a = R_MIR0; a <= R_MIR_END; a++) s->reg[a] = s->ee[a];
            break;
        default: break;
        }
    }

    /* countdown timer */
    if ((s->reg[R_CTRL1] & C1_TE) && s->tim_next_us > 0) {
        const int64_t per = s_td_us[s->reg[R_CTRL1] & C1_TD_M];
        while (s->tim_next_us <= now) {
            s->tim_next_us += per;
            if (s->tim_cnt > 0 && --s->tim_cnt == 0) {
                s->reg[R_STATUS] |= ST_TF;
                if (s->reg[R_CTRL1] & C1_TRPT) {
                    s->tim_cnt = (uint16_t)(s->reg[R_TIMEVAL0] |
                                            (s->reg[R_TIMEVAL1] << 8));
                } else {
                    s->reg[R_CTRL1] &= (uint8_t)~C1_TE;   /* one-shot   */
                    break;
                }
            }
        }
    }

    const int64_t dsec = (now - s->last_adv_us) / 1000000;
    if (dsec <= 0) return;
    const int se0 = bcd2bin(s->reg[R_SEC] & 0x7F);
    s->last_adv_us += dsec * 1000000;
    cal_add(s, dsec);

    /* UF: periodic time-update flag, gated by UIE (INT unwired — only
     * the latch is observable anyway)                                 */
    if (s->reg[R_CTRL2] & C2_UIE) {
        const int se1 = bcd2bin(s->reg[R_SEC] & 0x7F);
        if (!(s->reg[R_CTRL1] & C1_USEL) || (se1 < se0))
            s->reg[R_STATUS] |= ST_UF;
    }
    /* alarm match → AF */
    const bool m = alarm_match(s);
    if (m && !s->prev_alm) s->reg[R_STATUS] |= ST_AF;
    s->prev_alm = m;
}

static bool rv_ee_busy(const rv3028_t *s, int64_t now)
{
    return s->ee_op != 0 && now < s->ee_busy_until;
}

/* ------------------------------ reg access ----------------------------- */
static void rv_reg_write(rv3028_t *s, uint8_t a, uint8_t v, int64_t now)
{
    if (a >= R_NREGS) return;
    switch (a) {
    case R_STATUS:
        /* write-0-to-clear over the latchable flag bits; EEBUSY ro    */
        s->reg[R_STATUS] &= v;
        s->reg[R_STATUS] &= ST_FLAGS;
        return;
    case R_TIMESTAT0: case R_TIMESTAT1:           /* read-only           */
    case R_TS_COUNT:                              /* timestamp ro block  */
        return;
    case R_CTRL2:
        if (v & C2_RESET) {                        /* soft reset        */
            s->reg[R_CTRL1]   = 0;
            s->reg[R_CTRL2]   = 0;
            s->reg[R_GPBITS]  = 0;
            s->reg[R_INTMASK] = 0;
            s->reg[R_EVCTRL]  = 0;
            s->reg[R_STATUS]  = 0;             /* flags + latched PORF   */
            s->tim_next_us = 0;
            return;
        }
        s->reg[a] = v;
        return;
    case R_CTRL1: {
        const uint8_t old = s->reg[a];
        s->reg[a] = v;
        if ((v & C1_TE) && !(old & C1_TE)) {       /* timer start edge  */
            s->tim_cnt = (uint16_t)(s->reg[R_TIMEVAL0] |
                                    (s->reg[R_TIMEVAL1] << 8));
            s->tim_next_us = now + s_td_us[v & C1_TD_M];
            if (s->tim_cnt == 0) s->reg[R_STATUS] |= ST_TF;
        } else if (!(v & C1_TE)) {
            s->tim_next_us = 0;
        }
        return;
    }
    case R_UNIX0: case R_UNIX0 + 1: case R_UNIX0 + 2: case R_UNIX3: {
        s->reg[a] = v;                             /* build then rebase */
        s->unix0_s  = (uint32_t)s->reg[R_UNIX0]        |
                      ((uint32_t)s->reg[R_UNIX0 + 1] << 8)  |
                      ((uint32_t)s->reg[R_UNIX0 + 2] << 16) |
                      ((uint32_t)s->reg[R_UNIX3] << 24);
        s->unix0_us = now;
        return;
    }
    case R_EECMD:
        if (v == EEC_ARM) { s->ee_armed = true; return; }
        if (s->ee_armed && !rv_ee_busy(s, now) &&
            (v == EEC_WRITE1 || v == EEC_READ1 ||
             v == EEC_UPDATE || v == EEC_REFRESH)) {
            s->ee_op         = v;
            s->ee_op_addr    = s->reg[R_EEADDR] & (R_NREGS - 1);
            s->ee_op_data    = s->reg[R_EEDATA];
            s->ee_busy_until = now +
                ((v == EEC_WRITE1 || v == EEC_UPDATE) ? EE_WR_US : EE_RD_US);
        }
        s->ee_armed = false;
        return;
    default:
        if (a <= R_YEAR) {
            s->reg[a] = v;                        /* calendar write     */
            s->last_adv_us = now;                 /* resync sub-second  */
            return;
        }
        s->reg[a] = v;                            /* mirror 0x30-0x37    */
        return;                                   /* + everything else   */
    }
}

static uint8_t rv_reg_read(rv3028_t *s, uint8_t a, int64_t now)
{
    switch (a) {
    case R_STATUS:
        return (uint8_t)((s->reg[R_STATUS] & ST_FLAGS) |
                         (rv_ee_busy(s, now) ? ST_EEBUSY : 0));
    case R_HRS:
        if (s->reg[R_CTRL2] & C2_12_24) {           /* 12 h + PM view    */
            int h = bcd2bin(s->reg[R_HRS] & 0x3F);
            if (h > 23) h = 0;
            const int pm = h >= 12;
            h %= 12; if (h == 0) h = 12;
            return (uint8_t)(bin2bcd((uint8_t)h) | (pm ? 0x20 : 0));
        }
        return s->reg[R_HRS];
    case R_TIMESTAT0: return (uint8_t)(s->tim_cnt & 0xFF);
    case R_TIMESTAT1: return (uint8_t)(s->tim_cnt >> 8);
    case R_UNIX0: case R_UNIX0 + 1: case R_UNIX0 + 2: case R_UNIX3: {
        const uint32_t ux = s->unix0_s +
            (uint32_t)((now - s->unix0_us) / 1000000);
        return (uint8_t)(ux >> ((a - R_UNIX0) * 8));
    }
    default: break;
    }
    if (a >= R_MIR0 && a <= R_MIR_END && a != EE_CLKOUT &&
        a != EE_OFFSET && a != EE_BACKUP)
        return 0x00;            /* password mirror bytes: read-as-0   */
    return s->reg[a];
}

/* ------------------------------ vboard ops ----------------------------- */
static esp_err_t rv_write(vboard_dev_t *d, const uint8_t *data, size_t len)
{
    rv3028_t *s = (rv3028_t *)d->ctx;
    if (!vboard_rail(VB_RAIL_3V3)) return VB_ESP_ERR_TIMEOUT;   /* NACK */
    const int64_t now = vboard_now_us();
    pthread_mutex_lock(&s->lock);
    rv_advance(s, now);
    if (len > 0) {
        s->regptr = data[0];
        for (size_t i = 1; i < len; i++) {
            rv_reg_write(s, s->regptr, data[i], now);
            s->regptr = (uint8_t)((s->regptr + 1) & 0x3F);
        }
    }
    pthread_mutex_unlock(&s->lock);
    return VB_ESP_OK;
}

static esp_err_t rv_read(vboard_dev_t *d, uint8_t *data, size_t len)
{
    rv3028_t *s = (rv3028_t *)d->ctx;
    if (!vboard_rail(VB_RAIL_3V3)) return VB_ESP_ERR_TIMEOUT;   /* NACK */
    const int64_t now = vboard_now_us();
    pthread_mutex_lock(&s->lock);
    rv_advance(s, now);
    for (size_t i = 0; i < len; i++) {
        const uint8_t a = s->regptr;
        data[i] = (a < R_NREGS) ? rv_reg_read(s, a, now) : 0x00;
        s->regptr = (uint8_t)((a + 1) & 0x3F);
    }
    pthread_mutex_unlock(&s->lock);
    return VB_ESP_OK;
}

static void rv_por(rv3028_t *s, int64_t now)
{
    memset(s->reg, 0, sizeof s->reg);
    /* calendar + unix both seed from the same epoch constant; the two
     * counters are independent from there on (like the silicon).      */
    int yr, mo, dy, wd, hr, mi, se;
    civil_from_unix(POR_UNIX, &yr, &mo, &dy, &wd, &hr, &mi, &se);
    cal_store(s, se, mi, hr, wd, dy, mo, yr - 2000);
    s->last_adv_us = now;
    s->unix0_s     = POR_UNIX;
    s->unix0_us    = now;
    s->reg[R_STATUS] = ST_PORF;
    /* EEPROM defaults: CLKOUT = CLKOE|FD_32k (factory), Backup = 0.   */
    memset(s->ee, 0, sizeof s->ee);
    s->ee[EE_CLKOUT] = 0x80;                 /* CLKOE=1, FD=000 → 32k   */
    for (uint8_t a = R_MIR0; a <= R_MIR_END; a++) s->reg[a] = s->ee[a];
    s->ee_armed = false;
    s->ee_op = 0;
    s->tim_cnt = 0;
    s->tim_next_us = 0;
    s->prev_alm = false;
}

static void rv_tick(vboard_dev_t *d, int64_t now)
{
    rv3028_t *s = (rv3028_t *)d->ctx;
    pthread_mutex_lock(&s->lock);
    if (s->powered) rv_advance(s, now);
    pthread_mutex_unlock(&s->lock);
}

static void rv_reset(vboard_dev_t *d)
{
    rv3028_t *s = (rv3028_t *)d->ctx;
    pthread_mutex_lock(&s->lock);
    rv_por(s, vboard_now_us());
    pthread_mutex_unlock(&s->lock);
}

static void rv_rail_cb(vboard_rail_t r, bool on, void *ctx)
{
    (void)r;
    rv3028_t *s = (rv3028_t *)ctx;
    pthread_mutex_lock(&s->lock);
    if (on) {
        if (!s->powered) { s->powered = true; rv_por(s, vboard_now_us()); }
    } else {
        s->powered = false;
    }
    pthread_mutex_unlock(&s->lock);
}

static rv3028_t     s_rv;
static vboard_dev_t s_rv_dev = {
    .name      = "rv3028",
    .i2c_addr  = 0x52,
    .i2c_write = rv_write,
    .i2c_read  = rv_read,
    .spi_cs    = -1,
    .spi_xfer  = NULL,
    .tick      = rv_tick,
    .reset     = rv_reset,
    .ctx       = &s_rv,
};

void vbdev_rv3028_register(void)
{
    static bool done;
    if (done) return;
    done = true;
    pthread_mutex_init(&s_rv.lock, NULL);
    s_rv.powered = vboard_rail(VB_RAIL_3V3);
    rv_por(&s_rv, vboard_now_us());
    vboard_on_rail(VB_RAIL_3V3, rv_rail_cb, &s_rv);
    vboard_register(&s_rv_dev);
}

/* Self-register at load so the default population exists even before
 * vboard_init() wires anything explicitly (idempotent via `done`).    */
#if defined(__GNUC__)
__attribute__((constructor))
static void rv_ctor(void)
{
    vbdev_rv3028_register();
}
#endif
