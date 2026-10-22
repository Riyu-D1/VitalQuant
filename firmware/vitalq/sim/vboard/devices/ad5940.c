/* ad5940.c — vboard device model: AD5940 impedance AFE (U7, fix_v22).
 *
 * Bus/power:
 *   SPI2, CS = IO15, mode 0.  Powered from +3V3_ANA (R95 tie).
 *   ~RESET = TCA6408 P2 (VQ_EXP_AD5940_RST), active low — the model polls
 *   vboard_exp_pin(2) at every SPI frame + tick; held low => full POR
 *   state, SPI dead.  P2 == -1 (no expander model) is treated as
 *   released so the model can run in reduced-population scenarios.
 *   GPIO0 (ball F5) = INTC0 output -> ESP IO14 (polarity per INTCPOL).
 *   GPIO2 (ball E1) = NIR730 gate via SYNC function — NOT an ESP pin;
 *   level is recorded and exposed through vbad5940_nir_gate() /
 *   vbad5940_get_gpio2() (weak) for the AS7341 island model.
 *
 * SPI frame format (driver vq_drv_ad5940.c — MATCHES the driver's
 * framing, not the raw datasheet's):
 *   reg write: CS [0x20 addrH addrL] CS [0x2D data..] CS   (two frames;
 *               the address latch persists across the CS gap)
 *   reg read : CS [0x20 addrH addrL] CS [0x6D dummy data..] CS
 *   fifo     : CS [0x5F x6dummy then n words] CS           (no addr latch)
 *   width    : 32-bit regs for 0x1000..0x3014, else 16-bit, MSB first.
 *
 * Emulated behaviour:
 *   - ADIID=0x4144 / CHIPID=0x5501 (S2).  OSCCON OK bits latch in after
 *     oscillator-enable edges (HFOSC ~50us, LFOSC ~1 ms).
 *   - Command-SRAM writes (CMDFIFOWADDR/CMDFIFOWRITE) are kept; SEQnINFO
 *     binds base+len; SEQCON.SEQEN + TRIGSEQ bit n executes the bound
 *     program: SEQ_WRs apply to the shadow MMRs, SEQ_WAIT/SEQ_TOUT
 *     advance a 16 MHz (CLKCON0-scaled) timeline; an AFECON leg with
 *     ADCCONVEN produces a result at the end of the following wait
 *     (DFT src -> {re,im} pair, SINC2-notch src -> threshold-count words)
 *     pushed into the data FIFO; SEQ_STOP / program end sets ENDSEQ.
 *     DFT magnitude = DFT_K / loop_ohm so the driver's RCAL ratio math
 *     reports the injected impedance; RCAL leg => loop = vbb2_rcal_ohm(),
 *     EDA/BioZ legs => feed + 51k series-R (driver subtracts it back).
 *   - Interrupt: raw flag latch routed through INTCSEL0 to GP0; assert
 *     level = INTCPOL?0:1; INTCCLR clears.  Edges go through
 *     vboard_irq_pulse(14, level) — level transitions on the wire are
 *     real edges, the pulse both drives IO14 and queues the ISR call.
 *   - Hibernate: SEQSLPLOCK unlock + SEQTRGSLP 0->1 sleeps; ANY later
 *     SPI frame starts a ~300 us wake, reads return 0 until it lapses
 *     (the driver's chip_wake() polls ADIID until 0x4144 appears).
 *   - Deep-sleep equivalent: expander P2 held low keeps the model in
 *     full POR (driver sleep() does exactly this).
 *
 * NOT silicon-accurate — register-plausible + protocol-faithful per
 * vboard.h.  TODO(datasheet) items mirrored from the driver: exact GP0
 * polarity semantics, SYNC-output persistence across hibernate.
 */
#include "vboard.h"
#include "bio2_feed.h"

#include <string.h>
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* pins                                                                */
#define AD_PIN_CS       15          /* CS_AD5940                        */
#define AD_PIN_GP0      14          /* GPIO0 ball F5 -> ESP IO14        */
#define AD_EXP_RST      2           /* TCA6408 P2 = ~RESET (active low) */

/* ------------------------------------------------------------------ */
/* SPI command bytes (driver framing)                                  */
#define AD_CMD_SETADDR  0x20
#define AD_CMD_WRITEREG 0x2d
#define AD_CMD_READREG  0x6d
#define AD_CMD_READFIFO 0x5f
#define AD_WIDE(a)      ((a) >= 0x1000 && (a) <= 0x3014)

/* ------------------------------------------------------------------ */
/* register addresses used by the model (all 4-byte aligned; the file  */
/* shadow array is indexed by addr>>2)                                 */
#define R_GPCON0    0x0000
#define R_GPOEN     0x0004
#define R_GPPE      0x0008
#define R_GPIEN     0x000C
#define R_GPIN      0x0010
#define R_GPOUT     0x0014
#define R_GPSET     0x0018
#define R_GPCLR     0x001C
#define R_GPTGL     0x0020
#define R_ADIID     0x0400
#define R_CHIPID    0x0404
#define R_CLKCON0   0x0408
#define R_CLKSEL    0x0414
#define R_SWRSTCON  0x0424
#define R_TRIGSEQ   0x0430
#define R_PWRMOD    0x0A00
#define R_OSCKEY    0x0A0C
#define R_OSCCON    0x0A10
#define R_AFECON    0x2000
#define R_SEQCON    0x2004
#define R_FIFOCON   0x2008
#define R_SWCON     0x200C
#define R_SYNCEXT   0x2054
#define R_DATAFIFORD 0x206C
#define R_CMDFIFOWR 0x2070
#define R_DFTREAL   0x2078
#define R_DFTIMAG   0x207C
#define R_AFEGENINT 0x209C
#define R_HPOSCCON  0x20BC
#define R_SEQSLPLOCK 0x2118
#define R_SEQTRGSLP 0x211C
#define R_DSWFULL   0x2150
#define R_NSWFULL   0x2154
#define R_PSWFULL   0x2158
#define R_TSWFULL   0x215C
#define R_SEQ0INFO  0x21CC
#define R_SEQ2INFO  0x21D0
#define R_CMDWADDR  0x21D4
#define R_CMDDATACON 0x21D8
#define R_DATAFIFOTH 0x21E0
#define R_SEQ3INFO  0x21E4
#define R_SEQ1INFO  0x21E8
#define R_FIFOCNTSTA 0x2200
#define R_INTCPOL   0x3000
#define R_INTCCLR   0x3004
#define R_INTCSEL0  0x3008
#define R_INTCSEL1  0x300C
#define R_INTCFLAG0 0x3010
#define R_INTCFLAG1 0x3014

#define REGS_N      (0x3018 >> 2)
#define RIX(a)      ((a) >> 2)

/* IDs / keys                                                          */
#define AD_ADIID    0x4144u
#define AD_CHIPID   0x5501u          /* S2 silicon — driver's expected set */
#define AD_SLPKEY   0xa47e5u

/* OSCCON bits                                                         */
#define OSC_LFOSCEN  0x0001
#define OSC_HFOSCEN  0x0002
#define OSC_LFOSCOK  0x0100
#define OSC_HFOSCOK  0x0200

/* AFECON bits the model inspects                                      */
#define AFECON_ADCCONVEN (1u << 8)
#define AFECON_DFTEN     (1u << 15)

/* switch selects used to classify a measurement leg                   */
#define SWD_RCAL0   (1u << 0)
#define SWD_AIN1    (1u << 1)
#define SWD_CE0     (1u << 4)
#define SWP_RCAL0   (1u << 0)

/* INTC source bits                                                    */
#define INTSRC_CUSTOM0   0x00000200u
#define INTSRC_ENDSEQ    0x00008000u
#define INTSRC_SEQTOERR  0x00020000u
#define INTSRC_FIFOTH    0x02000000u
#define INTSRC_FIFOOF    0x04000000u
#define INTSRC_FIFOUF    0x08000000u

/* AGPIO pin function codes (2 bits per pin, pin n at bits 2n)         */
#define GPIO_FUNC_INT    0
#define GPIO_FUNC_TRIG   1
#define GPIO_FUNC_SYNC   2
#define GPIO_FUNC_GPIO   3

/* sequencer op decode (top 2 bits of the 32-bit cmd word)             */
#define SEQOP_WAIT   0
#define SEQOP_TOUT   1
/* op >= 2 => SEQ_WR: reg = 0x2000|(((cmd>>24)&0x7f)<<2), data = low 24 */

/* ------------------------------------------------------------------ */
/* tunables                                                            */
#define AD_BOOT_US      1000     /* post-reset OTP/trim boot time        */
#define AD_WAKE_US      300      /* hibernate -> first-answer latency    */
#define AD_HFOSC_US     50       /* HFOSC ok-flag settle                 */
#define AD_LFOSC_US     1000     /* LFOSC ok-flag settle                 */
#define AD_SRAM_WORDS   512      /* 2 kB command region (word index)     */
#define AD_FIFO_WORDS   1024     /* 4 kB data FIFO                       */
#define AD_EV_MAX       12       /* max events per sequencer program     */
#define AD_SERIES_R     51000.0f /* 51k defib leads in the measured loop */

/* DFT scale: fixed-voltage excitation => DFT magnitude tracks the loop
 * admittance.  K picked so a 10k RCAL leg lands mid-scale (~65536
 * counts, comfortably below the driver's 125000 saturation guard).    */
#define AD_DFT_K        6.5536e8f
#define AD_DFT_FS       ((1 << 18) - 1)

/* ------------------------------------------------------------------ */
/* feeds (scenario stimulus — see bio2_feed.h)                          */
static float   s_feed_rcal_ohm = 10000.0f;
static float   s_feed_eda_ohm  = 15000.0f;
static float   s_feed_bioz_ohm = 250.0f;
static int32_t s_feed_sweat    = 2400;

float vbb2_rcal_ohm(void)  { return s_feed_rcal_ohm; }
float vbb2_eda_ohm(void)   { return s_feed_eda_ohm; }
float vbb2_bioz_ohm(void)  { return s_feed_bioz_ohm; }
int32_t vbb2_sweat_counts(void) { return s_feed_sweat; }

void vbb2_set_rcal_ohm(float ohm) { if (ohm > 0.0f) s_feed_rcal_ohm = ohm; }
void vbb2_set_eda_ohm (float ohm) { if (ohm >= 0.0f) s_feed_eda_ohm  = ohm; }
void vbb2_set_bioz_ohm(float ohm) { if (ohm >= 0.0f) s_feed_bioz_ohm = ohm; }
void vbb2_set_sweat_counts(int32_t c) { s_feed_sweat = c; }

/* ------------------------------------------------------------------ */
/* sequencer events — scheduled on the virtual clock                    */
enum { SEV_DFT = 1, SEV_SINC2, SEV_GENINT, SEV_END };
enum { LEG_RCAL = 0, LEG_EDA, LEG_BIOZ, LEG_OTHER };
typedef struct { int64_t at; uint8_t kind, arg; } sev_t;

typedef struct {
    pthread_mutex_t lock;

    /* power / reset / sleep */
    bool     rail_on;
    bool     dead;              /* no rail or P2 asserted              */
    int64_t  boot_until;
    bool     sleeping;          /* hibernated via SEQTRGSLP             */
    bool     wake_armed;
    int64_t  wake_until;
    int64_t  hfosc_ok_at, lfosc_ok_at;

    /* SPI address latch (persists across CS frames) */
    uint16_t addr;

    /* byte-shift state — the firmware splits command and data legs
     * under one held CS (vq_spi_write_read); a real die just sees a
     * continuous bit stream, so frame position lives across xfers. */
    uint8_t  sh_cmd;
    int      sh_pos;
    uint32_t sh_acc;            /* WRITEREG data accumulator            */
    uint32_t sh_rv;             /* READREG value latched at data start  */

    /* shadow MMRs (all accesses are 4-aligned words) */
    uint32_t regs[REGS_N];

    /* command SRAM + sequencer bindings */
    uint32_t sram[AD_SRAM_WORDS];
    uint32_t cmd_waddr;

    /* sequencer execution plan */
    bool     seq_busy;
    sev_t    ev[AD_EV_MAX];
    int      ev_n, ev_i;
    int64_t  ev_t0;
    uint32_t run_id;

    /* data FIFO */
    uint32_t fifo[AD_FIFO_WORDS];
    int      fhead, fcnt;
    uint16_t fifo_thres;

    /* interrupt controller */
    uint32_t intfl;             /* raw flag latch (pre-mux)             */
    int      gp0_lvl;           /* last level driven onto IO14          */
    bool     slp_unlocked;
    int      nir;               /* recorded GP2/NIR730 gate level       */
} ad_t;

static ad_t s_ad;
static void ad_por_ctx(ad_t *s);

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static uint32_t rd(const ad_t *s, uint16_t a) { return s->regs[RIX(a)]; }
static void     wr(ad_t *s, uint16_t a, uint32_t v) { s->regs[RIX(a)] = v; }

/* sysclk in Hz from clock shadows: HFOSC 16 or 32 MHz / CLKCON0[5:0]   */
static uint32_t sysclk_hz(const ad_t *s)
{
    uint32_t base = (rd(s, R_HPOSCCON) & 0x4) ? 32000000u : 16000000u;
    uint32_t div  = rd(s, R_CLKCON0) & 0x3f;
    if (!div) div = 1;
    return base / div;
}

static int64_t clks_to_us(const ad_t *s, uint32_t clks)
{
    return (int64_t)(((uint64_t)clks * 1000000u) / sysclk_hz(s));
}

/* ------------------------------------------------------------------ */
/* GP0 (INTC0 output) -> IO14.  Output level = flags&sel0 asserted per  */
/* INTCPOL when GP0 is output-enabled on the INT function; otherwise    */
/* the pin releases to the board pull-up.  Driven transitions are real  */
/* wire edges -> vboard_irq_pulse (drives level + queues ISR); a        */
/* release to hi-Z is a plain level set, not an interrupt edge.         */
static void gp0_update(ad_t *s)
{
    uint32_t con = rd(s, R_GPCON0), oen = rd(s, R_GPOEN);
    int      lvl;
    bool     driven = false;

    if ((oen & 0x01) && ((con & 0x3) == GPIO_FUNC_INT)) {
        int asserted = (s->intfl & rd(s, R_INTCSEL0)) != 0;
        /* datasheet Table 137: INTCPOL=0 → asserts LOW (negative edge);
         * INTCPOL=1 → asserts HIGH (positive edge). Idle is the inverse.*/
        lvl    = asserted ? (rd(s, R_INTCPOL) & 1) : !(rd(s, R_INTCPOL) & 1);
        driven = true;
    } else if ((oen & 0x01) && ((con & 0x3) == GPIO_FUNC_GPIO)) {
        lvl    = (int)(rd(s, R_GPOUT) & 0x1);
        driven = true;
    } else {
        lvl = 1;                       /* hi-Z -> pull-up dominates     */
    }

    if (lvl == s->gp0_lvl)
        return;
    s->gp0_lvl = lvl;
    if (driven)
        vboard_irq_pulse(AD_PIN_GP0, lvl);   /* level + edge together   */
    else
        vboard_gpio_set(AD_PIN_GP0, lvl);    /* release to pull-up      */
}

/* GP2 (ball E1) = NIR730 gate.  No plain-GPIO mux: driven level comes  */
/* from the SYNC function (SYNCEXTDEVICE bit2) when OEN2 + FUNC_SYNC.   */
static void nir_update(ad_t *s)
{
    uint32_t con = rd(s, R_GPCON0), oen = rd(s, R_GPOEN);
    int      lvl = 0;
    unsigned f2  = (con >> 4) & 0x3;

    if (oen & 0x04) {
        if (f2 == GPIO_FUNC_SYNC)
            lvl = (int)((rd(s, R_SYNCEXT) >> 2) & 1);
        else if (f2 == GPIO_FUNC_GPIO)
            lvl = (int)((rd(s, R_GPOUT) >> 2) & 1);
    }
    s->nir = lvl;
}

/* weak cross-model getters — the AS7341 island model may weak-link     */
__attribute__((weak)) int vbad5940_nir_gate(void)  { return s_ad.nir; }
__attribute__((weak)) int vbad5940_get_gpio2(void) { return s_ad.nir; }

/* ------------------------------------------------------------------ */
/* data FIFO                                                           */
static void fifo_push(ad_t *s, uint32_t w)
{
    if (s->fcnt >= AD_FIFO_WORDS) {
        s->intfl |= INTSRC_FIFOOF;
        return;
    }
    int idx = (s->fhead + s->fcnt) % AD_FIFO_WORDS;
    s->fifo[idx] = w;
    s->fcnt++;
    if (s->fcnt >= s->fifo_thres)
        s->intfl |= INTSRC_FIFOTH;
}

static uint32_t fifo_pop(ad_t *s)
{
    if (s->fcnt == 0) {
        s->intfl |= INTSRC_FIFOUF;
        gp0_update(s);
        return 0;
    }
    uint32_t w = s->fifo[s->fhead];
    s->fhead = (s->fhead + 1) % AD_FIFO_WORDS;
    s->fcnt--;
    return w;
}

/* ------------------------------------------------------------------ */
/* sequencer program -> scheduled event list                           */
static void ev_add(ad_t *s, int64_t at, uint8_t kind, uint8_t arg)
{
    if (s->ev_n < AD_EV_MAX) {
        s->ev[s->ev_n].at   = at;
        s->ev[s->ev_n].kind = kind;
        s->ev[s->ev_n].arg  = arg;
        s->ev_n++;
    }
}

/* classify the measured loop from the D-switch leg                     */
static int leg_of(uint32_t dsw, uint32_t psw)
{
    if ((dsw & SWD_RCAL0) && (psw & SWP_RCAL0)) return LEG_RCAL;
    if (dsw & SWD_CE0)                          return LEG_EDA;
    if (dsw & SWD_AIN1)                         return LEG_BIOZ;
    return LEG_OTHER;
}

static uint32_t pack18(int32_t v)
{
    return (uint32_t)(v & 0x3ffff);
}

/* push a {re,im} pair whose magnitude = DFT_K / loop_ohm, phase ~ -10° */
static void push_dft(ad_t *s, int leg)
{
    float loop;
    switch (leg) {
    case LEG_RCAL: loop = s_feed_rcal_ohm;                      break;
    case LEG_EDA:  loop = s_feed_eda_ohm  + AD_SERIES_R;        break;
    case LEG_BIOZ: loop = s_feed_bioz_ohm + AD_SERIES_R;        break;
    default:       loop = 66000.0f;                             break;
    }
    if (loop < 1.0f)
        loop = 1.0f;
    float mag = AD_DFT_K / loop;
    /* small deterministic spread so repeat measurements aren't bit-    */
    /* identical (a real AFE has DFT noise).  Kept <= +-0.05% so the     */
    /* driver's self-anchored RCAL ratio stays inside its ~0.3% spec.   */
    s->run_id = s->run_id * 1103515245u + 12345u;
    float jit = 1.0f + (int)((s->run_id >> 16) & 0xff) / 128.0f * 5e-4f
                     - 5e-4f;                       /* +-0.05%           */
    mag *= jit;
    if (mag > (float)AD_DFT_FS)
        mag = (float)AD_DFT_FS;
    int32_t re = (int32_t)(mag * 0.9848f);      /* ~ -10 deg: mostly    */
    int32_t im = (int32_t)(-mag * 0.1736f);     /* capacitive-looking   */
    wr(s, R_DFTREAL, pack18(re));
    wr(s, R_DFTIMAG, pack18(im));
    fifo_push(s, pack18(re));
    fifo_push(s, pack18(im));
}

static void push_sinc2(ad_t *s, int n)
{
    for (int i = 0; i < n; i++) {
        s->run_id = s->run_id * 1103515245u + 12345u;
        int32_t w = s_feed_sweat + (int32_t)((s->run_id >> 16) & 0x3f) - 32;
        if (w >  0x1ffff) w =  0x1ffff;
        if (w < -0x20000) w = -0x20000;
        fifo_push(s, pack18(w));
    }
}

static void seq_apply(ad_t *s, sev_t *e)
{
    switch (e->kind) {
    case SEV_DFT:   push_dft(s, e->arg);                    break;
    case SEV_SINC2: push_sinc2(s, e->arg);                  break;
    case SEV_GENINT:s->intfl |= INTSRC_CUSTOM0;             break;
    case SEV_END:   s->intfl |= INTSRC_ENDSEQ;
                    s->seq_busy = false;                    break;
    default:                                                break;
    }
    gp0_update(s);
}

/* apply all due events (called from tick AND before each SPI frame so  */
/* FIFO reads stay fresh even on a coarse tick period)                  */
static void seq_pump(ad_t *s, int64_t now)
{
    while (s->ev_i < s->ev_n && s->ev[s->ev_i].at <= now) {
        sev_t e = s->ev[s->ev_i++];
        seq_apply(s, &e);
    }
    if (s->ev_i >= s->ev_n)
        s->ev_n = s->ev_i = 0;
}

/* execute the program bound to seqid (SEQnINFO: LEN[26:16] ADDR[10:0]) */
static void seq_start(ad_t *s, int id, int64_t now)
{
    static const uint16_t info_reg[4] = {
        R_SEQ0INFO, R_SEQ1INFO, R_SEQ2INFO, R_SEQ3INFO
    };
    if (s->seq_busy || !(rd(s, R_SEQCON) & 1))          /* SEQEN gate   */
        return;
    uint32_t info = rd(s, info_reg[id & 3]);
    uint32_t base = info & 0x7ff, len = (info >> 16) & 0x7ff;
    if (!len || base >= AD_SRAM_WORDS)
        return;

    /* parse the command list into a timed event plan.  Register side   */
    /* effects apply immediately (host never samples mid-sequence);     */
    /* measurement results land at the end of their wait.               */
    uint32_t dsw  = rd(s, R_DSWFULL), psw = rd(s, R_PSWFULL);
    int      pend = 0;
    uint32_t acc  = 0;                                  /* wait clks    */
    int      stopped = 0;

    s->ev_n = s->ev_i = 0;

    for (uint32_t i = 0; i < len && !stopped; i++) {
        uint32_t c  = s->sram[(base + i) % AD_SRAM_WORDS];
        uint32_t op = c >> 30;
        if (op == SEQOP_WAIT || op == SEQOP_TOUT) {
            acc += c & 0x3fffffffu;
            if (pend) {
                int64_t at = now + clks_to_us(s, acc);
                if (pend == SEV_DFT)
                    ev_add(s, at, SEV_DFT, (uint8_t)leg_of(dsw, psw));
                else
                    ev_add(s, at, SEV_SINC2, (uint8_t)(s->fifo_thres ?
                                             s->fifo_thres : 4));
                pend = 0;
            }
            continue;
        }
        /* SEQ_WR */
        uint16_t a   = (uint16_t)(0x2000u | (((c >> 24) & 0x7f) << 2));
        uint32_t dat = c & 0xffffffu;
        wr(s, a, dat);                              /* shadow update    */
        switch (a) {
        case R_DSWFULL: dsw = dat;                  break;
        case R_PSWFULL: psw = dat;                  break;
        case R_AFECON:
            if (dat & AFECON_ADCCONVEN) {
                uint32_t src = (rd(s, R_FIFOCON) >> 13) & 0x7;
                if (src == 2 && (dat & AFECON_DFTEN))      /* DFT src   */
                    pend = SEV_DFT;
                else if (src == 3)                          /* SINC2    */
                    pend = SEV_SINC2;
                else
                    pend = 0;
            } else {
                pend = 0;
            }
            break;
        case R_DATAFIFOTH:
            s->fifo_thres = (uint16_t)((dat >> 16) & 0x7ff);
            break;
        case R_FIFOCON:
            break;                                  /* src read per leg  */
        case R_AFEGENINT:
            ev_add(s, now + clks_to_us(s, acc), SEV_GENINT, 0);
            break;
        case R_SEQCON:
            if (!(dat & 1))
                stopped = 1;                        /* SEQ_STOP()        */
            break;
        case R_SEQTRGSLP:
            stopped = 1;                            /* SEQ_SLP(): end +  */
            s->sleeping = true;                     /* sleep the part    */
            break;
        default:
            break;
        }
    }
    /* program end: SEQ_STOP seen or commands exhausted                 */
    ev_add(s, now + clks_to_us(s, acc) + 1, SEV_END, 0);
    s->seq_busy = true;
}

/* ------------------------------------------------------------------ */
/* register read/write side effects                                    */
static uint32_t reg_read(ad_t *s, uint16_t a, int64_t now)
{
    switch (a) {
    case R_ADIID:    return AD_ADIID;
    case R_CHIPID:   return AD_CHIPID;
    case R_OSCCON: {
        uint32_t v = rd(s, R_OSCCON);
        if ((v & OSC_HFOSCEN) && now >= s->hfosc_ok_at) v |= OSC_HFOSCOK;
        if ((v & OSC_LFOSCEN) && now >= s->lfosc_ok_at) v |= OSC_LFOSCOK;
        return v;
    }
    case R_FIFOCNTSTA: return ((uint32_t)(s->fcnt & 0x7ff)) << 16;
    case R_DATAFIFORD: return fifo_pop(s);
    case R_INTCFLAG0:  return s->intfl & rd(s, R_INTCSEL0);
    case R_INTCFLAG1:  return s->intfl & rd(s, R_INTCSEL1);
    case R_GPIN:       return (uint32_t)((s->gp0_lvl & 1) |
                                         ((s->nir & 1) << 2));
    default:           return rd(s, a);
    }
}

static void reg_write(ad_t *s, uint16_t a, uint32_t v, int64_t now)
{
    switch (a) {
    case R_CMDWADDR:    s->cmd_waddr = v & (AD_SRAM_WORDS - 1);       break;
    case R_CMDFIFOWR:
        s->sram[s->cmd_waddr % AD_SRAM_WORDS] = v;
        s->cmd_waddr = (s->cmd_waddr + 1) & (AD_SRAM_WORDS - 1);
        break;
    case R_DATAFIFOTH:  s->fifo_thres = (uint16_t)((v >> 16) & 0x7ff);
                        wr(s, a, v);                                 break;
    case R_TRIGSEQ:
        for (int i = 0; i < 4; i++) {
            if (v & (1u << i)) {
                seq_start(s, i, now);
                break;
            }
        }
        break;
    case R_SEQCON:
        wr(s, a, v);
        if (!(v & 1)) {                     /* SEQEN 0 -> halt         */
            s->seq_busy = false;
            s->ev_n = s->ev_i = 0;
        }
        break;
    case R_SEQSLPLOCK:  s->slp_unlocked = (v == AD_SLPKEY);
                        wr(s, a, v);                                 break;
    case R_SEQTRGSLP:
        if (v && s->slp_unlocked) {
            s->sleeping   = true;
            s->wake_armed = false;
        }
        break;
    case R_INTCCLR:
        s->intfl &= ~v;
        gp0_update(s);
        break;
    case R_INTCPOL:  case R_INTCSEL0:  case R_INTCSEL1:
    case R_GPCON0:   case R_GPOEN:     case R_GPOUT:
        wr(s, a, v);
        gp0_update(s);
        nir_update(s);
        break;
    case R_GPSET:   wr(s, R_GPOUT, rd(s, R_GPOUT) |  v); gp0_update(s);
                    nir_update(s);                                   break;
    case R_GPCLR:   wr(s, R_GPOUT, rd(s, R_GPOUT) & ~v); gp0_update(s);
                    nir_update(s);                                   break;
    case R_GPTGL:   wr(s, R_GPOUT, rd(s, R_GPOUT) ^  v); gp0_update(s);
                    nir_update(s);                                   break;
    case R_SYNCEXT:
        wr(s, a, v);
        nir_update(s);
        break;
    case R_OSCCON: {
        uint32_t old = rd(s, R_OSCCON);
        wr(s, a, v);
        if ((v & OSC_HFOSCEN) && !(old & OSC_HFOSCEN))
            s->hfosc_ok_at = now + AD_HFOSC_US;
        if (!(v & OSC_HFOSCEN)) s->hfosc_ok_at = now + (int64_t)1e12;
        if ((v & OSC_LFOSCEN) && !(old & OSC_LFOSCEN))
            s->lfosc_ok_at = now + AD_LFOSC_US;
        if (!(v & OSC_LFOSCEN)) s->lfosc_ok_at = now + (int64_t)1e12;
        break;
    }
    case R_SWRSTCON:
        if (v)                              /* soft reset strobe -> POR */
            ad_por_ctx(s);
        break;
    default:
        wr(s, a, v);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* power-on reset                                                      */
static void ad_por_ctx(ad_t *s)
{
    memset(s->regs, 0, sizeof s->regs);
    memset(s->sram, 0, sizeof s->sram);
    s->cmd_waddr    = 0;
    s->fhead = s->fcnt = 0;
    s->fifo_thres   = 0;
    s->intfl        = 0;
    s->seq_busy     = false;
    s->ev_n = s->ev_i = 0;
    s->sleeping     = false;
    s->wake_armed   = false;
    s->slp_unlocked = false;
    s->addr         = 0;
    /* HFOSC+LFOSC enabled out of reset (internal oscs, no XTAL fitted) */
    wr(s, R_OSCCON, OSC_HFOSCEN | OSC_LFOSCEN);
    s->hfosc_ok_at  = vboard_now_us() + AD_BOOT_US + AD_HFOSC_US;
    s->lfosc_ok_at  = vboard_now_us() + AD_BOOT_US + AD_LFOSC_US;
    gp0_update(s);
    nir_update(s);
}

/* rail / expander-reset tracking — P2 low or rail down => POR + dead   */
static void update_power(ad_t *s, int64_t now)
{
    bool rail = vboard_rail(VB_RAIL_3V3_ANA);
    int  p2   = vboard_exp_pin(AD_EXP_RST);   /* 0=asserted, -1=no model */
    bool dead = !rail || (p2 == 0);

    if (dead) {
        if (!s->dead) {
            s->dead = true;
            ad_por_ctx(s);
        }
        s->boot_until = 0;
    } else if (s->dead) {
        s->dead       = false;
        s->boot_until = now + AD_BOOT_US;
        s->sleeping   = false;
        s->wake_armed = false;
        /* power-cycled while released -> fresh POR */
        ad_por_ctx(s);
    }
    s->rail_on = rail;
}

/* ------------------------------------------------------------------ */
/* vboard_dev hooks                                                    */

/* One MOSI byte in, one MISO byte out.  Command frames decode by byte
 * position so the driver's split cmd/data legs under held CS behave
 * like the continuous bit stream a real die sees.                      */
static uint8_t ad_byte(ad_t *s, uint8_t mosi, int64_t now)
{
    int pos = s->sh_pos++;
    if (pos == 0) {
        s->sh_cmd = mosi;
        s->sh_acc = 0;
        return 0xFF;
    }
    switch (s->sh_cmd) {
    case AD_CMD_SETADDR:
        if (pos == 1)      s->addr  = (uint16_t)(mosi << 8);
        else if (pos == 2) s->addr |= mosi;
        return 0xFF;
    case AD_CMD_WRITEREG: {
        size_t w = AD_WIDE(s->addr) ? 4 : 2;
        if ((size_t)pos <= w) {
            s->sh_acc = (s->sh_acc << 8) | mosi;
            if ((size_t)pos == w)
                reg_write(s, s->addr, s->sh_acc, now);
        }
        return 0xFF;
    }
    case AD_CMD_READREG: {
        size_t w = AD_WIDE(s->addr) ? 4 : 2;
        if (pos == 1) {
            s->sh_rv = reg_read(s, s->addr, now);
            return 0x00;                /* turnaround/dummy byte        */
        }
        int bi = pos - 2;
        if (bi >= 0 && (size_t)bi < w)
            return (uint8_t)(s->sh_rv >> (8 * (w - 1 - bi)));
        return 0xFF;
    }
    case AD_CMD_READFIFO:
        if (pos < 7)
            return 0x00;                /* vendor dummy bytes           */
        {
            int bi = pos - 7;
            if (bi % 4 == 0)
                s->sh_rv = fifo_pop(s);
            return (uint8_t)(s->sh_rv >> (8 * (3 - (bi % 4))));
        }
    default:
        return 0xFF;
    }
}

/* CS15 edges bracket a frame: reset the byte position on assert. */
static void ad_cs_cb(int gpio, int lvl, void *ctx)
{
    (void)gpio;
    ad_t *s = ctx;
    pthread_mutex_lock(&s->lock);
    if (lvl == 0) {
        s->sh_pos = 0;
        s->sh_cmd = 0;
        s->sh_acc = 0;
    }
    pthread_mutex_unlock(&s->lock);
}

static esp_err_t ad_spi(vboard_dev_t *d, const uint8_t *tx,
                        uint8_t *rx, size_t len)
{
    ad_t   *s   = d->ctx;
    int64_t now = vboard_now_us();

    pthread_mutex_lock(&s->lock);
    update_power(s, now);
    if (!s->dead)
        seq_pump(s, now);

    if (rx)
        memset(rx, 0xFF, len);              /* MISO default = released  */
    if (!tx || len == 0) {
        pthread_mutex_unlock(&s->lock);
        return VB_ESP_OK;
    }
    /* held in reset / unpowered / still booting: SPI block dead, MISO  */
    /* floats high (stays 0xFF).                                        */
    if (s->dead || (s->boot_until && now < s->boot_until)) {
        pthread_mutex_unlock(&s->lock);
        return VB_ESP_OK;
    }
    /* hibernate: any frame starts the wake timer; MMRs stay dead until */
    /* it lapses (the driver polls ADIID for 0x4144).                   */
    if (s->sleeping) {
        if (!s->wake_armed) {
            s->wake_armed = true;
            s->wake_until = now + AD_WAKE_US;
        } else if (now >= s->wake_until) {
            s->sleeping   = false;
            s->wake_armed = false;
        }
    }
    if (s->sleeping) {
        if (rx)
            memset(rx, 0x00, len);          /* waking: reads return 0   */
        pthread_mutex_unlock(&s->lock);
        return VB_ESP_OK;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t miso = ad_byte(s, tx != NULL ? tx[i] : 0xFF, now);
        if (rx)
            rx[i] = miso;
    }
    pthread_mutex_unlock(&s->lock);
    return VB_ESP_OK;
}

static void ad_tick(vboard_dev_t *d, int64_t now_us)
{
    ad_t *s = d->ctx;
    pthread_mutex_lock(&s->lock);
    update_power(s, now_us);
    if (!s->dead)
        seq_pump(s, now_us);
    pthread_mutex_unlock(&s->lock);
}

static void ad_reset(vboard_dev_t *d)
{
    ad_t *s = d->ctx;
    pthread_mutex_lock(&s->lock);
    ad_por_ctx(s);
    s->dead       = false;
    s->boot_until = vboard_now_us() + AD_BOOT_US;
    pthread_mutex_unlock(&s->lock);
}

static vboard_dev_t s_dev = {
    .name      = "ad5940",
    .i2c_addr  = 0,
    .i2c_write = NULL,
    .i2c_read  = NULL,
    .spi_cs    = AD_PIN_CS,
    .spi_xfer  = ad_spi,
    .tick      = ad_tick,
    .reset     = ad_reset,
    .ctx       = &s_ad,
};

/* rail transition callback — power loss = full POR                     */
static void ad_rail_cb(vboard_rail_t r, bool on, void *ctx)
{
    (void)r;
    ad_t *s = ctx;
    pthread_mutex_lock(&s->lock);
    int64_t now = vboard_now_us();
    if (!on) {
        s->dead = true;
        ad_por_ctx(s);
    } else {
        s->dead       = false;
        s->boot_until = now + AD_BOOT_US;
        ad_por_ctx(s);
    }
    pthread_mutex_unlock(&s->lock);
}

/* ------------------------------------------------------------------ */
/* registration (weak-called by vboard_init)                            */
void vbdev_ad5940_register(void)
{
    pthread_mutex_init(&s_ad.lock, NULL);
    s_ad.gp0_lvl = 1;
    s_ad.rail_on = vboard_rail(VB_RAIL_3V3_ANA);
    ad_por_ctx(&s_ad);
    s_ad.boot_until = vboard_now_us() + AD_BOOT_US;
    vboard_gpio_set(AD_PIN_GP0, 1);         /* released by pull-up       */
    vboard_register(&s_dev);
    vboard_on_rail(VB_RAIL_3V3_ANA, ad_rail_cb, &s_ad);
    vboard_on_gpio_out(AD_PIN_CS, ad_cs_cb, &s_ad);
}
