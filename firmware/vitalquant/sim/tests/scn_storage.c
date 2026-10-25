/* scn_storage.c — host-side scenario: W25Q512 log-store durability.
 *
 * Coverage:
 *   1. clean boot on a fresh 64 MiB image: journal-empty full rescan,
 *      mount, pipeline samples + events land in flash, logtail order.
 *   2. reboot persistence: second process mounts via the journal fast
 *      path (no 16k-sector rescan), head/next_id continue, old records
 *      readable.  The real `reboot` console path is exercised — the
 *      esp_restart shim exits the child with code 42.
 *   3. newest journal slot zapped: mount falls back to an older
 *      checkpoint and forward-scans to the true append point.
 *   4. head sector corrupted: mount abandons the torn sector, keeps
 *      the valid prefix iterable, no OOB on hostile record lengths.
 *   5. both journal sectors garbage: bounded full-ring rebuild.
 *   6. crafted journal near the ring end: wrap at 16382->2 plus
 *      drop-oldest reuse of the tail sector.
 *   7. /WP engaged: writes are dropped by the model (silicon-faithful
 *      silent ignore); verify no retry storm and clean resume.
 *   8. 3V3 rail interruptions during streaming: bounded write
 *      failures, resume after power returns.
 *   9. NVS: torn tail frame ignored (creds survive); scrambled store
 *      falls back to defaults ("no wifi creds").
 *
 * MULTI-PROCESS DESIGN — read this before hacking on it:
 *   vq_log_init() latches s.inited on success and the sleep-shim warm
 *   reboot longjmps back into fw_app_main WITHOUT tearing down tasks or
 *   static state, so an in-process "reboot" can never re-run the ring
 *   mount.  Mount/recovery is therefore tested with sequential child
 *   processes: the parent forks one child per boot, corrupts the
 *   64 MiB image file (bytes are stored inverted — all manipulation
 *   goes through vbfq_raw_peek/poke which decode for us) between
 *   children, and collects each child's check/fail tallies plus the
 *   mounted ring state over a small pipe.
 *
 * Every child:
 *   - tees fd1+fd2 into a RAM ring buffer ("cap") so ESP_LOG lines and
 *     console output can be asserted on directly,
 *   - sets g_vb_faults.wifi_link_down so the cloud drain never uploads
 *     (vq_log_drop_oldest would delete records and break persistence),
 *   - emits one "SCNSTATE ..." line on fd 3 for the parent.
 *
 * Exit protocol: children _exit() with their failure count, except the
 * warm-boot child which dies inside esp_restart() with code 42 (the
 * shim's "clean reboot requested" contract).  The parent prints
 *   SCN storage RESULT: checks=<n> fails=<k>
 * and returns g_vsim_failures.
 */
#define _GNU_SOURCE
#include "vsim.h"
#include "vboard.h"
#include "devices/w25q512_sim.h"
#include "devices/imu_feed.h"   /* vbimu_set_motion — motion-gate release */

#include "log_store.h"          /* vq_log_* public API              */
#include "wifi_mgr.h"           /* vq_wifi_have_creds()             */
#include "vq_decision_ext.h"    /* vq_decision_force_next()         */
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_heap_caps.h"      /* heap_caps_free/realloc           */

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <execinfo.h>

/* NOTE: this scenario once interposed free()/realloc() to route
 * heap_caps pointers around the sim's prepended alloc_hdr_t — the shim
 * has since moved allocator metadata to a side map (see heap.c), so a
 * plain free() on a heap_caps block is already correct and the probe
 * would just read out of bounds.  Removed. */

/* ------------------------------------------------------------------ */
/* shared constants (mirroring log_store.c — kept local, not linked)  */
/* ------------------------------------------------------------------ */
#define SECT_BYTES      4096u
#define RING_FIRST_S    2u
#define RING_LAST_S     16382u
#define SB_MAGIC        0x4A4C5156u     /* "VQLJ"                    */
#define SB_SLOT         64u
#define SB_SLOTS        (SECT_BYTES / SB_SLOT)
#define TYPE_SAMPLES    1u
#define TYPE_EVENT      2u
#define TYPE_ESC        3u

#define TIME_SCALE      64.0
#define OUT_SZ          (96 * 1024)

static int   n_checks;
static char  out[OUT_SZ];

#define CHECK(cond, msg) do { n_checks++; VSIM_CHECK(cond, msg); } while (0)

/* ------------------------------------------------------------------ */
/* crc32 (zlib poly) — identical table algorithm to log_store.c       */
/* ------------------------------------------------------------------ */
static uint32_t s_crc_tab[256];
static int      s_crc_ready;
static void crc_init(void)
{
    if (s_crc_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc_tab[i] = c;
    }
    s_crc_ready = 1;
}
static uint32_t crc32_buf(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    crc_init();
    while (len--) c = s_crc_tab[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32));
}

static int64_t wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ================================================================== *
 *  CHILD-SIDE: capture everything written to fd 1 + fd 2             *
 * ================================================================== */
#define CAP_BYTES (6u << 20)
static char            g_cap[CAP_BYTES];
static size_t          g_cap_len;
static pthread_mutex_t g_cap_mu = PTHREAD_MUTEX_INITIALIZER;
static int             g_real_fd = -1;
static unsigned long   g_n_backlog_drops;   /* 'log batch backlog' hits */

/* High-volume sensor/health chatter that would evict early-boot lines
 * from the capture within seconds at TIME_SCALE=64.  Everything is
 * still tee'd to the real fd — only the match buffer is filtered, so
 * cap_has()/cap_mount() stay valid for the whole phase. */
static int cap_noisy(const char *line, size_t len)
{
    static const char *const noisy[] = {
        "as7341", "afe4900", "max86141", "tmp117", "bme280", "sht45",
        "mlx90632", "im69d130", "lsm6", "ad5940", "fsr",
        "SENSOR_FAULT", "recovered — streaming", "drdy stall",
        "hub anomalies", "INT gpio init", "led_sched",
        "disconnected (reason", "reconnect in", "restart failed",
        "esp_timer: timer", "fifo overrun",
    };
    for (size_t i = 0; i < sizeof(noisy) / sizeof(noisy[0]); i++) {
        if (strstr(line, noisy[i]) != NULL) return 1;
    }
    (void)len;
    return 0;
}

static void cap_append_line(const char *line, size_t len)
{
    pthread_mutex_lock(&g_cap_mu);
    if (g_cap_len + len + 1 > CAP_BYTES - 1) {
        size_t drop = g_cap_len + len + 1 - (CAP_BYTES - 1);
        memmove(g_cap, g_cap + drop, g_cap_len - drop);
        g_cap_len -= drop;
    }
    memcpy(g_cap + g_cap_len, line, len);
    g_cap_len += len;
    g_cap[g_cap_len++] = '\n';
    g_cap[g_cap_len] = 0;
    pthread_mutex_unlock(&g_cap_mu);
}

static void *cap_drain(void *arg)
{
    int fd = *(int *)arg;
    int real = g_real_fd;
    char b[4096];
    static char line[8192];         /* assemble full lines for the mask */
    size_t have = 0;
    for (;;) {
        ssize_t n = read(fd, b, sizeof(b));
        if (n <= 0) { usleep(2000); continue; }
        if (real >= 0) {            /* everything still reaches stdout  */
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(real, b + off, (size_t)(n - off));
                if (w <= 0) break;
                off += w;
            }
        }
        /* split into lines; stash non-noisy ones in the match buffer */
        for (ssize_t i = 0; i < n; i++) {
            char c = b[i];
            if (c != '\n' && have < sizeof(line) - 1) {
                line[have++] = c;
                continue;
            }
            line[have] = 0;
            if (have > 0) {
                if (strstr(line, "log batch backlog") != NULL) {
                    __atomic_fetch_add(&g_n_backlog_drops, 1,
                                       __ATOMIC_RELAXED);
                }
                if (!cap_noisy(line, have))
                    cap_append_line(line, have);
            }
            have = 0;
        }
    }
    return NULL;
}

static void cap_install(void)
{
    int p[2];
    if (pipe(p) != 0) return;
    g_real_fd = dup(STDOUT_FILENO);
    dup2(p[1], STDOUT_FILENO);
    dup2(p[1], STDERR_FILENO);
    close(p[1]);
    static int rd;
    rd = p[0];
    pthread_t th;
    if (pthread_create(&th, NULL, cap_drain, &rd) == 0) {
        pthread_detach(th);
    }
}

static size_t cap_mark(void)
{
    pthread_mutex_lock(&g_cap_mu);
    size_t m = g_cap_len;
    pthread_mutex_unlock(&g_cap_mu);
    return m;
}
static int cap_has_since(size_t mark, const char *needle)
{
    pthread_mutex_lock(&g_cap_mu);
    int found = g_cap_len > mark &&
                strstr(g_cap + mark, needle) != NULL;
    pthread_mutex_unlock(&g_cap_mu);
    return found;
}
static int cap_has(const char *needle) { return cap_has_since(0, needle); }

static int cap_count_since(size_t mark, const char *needle)
{
    pthread_mutex_lock(&g_cap_mu);
    int n = 0;
    const char *p = g_cap + mark;
    const size_t nl = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) { n++; p += nl; }
    pthread_mutex_unlock(&g_cap_mu);
    return n;
}

/* wait (wall-clock) for a marker to appear in the capture */
static int cap_wait(const char *needle, int wall_ms_lim)
{
    int64_t dl = wall_ms() + wall_ms_lim;
    while (wall_ms() < dl) {
        if (cap_has(needle)) return 1;
        usleep(15000);
    }
    return cap_has(needle);
}

/* ================================================================== *
 *  CHILD-SIDE helpers                                                *
 * ================================================================== */

static int cmd(const char *c, int timeout_ms)
{
    out[0] = 0;
    int rc = vsim_cmd(c, out, sizeof(out), timeout_ms);
    return rc;
}

/* progress breadcrumb — straight to the real console, survives a
 * segv in whatever runs right after it. */
static void prog(const char *m)
{
    if (g_real_fd >= 0) {
        (void)!write(g_real_fd, "[prog] ", 7);
        (void)!write(g_real_fd, m, strlen(m));
        (void)!write(g_real_fd, "\n", 1);
    }
}

/* Wait until the console REPL answers a command. */
static int wait_console(int wall_ms_lim)
{
    int64_t dl = wall_ms() + wall_ms_lim;
    while (wall_ms() < dl) {
        if (cap_has("console started")) {
            int rc = cmd("status", 400000);
            if (rc >= 0 && (strstr(out, "vitalquant status") ||
                            cap_wait("vitalquant status", 2000)))
                return 1;
        }
        usleep(150000);
    }
    return 0;
}

/* vq_log_selftest report -> ring state. */
typedef struct {
    int      ran, pass;
    uint32_t used, used_tot, head, off, tail, nid, sbseq;
} ringst_t;
static void ring_state(ringst_t *r)
{
    static char rep[2048];
    memset(r, 0, sizeof(*r));
    rep[0] = 0;
    vq_err_t e = vq_log_selftest(rep, sizeof(rep));
    r->ran  = 1;
    r->pass = (strstr(rep, "result: PASS") != NULL);
    const char *p = strstr(rep, "ring:");
    if (p) {
        unsigned u = 0, ut = 0, h = 0, o = 0, t = 0, n = 0, sq = 0;
        if (sscanf(p, "ring: used=%u/%u head=%u+%u tail=%u next_id=%u sbseq=%u",
                   &u, &ut, &h, &o, &t, &n, &sq) == 7) {
            r->used = u; r->used_tot = ut; r->head = h; r->off = o;
            r->tail = t; r->nid = n; r->sbseq = sq;
        }
    }
    (void)e;
}

/* count records of a type via the real iterate path (SPI walk of the
 * firmware's used window — expensive on big rings, so callers use it
 * only where the real iterate path itself is under test) */
typedef struct { uint32_t n, maxid; } cnt_t;
static bool cnt_cb(const vq_log_hdr_t *h, const uint8_t *p, void *c)
{
    (void)p;
    cnt_t *x = c;
    x->n++;
    if (h->id > x->maxid) x->maxid = h->id;
    return true;
}
static uint32_t count_type(uint32_t type)
{
    cnt_t x = {0, 0};
    vq_log_iterate(type, cnt_cb, &x);
    return x.n;
}

/* Host-side ring scan: ONE backdoor read of the ring region, then the
 * same record-chain walk vq_log_iterate() does — but at memcpy speed,
 * without taking the log mutex (an SPI iterate on a packed ring walks
 * up to 16381 sectors while holding s.mu for a very long time).
 * Semantics mirror the firmware: sectors are walked tail->newest over
 * `used` entries only, each sector's chain stops at the first
 * blank/corrupt record. */
static uint8_t *g_img;
static uint32_t ring_next_s(uint32_t s)
{
    return (s >= RING_LAST_S) ? RING_FIRST_S : s + 1u;
}
static int img_load(void)
{
    size_t span = (size_t)(RING_LAST_S - RING_FIRST_S + 1u) * SECT_BYTES;
    if (!g_img) g_img = malloc(span);
    if (!g_img) return -1;
    return vbfq_raw_peek(RING_FIRST_S * SECT_BYTES, g_img, span);
}
static uint32_t img_count_window(uint32_t type, uint32_t tail,
                                 uint32_t used)
{
    if (img_load() != 0) return 0;
    if (used > RING_LAST_S - RING_FIRST_S + 1u)
        used = RING_LAST_S - RING_FIRST_S + 1u;
    uint32_t n = 0;
    for (uint32_t i = 0, s = tail; i < used; i++, s = ring_next_s(s)) {
        const uint8_t *sec = g_img + (size_t)(s - RING_FIRST_S) * SECT_BYTES;
        uint32_t off = 0;
        while (off + 24 <= SECT_BYTES) {
            uint32_t id = rd32(sec + off);
            if (id == 0xFFFFFFFFu) break;
            uint32_t len = rd32(sec + off + 16);
            if (len > SECT_BYTES - 24 || off + 24 + len > SECT_BYTES) break;
            if (crc32_buf(sec + off, 20 + len) !=
                rd32(sec + off + 20 + len)) break;
            if (rd32(sec + off + 4) == type) n++;
            off += 24 + len;
        }
    }
    return n;
}
/* whole-ring count (any sector) — used for "did anything land" checks
 * where the firmware's used window is still settling */
static uint32_t img_count_type(uint32_t type)
{
    return img_count_window(type, RING_FIRST_S,
                            RING_LAST_S - RING_FIRST_S + 1u);
}

/* advance virtual time until >= want records of `type` exist in the
 * image, bounded by WALL time: at TIME_SCALE=64 a virtual bound alone
 * turns SPI/image work into minutes of wall time on a populated ring. */
static uint32_t img_wait_count(uint32_t type, uint32_t want,
                               int64_t wall_ms_lim)
{
    int64_t t0 = wall_ms();
    uint32_t n = img_count_type(type);
    while (n < want && wall_ms() - t0 < wall_ms_lim) {
        vsim_advance_us(10LL * 1000 * 1000);      /* ~10 vs hops */
        n = img_count_type(type);
    }
    return n;
}

/* Parse "mounted: head=H+O tail=T used=U/R next_id=N" out of cap. */
typedef struct { int ok; uint32_t head, off, tail, used, nid; } mount_t;
static mount_t cap_mount(void)
{
    mount_t m = {0, 0, 0, 0, 0, 0};
    pthread_mutex_lock(&g_cap_mu);
    char *p = strstr(g_cap, "mounted: head=");
    if (p) {
        unsigned h = 0, o = 0, t = 0, u = 0, r = 0, n = 0;
        if (sscanf(p, "mounted: head=%u+%u tail=%u used=%u/%u next_id=%u",
                   &h, &o, &t, &u, &r, &n) == 6) {
            m.ok = 1; m.head = h; m.off = o; m.tail = t;
            m.used = u; m.nid = n;
        }
    }
    pthread_mutex_unlock(&g_cap_mu);
    return m;
}

/* Aggregate every "escalation: ... blob %u B queued %d persisted %d"
 * line captured since `mark` — the INVALID_SIZE investigation needs
 * per-line consistency, not just the first hit. */
typedef struct {
    int      n;               /* escalation lines seen                 */
    uint32_t max_blob;        /* largest blob_len field                */
    int      n_oversize;      /* lines with blob > 4072 B              */
    int      n_drop;          /* oversized lines with persisted == 0   */
    int      n_inconsist;     /* blob <= 4072 B yet persisted == 0     */
} escst_t;
static escst_t cap_esc_stats(size_t mark)
{
    escst_t r = {0, 0, 0, 0, 0};
    pthread_mutex_lock(&g_cap_mu);
    char *p = g_cap;
    char *lim = g_cap + g_cap_len;
    while ((p = strstr(p, "escalation: ")) != NULL && p < lim) {
        if ((size_t)(p - g_cap) < mark) { p += 12; continue; }
        char *bp = strstr(p, "blob ");
        unsigned b = 0;
        int q = -1, ps = -1;
        if (bp && sscanf(bp, "blob %u B queued %d persisted %d",
                         &b, &q, &ps) == 3) {
            r.n++;
            if (b > r.max_blob) r.max_blob = b;
            if (b > 4072u) {
                r.n_oversize++;
                if (ps == 0) r.n_drop++;
            } else if (ps == 0) {
                r.n_inconsist++;
            }
        }
        p += 12;
    }
    pthread_mutex_unlock(&g_cap_mu);
    return r;
}

/* logtail flash parse: count "  [t=<x> ms]" lines + backward jumps. */
typedef struct { int lines, backjumps; int64_t first_t, last_t; } lt_t;
static lt_t logtail_parse(const char *o)
{
    lt_t r = {0, 0, 0, 0};
    const char *p = o;
    int64_t prev = -1;
    while ((p = strstr(p, "[t=")) != NULL) {
        long long t;
        if (sscanf(p, "[t=%lld ms]", &t) == 1) {
            if (r.lines == 0) r.first_t = t;
            if (prev >= 0 && t < prev) r.backjumps++;
            prev = t;
            r.last_t = t;
            r.lines++;
        }
        p += 3;
    }
    return r;
}

/* bounded wait for a captured line to appear, advancing virtual time
 * in hops — used for asynchronous firmware log lines whose virtual
 * arrival time is uncertain (decision cadence, health sweeps). */
static int cap_wait_line(size_t mark, const char *needle,
                         int64_t hop_vus, int64_t wall_ms_lim)
{
    int64_t t0 = wall_ms();
    while (wall_ms() - t0 < wall_ms_lim) {
        if (cap_has_since(mark, needle)) return 1;
        vsim_advance_us(hop_vus);
    }
    return cap_has_since(mark, needle);
}


/* inherited tallies: each child is fork()ed AFTER the parent folded the
 * previous phases' results into g_vsim_failures/n_checks, so the child
 * starts life with the running total.  Snapshot it in child_common();
 * emit_state and the _exit code subtract the baseline so each child
 * reports only its own checks/fails (otherwise fails compound). */
static int s_base_checks, s_base_fails;

/* child -> parent state handoff */
typedef struct {
    int      have;
    int      checks, fails;
    uint32_t head, off, tail, nid, used, evts, samp;
} st_t;
static void emit_state(int fd, const ringst_t *r, uint32_t evts,
                       uint32_t samp)
{
    char line[256];
    int n = snprintf(line, sizeof(line),
                     "SCNSTATE checks=%d fails=%d head=%u off=%u tail=%u "
                     "nid=%u used=%u evts=%u samp=%u\n",
                     n_checks - s_base_checks,
                     g_vsim_failures - s_base_fails, r->head, r->off,
                     r->tail, r->nid, r->used, evts, samp);
    if (n > 0) (void)!write(fd, line, (size_t)n);
}
static void parse_state(const char *buf, st_t *s)
{
    memset(s, 0, sizeof(*s));
    const char *p = strstr(buf, "SCNSTATE");
    if (!p) return;
    unsigned c = 0, f = 0, h = 0, o = 0, t = 0, n = 0, u = 0, e = 0, m = 0;
    int got = sscanf(p,
                     "SCNSTATE checks=%u fails=%u head=%u off=%u tail=%u "
                     "nid=%u used=%u evts=%u samp=%u",
                     &c, &f, &h, &o, &t, &n, &u, &e, &m);
    if (got >= 2) {
        s->have = 1; s->checks = (int)c; s->fails = (int)f;
        s->head = h; s->off = o; s->tail = t; s->nid = n;
        s->used = u; s->evts = e; s->samp = m;
    }
}

static void segv_bt(int sig, siginfo_t *si, void *uc)
{
    (void)sig; (void)si; (void)uc;
    void *bt[24];
    int n = backtrace(bt, 24);
    if (g_real_fd >= 0)
        backtrace_symbols_fd(bt, n, g_real_fd);
    _exit(128 + SIGSEGV);
}

/* Debug watchdog: dumps flash-model + SPI counters to the real fd every
 * SCN_WD_MS wall ms so a hung phase reveals whether the bus is moving,
 * whether the model is stuck busy, and how much flash work commits. */
static void *wd_thread(void *arg)
{
    (void)arg;
    vbfq_stats_t prev = {0};
    for (;;) {
        usleep(5000 * 1000);
        vbfq_stats_t st;
        vbfq_state_t fs;
        vbfq_stats(&st);
        vbfq_get_state(&fs);
        char b[320];
        int n = snprintf(b, sizeof(b),
            "[wd] v=%llu spi_txns=%llu(+%llu) pp=%llu(+%llu) er=%llu(+%llu)"
            " ign_wel=%llu ign_wp=%llu busy=%d wel=%d pd=%d sr1=%02X\n",
            (unsigned long long)(vboard_now_us() / 1000),
            (unsigned long long)st.txns,
            (unsigned long long)(st.txns - prev.txns),
            (unsigned long long)st.pp_commits,
            (unsigned long long)(st.pp_commits - prev.pp_commits),
            (unsigned long long)st.erase_commits,
            (unsigned long long)(st.erase_commits - prev.erase_commits),
            (unsigned long long)st.wr_ignored_no_wel,
            (unsigned long long)st.wr_ignored_wp,
            (int)fs.busy, (int)fs.wel, (int)fs.powered_down, fs.sr1);
        prev = st;
        if (n > 0 && g_real_fd >= 0) (void)!write(g_real_fd, b, (size_t)n);
    }
    return NULL;
}

static void child_common(void)
{
    s_base_checks = n_checks;
    s_base_fails  = g_vsim_failures;
    cap_install();
    struct sigaction sa = {0};
    sa.sa_sigaction = segv_bt;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, NULL);
    if (!getenv("VSIM_TIME_SCALE"))
        vboard_clock_set_scale(TIME_SCALE);
    g_vb_faults.wifi_link_down = true;   /* cloud drain must not eat log */
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (getenv("SCN_WD")) {
        pthread_t t;
        if (pthread_create(&t, NULL, wd_thread, NULL) == 0)
            pthread_detach(t);
    }
}

/* decoded journal slot (layout mirrors vq_sb_t in log_store.c) */
typedef struct {
    uint32_t seq, head, off, tail, nid, appends, drops, used;
    int      found, sec, slot;
} jslot_t;

/* newest valid journal entry across sectors 0/1 */
static jslot_t journal_newest(void)
{
    uint8_t buf[SECT_BYTES];
    jslot_t j = {0};
    j.found = 0;
    for (uint32_t sec = 0; sec <= 1; sec++) {
        if (vbfq_raw_peek(sec * SECT_BYTES, buf, SECT_BYTES) != 0)
            continue;
        for (uint32_t i = 0; i < SB_SLOTS; i++) {
            const uint8_t *p = buf + i * SB_SLOT;
            if (rd32(p) == 0xFFFFFFFFu) continue;
            if (rd32(p) != SB_MAGIC) continue;
            if (crc32_buf(p, 36) != rd32(p + 36)) continue;
            if (!j.found || rd32(p + 4) > j.seq) {
                j.found   = 1;
                j.sec     = (int)sec;
                j.slot    = (int)i;
                j.seq     = rd32(p + 4);
                j.head    = rd32(p + 8);
                j.off     = rd32(p + 12);
                j.tail    = rd32(p + 16);
                j.nid     = rd32(p + 20);
                j.appends = rd32(p + 24);
                j.drops   = rd32(p + 28);
                j.used    = rd32(p + 32);
            }
        }
    }
    return j;
}

/* ================================================================== *
 *  PHASES                                                            *
 * ================================================================== */

/* PH1 — fresh 64 MiB image: rescan mount, data lands, INVALID_SIZE
 * investigation, logtail, wifi creds committed. */
static void ph_clean(int sfd)
{
    child_common();
    vbfq_stats_reset();
    vsim_boot();
    CHECK(wait_console(120000), "PH1 console did not come up");
    CHECK(cap_has("journal unreadable"),
          "PH1 fresh flash: expected full-ring rescan warning");
    CHECK(vq_log_flash_present(), "PH1 log store not present");
    /* rescan mounts don't print 'mounted:' (ring_mount returns via
     * sb_commit early) — read state through the selftest report instead. */
    ringst_t r;
    ring_state(&r);
    CHECK(r.pass, "PH1 log selftest FAIL");
    CHECK(r.tail == RING_FIRST_S,
          "PH1 fresh mount should start tail at ring first sector");
    CHECK(r.head >= RING_FIRST_S && r.head < 2000,
          "PH1 fresh mount head should sit near the ring start");
    CHECK(r.nid < 2000,
          "PH1 fresh mount next_id implausibly large");

    /* let the pipeline log real data. SAMPLES batches flush from the
     * log task; EVENT records land from health/decision/power paths —
     * all asynchronous, so poll the image (bounded by wall clock). */
    prog("ph1: pre-stream");
    uint32_t samp = img_wait_count(TYPE_SAMPLES, 2, 30000);
    prog("ph1: post-stream");
    CHECK(samp >= 2, "PH1 no SAMPLES records landed in flash");

    /* exercise the manual decision path too: the console `score` runs
     * one synchronous cycle (a scored/skipped EVENT record lands even
     * when the motion gate blocks scoring). force_next latches a
     * guaranteed escalation into the NEXT autonomous cycle. */
    vq_decision_force_next();
    size_t mark = cap_mark();
    int rc = cmd("score", 600000);
    CHECK(rc >= 0, "PH1 score cmd dead");

    /* --- the 'log append failed (ESP_ERR_INVALID_SIZE)' investigation
     * The SAMPLES batch is capped at VQ_LOG_BATCH_CAP =
     * 4072/sizeof(vq_sample_t) = 169 samples (4056 B) — INVALID_SIZE
     * can NEVER fire on the samples path. The remaining appender that
     * can exceed the 4072 B record limit is the escalation bundle
     * (raw channel windows staged up to VQ_ESC_CHANNELS_MAX deep).
     *
     * The autonomous decision cycle is motion-gated: 'moving' when
     * |a|_rms > 0.5 or still < 10 s. The sim feeds static gravity
     * ({0,0,1} g) — rms≈1.0 — so a motionless device is permanently
     * "moving" and the gate never releases while the IMU streams
     * (finding: the gate's rms term includes gravity; reported below).
     * Zero the feed so cycles can run, then wait for an escalation. */
    int n_gated = cap_count_since(0, "cycle skipped — motion gate");
    {
        const float zero[3] = {0, 0, 0};
        vbimu_set_motion(zero, NULL);
    }
    prog("ph1: await escalation");
    int got = cap_wait_line(mark, "escalation: ", 25LL * 1000 * 1000,
                            60000);
    /* blob size grows with the staged channel windows — keep collecting
     * a few more cycles (bounded) for a representative max. */
    for (int i = 0; i < 24; i++) {
        escst_t t = cap_esc_stats(mark);
        if (t.n >= 4 || t.n_oversize > 0) break;
        vsim_advance_us(60LL * 1000 * 1000);
    }
    prog("ph1: escalation wait done");
    escst_t es = cap_esc_stats(mark);
    CHECK(got && es.n >= 1,
          "PH1 no escalation line within the bounded window");
    /* consistency: a blob within record capacity must persist
     * (persisted=1), an oversized blob must show persisted=0 — the
     * durable-copy drop. queued is 0 throughout (link down by design). */
    CHECK(es.n_inconsist == 0,
          "escalation within 4072 B was still rejected by log append");
    if (es.n_oversize > 0) {
        CHECK(es.n_drop == es.n_oversize,
              "oversized escalation blob persisted past the size limit");
        n_checks++;
        VSIM_CHECK(0,
              "firmware defect: escalation bundle exceeds the 4072 B "
              "record limit — durable copy dropped (INVALID_SIZE); "
              "offline that is silent escalation loss");
    }
    int n_appfail = cap_count_since(0, "log append failed");
    /* 'log batch backlog' lines are filtered from the match buffer
     * (volume) but counted by the drainer. */
    int n_backlog = (int)__atomic_load_n(&g_n_backlog_drops,
                                       __ATOMIC_RELAXED);
    uint32_t esc_recs = img_count_type(TYPE_ESC);
    printf("[scn] INVALID_SIZE evidence: esc_lines=%d max_blob=%uB "
           "oversize=%d durable_dropped=%d esc_records=%u | "
           "'log append failed'=%d 'batch backlog drops'=%d\n",
           es.n, es.max_blob, es.n_oversize, es.n_drop, esc_recs,
           n_appfail, n_backlog);
    printf("[scn] findings: motion-gated cycles before imu feed zeroed "
           "= %d — |a|_rms includes gravity (~1g at rest) so the gate "
           "treats a motionless device as moving (firmware design "
           "defect; escalations only fire when IMU features are "
           "absent).\n", n_gated);
    CHECK(n_appfail == 0,
          "PH1 'log append failed' on a path that can fit a record");
    uint32_t evts = img_count_type(TYPE_EVENT);
    CHECK(evts >= 1, "PH1 no EVENT records landed in flash");

    /* logtail ordering */
    rc = cmd("logtail 20 flash", 900000);
    CHECK(rc >= 0, "PH1 logtail dead");
    lt_t lt = logtail_parse(out);
    CHECK(lt.lines >= 1, "PH1 logtail flash empty");
    CHECK(lt.backjumps <= 1,
          "PH1 logtail timestamps wildly out of order");

    /* commit wifi creds into NVS for later phases */
    rc = cmd("wifi set scn-ssid scn-pass", 200000);
    CHECK(rc >= 0 && strstr(out, "credentials stored"),
          "PH1 wifi set failed");
    CHECK(vq_wifi_have_creds(), "PH1 creds not visible after set");

    ring_state(&r);
    emit_state(sfd, &r,
               img_count_window(TYPE_EVENT, r.tail, r.used),
               img_count_window(TYPE_SAMPLES, r.tail, r.used));
    /* never fflush(NULL): the console REPL's blocking fgets(stdin)
     * holds stdin's FILE lock forever — an all-stream flush deadlocks.
     * Flush just our output streams instead. */
    fflush(stdout);
    fflush(stderr);
    usleep(30000);
    _exit((g_vsim_failures - s_base_fails) & 0xFF);
}

/* PH2 — warm boot: journal fast-path mount + real `reboot` (exit 42). */
static void ph_warm(int sfd)
{
    child_common();
    vbfq_stats_reset();
    vsim_boot();
    CHECK(wait_console(90000), "PH2 console did not come up");
    vbfq_stats_t st;
    vbfq_stats(&st);
    CHECK(st.spi_bytes < 2u * 1024u * 1024u,
          "PH2 mount did a full rescan (journal fast-path broken)");
    CHECK(!cap_has("journal unreadable"),
          "PH2 unexpected full-ring rescan");
    CHECK(vq_log_flash_present(), "PH2 log store not present");
    mount_t m = cap_mount();
    CHECK(m.ok, "PH2 no 'mounted:' line");
    ringst_t r;
    ring_state(&r);
    CHECK(r.pass, "PH2 log selftest FAIL");

    /* prior records must still be there — checked both through the
     * real iterate path (logtail) and the image-side window scan */
    uint32_t evts = img_count_window(TYPE_EVENT, r.tail, r.used);
    CHECK(evts >= 1, "PH2 old events gone after reboot");

    int rc = cmd("logtail 20 flash", 900000);
    CHECK(rc >= 0, "PH2 logtail dead");
    lt_t lt = logtail_parse(out);
    CHECK(lt.lines >= 1, "PH2 logtail flash empty");
    CHECK(lt.backjumps <= 1,
          "PH2 logtail timestamps wildly out of order");

    CHECK(vq_wifi_have_creds(), "PH2 wifi creds lost across reboot");

    emit_state(sfd, &r, evts, img_count_window(TYPE_SAMPLES, r.tail,
                                               r.used));
    /* never fflush(NULL): the console REPL's blocking fgets(stdin)
     * holds stdin's FILE lock forever — an all-stream flush deadlocks.
     * Flush just our output streams instead. */
    fflush(stdout);
    fflush(stderr);
    usleep(30000);

    /* the real reboot path: cmd_reboot -> esp_restart -> _exit(42).
     * If esp_restart does not fire in ~3 s wall, leave with 43 so the
     * parent can flag the reboot path as broken. */
    (void)cmd("reboot", 30000);
    vsim_advance_us(3 * 1000 * 1000);
    _exit(43);
}

/* PH3 — newest journal slot corrupt: stale-checkpoint mount. */
static void ph_slotcorrupt(int sfd)
{
    child_common();
    vbfq_stats_reset();
    vsim_boot();
    CHECK(wait_console(90000), "PH3 console did not come up");
    vbfq_stats_t st;
    vbfq_stats(&st);
    CHECK(st.spi_bytes < 2u * 1024u * 1024u,
          "PH3 stale journal should NOT trigger full rescan");
    CHECK(!cap_has("journal unreadable"),
          "PH3 unexpected full-ring rescan");
    mount_t m = cap_mount();
    CHECK(m.ok, "PH3 no 'mounted:' line");
    ringst_t r;
    ring_state(&r);
    CHECK(r.pass, "PH3 log selftest FAIL");
    uint32_t evts = img_count_window(TYPE_EVENT, r.tail, r.used);
    CHECK(evts >= 1, "PH3 events gone after stale-journal mount");
    emit_state(sfd, &r, evts, img_count_window(TYPE_SAMPLES, r.tail,
                                               r.used));
    /* never fflush(NULL): the console REPL's blocking fgets(stdin)
     * holds stdin's FILE lock forever — an all-stream flush deadlocks.
     * Flush just our output streams instead. */
    fflush(stdout);
    fflush(stderr);
    usleep(30000);
    _exit((g_vsim_failures - s_base_fails) & 0xFF);
}

/* PH4 — head sector corrupt (journal intact): torn-sector handling. */
static void ph_headcorrupt(int sfd)
{
    child_common();
    vsim_boot();
    CHECK(wait_console(90000), "PH4 console did not come up");
    CHECK(cap_has("corrupt tail — abandoned"),
          "PH4 mount did not abandon the corrupted head sector");
    CHECK(!cap_has("journal unreadable"),
          "PH4 unexpected full-ring rescan");
    mount_t m = cap_mount();
    CHECK(m.ok, "PH4 no 'mounted:' line");
    ringst_t r;
    ring_state(&r);
    CHECK(r.pass, "PH4 log selftest FAIL");
    /* the corrupted sector stays inside used range: the REAL iterate
     * must skip it, not crash (this also produces the 'corrupt
     * record(s) skipped' log the CHECK below asserts on) */
    uint32_t evts = count_type(TYPE_EVENT);
    CHECK(cap_wait("corrupt record(s) skipped", 3000),
          "PH4 iterate did not report skipped corrupt records");
    CHECK(evts >= 1, "PH4 all events lost — overly aggressive recovery");
    /* appends must continue on a fresh sector */
    uint32_t s0 = img_count_window(TYPE_SAMPLES, r.tail, r.used);
    uint32_t s1 = img_wait_count(TYPE_SAMPLES, s0 + 1, 20000);
    CHECK(s1 > s0, "PH4 appends stalled after torn-sector recovery");
    ring_state(&r);
    emit_state(sfd, &r, evts,
               img_count_window(TYPE_SAMPLES, r.tail, r.used));
    /* never fflush(NULL): the console REPL's blocking fgets(stdin)
     * holds stdin's FILE lock forever — an all-stream flush deadlocks.
     * Flush just our output streams instead. */
    fflush(stdout);
    fflush(stderr);
    usleep(30000);
    _exit((g_vsim_failures - s_base_fails) & 0xFF);
}

/* PH5 — both journals destroyed: bounded full rebuild + hostile
 * record lengths planted by the parent. */
static void ph_rescan(int sfd)
{
    child_common();
    vbfq_stats_reset();
    vsim_boot();
    int64_t t0 = wall_ms();
    CHECK(wait_console(120000), "PH5 console did not come up");
    int64_t boot_ms = wall_ms() - t0;
    CHECK(cap_has("journal unreadable"),
          "PH5 expected full-ring rescan warning");
    CHECK(boot_ms < 120000,
          "PH5 rescan mount took unreasonably long (infinite scan?)");
    ringst_t r;                            /* rescan: no 'mounted:' line */
    ring_state(&r);
    CHECK(r.pass, "PH5 log selftest FAIL");
    /* real iterate over the hostile sectors: must skip, not crash or
     * read out of bounds (planted 0xFFFFFFF0 / 0xFFFF0000 lengths) */
    uint32_t evts = count_type(TYPE_EVENT);
    CHECK(evts >= 1, "PH5 events all lost in rescan");
    CHECK(cap_wait("corrupt record(s) skipped", 3000) || evts >= 1,
          "PH5 iterate did not survive hostile record lengths");
    emit_state(sfd, &r, evts,
               img_count_window(TYPE_SAMPLES, r.tail, r.used));
    /* never fflush(NULL): the console REPL's blocking fgets(stdin)
     * holds stdin's FILE lock forever — an all-stream flush deadlocks.
     * Flush just our output streams instead. */
    fflush(stdout);
    fflush(stderr);
    usleep(30000);
    _exit((g_vsim_failures - s_base_fails) & 0xFF);
}

/* PH6 — crafted journal: head near ring end, tail right behind it.
 * Streaming must wrap head past 16382 -> 2 and drop oldest sectors. */
static void ph_wrap(int sfd)
{
    child_common();
    vsim_boot();
    CHECK(wait_console(120000), "PH6 console did not come up");
    mount_t m = cap_mount();
    CHECK(m.ok, "PH6 no 'mounted:' line");
    if (m.ok) {
        CHECK(m.head >= 16380,
              "PH6 mount did not pick up crafted head near ring end");
        CHECK(m.tail == 16381,
              "PH6 mount did not pick up crafted tail 16381");
    }
    ringst_t r0;
    ring_state(&r0);
    /* Stream ~10 vs: SAMPLES batches fill ~1 sector each, so the head
     * walks 16380 -> 16381 -> 16382 -> 2 -> onward within ~4 advances.
     * (No count_type polling here — an iterate call walks `used`
     * sectors, and used is ~16380 sectors on this packed ring.) */
    vsim_advance_us(10LL * 1000 * 1000);
    {
        jslot_t jm = journal_newest();
        uint32_t lu = 0, lt = 0;
        vq_log_space(&lu, &lt);
        printf("[scn] PH6 mid: journal seq=%u head=%u+%u tail=%u drops=%u "
               "used=%u | vq_log_space used=%u/%u | selftest head=%u\n",
               jm.seq, jm.head, jm.off, jm.tail, jm.drops, jm.used,
               lu, lt, r0.head);
    }
    ringst_t r;
    ring_state(&r);
    CHECK(r.head != r0.head || r.tail != 16381,
          "PH6 append cursor never moved — streaming stalled");
    CHECK(r.tail != 16381 || r.head < 16,
          "PH6 tail never advanced (drop-oldest broken)");
    /* iterate over the near-packed ring must still complete */
    uint32_t evts = count_type(TYPE_EVENT);
    CHECK(evts >= 1, "PH6 iterate over packed ring returned nothing");
    emit_state(sfd, &r, evts, 0);
    /* never fflush(NULL): the console REPL's blocking fgets(stdin)
     * holds stdin's FILE lock forever — an all-stream flush deadlocks.
     * Flush just our output streams instead. */
    fflush(stdout);
    fflush(stderr);
    usleep(30000);
    _exit((g_vsim_failures - s_base_fails) & 0xFF);
}

/* PH7 — /WP hard protection then 3V3 rail interruptions, in one boot. */
static void ph_wp_rail(int sfd)
{
    child_common();
    vsim_boot();
    CHECK(wait_console(120000), "PH7 console did not come up");
    ringst_t r0;
    ring_state(&r0);

    /* ---------------- write-protect ---------------- */
    vbfq_stats_reset();
    /* engage /WP BEFORE snapshotting the image count — real writes may
     * still legally land in the gap between a snapshot and the hold. */
    vbfq_set_wp_hold(VBFQ_WP | VBFQ_WP_HARD);
    uint32_t cnt0 = img_count_type(TYPE_SAMPLES);
    uint64_t sb0;
    {
        vbfq_stats_t s;
        vbfq_stats(&s);
        sb0 = s.spi_bytes;
    }
    vsim_advance_us(6LL * 1000 * 1000);    /* ~6 vs of attempted writes */
    vbfq_stats_t s;
    vbfq_stats(&s);
    CHECK(s.wr_ignored_wp > 0,
          "PH7 /WP window saw zero protected writes (model inert?)");
    CHECK(s.pp_commits == 0 && s.erase_commits == 0,
          "PH7 writes committed despite /WP — model bug");
    /* no retry storm: bounded SPI traffic during the window
     * (a storm would be MBs/s; a handful of attempts is ~KBs) */
    CHECK(s.spi_bytes - sb0 < 4u * 1024u * 1024u,
          "PH7 /WP write attempts unbounded (retry storm)");
    /* and nothing landed: the decoded image SAMPLES count is frozen
     * (phantom appends advance head_off in RAM but cannot program) */
    CHECK(img_count_type(TYPE_SAMPLES) == cnt0,
          "PH7 /WP writes landed — driver silently lost them");

    vbfq_set_wp_hold(0);
    {
        vbfq_stats_t pp0;
        vbfq_stats(&pp0);
        vsim_advance_us(4LL * 1000 * 1000);
        vbfq_stats_t pp1;
        vbfq_stats(&pp1);
        /* resume: page-programs must commit again (phantom appends in
         * the WP window already advanced head_off, so a byte-peek at a
         * fixed offset is not reliable evidence). */
        CHECK(pp1.pp_commits > pp0.pp_commits,
              "PH7 no page programs committed after /WP released");
        CHECK(pp1.wr_ignored_wp == pp0.wr_ignored_wp,
              "PH7 writes still dropped after /WP released");
    }

    /* ---------------- rail interruptions ---------------- */
    vbfq_stats_reset();
    /* 3V3 feeds the sensors too — streaming alone may stall with the
     * whole board unpowered and produce no flash traffic at all.  Force
     * deterministic flash I/O through the real driver while the rail is
     * down: `logtail n flash` issues SPI reads -> chip_step counts the
     * ignored bytes.  A model write is also attempted implicitly by any
     * in-flight append. */
    uint64_t off_pp = 0, off_er = 0;
    for (int i = 0; i < 3; i++) {
        vboard_rail_set(VB_RAIL_3V3, false);
        vbfq_stats_t sa;
        vbfq_stats(&sa);
        (void)cmd("logtail 5 flash", 200000);  /* flash reads while off */
        vsim_advance_us(300LL * 1000);
        vbfq_stats_t so;
        vbfq_stats(&so);                       /* stats still OFF       */
        off_pp += so.pp_commits - sa.pp_commits;
        off_er += so.erase_commits - sa.erase_commits;
        vboard_rail_set(VB_RAIL_3V3, true);
        vsim_advance_us(700LL * 1000);         /* let fw breathe       */
    }
    vbfq_stats(&s);
    CHECK(s.ignored_rail > 0,
          "PH7 rail-off window saw no bus traffic (test did not fire)");
    CHECK(off_pp == 0 && off_er == 0,
          "PH7 writes committed while 3V3 rail was off");
    int rc = cmd("status", 2000000);
    /* rc>=0 proves the REPL answered; the reply text is checked via the
     * filtered capture, not `out` — the response range can exceed OUT_SZ
     * when iterate/event spam precedes it, truncating the status line. */
    CHECK(rc >= 0 && cap_wait("vitalquant status", 5000),
          "PH7 firmware dead after rail interruptions");
    ringst_t r2;
    ring_state(&r2);
    vsim_advance_us(4LL * 1000 * 1000);
    ringst_t r3;
    ring_state(&r3);
    CHECK(r3.nid >= r2.nid,
          "PH7 next_id regressed after rail interruptions");
    emit_state(sfd, &r3, img_count_window(TYPE_EVENT, r3.tail, r3.used),
               img_count_window(TYPE_SAMPLES, r3.tail, r3.used));
    /* never fflush(NULL): the console REPL's blocking fgets(stdin)
     * holds stdin's FILE lock forever — an all-stream flush deadlocks.
     * Flush just our output streams instead. */
    fflush(stdout);
    fflush(stderr);
    usleep(30000);
    _exit((g_vsim_failures - s_base_fails) & 0xFF);
}

/* PH8 — NVS tail frame corrupt: last good snapshot must survive. */
static void ph_nvs_tail(int sfd)
{
    child_common();
    vsim_boot();
    CHECK(wait_console(90000), "PH8 console did not come up");
    CHECK(vq_wifi_have_creds(),
          "PH8 valid NVS snapshot lost after tail-frame corruption");
    ringst_t r;
    ring_state(&r);
    emit_state(sfd, &r, img_count_window(TYPE_EVENT, r.tail, r.used), 0);
    /* never fflush(NULL): the console REPL's blocking fgets(stdin)
     * holds stdin's FILE lock forever — an all-stream flush deadlocks.
     * Flush just our output streams instead. */
    fflush(stdout);
    fflush(stderr);
    usleep(30000);
    _exit((g_vsim_failures - s_base_fails) & 0xFF);
}

/* PH9 — NVS fully scrambled: defaults, "no wifi creds". */
static void ph_nvs_dead(int sfd)
{
    child_common();
    vsim_boot();
    CHECK(wait_console(90000), "PH9 console did not come up");
    CHECK(!vq_wifi_have_creds(),
          "PH9 scrambled NVS still yields creds");
    CHECK(cap_has("no wifi creds"),
          "PH9 missing 'no wifi creds' fallback log");
    int rc = cmd("status", 200000);
    CHECK(rc >= 0, "PH9 console dead on defaults");
    ringst_t r;
    ring_state(&r);
    emit_state(sfd, &r, 0, 0);
    /* never fflush(NULL): the console REPL's blocking fgets(stdin)
     * holds stdin's FILE lock forever — an all-stream flush deadlocks.
     * Flush just our output streams instead. */
    fflush(stdout);
    fflush(stderr);
    usleep(30000);
    _exit((g_vsim_failures - s_base_fails) & 0xFF);
}

/* ================================================================== *
 *  PARENT-SIDE image surgery                                         *
 * ================================================================== */

static void erase_sector_img(uint32_t sec)
{
    static const uint8_t ff[SECT_BYTES] = {[0 ... SECT_BYTES - 1] = 0xFF};
    (void)vbfq_raw_poke(sec * SECT_BYTES, ff, SECT_BYTES);
}

/* garbage into the newest journal slot only (CRC fails, slot stays
 * non-blank) -> mount must fall back to the previous checkpoint. */
static void corrupt_newest_journal_slot(void)
{
    jslot_t j = journal_newest();
    if (!j.found) return;
    uint8_t junk[SB_SLOT];
    for (size_t i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(0x5Au ^ (i * 37u));
    (void)vbfq_raw_poke((uint32_t)j.sec * SECT_BYTES +
                        (uint32_t)j.slot * SB_SLOT, junk, sizeof(junk));
    (void)vbfq_flush();
}

static void corrupt_journals(void)
{
    (void)vbfq_corrupt_sector(0);
    (void)vbfq_corrupt_sector(SECT_BYTES);
    (void)vbfq_flush();
}

/* plant three hostile sectors mid-ring: a huge-len header, a valid
 * record followed by an evil-len header, and raw garbage. `mid_id`
 * keeps crafted ids away from min/max so tail/head stay put. */
static void plant_hostile_sectors(uint32_t base_sec, uint32_t mid_id)
{
    uint8_t b[SECT_BYTES];
    /* (a) len = 0xFFFFFFF0 at offset 0 */
    memset(b, 0xFF, sizeof(b));
    wr32(b + 0, 0x00C0FFEEu); wr32(b + 4, 1); wr64(b + 8, 1);
    wr32(b + 16, 0xFFFFFFF0u);
    for (size_t i = 24; i < 64; i++) b[i] = (uint8_t)(i * 13u);
    (void)vbfq_raw_poke(base_sec * SECT_BYTES, b, sizeof(b));
    /* (b) one valid record then an evil header (valid id, bad chain) */
    memset(b, 0xFF, sizeof(b));
    wr32(b + 0, mid_id); wr32(b + 4, 1); wr64(b + 8, 1);
    wr32(b + 16, 8);
    memcpy(b + 20, "SCNTOST!", 8);
    wr32(b + 28, crc32_buf(b, 28));
    uint8_t *e = b + 32;
    wr32(e + 0, 0x51515151u); wr32(e + 4, 7); wr64(e + 8, 2);
    wr32(e + 16, 0xFFFF0000u);
    for (size_t i = 24; i < 96; i++) e[i] = (uint8_t)(i * 29u);
    (void)vbfq_raw_poke((base_sec + 1) * SECT_BYTES, b, sizeof(b));
    /* (c) pure garbage sector (vbfq_corrupt_sector pattern) */
    (void)vbfq_corrupt_sector((base_sec + 2) * SECT_BYTES);
    (void)vbfq_flush();
}

/* crafted wrap journal: erase both journal sectors, write a single
 * slot {seq, head=16380+0, tail=16381}; plant record data in 16381
 * and 16382 so the tail-walk stops there. */
static void craft_wrap_journal(uint32_t seq, uint32_t next_id,
                               uint32_t appends, uint32_t drops,
                               uint32_t used)
{
    uint8_t data[SECT_BYTES];
    /* stash a real data sector to plant near the ring end */
    uint32_t donor = 0;
    for (uint32_t sec = RING_FIRST_S; sec < 6000; sec += 977u) {
        if (vbfq_raw_peek(sec * SECT_BYTES, data, sizeof(data)) == 0) {
            int nonff = 0;
            for (size_t i = 0; i < sizeof(data); i++)
                if (data[i] != 0xFF) { nonff = 1; break; }
            if (nonff) { donor = sec; break; }
        }
    }
    if (donor) {
        (void)vbfq_raw_peek(donor * SECT_BYTES, data, sizeof(data));
        (void)vbfq_raw_poke(16381u * SECT_BYTES, data, sizeof(data));
        (void)vbfq_raw_poke(16382u * SECT_BYTES, data, sizeof(data));
    }
    erase_sector_img(16380u);   /* head must be erased */
    /* journals: A gets exactly one crafted slot, B fully erased */
    erase_sector_img(0);
    erase_sector_img(1);
    uint8_t slot[SB_SLOT];
    memset(slot, 0xFF, sizeof(slot));
    wr32(slot + 0,  SB_MAGIC);
    wr32(slot + 4,  seq);
    wr32(slot + 8,  16380u);            /* head_sector */
    wr32(slot + 12, 0u);                /* head_off    */
    wr32(slot + 16, 16381u);            /* tail_sector */
    wr32(slot + 20, next_id);
    wr32(slot + 24, appends);
    wr32(slot + 28, drops);
    wr32(slot + 32, used);
    wr32(slot + 36, crc32_buf(slot, 36));
    (void)vbfq_raw_poke(0, slot, sizeof(slot));
    (void)vbfq_flush();
}

static void nvs_append_badframe(const char *path)
{
    FILE *f = fopen(path, "ab");
    if (!f) return;
    uint8_t junk[64];
    for (size_t i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(0xC3u ^ (i * 11u));
    fwrite(junk, 1, sizeof(junk), f);
    fclose(f);
}
/* parse + print the sim NVS frame log (mirrors nvs_sim.c store_load). */
static void nvs_dump_frames(const char *path, const char *tag)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("[scn] %s: nvs.bin missing\n", tag); return; }
    printf("[scn] %s:", tag);
    int idx = 0;
    for (;;) {
        uint8_t hdr[16];
        long off = ftell(f);
        if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) break;
        uint32_t magic, seq, plen, crc;
        memcpy(&magic, hdr + 0, 4); memcpy(&seq, hdr + 4, 4);
        memcpy(&plen, hdr + 8, 4);  memcpy(&crc, hdr + 12, 4);
        if (magic != 0x3153564eu || plen == 0 || plen > (1u << 20)) {
            printf(" frame@%ld BAD(magic=%08x plen=%u)", off, magic, plen);
            break;
        }
        uint8_t *pl = malloc(plen);
        uint32_t got = pl ? (uint32_t)fread(pl, 1, plen, f) : 0;
        uint32_t h = 2166136261u;                 /* FNV-1a/32         */
        if (pl) {
            for (uint32_t b = 0; b < got; b++)
                h = (h ^ pl[b]) * 16777619u;
        }
        int ok = pl && got == plen && h == crc;
        printf(" f%d(seq=%u plen=%u %s)", idx, seq, plen,
               ok ? "ok" : "BADPAY");
        free(pl);
        if (!ok) break;
        idx++;
    }
    printf("\n");
    fclose(f);
}

static void nvs_scramble(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint8_t junk[256];
    for (size_t i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(0x3Cu ^ (i * 7u));
    fwrite(junk, 1, sizeof(junk), f);
    fclose(f);
}

/* ================================================================== *
 *  parent driver                                                     *
 * ================================================================== */

static int run_child(const char *name, void (*fn)(int), st_t *st,
                     int expect42)
{
    /* SCN_ONLY env: optional case-sensitive substring to limit phases */
    const char *only = getenv("SCN_ONLY");
    if (only && *only && !strstr(name, only)) {
        printf("\n========== %s ==========  (skipped: SCN_ONLY)\n", name);
        return 0;
    }
    int pfd[2];
    if (pipe(pfd) != 0) { n_checks++; VSIM_CHECK(0, "pipe failed"); return -1; }
    printf("\n========== %s ==========\n", name);
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]);
        fn(pfd[1]);
        _exit(99);                  /* phase fn never returns         */
    }
    close(pfd[1]);
    /* wait with a wall-clock bound */
    int status = -1, timed_out = 0;
    int64_t dl = wall_ms() + 240 * 1000;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        if (wall_ms() > dl) {
            timed_out = 1;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        usleep(100 * 1000);
    }
    if (timed_out)
        printf("[scn] %s: child hit 240 s wall limit (hung?)\n", name);
    else if (WIFSIGNALED(status))
        printf("[scn] %s: child died on signal %d%s\n", name,
               WTERMSIG(status),
               WCOREDUMP(status) ? " (core dumped)" : "");
    else if (WIFEXITED(status))
        printf("[scn] %s: child exit=%d\n", name, WEXITSTATUS(status));
    /* drain the state pipe */
    char sbuf[1024];
    ssize_t sn = read(pfd[0], sbuf, sizeof(sbuf) - 1);
    if (sn > 0) sbuf[sn] = 0; else sbuf[0] = 0;
    close(pfd[0]);
    parse_state(sbuf, st);

    n_checks++;
    if (expect42) {
        VSIM_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 42,
                   "reboot path did not exit 42 (esp_restart broken?)");
    } else {
        VSIM_CHECK(WIFEXITED(status),
                   "child crashed/signalled (segfault/assert/killed)");
    }
    if (st->have) {
        n_checks += st->checks;
        g_vsim_failures += st->fails;
    } else {
        n_checks++;
        VSIM_CHECK(0, "child produced no state line (early crash?)");
    }
    return status;
}

/* resolve "<...>/sim/build/<file>" from this file's own path */
static void sim_build_path(char *out, size_t n, const char *file)
{
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", __FILE__);   /* .../sim/tests/x.c */
    char *p = strrchr(tmp, '/');                  /* strip scn_storage.c */
    if (p) *p = 0;
    p = strrchr(tmp, '/');                        /* strip tests/        */
    if (p) *p = 0;
    snprintf(out, n, "%.3990s/build/%s", tmp, file);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    char img[4096], nvs[4096];
    sim_build_path(img, sizeof(img), "extflash.bin");
    sim_build_path(nvs, sizeof(nvs), "nvs.bin");
    setenv("VBFQ_IMAGE", img, 1);
    setenv("VSIM_NVS_PATH", nvs, 1);
    /* fresh slate for the scenario's cold boot */
    unlink(img);
    unlink(nvs);

    st_t s1 = {0}, s2 = {0}, s3 = {0}, s4 = {0}, s5 = {0};
    st_t s6 = {0}, s7 = {0};

    run_child("PH1 clean boot / streaming / INVALID_SIZE",
              ph_clean, &s1, 0);
    CHECK(s1.have, "PH1 no state");

    run_child("PH2 warm reboot (journal fast path + esp_restart)",
              ph_warm, &s2, 1);
    CHECK(s2.have, "PH2 no state");
    if (s1.have && s2.have) {
        CHECK(s2.nid >= s1.nid,
              "PH2 next_id regressed across reboot");
        CHECK(s2.evts >= s1.evts - 2,
              "PH2 events lost across reboot");
        CHECK(s2.tail == s1.tail || s2.used >= s1.used - 2,
              "PH2 ring geometry moved unexpectedly");
    }

    corrupt_newest_journal_slot();
    run_child("PH3 stale journal checkpoint", ph_slotcorrupt, &s3, 0);
    CHECK(s3.have, "PH3 no state");
    if (s2.have && s3.have) {
        CHECK(s3.nid >= s2.nid - 2,
              "PH3 next_id regressed vs warm boot");
        CHECK(s3.evts >= s2.evts - 2,
              "PH3 events lost vs warm boot");
    }

    /* Corrupt the sector the NEXT mount will treat as its append point:
     * find_append_point() resumes at the journal's head_sector, which can
     * differ from the selftest head (journal lags <= one sector-advance).
     * Corrupting s3.head only damages a mid-ring data sector — mount
     * never scans it and the "abandoned" warning can't fire. */
    {
        jslot_t jh = journal_newest();
        uint32_t tgt = jh.found ? jh.head
                                : (s3.have ? s3.head : 0u);
        printf("[scn] PH4 setup: journal head=%u selftest head=%u -> "
               "corrupting sector %u\n",
               jh.found ? jh.head : 0u, s3.have ? s3.head : 0u, tgt);
        if (tgt) {
            (void)vbfq_corrupt_sector(tgt * SECT_BYTES);
            (void)vbfq_flush();
        }
    }
    run_child("PH4 corrupted head sector", ph_headcorrupt, &s4, 0);
    CHECK(s4.have, "PH4 no state");
    if (s3.have && s4.have) {
        CHECK(s4.evts >= s3.evts - 4,
              "PH4 too many events lost to head-sector corruption");
    }

    corrupt_journals();
    plant_hostile_sectors(300u, s4.have ? (s4.nid > 2 ? s4.nid / 2 : 1)
                                       : 1u);
    run_child("PH5 journal loss -> full rescan", ph_rescan, &s5, 0);
    CHECK(s5.have, "PH5 no state");
    if (s4.have && s5.have) {
        CHECK(s5.evts >= s4.evts - 4,
              "PH5 events lost beyond corrupted sectors");
    }

    nvs_dump_frames(nvs, "nvs frames pre-PH6");
    /* wrap: craft a packed-ring journal behind the current ids */
    {
        jslot_t j = journal_newest();
        uint32_t seq = j.found ? j.seq + 10u : 1u;
        uint32_t nid = s5.have && s5.nid ? s5.nid : 1;
        craft_wrap_journal(seq, nid, 0, 0, 16380u);
    }
    run_child("PH6 ring wrap + drop-oldest", ph_wrap, &s6, 0);
    CHECK(s6.have, "PH6 no state");
    if (s6.have) {
        CHECK(s6.head < 16380u,
              "PH6 head never wrapped past ring end");
        /* drop-oldest evidence: the ESP_LOGD line is compiled out
         * (CONFIG_LOG_MAXIMUM_LEVEL_INFO), but the journaled `drops`
         * counter is authoritative. */
        jslot_t j = journal_newest();
        CHECK(j.found && j.drops > 0,
              "PH6 journal drops counter never moved (no drop-oldest)");
        printf("[scn] PH6 journal: seq=%u head=%u+%u tail=%u nid=%u "
               "appends=%u drops=%u used=%u | s6: head=%u+%u tail=%u "
               "nid=%u used=%u\n",
               j.seq, j.head, j.off, j.tail, j.nid, j.appends, j.drops,
               j.used, s6.head, s6.off, s6.tail, s6.nid, s6.used);
    }

    run_child("PH7 write-protect + rail interruptions",
              ph_wp_rail, &s7, 0);
    CHECK(s7.have, "PH7 no state");

    nvs_dump_frames(nvs, "nvs frames pre-PH8");
    nvs_append_badframe(nvs);
    nvs_dump_frames(nvs, "nvs frames post-badframe");
    st_t s8 = {0};
    run_child("PH8 NVS torn tail frame", ph_nvs_tail, &s8, 0);
    CHECK(s8.have, "PH8 no state");

    nvs_scramble(nvs);
    st_t s9 = {0};
    run_child("PH9 NVS scrambled -> defaults", ph_nvs_dead, &s9, 0);
    CHECK(s9.have, "PH9 no state");

    printf("\nSCN storage RESULT: checks=%d fails=%d\n",
           n_checks, g_vsim_failures);
    fflush(NULL);
    return g_vsim_failures;
}
