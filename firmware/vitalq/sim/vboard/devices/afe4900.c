/* afe4900.c — AFE4900YZR (U6) PPG AFE device model.
 *
 * The firmware driver (components/vq_drv_afe4900/src/vq_drv_afe4900.c +
 * afe4900_regs.h) is the protocol spec — its map is placeholder-per-
 * datasheet and marked TODO(datasheet); this model implements exactly
 * what the driver exercises.
 *
 * Wire truth (board_pins.h / driver):
 *   SEN = CS_AFE4900 = IO18, SPI mode 0, 32-bit frames {8-bit addr}
 *   {24-bit data}, MSB first. Back-to-back frames inside one CS window
 *   are legal (FIFO bursts ride a single CS — the driver clocks n*4
 *   bytes of {0x42,0,0,0} in one xfer).
 *   RESETZ = TCA6408 P0 (default low = held in reset/power-down). The
 *   model answers ONLY while P0 is high AND VB_RAIL_3V3_ANA is up;
 *   in reset MISO floats high (0xFF) and writes are ignored.
 *   ADC_RDY = IO21, active-HIGH pulse (driver arms POSEDGE).
 *   CLK strapped to GND -> internal 128 kHz osc: PRF = 128k/(PRPCT+1),
 *   output/ADC_RDY rate = PRF/(NUMAV+1) -> 800/4 = 200 Hz at the
 *   driver's default PRPCOUNT=159, CONTROL1=0x103.
 *
 * Read semantics (afe4900_regs.h): set CONTROL0.SPI_READ once, then a
 * write-shaped frame {addr}{24 dummy} returns the register on SDOUT
 * during the data clocks. A non-zero write to CONTROL0 while SPI_READ=1
 * is the only way back to write mode — a zero payload to CONTROL0 is a
 * CONTROL0 read (the driver's selftest relies on both behaviours).
 *
 * FIFO: 128 x 24-bit; each output frame enqueues the 4 slot results in
 * the driver's RIGID plan order {amb_red(ALED1), red(ALED2), amb_ir
 * (LED2), ir(LED1)} subject to FIFO_PHASE mask. FIFO_COUNT returns the
 * live depth (low byte); FIFO_DATA pops one word per read frame.
 *
 * Photons: lit-slot counts = ambient + LED-code * (DC + pulse*w),
 * scaled by the LEDCNTRL1/2 code fields the driver wrote (LED3 field ->
 * red on TX3, LED4 field -> IR on TX4 — RIGID plan binding). The LED
 * pulse train needs the TX_5V boost: with VB_RAIL_TX5 down the photodiode
 * sees ambient only (delta collapses — that is the honest model for the
 * led_sched budget tests).
 */
#include "vboard.h"
#include "bioz_feed.h"

#include <string.h>

/* ------------------------------ Pins / wiring -------------------------- */
#define AFE_CS_GPIO       18     /* CS_AFE4900 (SEN)                       */
#define AFE_RDY_GPIO      21     /* AFE4900_ADC_RDY, active-high pulse     */
#define AFE_EXP_RESETZ    0      /* TCA6408 P0 — high = out of reset       */

/* ------------------------------ Register map --------------------------- */
/* Addresses as used by the driver / afe4900_regs.h (subset the model
 * treats specially; everything else is a plain 24-bit store).          */
#define AFE_REG_CONTROL0    0x00
#define AFE_REG_LED2STC     0x01
#define AFE_REG_PRPCOUNT    0x1D
#define AFE_REG_CONTROL1    0x1E
#define AFE_REG_LEDCNTRL1   0x22
#define AFE_REG_CONTROL2    0x23
#define AFE_REG_LEDCNTRL2   0x24
#define AFE_REG_LED2VAL     0x2A   /* slot2 — ambient for IR             */
#define AFE_REG_ALED2VAL    0x2B   /* slot1 — red-lit sample             */
#define AFE_REG_LED1VAL     0x2C   /* slot3 — IR-lit sample              */
#define AFE_REG_ALED1VAL    0x2D   /* slot0 — ambient for red            */
#define AFE_REG_LED2_ALED2  0x2E   /* LED2VAL - ALED2VAL                 */
#define AFE_REG_LED1_ALED1  0x2F   /* LED1VAL - ALED1VAL                 */
#define AFE_REG_AVG_LED2    0x3F   /* averaged differences               */
#define AFE_REG_AVG_LED1    0x40
#define AFE_REG_FIFO_DATA   0x42   /* FIFO pop port                      */
#define AFE_REG_FIFO_CTRL   0x48
#define AFE_REG_FIFO_COUNT  0x49
#define AFE_REG_FIFO_PHASE  0x4A
#define AFE_REG_POWERDN     0x4C

/* CONTROL0 fields the driver toggles. */
#define AFE_C0_SPI_READ     (1u << 0)
#define AFE_C0_TM_COUNT_RST (1u << 1)
#define AFE_C0_SW_RESET     (1u << 3)
#define AFE_C0_FIFO_EN      (1u << 4)

/* CONTROL1: NUMAV in [7:0], TIMEREN bit8. */
#define AFE_C1_TIMEREN      (1u << 8)

/* LED current code fields (driver-named shifts — TODO(datasheet) layout,
 * LED3 -> red fire window -> TX3, LED4 -> IR fire window -> TX4).      */
#define AFE_LED3_CODE(c)    ((c)->regs[AFE_REG_LEDCNTRL1] & 0xFFu)
#define AFE_LED4_CODE(c)    (((c)->regs[AFE_REG_LEDCNTRL2] >> 16) & 0xFFu)

/* Timing. */
#define AFE_OSC_HZ          128000u                 /* internal osc        */
#define AFE_RDY_HIGH_US     60                      /* ADC_RDY pulse width */

/* FIFO. */
#define AFE_FIFO_DEPTH      128
#define AFE_FIFO_WORDS      4      /* {amb_red, red, amb_ir, ir}          */

/* Optical model. */
#define AFE_AMB_COUNTS      60000  /* ambient-light PD baseline          */
#define AFE_AMB_NOISE       300    /* +/- ambient noise                  */
#define AFE_LED_DC_PER_CODE 200    /* reflected-DC counts per LED code   */
#define AFE_LED_AC_PER_CODE 400    /* pulse counts per code at v=1       */

/* ------------------------------ Model state ---------------------------- */
typedef struct {
    uint32_t regs[256];
    uint32_t fifo[AFE_FIFO_DEPTH];
    uint16_t fifo_head;     /* oldest word index                        */
    uint16_t fifo_cnt;
    bool     active;        /* railed + RESETZ released                 */
    bool     engine;        /* TIMEREN && !POWERDN                      */
    int64_t  frame_us;      /* output period from PRP*NUMAV             */
    int64_t  next_frame_us;
    bool     rdy_high;
    int64_t  rdy_release_us;
    uint32_t frame_seq;
} afe_ctx_t;

static afe_ctx_t s_afe;

/* Deterministic per-(t,lane) noise in [0,1). */
static float afe_noise(int64_t t_us, int lane)
{
    uint64_t h = (uint64_t)t_us * 0x9E3779B97F4A7C15ull
               + (uint64_t)lane * 0xD1B54A32D192ED03ull;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27; h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return (float)(h & 0xFFFFFFu) / 16777216.0f;
}

/* ------------------------------ Reset / power --------------------------- */
static void afe_por(afe_ctx_t *c)
{
    memset(c->regs, 0, sizeof(c->regs));   /* family reset = all zeros   */
    c->regs[AFE_REG_FIFO_PHASE] = 0x0F;    /* all four slots stored      */
    c->fifo_head = 0;
    c->fifo_cnt  = 0;
    c->engine    = false;
    c->frame_us  = 0;
    c->frame_seq = 0;
}

/* Active iff rail up AND RESETZ released; a release edge is a full POR
 * (the part loses its register file while held in reset). -1 (expander
 * absent) = held in reset — no SPI answer before P0 is driven.         */
static void afe_active_sync(afe_ctx_t *c)
{
    bool a = vboard_rail(VB_RAIL_3V3_ANA) && (vboard_exp_pin(AFE_EXP_RESETZ) == 1);
    if (a && !c->active) afe_por(c);
    if (!a) {
        c->engine = false;
        if (c->rdy_high) { vboard_gpio_set(AFE_RDY_GPIO, 0); c->rdy_high = false; }
    }
    c->active = a;
}

/* ------------------------------ Timing engine --------------------------- */
/* frame_us = (PRPCT+1) * (NUMAV+1) counts at 7.8125 us/count. */
static void afe_retime(afe_ctx_t *c)
{
    uint64_t prp   = (c->regs[AFE_REG_PRPCOUNT] & 0xFFFFu) + 1u;
    uint64_t numav = (c->regs[AFE_REG_CONTROL1] & 0xFFu) + 1u;
    c->frame_us = (int64_t)((prp * numav * 78125ull) / 10000ull);
    c->engine   = (c->regs[AFE_REG_CONTROL1] & AFE_C1_TIMEREN) != 0
               && (c->regs[AFE_REG_POWERDN] & 1u) == 0;
}

/* ------------------------------ FIFO ------------------------------------ */
static void afe_fifo_push(afe_ctx_t *c, uint32_t w)
{
    if (c->fifo_cnt >= AFE_FIFO_DEPTH) return;   /* full: word dropped    */
    uint16_t tail = (uint16_t)((c->fifo_head + c->fifo_cnt) % AFE_FIFO_DEPTH);
    c->fifo[tail] = w & 0xFFFFFFu;
    c->fifo_cnt++;
}

static uint32_t afe_fifo_pop(afe_ctx_t *c)
{
    if (c->fifo_cnt == 0) return 0;
    uint32_t w = c->fifo[c->fifo_head];
    c->fifo_head = (uint16_t)((c->fifo_head + 1u) % AFE_FIFO_DEPTH);
    c->fifo_cnt--;
    return w;
}

/* ------------------------------ Conversions ----------------------------- */
/* One output frame: refresh the four slot VAL registers and enqueue them
 * in the driver's slot order (subject to FIFO_PHASE mask + FIFO_EN).   */
static void afe_convert(afe_ctx_t *c, int64_t now_us)
{
    float wave = vbbio_ppg(now_us);                /* ~0..1 pulse        */
    bool  lit  = vboard_rail(VB_RAIL_TX5);         /* LED anode supply   */
    uint32_t c_red = AFE_LED3_CODE(c);             /* red  -> TX3        */
    uint32_t c_ir  = AFE_LED4_CODE(c);             /* IR   -> TX4        */

    int32_t amb_r = AFE_AMB_COUNTS
                  + (int32_t)((afe_noise(now_us, 1) - 0.5f) * 2 * AFE_AMB_NOISE);
    int32_t amb_i = AFE_AMB_COUNTS
                  + (int32_t)((afe_noise(now_us, 2) - 0.5f) * 2 * AFE_AMB_NOISE);
    int32_t red = amb_r + (lit ? (int32_t)(c_red * (AFE_LED_DC_PER_CODE
                  + (int)(wave * AFE_LED_AC_PER_CODE))) : 0);
    int32_t ir  = amb_i + (lit ? (int32_t)(c_ir  * (AFE_LED_DC_PER_CODE
                  + (int)(wave * AFE_LED_AC_PER_CODE))) : 0);

    uint32_t words[AFE_FIFO_WORDS] = {
        (uint32_t)amb_r & 0xFFFFFFu,   /* slot0 -> ALED1VAL (amb_red)    */
        (uint32_t)red   & 0xFFFFFFu,   /* slot1 -> ALED2VAL (red)        */
        (uint32_t)amb_i & 0xFFFFFFu,   /* slot2 -> LED2VAL  (amb_ir)     */
        (uint32_t)ir    & 0xFFFFFFu,   /* slot3 -> LED1VAL  (ir)         */
    };
    c->regs[AFE_REG_ALED1VAL] = words[0];
    c->regs[AFE_REG_ALED2VAL] = words[1];
    c->regs[AFE_REG_LED2VAL]  = words[2];
    c->regs[AFE_REG_LED1VAL]  = words[3];
    /* LED-minus-ambient + averaged-difference helpers (TODO(datasheet):
     * the averaging window is unconfirmed — report the latest diff).   */
    c->regs[AFE_REG_LED2_ALED2] = ((uint32_t)(amb_i - red)) & 0xFFFFFFu;
    c->regs[AFE_REG_LED1_ALED1] = ((uint32_t)(ir - amb_r))  & 0xFFFFFFu;
    c->regs[AFE_REG_AVG_LED2]   = c->regs[AFE_REG_LED2_ALED2];
    c->regs[AFE_REG_AVG_LED1]   = c->regs[AFE_REG_LED1_ALED1];
    c->frame_seq++;

    if (!(c->regs[AFE_REG_CONTROL0] & AFE_C0_FIFO_EN)) return;
    uint32_t mask = c->regs[AFE_REG_FIFO_PHASE] & 0xFu;
    for (int i = 0; i < AFE_FIFO_WORDS; i++)
        if (mask & (1u << i)) afe_fifo_push(c, words[i]);
}

/* ------------------------------ SPI frames ------------------------------ */
static uint32_t afe_reg_read(afe_ctx_t *c, uint8_t addr)
{
    switch (addr) {
    case AFE_REG_FIFO_COUNT: return c->fifo_cnt & 0xFFu;
    case AFE_REG_FIFO_DATA:  return afe_fifo_pop(c);
    default:                 return c->regs[addr] & 0xFFFFFFu;
    }
}

static void afe_reg_write(afe_ctx_t *c, uint8_t addr, uint32_t val)
{
    switch (addr) {
    case AFE_REG_CONTROL0:
        if (val & AFE_C0_SW_RESET) {
            afe_por(c);                    /* soft reset -> POR incl. C0 */
            break;
        }
        /* SW_RESET / TM_COUNT_RST are strobes — never latched. */
        c->regs[addr] = val & ~(AFE_C0_SW_RESET | AFE_C0_TM_COUNT_RST)
                        & 0xFFFFFFu;
        if (val & AFE_C0_TM_COUNT_RST) {   /* restart the timing counter */
            c->fifo_head = c->fifo_cnt = 0;
            if (c->engine)
                c->next_frame_us = vboard_now_us() + c->frame_us;
        }
        break;
    case AFE_REG_CONTROL1:
    case AFE_REG_PRPCOUNT:
        c->regs[addr] = val & 0xFFFFFFu;
        {
            bool was = c->engine;
            afe_retime(c);
            if (c->engine && !was)
                c->next_frame_us = vboard_now_us() + c->frame_us;
        }
        break;
    case AFE_REG_POWERDN:
        c->regs[addr] = val & 0xFFFFFFu;
        afe_retime(c);                     /* bit0 gates the engine      */
        break;
    case AFE_REG_FIFO_COUNT:
    case AFE_REG_FIFO_DATA:
        break;                             /* read-only                  */
    default:
        c->regs[addr] = val & 0xFFFFFFu;
        break;
    }
}

/* One 32-bit frame at (tx[4], rx[4]). */
static void afe_frame(afe_ctx_t *c, const uint8_t *tx, uint8_t *rx)
{
    uint8_t  addr = tx ? tx[0] : 0;
    uint32_t val  = tx ? ((uint32_t)tx[1] << 16) |
                         ((uint32_t)tx[2] << 8)  | (uint32_t)tx[3] : 0;
    if (rx) { rx[0] = rx[1] = rx[2] = rx[3] = 0xFF; }
    if (!c->active) return;

    bool rd_mode = (c->regs[AFE_REG_CONTROL0] & AFE_C0_SPI_READ) != 0;
    /* While SPI_READ=1 every frame is a read — EXCEPT a CONTROL0 frame
     * carrying a real (non-zero) value, which is the driver's escape back
     * to write mode (afe_set_read_mode writes c0 & ~SPI_READ while the
     * bus is still in read mode). A zero payload to CONTROL0 is a read of
     * CONTROL0 itself — the selftest's 8 MHz stage depends on this.    */
    if (rd_mode && !(addr == AFE_REG_CONTROL0 && val != 0)) {
        uint32_t v = afe_reg_read(c, addr);
        if (rx) { rx[1] = (uint8_t)(v >> 16);
                  rx[2] = (uint8_t)(v >> 8);
                  rx[3] = (uint8_t)v; }
        return;
    }
    afe_reg_write(c, addr, val);
}

static esp_err_t afe_spi_xfer(vboard_dev_t *d, const uint8_t *tx,
                              uint8_t *rx, size_t len)
{
    afe_ctx_t *c = (afe_ctx_t *)d->ctx;
    afe_active_sync(c);
    size_t i = 0;
    for (; i + 4 <= len; i += 4)
        afe_frame(c, tx ? tx + i : NULL, rx ? rx + i : NULL);
    for (; i < len; i++)                      /* tail — not a full frame */
        if (rx) rx[i] = 0xFF;
    return VB_ESP_OK;
}

/* ------------------------------ tick / reset ---------------------------- */
static void afe_tick(vboard_dev_t *d, int64_t now_us)
{
    afe_ctx_t *c = (afe_ctx_t *)d->ctx;
    afe_active_sync(c);

    if (c->rdy_high && now_us >= c->rdy_release_us) {
        vboard_gpio_set(AFE_RDY_GPIO, 0);
        c->rdy_high = false;
    }
    if (!c->active || !c->engine || c->frame_us <= 0)
        return;

    int n = 0;
    while (now_us >= c->next_frame_us && n < 8) {
        afe_convert(c, c->next_frame_us);
        vboard_gpio_set(AFE_RDY_GPIO, 1);        /* ADC_RDY high pulse   */
        c->rdy_high       = true;
        c->rdy_release_us = now_us + AFE_RDY_HIGH_US;
        vboard_irq_pulse(AFE_RDY_GPIO, 1);       /* POSEDGE to firmware  */
        c->next_frame_us += c->frame_us;
        n++;
    }
    if (n == 8) c->next_frame_us = now_us + c->frame_us;  /* shed backlog */
}

static void afe_dev_reset(vboard_dev_t *d)
{
    afe_ctx_t *c = (afe_ctx_t *)d->ctx;
    afe_por(c);
    c->active  = false;         /* RESETZ re-read on next touch           */
    c->rdy_high = false;
    vboard_gpio_set(AFE_RDY_GPIO, 0);
}

/* ------------------------------ registration ---------------------------- */
static vboard_dev_t s_afe_dev = {
    .name      = "afe4900",
    .i2c_addr  = 0,
    .spi_cs    = AFE_CS_GPIO,
    .spi_xfer  = afe_spi_xfer,
    .tick      = afe_tick,
    .reset     = afe_dev_reset,
    .ctx       = &s_afe,
};

/* Registration entry point — vboard_init() weak-calls this when wiring
 * the default device population; vboard_register() dedups by name so
 * re-registration is safe.                                            */
void vbdev_afe4900_register(void)
{
    static bool done;
    if (done) return;
    done = true;
    vboard_gpio_set(AFE_RDY_GPIO, 0);    /* ADC_RDY idles low             */
    vboard_register(&s_afe_dev);
}

/* Self-register at load so the default population exists even before
 * vboard_init() wires anything explicitly (idempotent via `done`).    */
#if defined(__GNUC__)
__attribute__((constructor))
static void afe_ctor(void)
{
    vbdev_afe4900_register();
}
#endif
