/* tca6408.c — virtual TCA6408A 8-bit I2C GPIO expander @ 0x20 (fix_v22).
 *
 * Register-plausible model of the part vq_expander.c drives.  Real
 * register semantics (TI SCPS263 / driver EXP_REG_*):
 *   0x00 Input Port    — live pin levels (read clears ~INT)
 *   0x01 Output Port   — output latch, r/w
 *   0x02 Polarity Inv. — XOR on input-pin reads (driver keeps 0x00)
 *   0x03 Config        — 1 = input (POR 0xFF)
 *
 * Board wiring (board_pins.h vq_exp_pin_t, hw_v3.yaml):
 *   P0 AFE4900_RESETZ (low = reset)   P4 IR_GATE   (high = IR on)
 *   P1 ADS1292_PWDN   (low = pwdn)    P5 CHG_STAT  (in, low = charging)
 *   P2 AD5940_RESET   (low = reset)   P6 VBUS_DET  (in, high = USB in)
 *   P3 TX5_EN         (high = boost)  P7 CHG_DIS   (high = inhibit)
 *
 * Power-on state (task spec): CONFIG=0xFF — every pin an input; OUTPUT
 * latch 0x00; POLARITY 0x00.  (Real silicon PORs the output latch at
 * 0xFF — that is why the driver preloads Output=0x00 *before* Config.
 * Immaterial here: at POR all pins are inputs so the latch never reaches
 * a pin, and the driver's first write is Output=0x00 anyway.)
 *
 * External pulls hold P0-P4,P7 LOW so a freshly-reset expander keeps all
 * domains off; P5/P6 default-follow the VBUS rail until a scenario nails
 * them via vboard_exp_set_input() → vbdev_tca6408_set_input().
 *
 * ~INT (EXP_INT = IO3): open-drain, R53 10k pull-up.  The model only
 * ever pulls down (vboard_gpio_set(3,0)) and releases (set(3,1)).
 * Asserted when the input-port value a reg0 read would return differs
 * from the last reg0 read on a CONFIG=input pin; released by the read.
 *
 * Concurrency: deliberately lock-free.  I2C txns run on the firmware
 * thread, rail/level updates on the tick/scenario thread; worst case is
 * a duplicate idempotent vboard_gpio_set() or a stale pin level for one
 * poll — same behaviour as the open-drain line itself.
 */
#include "vboard.h"

#include <string.h>

#define EXP_ADDR        0x20
#define EXP_INT_GPIO    3       /* IO3 — strap, open-drain, R53 pull-up */

/* register map */
#define R_INPUT         0x00
#define R_OUTPUT        0x01
#define R_POL           0x02
#define R_CFG           0x03

/* port map (board_pins.h) */
#define P_AFE_RESETZ    0
#define P_ADS_PWDN      1
#define P_AD5940_RST    2
#define P_TX5_EN        3
#define P_IR_GATE       4
#define P_CHG_STAT      5
#define P_VBUS_DET      6
#define P_CHG_DIS       7

#define AUTO_IN_MASK    0x60u   /* P5,P6 follow VBUS until overridden */

typedef struct {
    uint8_t reg_ptr;    /* address pointer: write[0] sets it, streams++ */
    uint8_t in_latch;   /* external pin levels, pre-polarity            */
    uint8_t out_latch;  /* reg 0x01                                     */
    uint8_t pol;        /* reg 0x02                                     */
    uint8_t cfg;        /* reg 0x03 — 1 = input                         */
    uint8_t auto_in;    /* bits still slaved to the VBUS rail           */
    uint8_t last_rd;    /* byte returned by the last reg0 read          */
    uint8_t driven;     /* last electrically-driven level, per pin      */
    int     int_low;    /* ~INT asserted (dedup gpio_set calls)         */
} tca_st_t;

static tca_st_t s;

/* ---- device lookup --------------------------------------------------- *
 * The task contract calls for find_name("afe4900") etc.; the core header
 * currently offers only vboard_find_i2c/vboard_find_spi.  Declare the
 * by-name resolver weak: when the core provides it we use it, otherwise
 * fall back to the coupling targets' fixed CS GPIOs (board_pins.h).   */
extern vboard_dev_t *vboard_find_name(const char *name) __attribute__((weak));

static vboard_dev_t *find_dev(const char *name)
{
    if (vboard_find_name != NULL) {
        vboard_dev_t *d = vboard_find_name(name);
        if (d != NULL) {
            return d;
        }
    }
    int cs = -1;
    if (!strcmp(name, "afe4900")) {
        cs = 18;        /* VQ_CS_AFE4900_GPIO  */
    } else if (!strcmp(name, "ads1292r") || !strcmp(name, "ads1292")) {
        cs = 10;        /* VQ_CS_ADS1292_GPIO  */
    } else if (!strcmp(name, "ad5940")) {
        cs = 15;        /* VQ_CS_AD5940_GPIO   */
    }
    return (cs < 0) ? NULL : vboard_find_spi(cs);
}

static void reset_target(const char *name)
{
    vboard_dev_t *d = find_dev(name);
    if (d != NULL && d->reset != NULL) {
        d->reset(d);
    }
}

/* ---- pin levels -------------------------------------------------------*/

/* What a reg0 read returns: input pins -> external level ^ polarity,
 * output pins -> output latch (polarity does not touch outputs).      */
static uint8_t port_value(void)
{
    uint8_t v = 0;
    for (int p = 0; p < 8; p++) {
        int bit = (s.cfg >> p) & 1;
        int lvl = bit ? (((s.in_latch >> p) & 1) ^ ((s.pol >> p) & 1))
                      : ((s.out_latch >> p) & 1);
        v |= (uint8_t)(lvl << p);
    }
    return v;
}

/* ~INT: watched = pins configured as inputs.  Assert when the readable
 * input levels differ from the last reg0 read; the read releases.      */
static void int_eval(void)
{
    uint8_t now = port_value();
    if (!s.int_low && ((now ^ s.last_rd) & s.cfg) != 0) {
        s.int_low = 1;
        vboard_gpio_set(EXP_INT_GPIO, 0);   /* open-drain pulls low   */
    }
}

static void int_release(void)
{
    if (s.int_low) {
        s.int_low = 0;
    }
    vboard_gpio_set(EXP_INT_GPIO, 1);       /* R53 restores high      */
}

/* Electrical level actually driven on each pin (inputs float to their
 * external level; outputs drive the latch).                            */
static int driven_level(int p)
{
    return ((s.cfg >> p) & 1) ? ((s.in_latch >> p) & 1)
                              : ((s.out_latch >> p) & 1);
}

/* Propagate driven-level changes into board couplings.  Runs after any
 * Output/Config/por change — including a pin *leaving* output mode,
 * which drops it to the pull level (ESP_EN reset -> domains off).      */
static void propagate(void)
{
    for (int p = 0; p < 8; p++) {
        int lvl = driven_level(p);
        int old = (s.driven >> p) & 1;
        if (lvl == old) {
            continue;
        }
        s.driven = (uint8_t)((s.driven & ~(1u << p)) | ((uint8_t)lvl << p));
        int fell = (old == 1 && lvl == 0);
        switch (p) {
        case P_AFE_RESETZ:
            if (fell) {                 /* low = AFE4900 in reset     */
                reset_target("afe4900");
            }
            break;
        case P_ADS_PWDN:
            if (fell) {                 /* low = ADS1292 powered down */
                reset_target("ads1292r");
            }
            break;
        case P_AD5940_RST:
            if (fell) {                 /* low = AD5940 in reset      */
                reset_target("ad5940");
            }
            break;
        case P_TX5_EN:
            vboard_rail_set(VB_RAIL_TX5, lvl != 0);
            break;
        case P_IR_GATE:                 /* readable via get_pin(4)    */
        case P_CHG_DIS:                 /* readable via get_pin(7)    */
        default:
            break;
        }
    }
}

/* ---- power-on -------------------------------------------------------- */
static void tca_por(void)
{
    s.cfg       = 0xFF;
    s.out_latch = 0x00;
    s.pol       = 0x00;
    s.reg_ptr   = 0;
    s.auto_in   = AUTO_IN_MASK;
    s.in_latch  = vboard_rail(VB_RAIL_VBUS) ? AUTO_IN_MASK : 0x00;
    s.last_rd   = port_value();
    propagate();            /* falling edges on P0-P4,P7 -> domains off */
    s.int_low = 0;
    vboard_gpio_set(EXP_INT_GPIO, 1);   /* open-drain released        */
}

/* ---- I2C interface ----------------------------------------------------*/

static esp_err_t tca_write(vboard_dev_t *d, const uint8_t *data, size_t len)
{
    (void)d;
    if (len == 0) {
        return VB_ESP_OK;               /* bare probe: ACK            */
    }
    s.reg_ptr = data[0];
    for (size_t i = 1; i < len; i++) {
        uint8_t reg = s.reg_ptr++;
        uint8_t v   = data[i];
        switch (reg) {
        case R_OUTPUT:
            s.out_latch = v;
            propagate();
            break;
        case R_POL:
            s.pol = v;
            int_eval();                 /* polarity flips read values */
            break;
        case R_CFG:
            s.cfg = v;
            propagate();                /* pins enter/leave out mode  */
            int_eval();                 /* watched-pin mask changed   */
            break;
        case R_INPUT:
        default:
            break;                      /* input reg: writes ignored  */
        }
    }
    return VB_ESP_OK;
}

static esp_err_t tca_read(vboard_dev_t *d, uint8_t *data, size_t len)
{
    (void)d;
    for (size_t i = 0; i < len; i++) {
        uint8_t reg = s.reg_ptr++;
        switch (reg) {
        case R_INPUT:
            data[i]   = port_value();
            s.last_rd = data[i];
            int_release();              /* reading Input clears ~INT  */
            break;
        case R_OUTPUT:  data[i] = s.out_latch; break;
        case R_POL:     data[i] = s.pol;       break;
        case R_CFG:     data[i] = s.cfg;       break;
        default:        data[i] = 0xFF;        break;  /* unimplemented */
        }
    }
    return VB_ESP_OK;
}

static void tca_reset(vboard_dev_t *d)
{
    (void)d;
    tca_por();          /* ~RESET = ESP_EN: full power-on fallback      */
}

/* ---- core / scenario entry points ------------------------------------ */

/* vboard_exp_pin() in the core calls this.  Returns the real electrical
 * level (inputs: external level; outputs: latch), -1 out of range.     */
int vbdev_tca6408_get_pin(int p)
{
    if (p < 0 || p > 7) {
        return -1;
    }
    return driven_level(p);
}

/* vboard_exp_set_input() in the core calls this (board -> P5/P6, but
 * any pin is accepted — the latch only shows while the pin is input).
 * Takes the pin off the VBUS auto-follow so a scenario value sticks.   */
void vbdev_tca6408_set_input(int p, int lvl)
{
    if (p < 0 || p > 7) {
        return;
    }
    s.in_latch = (uint8_t)((s.in_latch & ~(1u << p)) |
                           ((uint8_t)(lvl != 0) << p));
    s.auto_in  &= (uint8_t)~(1u << p);
    int_eval();
}

static void on_vbus(vboard_rail_t r, bool on, void *ctx)
{
    (void)r; (void)ctx;
    for (int p = P_CHG_STAT; p <= P_VBUS_DET; p++) {
        if ((s.auto_in >> p) & 1) {
            s.in_latch = (uint8_t)((s.in_latch & ~(1u << p)) |
                                   ((uint8_t)(on != 0) << p));
        }
    }
    int_eval();
}

static vboard_dev_t s_dev = {
    .name      = "tca6408",
    .i2c_addr  = EXP_ADDR,
    .i2c_write = tca_write,
    .i2c_read  = tca_read,
    .spi_cs    = -1,
    .spi_xfer  = NULL,
    .tick      = NULL,                  /* fully event-driven          */
    .reset     = tca_reset,
    .ctx       = NULL,
};

void vbdev_tca6408_register(void)
{
    tca_por();
    vboard_on_rail(VB_RAIL_VBUS, on_vbus, NULL);
    vboard_register(&s_dev);
}
