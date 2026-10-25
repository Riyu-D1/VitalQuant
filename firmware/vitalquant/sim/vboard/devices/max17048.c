/* max17048.c — virtual MAX17048G+ fuel gauge @ 0x36 (fix_v22 U17).
 *
 * Register map + scaling are taken from the firmware driver
 * (components/vq_drv_aux/vq_max17048.c, DS 19-4209 Rev 7) — NOT from
 * memory of the datasheet:
 *
 *   0x02 VCELL   78.125 µV/LSB — reg = (mV·4/5)<<4  (12-bit ADC <<4)
 *   0x04 SOC     %/256         — reg = pct·256
 *   0x06 MODE    bit14 QuickStart (strobe, self-clears), bit13 EnSleep
 *   0x08 VERSION 0x0012 — probe accepts (v & 0xFFF0) == 0x0010
 *   0x0A HIBRT   hibernate control (stored)
 *   0x0C CONFIG  RCOMP|SLEEP ALSC ALRT ATHD[4:0]; ALRT = w0c flag
 *   0x14 VALRT   byte@0x14 = MIN, byte@0x15 = MAX, 20 mV/LSB (POR 0x00FF
 *                = "no window" per driver TODO(datasheet) note)
 *   0x16 CRATE   signed, 0.208 %/h per LSB (model-derived dSOC/dt)
 *   0x18 VRESET/ID stored
 *   0x1A STATUS  RI|VH|VL|VR|HD|SC = 0x3F low-byte flags, write-0-clear
 *   0xFE CMD     0x5400 = POR — NACKs on purpose (driver comment), the
 *                reset still happens model-side
 *
 * Behaviour:
 *  - Powered by VBAT: vboard_vbat_mv() <= 0 ⇒ chip absent ⇒ NACK, and
 *    on re-power the regs POR (no backup state on a dead VDD).
 *  - VCELL is LIVE off vboard_vbat_mv(); SOC is a linear approximation
 *    3.3 V→0 %, 4.2 V→100 % (documented approx — not ModelGauge) or a
 *    scenario override via vbdev_gauge_set_soc_pct().
 *  - VALRT window violation / SOC≤(32−ATHD) / 1 % SOC step under ALSC
 *    latch STATUS cause bits and set CONFIG.ALRT → open-drain ALRT
 *    pulls IO6 low.  STATUS is w0c, ALRT is w0c; while a violation
 *    persists the next eval re-latches it — level-held, like silicon.
 *  - QuickStart (MODE 0x4000) drops the SOC override and re-seeds the
 *    CRATE baseline — the "recompute SOC from VCELL" command.
 *  - EnSleep/HIBRT only affect the (irrelevant for a live model) update
 *    rate — stored for read-back, no behavioural cut.
 *  - reset() is a deliberate no-op: the gauge is VBAT-powered with no
 *    RESET pin — ESP_EN never reaches it; SOC survives an ESP reboot.
 *
 * Lock-free by the same argument as tca6408.c: worst race is an
 * idempotent duplicate ALRT gpio_set.
 */
#include "vboard.h"
#include "max17048.h"

#include <string.h>

#define GA_ADDR         0x36
#define GA_ALRT_GPIO    6       /* IO6 — open-drain, R74 10k pull-up   */

/* register addresses (16-bit, MSB first) */
#define R_VCELL         0x02
#define R_SOC           0x04
#define R_MODE          0x06
#define R_VERSION       0x08
#define R_HIBRT         0x0A
#define R_CONFIG        0x0C
#define R_VALRT         0x14
#define R_CRATE         0x16
#define R_VRESET_ID     0x18
#define R_STATUS        0x1A
#define R_CMD           0xFE

#define MODE_QUICKSTART 0x4000u
#define MODE_ENSLEEP    0x2000u
#define CFG_SLEEP       0x0080u
#define CFG_ALSC        0x0040u
#define CFG_ALRT        0x0020u
#define CFG_ATHD        0x001Fu
#define ST_FLAGS        0x003Fu /* RI|VH|VL|VR|HD|SC low byte, w0c     */
#define ST_RI           0x01u
#define ST_VH           0x02u
#define ST_VL           0x04u
#define ST_VR           0x08u
#define ST_HD           0x10u
#define ST_SC           0x20u
#define VER_ID          0x0012u /* satisfies (v&0xFFF0)==0x0010        */
#define CFG_POR         0x971Cu /* datasheet POR: RCOMP 0x97, ATHD 28  */
#define VALRT_POR       0x00FFu /* min=0 / max=255·20mV — "no window"  */
#define CMD_POR         0x5400u
#define VALRT_LSB_MV    20u
#define CRATE_LSB_PH    0.208f  /* %/h per LSB                         */

/* SOC approximation (documented approx — not ModelGauge):             */
#define SOC_EMPTY_MV    3300    /* 3.3 V -> 0 %                        */
#define SOC_FULL_MV     4200    /* 4.2 V -> 100 %                      */

static uint8_t  s_reg[256];     /* byte-addressable register file      */
static uint8_t  s_reg_ptr;
static int      s_powered;      /* a cell is attached (vbat_mv > 0)    */
static int      s_alrt_low;     /* IO6 currently pulled down           */
static float    s_soc_ovr = -1.0f;   /* >=0 = forced SOC %             */
static float    s_soc_prev;     /* SOC at previous eval → CRATE        */
static float    s_sc_ref;       /* SOC at last SC latch                */
static int64_t  s_t_prev_us;    /* previous eval stamp → CRATE         */

/* ---- helpers --------------------------------------------------------- */
static uint16_t rd16(int a) { return (uint16_t)((s_reg[a] << 8) | s_reg[a+1]); }
static void     wr16(int a, uint16_t v)
{
    s_reg[a]   = (uint8_t)(v >> 8);
    s_reg[a+1] = (uint8_t)(v & 0xFF);
}

static int cell_mv(void) { return vboard_vbat_mv(); }

/* 78.125 µV/LSB: ADC is 12-bit (1.25 mV/LSB) in the upper bits —
 * vraw = (mv / 1.25) << 4, matching the driver's v = raw·78.125e-6.   */
static uint16_t vcell_raw(int mv)
{
    if (mv < 0) {
        mv = 0;
    }
    uint32_t adc = (uint32_t)(mv * 4 / 5);      /* 1.25 mV/LSB          */
    if (adc > 0x0FFFu) {
        adc = 0x0FFFu;
    }
    return (uint16_t)(adc << 4);
}

static float soc_auto(int mv)
{
    float s = (float)(mv - SOC_EMPTY_MV) * 100.0f /
              (float)(SOC_FULL_MV - SOC_EMPTY_MV);
    if (s < 0.0f)   { s = 0.0f; }
    if (s > 100.0f) { s = 100.0f; }
    return s;
}

static float soc_now(int mv)
{
    return (s_soc_ovr >= 0.0f) ? s_soc_ovr : soc_auto(mv);
}

static void alrt_drive(int low)
{
    s_alrt_low = low;
    vboard_gpio_set(GA_ALRT_GPIO, low ? 0 : 1);  /* R74 restores high  */
}

/* ---- POR ----------------------------------------------------------- */
static void ga_por(int64_t now_us)
{
    memset(s_reg, 0, sizeof(s_reg));
    wr16(R_VERSION, VER_ID);
    wr16(R_CONFIG,  CFG_POR);
    wr16(R_VALRT,   VALRT_POR);
    wr16(R_STATUS,  ST_RI);          /* reset indicator latches at POR */
    s_soc_ovr   = -1.0f;
    int mv      = cell_mv();
    s_soc_prev  = soc_now(mv);
    s_sc_ref    = s_soc_prev;
    s_t_prev_us = now_us;
    s_reg_ptr   = 0;
    alrt_drive(0);
}

/* ---- live evaluate ------------------------------------------------- *
 * Refresh VCELL/SOC/CRATE, run the alert comparators, drive IO6.
 * Called on every bus txn and every tick — cheap and level-faithful.  */
static void ga_eval(int64_t now_us)
{
    int mv = cell_mv();
    if (mv <= 0) {
        if (s_powered) {
            s_powered = 0;
            alrt_drive(0);              /* dead chip can't hold OD low */
        }
        return;                         /* NACKs handled by txn gate   */
    }
    if (!s_powered) {
        s_powered = 1;
        ga_por(now_us);                 /* VDD re-power ⇒ POR          */
    }

    float soc = soc_now(mv);
    wr16(R_VCELL, vcell_raw(mv));
    wr16(R_SOC,   (uint16_t)(soc * 256.0f));

    /* CRATE: dSOC/dt over the last eval interval, int16 0.208 %/h.     */
    if (now_us > s_t_prev_us) {
        float dt_h = (float)(now_us - s_t_prev_us) / 3.6e9f;
        float ph   = (soc - s_soc_prev) / dt_h;
        int   raw  = (int)(ph / CRATE_LSB_PH);
        if (raw >  32767) { raw =  32767; }
        if (raw < -32768) { raw = -32768; }
        wr16(R_CRATE, (uint16_t)(int16_t)raw);
        s_soc_prev  = soc;
        s_t_prev_us = now_us;
    }

    /* alert comparators — cause bits are set-sticky until w0c'd.       */
    uint16_t valrt = rd16(R_VALRT);
    int vmin_mv = ((valrt >> 8) & 0xFF) * VALRT_LSB_MV;
    int vmax_mv = (valrt & 0xFF)        * VALRT_LSB_MV;
    uint16_t cfg = rd16(R_CONFIG);

    uint16_t causes = 0;
    if (mv > vmax_mv) { causes |= ST_VH; }
    if (mv < vmin_mv) { causes |= ST_VL; }
    if (soc <= (float)(32u - (cfg & CFG_ATHD))) {
        causes |= ST_HD;
    }
    if ((cfg & CFG_ALSC) &&
        (soc - s_sc_ref >= 1.0f || s_sc_ref - soc >= 1.0f)) {
        causes |= ST_SC;
        s_sc_ref = soc;
    }

    if (causes != 0) {
        uint16_t st = rd16(R_STATUS);
        wr16(R_STATUS, st | causes);
        if ((cfg & CFG_ALRT) == 0) {    /* master flag → line low      */
            wr16(R_CONFIG, cfg | CFG_ALRT);
            alrt_drive(1);
        }
    }
}

/* ---- register write semantics -------------------------------------- */
static esp_err_t ga_wr_reg(uint8_t a, uint16_t v, int64_t now_us)
{
    switch (a) {
    case R_MODE:
        if (v & MODE_QUICKSTART) {
            /* re-seed SOC from instantaneous VCELL: drop any scenario
             * override and re-anchor the CRATE baseline.              */
            s_soc_ovr  = -1.0f;
            s_soc_prev = soc_auto(cell_mv());
            s_sc_ref   = s_soc_prev;
            s_t_prev_us = now_us;
            v &= (uint16_t)~MODE_QUICKSTART;   /* strobe self-clears   */
        }
        wr16(a, v);
        break;
    case R_CONFIG: {                    /* ALRT bit is write-0-to-clear */
        uint16_t old = rd16(R_CONFIG);
        uint16_t nv  = (v & (uint16_t)~CFG_ALRT) |
                       (old & v & CFG_ALRT);   /* write 1 keeps it set */
        wr16(a, nv);
        if ((old & CFG_ALRT) && !(nv & CFG_ALRT) && s_alrt_low) {
            alrt_drive(0);              /* master flag released        */
        }
        break;
    }
    case R_STATUS: {                    /* flags are write-0-to-clear   */
        uint16_t old = rd16(R_STATUS);
        uint16_t nv  = (v & (uint16_t)~ST_FLAGS) | (old & v & ST_FLAGS);
        wr16(a, nv);
        break;
    }
    case R_VALRT:
    case R_HIBRT:
    case R_VRESET_ID:
        wr16(a, v);
        break;
    case R_CMD:
        if (v == CMD_POR) {
            ga_por(now_us);             /* reset happens…               */
            return VB_ESP_FAIL;         /* …but the bus sees a NACK    */
        }
        break;                          /* other CMDs: ignored         */
    case R_VCELL:
    case R_SOC:
    case R_CRATE:
    case R_VERSION:
    default:
        break;                          /* RO / unimplemented — ignore */
    }
    return VB_ESP_OK;
}

/* ---- I2C interface ---------------------------------------------------*/
static esp_err_t ga_i2c_write(vboard_dev_t *d, const uint8_t *data, size_t len)
{
    (void)d;
    if (cell_mv() <= 0) {
        return VB_ESP_FAIL;             /* no cell → VDD off → no ACK  */
    }
    if (len == 0) {
        return VB_ESP_OK;               /* probe: ACK                  */
    }
    s_reg_ptr = data[0];
    uint8_t a = s_reg_ptr;
    size_t  i = 1;
    while (i < len) {
        if (i + 1 < len) {
            esp_err_t e = ga_wr_reg(a, (uint16_t)((data[i] << 8) | data[i+1]),
                                    vboard_now_us());
            if (e != VB_ESP_OK) {
                return e;
            }
            i += 2;
            a += 2;
        } else {
            s_reg[a++] = data[i++];     /* odd trailing byte — stash   */
        }
    }
    s_reg_ptr = a;
    /* no eval here: alert/liveness re-checks run on reads and ticks —
     * like the real conversion cadence, so a STATUS/ALRT clear actually
     * releases the line before a persisting violation re-latches it.  */
    return VB_ESP_OK;
}

static esp_err_t ga_i2c_read(vboard_dev_t *d, uint8_t *data, size_t len)
{
    (void)d;
    if (cell_mv() <= 0) {
        return VB_ESP_FAIL;
    }
    ga_eval(vboard_now_us());           /* live regs + alert scan      */
    for (size_t i = 0; i < len; i++) {
        data[i] = s_reg[s_reg_ptr++];
    }
    return VB_ESP_OK;
}

/* ---- async + reset ----------------------------------------------------*/
static void ga_tick(vboard_dev_t *d, int64_t now_us)
{
    (void)d;
    ga_eval(now_us);
}

static void ga_reset(vboard_dev_t *d)
{
    (void)d;
    /* no-op by design: VBAT-powered, no RESET pin — ESP_EN (which calls
     * vboard_reset_all) never reaches U17.  SOC/state ride through an
     * ESP reboot exactly like silicon.  Real POR path: CMD 0xFE/0x5400
     * or VDD loss (vbat_mv -> 0).                                     */
}

/* ---- scenario knob -----------------------------------------------------*/
void vbdev_gauge_set_soc_pct(float pct)
{
    s_soc_ovr = pct;
    ga_eval(vboard_now_us());
}

/* ---- registration ------------------------------------------------------*/
static vboard_dev_t s_dev = {
    .name      = "max17048",
    .i2c_addr  = GA_ADDR,
    .i2c_write = ga_i2c_write,
    .i2c_read  = ga_i2c_read,
    .spi_cs    = -1,
    .spi_xfer  = NULL,
    .tick      = ga_tick,
    .reset     = ga_reset,
    .ctx       = NULL,
};

void vbdev_max17048_register(void)
{
    ga_por(vboard_now_us());
    s_powered = (cell_mv() > 0);
    vboard_register(&s_dev);
}
