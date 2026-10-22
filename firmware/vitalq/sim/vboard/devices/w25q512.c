/* w25q512.c — vboard model of the Winbond W25Q512JV (512 Mbit / 64 MiB)
 * SPI NOR flash on CS_FLASH = IO16 (SPI mode 0, <= 40 MHz).
 *
 * Speaks the firmware's transaction level: a full-duplex byte stream per
 * CS window.  The model is a byte-at-a-time FSM — MOSI bytes are consumed
 * and MISO bytes produced positionally — so it is correct whether the
 * shim delivers each CS window as one spi_xfer() call or as split
 * TX-then-RX legs (vq_spi_write_read style).  Command boundaries come
 * from the CS pin: the model subscribes to IO16 via vboard_on_gpio_out()
 * and commits pending program/erase/WRSR work on the CS rising edge,
 * exactly like the die.  If a shim never routes CS through the GPIO shim,
 * a fallback treats every spi_xfer() call as a complete window.
 *
 * =====================  OPCODE COVERAGE  =====================
 *   9Fh JEDEC ID        -> EF 40 20 (repeat)          05h RDSR1 (repeats)
 *   03h READ (3B, or 4B in 4B mode)                   35h RDSR2 (repeats)
 *   13h READ 4B (always 4-byte)                       15h RDSR3 (repeats)
 *   0Bh FAST READ (3B/4B per mode + dummy)            01h WRSR1 (1-2 B data)
 *   0Ch FAST READ 4B (+dummy)                         31h WRSR2 / 11h WRSR3
 *   02h PP (3B/4B per mode) / 12h PP4B                06h WREN / 04h WRDI
 *   20h SE 4k (3B/4B) / 21h SE4B                      50h volatile-SR WREN
 *   52h BE 32k (3B/4B)                                B7h enter-4B / E9h exit-4B
 *   D8h BE 64k (3B/4B) / DCh BE64-4B                  B9h power-down
 *   C7h / 60h chip erase (instant, still 50us WIP)    ABh release-PD / device ID
 *   90h MFID (EF,19 alternating)                      4Bh unique ID (4 dummy + 8 B)
 *   66h + 99h enable/software-reset sequence
 * Not modelled: suspend/resume 75h/7Ah (erases are instant), security
 * registers 42h/44h/48h, SFDP 5Ah, block-lock 36h/39h/3Dh/7Eh/98h,
 * dual/quad paths — unknown opcodes are ignored like real silicon.
 *
 * =====================  NOR FIDELITY  =====================
 *  - Program = AND: only 1->0 transitions.  Write-without-erase lands on
 *    the image as old & new (real chips!) and logstore CRCs will catch
 *    firmware bugs that skip the erase.
 *  - Erase = 0xFF: sector/block erases punch holes in the image (Linux
 *    FALLOC_FL_PUNCH_HOLE, zero-write fallback); chip erase re-truncates.
 *  - Page program wraps inside the 256 B page (a data byte stream >256 B
 *    overwrites the latch — datasheet "wraps and overwrites").
 *  - Program/erase/WRSR need WEL latched by a prior 06h; without it the
 *    command is silently dropped at CS rise — a real fault vector the
 *    driver guards against by reading WEL back (w25q512.c write_enable).
 *  - WIP (SR1.0) is held ~50 us (virtual clock) after a committed
 *    program/erase/WRSR so polling drivers see BUSY once, then clear.
 *    While busy only status reads and the reset sequence are honoured.
 *  - ADS latch (SR3.0) tracks B7h/E9h: 03h/02h/20h/52h/D8h/0Bh take a
 *    3- or 4-byte address accordingly; the dedicated *4B opcodes always
 *    take 4 — matching the driver's mode-independent strategy.  Power-on
 *    and reset restore 3B mode, so does power-down release? NO — the
 *    real die keeps ADS across B9h sleep but drops it on power loss and
 *    66h/99h reset; PD release (ABh) leaves it alone.  The driver
 *    re-issues B7h after every release anyway.
 *  - Power-down B9h: everything ignored until ABh + tRES1 (~30 us
 *    virtual).  ABh awake returns the 19h device ID after 3 dummy bytes.
 *  - 3V3 rail down: the die is inert — MISO reads 0xFF, nothing latches.
 *
 * Backing store: 64 MiB sparse image (see w25q512_sim.h for the inverted
 * byte scheme and persistence contract).  All array I/O is pread/pwrite.
 * C11, -Wall -Wextra clean; pthread-guarded for scenario-thread hooks.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE           /* fallocate, FALLOC_FL_PUNCH_HOLE, pread */
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "vboard.h"
#include "w25q512_sim.h"

#ifdef __linux__
#include <linux/falloc.h>
#endif

/* ---- tunables ----------------------------------------------------------
 * Driver timing (w25q512.c): tDP ~3us, tRES1 ~30us, tPP 0.7ms, tSE ~50ms.
 * The model compresses program/erase WIP to 50us of VIRTUAL time: long
 * enough for an immediate RDSR1 poll to catch BUSY=1 once, short enough
 * that vTaskDelay(1) granularity always observes completion.            */
#define T_WIP_US        50
#define T_RES1_US       30
#define T_RST_US        30

#define RD_CACHE_BYTES  VBFQ_SECTOR_4K     /* one flash sector           */

/* ---- opcodes -----------------------------------------------------------*/
enum {
    OP_WREN = 0x06, OP_WRDI = 0x04, OP_WREN_VSR = 0x50,
    OP_RDSR1 = 0x05, OP_RDSR2 = 0x35, OP_RDSR3 = 0x15,
    OP_WRSR1 = 0x01, OP_WRSR2 = 0x31, OP_WRSR3 = 0x11,
    OP_READ = 0x03, OP_READ4B = 0x13, OP_FREAD = 0x0B, OP_FREAD4B = 0x0C,
    OP_PP = 0x02, OP_PP4B = 0x12,
    OP_SE = 0x20, OP_SE4B = 0x21,
    OP_BE32 = 0x52, OP_BE64 = 0xD8, OP_BE64_4B = 0xDC,
    OP_CE = 0xC7, OP_CE_ALT = 0x60,
    OP_JEDEC = 0x9F, OP_MFID = 0x90, OP_UID = 0x4B, OP_RELS = 0xAB,
    OP_PWRDN = 0xB9,
    OP_ENTER4B = 0xB7, OP_EXIT4B = 0xE9,
    OP_RSTEN = 0x66, OP_RST = 0x99,
};

/* SR writable masks (datasheet-plausible; read-only status bits stay out):
 * SR1: WIP(0)/WEL(1) are status -> writable 0xFC (BP0-3, TB, SEC, SRP0)
 * SR2: SUS(7) is status             -> writable 0x7F (SRP1, QE, LB1-3, CMP)
 * SR3: ADS(0) is a mode latch        -> writable 0xFE                       */
#define SR1_WMASK   0xFCu
#define SR2_WMASK   0x7Fu
#define SR3_WMASK   0xFEu
#define SR1_WIP     0x01u
#define SR1_WEL     0x02u
#define SR3_ADS     0x01u
#define SR1_BP_BITS 0x7Cu          /* SEC|TB|BP2..0 region-protect field   */
#define SR2_CMP     0x40u

/* ---- FSM states --------------------------------------------------------*/
enum {
    ST_CMD = 0,        /* next MOSI byte is an opcode                     */
    ST_ADDR,           /* collecting address bytes                        */
    ST_DUMMY,          /* consuming dummy cycles before output            */
    ST_OUT,            /* output phase — MISO driven, MOSI ignored        */
    ST_PP,             /* collecting page-program data until CS rise      */
    ST_SR,             /* collecting WRSR payload until CS rise           */
    ST_IGNORE,         /* dump bytes until CS rise                        */
    ST_DONE,           /* header complete; execute/commit at CS rise      */
};

/* ---- MISO producers ----------------------------------------------------*/
enum {
    OUT_NONE = 0,
    OUT_JEDEC,         /* EF 40 20 repeating                              */
    OUT_SR1, OUT_SR2, OUT_SR3,
    OUT_READ,          /* sequential array bytes                          */
    OUT_UID,           /* 8 B unique id, repeating                        */
    OUT_DEVID,         /* 19h repeating (ABh awake / after dummy)         */
    OUT_MFID,          /* EF,19 alternating (90h; A0 picks the phase)     */
};

enum { PEND_NONE = 0, PEND_ERASE, PEND_PP, PEND_WRSR };

typedef struct {
    vboard_dev_t dev;

    /* backing image */
    int      fd;                          /* -1 until lazily opened      */
    char     img[PATH_MAX];               /* override/actual image path  */
    bool     img_forced;                  /* vbfq_set_image_path() used  */

    /* volatile chip state (reset on power-on/reset, not on file ops)    */
    uint8_t  sr1, sr2, sr3;               /* stored bits; WIP is derived */
    bool     wel, vsr_en;
    bool     ads4b;
    bool     powered_down;
    int64_t  busy_until;                  /* virtual us when WIP clears  */
    int64_t  ready_at;                    /* tRES1/tRST gate             */
    bool     reset_armed;                 /* 66h seen, 99h pending       */

    /* parser */
    uint8_t  st;
    uint8_t  op;                          /* opcode being framed         */
    uint8_t  addr_need, addr_got;
    uint32_t addr;
    uint8_t  dummy_need;
    uint8_t  out_kind;
    uint32_t out_pos;
    uint32_t rd_addr;
    uint8_t  mfid_phase;                  /* 90h: 0 => MF first          */

    /* pending commit at CS rise */
    uint8_t  pending;
    uint32_t pend_addr, pend_len;
    uint8_t  pp_latch[VBFQ_PAGE_SIZE];    /* wrap buffer (init FF)       */
    uint32_t pp_nbytes;                   /* data bytes clocked in       */
    uint8_t  sr_bytes[2];
    uint8_t  sr_nbytes;
    uint8_t  wrsr_op;                     /* which WRSR we're collecting */

    /* CS tracking */
    bool     cs_seen, cs_low;

    /* fault knobs + identity (scenario-owned, persist across reset)     */
    int      wp_hold;
    uint8_t  jedec[3];
    uint8_t  uid[8];

    /* read cache — one sector, invalidated every CS window              */
    uint8_t  rcache[RD_CACHE_BYTES];
    uint32_t rcache_base;                 /* byte base of cached window  */
    bool     rcache_valid;

    vbfq_stats_t stats;
    pthread_mutex_t mu;
} w25q_t;

/* ---- prototypes --------------------------------------------------------*/
static esp_err_t w25_xfer(vboard_dev_t *d, const uint8_t *tx,
                          uint8_t *rx, size_t len);
static void      w25_reset(vboard_dev_t *d);

/* The one model instance — fault hooks address it directly so they work
 * even before (or without) bus registration.                            */
static w25q_t s_w25 = {
    .dev = {
        .name     = "w25q512",
        .i2c_addr = 0,
        .spi_cs   = VBFQ_CS_GPIO,
        .spi_xfer = w25_xfer,
        .tick     = NULL,
        .reset    = w25_reset,
        .ctx      = NULL,
    },
    .fd          = -1,
    .st          = ST_CMD,
    .jedec       = { VBFQ_JEDEC_MFR, VBFQ_JEDEC_TYPE, VBFQ_JEDEC_CAP },
    /* deterministic UID: "VQ512\0\0\0" — scenarios may override.        */
    .uid         = { 'V', 'Q', '5', '1', '2', 0x51, 0x25, 0x19 },
    .rcache_base = 0,
    .mu          = PTHREAD_MUTEX_INITIALIZER,
};

/* ====================================================================== *
 *  Image file — sparse, inverted-byte, lazily opened                      *
 * ====================================================================== */

/* <this file> = .../sim/vboard/devices/w25q512.c — sim root is two
 * directories up; the image lives under sim/build.                      */
static void default_image_path(char *out, size_t n)
{
    const char *env = getenv("VBFQ_IMAGE");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, n, "%s", env);
        return;
    }
    char simdir[PATH_MAX];
    snprintf(simdir, sizeof(simdir), "%s", __FILE__);
    char *p = strrchr(simdir, '/');          /* strip w25q512.c          */
    if (p) { *p = '\0'; }
    p = strrchr(simdir, '/');                /* strip devices/           */
    if (p) { *p = '\0'; }
    p = strrchr(simdir, '/');                /* strip vboard/            */
    if (p) { *p = '\0'; }
    snprintf(out, n, "%s/build/extflash.bin", simdir);
}

static void mkdir_p_for_file(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            (void)mkdir(tmp, 0755);          /* EEXIST is fine           */
            *p = '/';
        }
    }
}

/* Lazily create <image>: O_RDWR|O_CREAT, grown to 64 MiB sparse on first
 * open.  An existing file is reused untouched — that IS the persistence
 * contract.  Returns true when the fd is usable.                         */
static bool img_open(w25q_t *s)
{
    if (s->fd >= 0) {
        return true;
    }
    if (!s->img_forced) {
        default_image_path(s->img, sizeof(s->img));
    }
    mkdir_p_for_file(s->img);
    s->fd = open(s->img, O_RDWR | O_CREAT, 0644);
    if (s->fd < 0) {
        fprintf(stderr, "w25q512_sim: cannot open %s: %s\n",
                s->img, strerror(errno));
        return false;
    }
    struct stat stb;
    if (fstat(s->fd, &stb) == 0 && stb.st_size < (off_t)VBFQ_TOTAL_BYTES) {
        /* grow-only; extending with holes keeps old content intact     */
        if (ftruncate(s->fd, (off_t)VBFQ_TOTAL_BYTES) != 0) {
            fprintf(stderr, "w25q512_sim: ftruncate %s: %s\n",
                    s->img, strerror(errno));
        }
    }
    s->rcache_valid = false;
    return true;
}

/* Read one decoded array byte through a one-sector cache.  Short reads
 * (holes tail past ftruncate bookkeeping, or EOF) mean erased data.     */
static uint8_t img_read_byte(w25q_t *s, uint32_t addr)
{
    if (!img_open(s)) {
        return 0xFF;
    }
    uint32_t base = addr & ~(RD_CACHE_BYTES - 1u);
    if (!s->rcache_valid || s->rcache_base != base) {
        ssize_t n = pread(s->fd, s->rcache, sizeof(s->rcache), (off_t)base);
        if (n < 0) {
            fprintf(stderr, "w25q512_sim: pread %s: %s\n",
                    s->img, strerror(errno));
            return 0xFF;
        }
        if ((size_t)n < sizeof(s->rcache)) {
            memset(s->rcache + n, 0, sizeof(s->rcache) - (size_t)n);
        }
        s->rcache_base  = base;
        s->rcache_valid = true;
    }
    return s->rcache[addr - s->rcache_base] ^ 0xFFu;
}

/* Program a full page latch with AND semantics: flash_new = old & data.
 * stored = ~flash  =>  stored_new = ~flash_new = stored_old | ~data.    */
static void img_program_page(w25q_t *s, uint32_t addr)
{
    if (!img_open(s)) {
        return;
    }
    uint32_t base = addr & ~(VBFQ_PAGE_SIZE - 1u);
    uint8_t  stored[VBFQ_PAGE_SIZE];
    ssize_t n = pread(s->fd, stored, sizeof(stored), (off_t)base);
    if (n < 0) {
        fprintf(stderr, "w25q512_sim: pread(pp) %s: %s\n",
                s->img, strerror(errno));
        return;
    }
    if ((size_t)n < sizeof(stored)) {
        memset(stored + n, 0, sizeof(stored) - (size_t)n);
    }
    for (size_t i = 0; i < sizeof(stored); i++) {
        stored[i] |= (uint8_t)~s->pp_latch[i];
    }
    if (pwrite(s->fd, stored, sizeof(stored), (off_t)base)
        != (ssize_t)sizeof(stored)) {
        fprintf(stderr, "w25q512_sim: pwrite(pp) %s: %s\n",
                s->img, strerror(errno));
    }
    s->rcache_valid = false;
}

/* Erase -> stored zeros -> decoded 0xFF.  Hole-punch keeps the image
 * sparse where the fs supports it; chip erase re-truncates entirely.    */
static void img_erase(w25q_t *s, uint32_t addr, uint32_t len)
{
    if (!img_open(s)) {
        return;
    }
    if (len >= VBFQ_TOTAL_BYTES) {                          /* chip erase */
        /* Re-truncate to a pure hole; on failure fall through and
         * punch/zero the whole range like a giant block erase.        */
        s->stats.chip_erases++;
        if (ftruncate(s->fd, 0) == 0 &&
            ftruncate(s->fd, (off_t)VBFQ_TOTAL_BYTES) == 0) {
            s->rcache_valid = false;
            return;
        }
        addr = 0;
        len  = VBFQ_TOTAL_BYTES;
    } else {
        addr &= ~(len - 1u);             /* containing sector/block      */
    }
#ifdef __linux__
    if (fallocate(s->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                  (off_t)addr, (off_t)len) == 0) {
        s->rcache_valid = false;
        return;
    }
#endif
    /* portable fallback: stored zeros                                  */
    uint8_t zeros[4096] = {0};
    uint32_t left = len;
    uint64_t off  = addr;
    while (left > 0) {
        size_t chunk = (left < sizeof(zeros)) ? left : sizeof(zeros);
        if (pwrite(s->fd, zeros, chunk, (off_t)off) != (ssize_t)chunk) {
            fprintf(stderr, "w25q512_sim: erase fallback %s: %s\n",
                    s->img, strerror(errno));
            break;
        }
        off += chunk;
        left -= (uint32_t)chunk;
    }
    s->rcache_valid = false;
}

/* ====================================================================== *
 *  Chip helpers                                                           *
 * ====================================================================== */

static bool rail_up(void)
{
    return vboard_rail(VB_RAIL_3V3);
}

static int64_t now_us(void)
{
    return vboard_now_us();
}

static bool chip_busy(w25q_t *s)
{
    return now_us() < s->busy_until;
}

static uint8_t sr1(w25q_t *s)
{
    uint8_t v = s->sr1 & ~SR1_WIP;
    if (chip_busy(s))            { v |= SR1_WIP; }
    if (s->wel)                  { v |= SR1_WEL; }
    return v;
}
static uint8_t sr3(w25q_t *s)
{
    uint8_t v = s->sr3 & ~SR3_ADS;
    if (s->ads4b)                { v |= SR3_ADS; }
    return v;
}

/* /WP semantics (fix_v22 ties /WP and /HOLD to +3V3 — these are scenario
 * fault knobs).  VBFQ_WP_HARD models a part with protection configured.  */
static bool wp_blocks_status(w25q_t *s) { return (s->wp_hold & VBFQ_WP) != 0; }
static bool wp_blocks_array(w25q_t *s)
{
    if (s->wp_hold & VBFQ_WP_HARD) { return true; }
    if (!(s->wp_hold & VBFQ_WP))   { return false; }
    return (s->sr1 & SR1_BP_BITS) != 0 || (s->sr2 & SR2_CMP) != 0;
}

static void parser_reset(w25q_t *s)
{
    s->st         = ST_CMD;
    s->op         = 0;
    s->addr_need  = 0;
    s->addr_got   = 0;
    s->dummy_need = 0;
    s->out_kind   = OUT_NONE;
    s->out_pos    = 0;
    s->pending    = PEND_NONE;
    /* NOTE: pp_nbytes / sr_nbytes / pend_* are the COMMIT payload — they
     * are read after parser_reset() runs on the CS rising edge, and are
     * re-initialised whenever a new command stages them (pp_common,
     * erase dispatch, WRSR).  Clearing them here would silently drop
     * every commit.                                                    */
    /* reset_armed survives CS cycles (66h and 99h arrive as separate
     * transactions) — it is cleared only by a non-reset opcode.        */
}

static void power_on_reset(w25q_t *s)
{
    s->sr1 = s->sr2 = s->sr3 = 0;
    s->wel = s->vsr_en = false;
    s->ads4b         = false;          /* ADS restores 3B on reset/power  */
    s->powered_down  = false;
    s->busy_until    = 0;
    s->ready_at      = 0;
    s->reset_armed   = false;
    parser_reset(s);
    s->rcache_valid  = false;
}

static void sw_reset(w25q_t *s)
{
    power_on_reset(s);
    s->ready_at = now_us() + T_RST_US;   /* tRST before next command     */
    s->stats.sw_resets++;
}

/* ---- committed operations ---------------------------------------------*/

static void set_wip(w25q_t *s)
{
    s->busy_until = now_us() + T_WIP_US;
}

static void commit_erase(w25q_t *s)
{
    img_erase(s, s->pend_addr, s->pend_len);
    s->wel = false;                    /* WEL auto-clears on completion  */
    set_wip(s);
    s->stats.erase_commits++;
}

static void commit_pp(w25q_t *s)
{
    img_program_page(s, s->pend_addr);
    s->wel = false;
    set_wip(s);
    s->stats.pp_commits++;
}

static void commit_wrsr(w25q_t *s)
{
    if (s->sr_nbytes == 0) {
        return;                        /* bare opcode, no data: no-op    */
    }
    switch (s->wrsr_op) {
    case OP_WRSR1:
        s->sr1 = s->sr_bytes[0] & SR1_WMASK;
        if (s->sr_nbytes >= 2) {
            s->sr2 = s->sr_bytes[1] & SR2_WMASK;
        }
        break;
    case OP_WRSR2:
        s->sr2 = s->sr_bytes[0] & SR2_WMASK;
        break;
    case OP_WRSR3:
        s->sr3 = s->sr_bytes[0] & SR3_WMASK;  /* ADS bit is read-only     */
        break;
    default:
        break;
    }
    s->wel = s->vsr_en = false;
    set_wip(s);
    s->stats.wrsr_commits++;
}

/* Execute whatever the CS window staged.  Called on the CS rising edge,
 * or at the end of an spi_xfer() call when the shim never delivers CS
 * edges (whole-window-per-call fallback).                               */
static void commit(w25q_t *s)
{
    uint8_t pend = s->pending;
    parser_reset(s);
    s->stats.txns++;
    switch (pend) {
    case PEND_ERASE:
        /* 50h (volatile-SR write enable) authorizes WRSR ONLY — program
         * and erase still require the real WEL latch (06h).            */
        if (!s->wel)                 { s->stats.wr_ignored_no_wel++; }
        else if (wp_blocks_array(s)) { s->stats.wr_ignored_wp++; }
        else { commit_erase(s); }
        break;
    case PEND_PP:
        if (!s->wel)                 { s->stats.wr_ignored_no_wel++; }
        else if (s->pp_nbytes == 0)  { /* empty program: no-op */ }
        else if (wp_blocks_array(s)) { s->stats.wr_ignored_wp++; }
        else { commit_pp(s); }
        break;
    case PEND_WRSR:
        if (!s->wel && !s->vsr_en)    { s->stats.wr_ignored_no_wel++; }
        else if (wp_blocks_status(s)) { s->stats.wr_ignored_wp++; }
        else { commit_wrsr(s); }
        break;
    default:
        break;
    }
}

/* ====================================================================== *
 *  Byte-stream FSM                                                        *
 * ====================================================================== */

static uint8_t miso_byte(w25q_t *s)
{
    uint8_t v = 0xFF;
    switch (s->out_kind) {
    case OUT_JEDEC: v = s->jedec[s->out_pos % 3];        break;
    case OUT_SR1:   v = sr1(s);                          break;
    case OUT_SR2:   v = s->sr2;                          break;
    case OUT_SR3:   v = sr3(s);                          break;
    case OUT_DEVID: v = VBFQ_DEVICE_ID;                  break;
    case OUT_UID:   v = s->uid[s->out_pos & 7u];         break;
    case OUT_MFID:
        v = ((s->out_pos ^ s->mfid_phase) & 1u)
            ? VBFQ_DEVICE_ID : s->jedec[0];
        break;
    case OUT_READ:
        v = img_read_byte(s, s->rd_addr);
        s->rd_addr = (s->rd_addr + 1u) & (VBFQ_TOTAL_BYTES - 1u);
        break;
    default:
        return 0xFF;
    }
    s->out_pos++;
    return v;
}

/* Opcode arrived at ST_CMD.  Gates: power-down, tRES1 window, WIP.      */
static uint8_t dispatch(w25q_t *s, uint8_t op)
{
    if (s->powered_down) {
        if (op != OP_RELS) {
            s->stats.ignored_pd++;
            s->st = ST_IGNORE;
            return 0xFF;
        }
    } else if (now_us() < s->ready_at) {
        if (op != OP_RELS) {           /* still in tRES1/tRST — inert    */
            s->st = ST_IGNORE;
            return 0xFF;
        }
    } else if (chip_busy(s)) {
        /* mid program/erase: only status + reset survive the gate       */
        switch (op) {
        case OP_RDSR1: case OP_RDSR2: case OP_RDSR3:
        case OP_RSTEN: case OP_RST:
            break;
        default:
            s->stats.ignored_busy++;
            s->st = ST_IGNORE;
            return 0xFF;
        }
    }

    /* reset pairing: anything between 66h and 99h breaks the sequence   */
    if (op != OP_RST && op != OP_RSTEN) {
        s->reset_armed = false;
    }

    s->op         = op;
    s->out_pos    = 0;
    s->dummy_need = 0;
    s->out_kind   = OUT_NONE;

    switch (op) {
    /* -- no-data commands --------------------------------------------  */
    case OP_WREN:    s->wel    = true;                 s->st = ST_DONE; break;
    case OP_WRDI:    s->wel    = false;
                     s->vsr_en = false;                s->st = ST_DONE; break;
    case OP_WREN_VSR:s->vsr_en = true;                 s->st = ST_DONE; break;
    case OP_ENTER4B: s->ads4b  = true;
                     s->stats.enter4b++;               s->st = ST_DONE; break;
    case OP_EXIT4B:  s->ads4b  = false;
                     s->stats.exit4b++;                s->st = ST_DONE; break;
    case OP_PWRDN:   s->powered_down = true;
                     s->stats.pd_entries++;            s->st = ST_DONE; break;
    case OP_RSTEN:   s->reset_armed = true;            s->st = ST_DONE; break;
    case OP_RST:
        if (s->reset_armed) { sw_reset(s); }
        s->st = ST_DONE;
        break;

    /* -- immediate-output commands ------------------------------------  */
    case OP_JEDEC: s->out_kind = OUT_JEDEC;            s->st = ST_OUT;  break;
    case OP_RDSR1: s->out_kind = OUT_SR1;              s->st = ST_OUT;  break;
    case OP_RDSR2: s->out_kind = OUT_SR2;              s->st = ST_OUT;  break;
    case OP_RDSR3: s->out_kind = OUT_SR3;              s->st = ST_OUT;  break;

    /* -- dummy-then-output --------------------------------------------  */
    case OP_RELS:
        if (s->powered_down) {
            s->powered_down = false;
            s->ready_at = now_us() + T_RES1_US;
            s->stats.pd_wakes++;
        }
        s->dummy_need = 3;
        s->addr_got   = 0;             /* reuse addr_got as dummy counter */
        s->out_kind   = OUT_DEVID;
        s->st         = ST_DUMMY;
        break;
    case OP_UID:
        s->dummy_need = 4;
        s->addr_got   = 0;
        s->out_kind   = OUT_UID;
        s->st         = ST_DUMMY;
        break;
    case OP_MFID:
        s->dummy_need = 3;
        s->addr_got   = 0;
        s->addr       = 0;
        s->out_kind   = OUT_MFID;
        s->st         = ST_DUMMY;
        break;

    /* -- address-bearing reads ----------------------------------------  */
    case OP_READ:    s->addr_need = s->ads4b ? 4 : 3;
                     s->addr = s->addr_got = 0;
                     s->out_kind = OUT_READ;           s->st = ST_ADDR; break;
    case OP_READ4B:  s->addr_need = 4;
                     s->addr = s->addr_got = 0;
                     s->out_kind = OUT_READ;           s->st = ST_ADDR; break;
    case OP_FREAD:   s->addr_need = s->ads4b ? 4 : 3;
                     s->addr = s->addr_got = 0;
                     s->dummy_need = 1;
                     s->out_kind = OUT_READ;           s->st = ST_ADDR; break;
    case OP_FREAD4B: s->addr_need = 4;
                     s->addr = s->addr_got = 0;
                     s->dummy_need = 1;
                     s->out_kind = OUT_READ;           s->st = ST_ADDR; break;

    /* -- program -------------------------------------------------------  */
    case OP_PP:      s->addr_need = s->ads4b ? 4 : 3;
                     goto pp_common;
    case OP_PP4B:    s->addr_need = 4;
pp_common:
        s->addr = s->addr_got = 0;
        s->pp_nbytes = 0;
        memset(s->pp_latch, 0xFF, sizeof(s->pp_latch));
        s->st = ST_ADDR;
        break;

    /* -- erase ---------------------------------------------------------  */
    case OP_SE:      s->addr_need = s->ads4b ? 4 : 3;
                     s->pend_len = VBFQ_SECTOR_4K;     goto er_common;
    case OP_SE4B:    s->addr_need = 4;
                     s->pend_len = VBFQ_SECTOR_4K;     goto er_common;
    case OP_BE32:    s->addr_need = s->ads4b ? 4 : 3;
                     s->pend_len = VBFQ_BLOCK_32K;     goto er_common;
    case OP_BE64:    s->addr_need = s->ads4b ? 4 : 3;
                     s->pend_len = VBFQ_BLOCK_64K;     goto er_common;
    case OP_BE64_4B: s->addr_need = 4;
                     s->pend_len = VBFQ_BLOCK_64K;     goto er_common;
er_common:
        s->addr = s->addr_got = 0;
        s->st = ST_ADDR;
        break;
    case OP_CE:
    case OP_CE_ALT:
        s->pend_addr = 0;
        s->pend_len  = VBFQ_TOTAL_BYTES;
        s->pending   = PEND_ERASE;
        s->st        = ST_DONE;
        break;

    /* -- status-register writes (payload collected until CS rise) ------  */
    case OP_WRSR1:
    case OP_WRSR2:
    case OP_WRSR3:
        s->wrsr_op    = (uint8_t)op;
        s->sr_nbytes  = 0;
        s->pending    = PEND_WRSR;
        s->st         = ST_SR;
        break;

    default:
        s->st = ST_IGNORE;             /* unknown opcode: real die shrugs */
        break;
    }
    return 0xFF;
}

/* One bit-time of the die: consume a MOSI byte, produce the MISO byte.   */
static uint8_t chip_step(w25q_t *s, uint8_t mosi)
{
    s->stats.spi_bytes++;
    if (!rail_up()) {
        s->stats.ignored_rail++;
        return 0xFF;
    }
    if (s->wp_hold & VBFQ_HOLD) {
        return 0xFF;                   /* frozen mid-transaction         */
    }
    if (s->cs_seen && !s->cs_low) {
        return 0xFF;                   /* CS high: die is deaf           */
    }

    switch (s->st) {
    case ST_CMD:
        return dispatch(s, mosi);

    case ST_ADDR:
        s->addr = (s->addr << 8) | mosi;
        if (++s->addr_got >= s->addr_need) {
            s->addr &= (VBFQ_TOTAL_BYTES - 1u);   /* wraps at 64 MiB      */
            s->rd_addr   = s->addr;
            s->pend_addr = s->addr;
            if (s->op == OP_PP || s->op == OP_PP4B) {
                s->st = ST_PP;
            } else if (s->op == OP_SE || s->op == OP_SE4B ||
                       s->op == OP_BE32 || s->op == OP_BE64 ||
                       s->op == OP_BE64_4B) {
                s->pending = PEND_ERASE;
                s->st      = ST_DONE;
            } else if (s->dummy_need) {     /* fast-read dummy byte       */
                s->st = ST_DUMMY;
            } else {
                s->st = ST_OUT;
            }
        }
        return 0xFF;

    case ST_DUMMY:
        /* MFID's third "dummy" byte is really A0 — picks MF/ID phase.   */
        if (s->op == OP_MFID && s->addr_got == 2) {
            s->mfid_phase = mosi & 1u;
        }
        if (++s->addr_got >= s->dummy_need) {
            s->st = ST_OUT;
        }
        return 0xFF;

    case ST_PP: {
        /* load into the page latch; >256 B wraps within the page and
         * overwrites earlier bytes (datasheet behaviour).               */
        uint32_t idx = (s->pend_addr + s->pp_nbytes) & (VBFQ_PAGE_SIZE - 1u);
        s->pp_latch[idx] = mosi;
        s->pp_nbytes++;
        s->pending = PEND_PP;
        return 0xFF;
    }

    case ST_SR:
        if (s->sr_nbytes < sizeof(s->sr_bytes)) {
            s->sr_bytes[s->sr_nbytes++] = mosi;
        }
        /* extra bytes are dropped, like the die's SR data register      */
        return 0xFF;

    case ST_OUT:
        return miso_byte(s);

    case ST_DONE:
    case ST_IGNORE:
    default:
        return 0xFF;
    }
}

/* ====================================================================== *
 *  vboard_dev_t interface                                                 *
 * ====================================================================== */

static esp_err_t w25_xfer(vboard_dev_t *d, const uint8_t *tx,
                          uint8_t *rx, size_t len)
{
    w25q_t *s = (w25q_t *)d;
    pthread_mutex_lock(&s->mu);
    for (size_t i = 0; i < len; i++) {
        uint8_t m = chip_step(s, tx != NULL ? tx[i] : 0xFFu);
        if (rx != NULL) {
            rx[i] = m;
        }
    }
    /* Whole-window-per-call fallback: with no CS edges the call boundary
     * is the only end-of-command marker we get.  When CS tracking is
     * live, a call that ends while CS is still low is just a leg — the
     * rising edge commits instead.                                       */
    if (!s->cs_seen) {
        commit(s);
    }
    pthread_mutex_unlock(&s->mu);
    return VB_ESP_OK;
}

static void w25_reset(vboard_dev_t *d)
{
    w25q_t *s = (w25q_t *)d;
    pthread_mutex_lock(&s->mu);
    power_on_reset(s);
    pthread_mutex_unlock(&s->mu);
}

/* CS edges from the GPIO shim (firmware drives CS via gpio_set_level).   */
static void cs_cb(int gpio, int level, void *ctx)
{
    w25q_t *s = (w25q_t *)ctx;
    (void)gpio;
    pthread_mutex_lock(&s->mu);
    s->cs_seen = true;
    if (level == 0) {
        s->cs_low = true;
        parser_reset(s);
        s->rcache_valid = false;       /* new window: fresh array view   */
    } else {
        s->cs_low = false;
        commit(s);
    }
    pthread_mutex_unlock(&s->mu);
}

static void rail_cb(vboard_rail_t r, bool on, void *ctx)
{
    w25q_t *s = (w25q_t *)ctx;
    if (r != VB_RAIL_3V3) {
        return;
    }
    pthread_mutex_lock(&s->mu);
    if (on) {
        power_on_reset(s);             /* power-on defaults              */
    }
    /* rail off: chip_step() already goes inert; volatile latches are
     * restored by the power-on reset when the rail returns.            */
    pthread_mutex_unlock(&s->mu);
}

/* ====================================================================== *
 *  Public / scenario API                                                  *
 * ====================================================================== */

vboard_dev_t *vbfq_w25q512_device(void)
{
    s_w25.dev.ctx = &s_w25;
    return &s_w25.dev;
}

void vbfq_w25q512_register(void)
{
    static bool registered;
    if (registered) {
        return;
    }
    registered = true;
    vbfq_w25q512_device();
    vboard_register(&s_w25.dev);
    vboard_on_gpio_out(VBFQ_CS_GPIO, cs_cb, &s_w25);
    vboard_on_rail(VB_RAIL_3V3, rail_cb, &s_w25);
}

/* vboard_init.c's WEAK_REG table calls vbdev_*_register — alias. */
void vbdev_w25q512_register(void) { vbfq_w25q512_register(); }

void vbfq_set_image_path(const char *path)
{
    pthread_mutex_lock(&s_w25.mu);
    if (s_w25.fd < 0 && path != NULL) {
        snprintf(s_w25.img, sizeof(s_w25.img), "%s", path);
        s_w25.img_forced = true;
    }
    pthread_mutex_unlock(&s_w25.mu);
}

const char *vbfq_image_path(void)
{
    pthread_mutex_lock(&s_w25.mu);
    if (!s_w25.img_forced && s_w25.img[0] == '\0') {
        default_image_path(s_w25.img, sizeof(s_w25.img));
    }
    const char *p = s_w25.img;
    pthread_mutex_unlock(&s_w25.mu);
    return p;
}

void vbfq_flush(void)
{
    pthread_mutex_lock(&s_w25.mu);
    if (s_w25.fd >= 0) {
        (void)fsync(s_w25.fd);
    }
    pthread_mutex_unlock(&s_w25.mu);
}

/* Backdoor read of decoded flash bytes.                                 */
int vbfq_raw_peek(uint32_t addr, uint8_t *out, size_t len)
{
    if (out == NULL || addr >= VBFQ_TOTAL_BYTES ||
        len > VBFQ_TOTAL_BYTES - addr) {
        return -1;
    }
    pthread_mutex_lock(&s_w25.mu);
    if (!img_open(&s_w25)) {
        pthread_mutex_unlock(&s_w25.mu);
        return -1;
    }
    ssize_t n = pread(s_w25.fd, out, len, (off_t)addr);
    if (n < 0) {
        pthread_mutex_unlock(&s_w25.mu);
        return -1;
    }
    for (ssize_t i = 0; i < n; i++) {
        out[i] ^= 0xFFu;
    }
    if ((size_t)n < len) {
        memset(out + n, 0xFF, len - (size_t)n);     /* past EOF = erased */
    }
    pthread_mutex_unlock(&s_w25.mu);
    return 0;
}

/* Backdoor write of decoded flash bytes (bypasses NOR AND-rules — can
 * set 0->1, i.e. seed torn or future states).                            */
int vbfq_raw_poke(uint32_t addr, const uint8_t *data, size_t len)
{
    if (data == NULL || addr >= VBFQ_TOTAL_BYTES ||
        len > VBFQ_TOTAL_BYTES - addr) {
        return -1;
    }
    pthread_mutex_lock(&s_w25.mu);
    if (!img_open(&s_w25)) {
        pthread_mutex_unlock(&s_w25.mu);
        return -1;
    }
    uint8_t tmp[1024];
    size_t  done = 0;
    int     rc   = 0;
    while (done < len) {
        size_t chunk = (len - done < sizeof(tmp)) ? len - done : sizeof(tmp);
        for (size_t i = 0; i < chunk; i++) {
            tmp[i] = data[done + i] ^ 0xFFu;
        }
        if (pwrite(s_w25.fd, tmp, chunk, (off_t)(addr + done))
            != (ssize_t)chunk) {
            rc = -1;
            break;
        }
        done += chunk;
    }
    s_w25.rcache_valid = false;
    pthread_mutex_unlock(&s_w25.mu);
    return rc;
}

/* Deterministic non-0xFF junk pattern — scenario-visible via raw_peek as
 * exactly `pattern`.  CRCs fail and the record-chain scanner sees
 * garbage: a faithful torn-program / corruption vector.                 */
int vbfq_corrupt_sector(uint32_t addr)
{
    if (addr >= VBFQ_TOTAL_BYTES) {
        return -1;
    }
    uint32_t base = addr & ~(VBFQ_SECTOR_4K - 1u);
    uint8_t  stored[VBFQ_SECTOR_4K];
    for (uint32_t i = 0; i < sizeof(stored); i++) {
        uint8_t pattern = (uint8_t)(0x5Au ^ (i * 31u) ^ (base >> 8));
        stored[i] = (uint8_t)~pattern;
    }
    pthread_mutex_lock(&s_w25.mu);
    if (!img_open(&s_w25)) {
        pthread_mutex_unlock(&s_w25.mu);
        return -1;
    }
    int rc = (pwrite(s_w25.fd, stored, sizeof(stored), (off_t)base)
              == (ssize_t)sizeof(stored)) ? 0 : -1;
    s_w25.rcache_valid = false;
    pthread_mutex_unlock(&s_w25.mu);
    return rc;
}

int vbfq_get_wp_hold(void)
{
    pthread_mutex_lock(&s_w25.mu);
    int v = s_w25.wp_hold;
    pthread_mutex_unlock(&s_w25.mu);
    return v;
}

void vbfq_set_wp_hold(int mask)
{
    pthread_mutex_lock(&s_w25.mu);
    s_w25.wp_hold = mask;
    pthread_mutex_unlock(&s_w25.mu);
}

void vbfq_set_jedec(uint8_t mfr, uint8_t type, uint8_t cap)
{
    pthread_mutex_lock(&s_w25.mu);
    s_w25.jedec[0] = mfr;
    s_w25.jedec[1] = type;
    s_w25.jedec[2] = cap;
    pthread_mutex_unlock(&s_w25.mu);
}

void vbfq_get_jedec(uint8_t out3[3])
{
    if (out3 == NULL) {
        return;
    }
    pthread_mutex_lock(&s_w25.mu);
    memcpy(out3, s_w25.jedec, 3);
    pthread_mutex_unlock(&s_w25.mu);
}

void vbfq_set_uid(const uint8_t uid8[8])
{
    if (uid8 == NULL) {
        return;
    }
    pthread_mutex_lock(&s_w25.mu);
    memcpy(s_w25.uid, uid8, 8);
    pthread_mutex_unlock(&s_w25.mu);
}

void vbfq_get_uid(uint8_t uid8[8])
{
    if (uid8 == NULL) {
        return;
    }
    pthread_mutex_lock(&s_w25.mu);
    memcpy(uid8, s_w25.uid, 8);
    pthread_mutex_unlock(&s_w25.mu);
}

void vbfq_stats(vbfq_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    pthread_mutex_lock(&s_w25.mu);
    *out = s_w25.stats;
    pthread_mutex_unlock(&s_w25.mu);
}

void vbfq_stats_reset(void)
{
    pthread_mutex_lock(&s_w25.mu);
    memset(&s_w25.stats, 0, sizeof(s_w25.stats));
    pthread_mutex_unlock(&s_w25.mu);
}

void vbfq_get_state(vbfq_state_t *out)
{
    if (out == NULL) {
        return;
    }
    pthread_mutex_lock(&s_w25.mu);
    out->wel          = s_w25.wel;
    out->ads4b        = s_w25.ads4b;
    out->powered_down = s_w25.powered_down;
    out->busy         = chip_busy(&s_w25);
    out->reset_armed  = s_w25.reset_armed;
    out->sr1          = sr1(&s_w25);
    out->sr2          = s_w25.sr2;
    out->sr3          = sr3(&s_w25);
    pthread_mutex_unlock(&s_w25.mu);
}
