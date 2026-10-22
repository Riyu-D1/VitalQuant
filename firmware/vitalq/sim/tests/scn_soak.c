/* scn_soak — 24 h endurance / leak / staleness scenario for fix_v22 fw.
 *
 * Phases:
 *   1. clean boot @3900 mV, baseline status/sensors
 *   2. N x 1 h virtual soak; per-chunk probes: uptime monotonic, every
 *      producer's sample count increasing, heap-free trend
 *   3. IMU (0x6A) i2c_silence 10 min mid-soak -> SENSOR_FAULT churn ->
 *      restore -> recovery via the health task
 *   4. clock_jitter_ppm=200 segment (knob is currently a sim no-op — the
 *      scenario reports that instead of claiming a firmware result)
 *   5. log ring wrap -> ordering verified via vq_log_iterate() directly.
 *      The console 'logtail <n> flash' command is deliberately NOT run in
 *      this process: on the host heap shim, heap_caps_malloc returns a
 *      header-offset pointer, and cmd_logtail calls plain free() on it —
 *      a deterministic abort(). That is a SIM SHIM gap (free() is legal
 *      on caps blocks under real ESP-IDF); a sacrificial fork'ed child
 *      demonstrates it at the end without killing the soak.
 *   6. M cold reboots via fork+exec of this binary (a REAL fresh
 *      app_main, unlike vsim_reset() which leaves fw tasks/statics
 *      running). Each child boots on a private COPY of the soaked flash
 *      image — concurrent dual-writer ring corruption would be a harness
 *      artifact otherwise.
 *   7. final status/sensors/selftest decision_loop
 *
 * Telemetry: firmware ESP_LOG* is redirected to a file that the harness
 * scans incrementally — nonmono / INVALID_SIZE / SENSOR_FAULT /
 * decision cycles / mount lines are counted from the log stream.
 *
 * Roles (env SCN_SOAK_ROLE):
 *   (unset)      -> the soak driver (parent)
 *   bootprobe    -> fresh-process boot for the parent's reboot loop
 *   logtailcrash -> boots, runs 'logtail <n> flash', documents the abort
 */
#include "vsim.h"
#include "vboard.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "log_store.h"

#include <errno.h>
#include <sys/file.h>
#include <signal.h>
#include <execinfo.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ----------------------------- tunables ------------------------------ */
#define TIME_SCALE        300.0     /* virtual us per wall us            */
#define SOAK_HOURS        24
#define HOUR_US           (3600LL * 1000000LL)
#define MIN_US            (60LL * 1000000LL)
#define KILL_ADDR         0x6A      /* LSM6DSV80X IMU                    */
#define KILL_MINUTES      10
#define KILL_NAME         "lsm6dsv80x"
#define JITTER_PPM        200
#define REBOOTS           5
#define CMD_TMO_VMS       120000    /* 2 virtual min per command attempt */
#define CMD_TRIES         6
#define BOOTPROBE_TMO_S   240       /* wall seconds per child            */
static double s_scale = TIME_SCALE;
static int    s_hours = SOAK_HOURS;
static int    s_reboots = REBOOTS;

/* ----------------------------- console ------------------------------- */
static int  s_con_fd = -1;          /* saved real stdout                 */
static void con(const char *fmt, ...)
{
    if (s_con_fd < 0) { s_con_fd = 2; }
    char buf[1200];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) (void)!write(s_con_fd, buf, (size_t)n);
}

static int s_checks;
#define CHK(cond, ...) do { \
    s_checks++; \
    if (!(cond)) { g_vsim_failures++; \
        con("FAIL %s:%d: ", __FILE__, __LINE__); \
        con(__VA_ARGS__); con("\n"); } \
} while (0)
#define NOTE(...) do { con(__VA_ARGS__); con("\n"); } while (0)

/* ----------------------------- paths --------------------------------- */
static char s_builddir[512];
static char s_fwlog[600];
static void make_paths(void)
{
    /* __FILE__ = <root>/sim/tests/scn_soak.c (absolute via Makefile) */
    snprintf(s_builddir, sizeof s_builddir, "%s", __FILE__);
    char *p = strrchr(s_builddir, '/'); if (p) *p = 0;   /* tests/    */
    p = strrchr(s_builddir, '/');        if (p) *p = 0;   /* sim/      */
    strncat(s_builddir, "/build", sizeof(s_builddir) - strlen(s_builddir) - 1);
    snprintf(s_fwlog, sizeof s_fwlog, "%s/scn_soak_fw.log", s_builddir);
}

static int redirect_stdio(const char *path)
{
    fflush(NULL);
    if (s_con_fd < 0) s_con_fd = dup(STDOUT_FILENO);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > 2) close(fd);
    return fd;
}

/* ----------------------- firmware-log scanner ------------------------ */
typedef struct {
    long    off;                    /* read offset into the log file   */
    unsigned long nonmono;          /* summed nonmono= sightings       */
    unsigned long bad_ch;
    unsigned long invalid_size;     /* 'log append failed' occurrences */
    unsigned long logdrop;          /* 'log batch backlog' lines       */
    unsigned long dropped_s;        /* summed dropped-sample count     */
    unsigned long sensor_fault;
    unsigned long kill_faults;      /* SENSOR_FAULT for the killed dev */
    unsigned long recovered;        /* '<name> recovered — streaming'  */
    unsigned long restart_fail;
    unsigned long dec_scored;       /* 'score: prob'                   */
    unsigned long dec_escal;        /* 'escalation:'                   */
    unsigned long esc_nopersist;    /* escalations with persisted 0    */
    unsigned long dec_skip;         /* 'cycle skipped'                 */
    unsigned long led_nomem;        /* led_sched NO_MEM noise          */
    unsigned long grant_lost;
    unsigned long journal_bad;      /* 'journal unreadable'            */
    unsigned long full_rescan;
    unsigned long mounts;
    unsigned long prog_fail;        /* 'program failed' on flash       */
    unsigned long corrupt;          /* 'iterate: corrupt' sightings    */
    unsigned long psram_fail;       /* 'PSRAM alloc failed'            */
    unsigned long aborts;           /* abort/panic/assert lines        */
    char   last_mount[160];
    char   last_escal[160];
} fwstats_t;

static void scan_line(fwstats_t *st, const char *ln)
{
    const char *p;
    if ((p = strstr(ln, "nonmono=")))  st->nonmono += strtoul(p + 8, NULL, 10);
    if ((p = strstr(ln, "bad_ch=")))   st->bad_ch  += strtoul(p + 7, NULL, 10);
    if (strstr(ln, "log append failed"))        st->invalid_size++;
    if (strstr(ln, "log batch backlog")) {
        st->logdrop++;
        if ((p = strstr(ln, "+")))   st->dropped_s += strtoul(p + 1, NULL, 10);
    }
    if (strstr(ln, "SENSOR_FAULT")) {
        st->sensor_fault++;
        if (strstr(ln, "SENSOR_FAULT " KILL_NAME)) st->kill_faults++;
    }
    if (strstr(ln, "recovered — streaming") ||
        strstr(ln, "recovered - streaming"))    st->recovered++;
    if (strstr(ln, "restart failed"))           st->restart_fail++;
    if (strstr(ln, "score: prob"))              st->dec_scored++;
    if (strstr(ln, "escalation:")) {
        st->dec_escal++;
        if (strstr(ln, "persisted 0"))          st->esc_nopersist++;
        snprintf(st->last_escal, sizeof st->last_escal, "%s",
                 strchr(ln, ']') ? strchr(ln, ']') + 2 : ln);
    }
    if (strstr(ln, "cycle skipped"))            st->dec_skip++;
    if (strstr(ln, "led_sched submit: ESP_ERR_NO_MEM")) st->led_nomem++;
    if (strstr(ln, "grant lost"))               st->grant_lost++;
    if (strstr(ln, "journal unreadable"))       st->journal_bad++;
    if (strstr(ln, "full ring rescan"))         st->full_rescan++;
    if (strstr(ln, "program failed"))           st->prog_fail++;
    if ((p = strstr(ln, "iterate: ")))          st->corrupt += strtoul(p + 9, NULL, 10);
    if (strstr(ln, "PSRAM alloc failed"))       st->psram_fail++;
    if (strstr(ln, "abort") || strstr(ln, "ESP_ERROR_CHECK failed") ||
        strstr(ln, "assert"))                   st->aborts++;
    if (strstr(ln, "mounted: head=")) {
        st->mounts++;
        snprintf(st->last_mount, sizeof st->last_mount, "%s", ln);
    }
}

static void fw_scan(fwstats_t *st, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    if (lseek(fd, st->off, SEEK_SET) < 0) { close(fd); return; }
    static char buf[1 << 16];
    static char tail[512];
    size_t tailn = 0;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) break;
        st->off += n;
        char *s = buf, *end = buf + n;
        while (s < end) {
            char *nl = memchr(s, '\n', (size_t)(end - s));
            size_t len = nl ? (size_t)(nl - s) : (size_t)(end - s);
            if (len >= sizeof(tail) - tailn - 1)
                len = sizeof(tail) - tailn - 1;
            memcpy(tail + tailn, s, len); tailn += len; tail[tailn] = 0;
            if (nl) { scan_line(st, tail); tailn = 0; }
            s += (nl ? len + 1 : len);
        }
    }
    close(fd);
}

/* Crash forensics: a SEGV inside a firmware thread must be reported
 * with a backtrace instead of a bare signal death.                    */
static void segv_dump(int sig, siginfo_t *si, void *u)
{
    (void)u;
    char m[128];
    int n = snprintf(m, sizeof m,
                     "\n=== SCN-SOAK %s at %p — backtrace ===\n",
                     strsignal(sig), si->si_addr);
    if (s_con_fd >= 0) (void)!write(s_con_fd, m, n);
    (void)!write(STDERR_FILENO, m, n);
    void *bt[48];
    int nb = backtrace(bt, 48);
    backtrace_symbols_fd(bt, nb, STDERR_FILENO);
    backtrace_symbols_fd(bt, nb, s_con_fd >= 0 ? s_con_fd : 2);
    _exit(128 + sig);
}

/* ------------------------- command helpers --------------------------- */
static char s_out[1 << 17];
static unsigned long s_cmd_timeouts;

/* vsim_cmd with retries + expect-substring realign: a -3 under wall
 * load is transient, AND a timed-out command can leave a stale
 * ===CMD-END=== that the NEXT vsim_cmd then matches early. Resending
 * and requiring `expect` in the output makes each call consume at most
 * one queued reply, so the stream self-realigns within a few tries.
 * NOTE: vsim_cmd copies partial output even on -3 — if the expected
 * text already arrived, the command finished; only the marker lagged.*/
static int cmd(const char *c, const char *expect, int tmo_vms)
{
    int rc = -1;
    for (int t = 0; t < CMD_TRIES; t++) {
        s_out[0] = 0;
        rc = vsim_cmd(c, s_out, sizeof s_out, tmo_vms);
        if (expect && strstr(s_out, expect)) return (rc < 0) ? 0 : rc;
        if (rc >= 0 && !expect) return rc;
        if (rc == -1 || rc == -2) {                       /* pipe dead   */
            s_cmd_timeouts++;
            return rc;
        }
        s_cmd_timeouts++;
        usleep(250 * 1000);              /* wall settle — drain stale   */
        vsim_advance_us(300 * 1000);     /* virtual nudge               */
    }
    return -100;                         /* never got expected output   */
}

/* Boot-phase console warm-up: the REPL can be starved for seconds of
 * wall time while boot work floods the box; poll until it answers.    */
static int wait_console(void)
{
    for (int t = 0; t < 40; t++) {
        s_out[0] = 0;
        int rc = vsim_cmd("help", s_out, sizeof s_out, 60000);
        if (strstr(s_out, "logtail") || (rc >= 0 && strstr(s_out, "help")))
            return 0;
        usleep(250 * 1000);
        vsim_advance_us(200 * 1000);
    }
    return -1;
}

/* last-occurrence match — a stale CMD-END from a timed-out command can
 * drag older output into this capture; newest values win.            */
static const char *last_of(const char *key)
{
    const char *hit = NULL, *p = s_out;
    while ((p = strstr(p, key))) { hit = p; p += strlen(key); }
    return hit;
}
static long long parse_i64_after(const char *key)
{
    const char *p = last_of(key);
    return p ? strtoll(p + strlen(key), NULL, 10) : -1;
}
static unsigned long parse_ul_after(const char *key)
{
    const char *p = last_of(key);
    return p ? strtoul(p + strlen(key), NULL, 10) : 0;
}

/* status: returns uptime s; -1 if the command never produced output. */
static long long probe_status(unsigned long *heap_i, unsigned long *log_used,
                              unsigned long *log_tot)
{
    if (heap_i)   *heap_i   = 0;
    if (log_used) *log_used = 0;
    if (log_tot)  *log_tot  = 0;
    int rc = cmd("status", "uptime", CMD_TMO_VMS);
    if (rc < 0 || !strstr(s_out, "uptime")) return -1;
    long long up = parse_i64_after("uptime ");
    if (heap_i) {
        const char *p = last_of("heap");
        if (p) { const char *c = strchr(p, ':'); if (c) *heap_i = strtoul(c + 1, NULL, 10); }
    }
    if (log_used) {
        const char *p = last_of("log_store");
        if (p) { const char *c = strchr(p, ':'); if (c) *log_used = strtoul(c + 1, NULL, 10); }
    }
    if (log_tot) {
        const char *p = last_of("log_store");
        if (p) { const char *s2 = strchr(p, '/'); if (s2) *log_tot = strtoul(s2 + 1, NULL, 10); }
    }
    return up;
}

/* ------------------------- sensor table ------------------------------ */
#define MAX_SENS 32
typedef struct {
    char name[20];
    float rate;
    unsigned long samples, errs;
    int healthy;                        /* 1 ok / 0 none / -1 FAULT    */
    int dead_chunks;                    /* consecutive 0-sample chunks */
} sens_t;
static sens_t s_prev[MAX_SENS], s_cur[MAX_SENS];
static int    s_n_prev, s_n_cur;

/* Parse `sensors` output into s_cur; returns #rows, <0 on cmd fail. */
static int probe_sensors(void)
{
    int rc = cmd("sensors", "rate_hz", CMD_TMO_VMS);
    if (rc < 0) { s_n_cur = -1; return -1; }
    /* find the LAST header line (stale captures may repeat the table) */
    char *hdr = NULL, *p = s_out;
    while ((p = strstr(p, "rate_hz"))) { hdr = p; p += 7; }
    if (!hdr) { s_n_cur = 0; return 0; }
    s_n_cur = 0;
    char *save = NULL;
    for (char *ln = strtok_r(hdr, "\n", &save); ln && s_n_cur < MAX_SENS;
         ln = strtok_r(NULL, "\n", &save)) {
        char nm[20]; float rate; unsigned long sm, er; char lastt[24], hl[12];
        int k = sscanf(ln, "%19s %f %lu %lu %23s %11s",
                       nm, &rate, &sm, &er, lastt, hl);
        if (k == 6 && strcmp(nm, "name") != 0 && strcmp(nm, "last_t_ms") != 0 &&
            (lastt[0] == '(' || (lastt[0] >= '0' && lastt[0] <= '9') ||
             lastt[0] == '-')) {
            sens_t *pr = NULL;
            for (int j = 0; j < s_n_prev; j++)
                if (strcmp(s_prev[j].name, nm) == 0) { pr = &s_prev[j]; break; }
            snprintf(s_cur[s_n_cur].name, sizeof s_cur[0].name, "%s", nm);
            s_cur[s_n_cur].rate     = rate;
            s_cur[s_n_cur].samples  = sm;
            s_cur[s_n_cur].errs     = er;
            s_cur[s_n_cur].healthy  = (strcmp(hl, "ok") == 0) ? 1
                                    : (strcmp(hl, "FAULT") == 0 ? -1 : 0);
            s_cur[s_n_cur].dead_chunks =
                (sm == 0 && pr) ? pr->dead_chunks + 1 : (sm == 0 ? 1 : 0);
            s_n_cur++;
        }
    }
    return s_n_cur;
}

static const sens_t *sens_find(const sens_t *t, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (strcmp(t[i].name, name) == 0) return &t[i];
    return NULL;
}

/* every previously-producing sensor must still be increasing.
 * exempt: one expected-stalled name (kill window). FAULT sensors are
 * skipped — a stalled-and-faulted sensor is the health task doing its
 * job, not a stuck stream. Dead-on-arrival producers (rate>0, never a
 * sample) are tracked separately via dead_chunks.                     */
static int check_growth(const char *exempt, int verbose, int *grown_out)
{
    int stuck = 0, grown = 0, zero = 0;
    for (int i = 0; i < s_n_cur; i++) {
        const sens_t *pr = sens_find(s_prev, s_n_prev, s_cur[i].name);
        if (!pr) continue;
        if (pr->samples == 0 && s_cur[i].samples == 0) { zero++; continue; }
        if (s_cur[i].samples > pr->samples) { grown++; continue; }
        if (s_cur[i].healthy != 1) continue;     /* FAULT churn, expected */
        if (!exempt || strcmp(s_cur[i].name, exempt) != 0) {
            stuck++;
            if (verbose) NOTE("  STUCK producer: %s (%lu -> %lu)",
                              s_cur[i].name, pr->samples, s_cur[i].samples);
        }
    }
    if (verbose) NOTE("  sensors: %d growing, %d stuck, %d never-produced",
                      grown, stuck, zero);
    if (grown_out) *grown_out = grown;
    return stuck;
}

static void sens_commit(void)
{
    memcpy(s_prev, s_cur, sizeof s_cur);
    s_n_prev = s_n_cur < 0 ? 0 : s_n_cur;
}

/* ------------------------------ RSS ---------------------------------- */
static long vmrss_kb(void)
{
    int fd = open("/proc/self/status", O_RDONLY);
    if (fd < 0) return -1;
    char b[4096]; ssize_t n = read(fd, b, sizeof b - 1); close(fd);
    if (n <= 0) return -1;
    b[n] = 0;
    char *p = strstr(b, "VmRSS:");
    return p ? strtol(p + 6, NULL, 10) : -1;
}

/* --------------------- log ring verification ------------------------- */
typedef struct {
    unsigned long n_rec;            /* records of the asked type       */
    unsigned long total_rec;        /* all records                     */
    int           monotonic;        /* ids strictly increasing         */
    uint32_t      first_id, last_id;
    int64_t       first_t, last_t;
} itctx_t;

static bool it_cb(const vq_log_hdr_t *h, const uint8_t *payload, void *v)
{
    (void)payload;
    itctx_t *c = v;
    if (c->total_rec > 0 && h->id <= c->last_id) c->monotonic = 0;
    if (c->total_rec == 0) { c->first_id = h->id; c->first_t = h->t_us; }
    c->last_id = h->id; c->last_t = h->t_us;
    c->total_rec++;
    if (h->type == VQ_LOG_EVENT) c->n_rec++;
    return true;
}

/* Walk the whole ring (oldest->newest) — the authoritative wrap/order
 * check that does NOT go through the crash-prone console path.        */
static int log_walk(itctx_t *c)
{
    memset(c, 0, sizeof *c);
    c->monotonic = 1;
    return (int)vq_log_iterate(0 /* all types */, it_cb, c);
}

/* --------------------------- child spawner --------------------------- */
static int run_child(const char *role, const char *bootimg,
                     const char *logpath)
{
    pid_t pid = fork();
    if (pid == 0) {
        setenv("SCN_SOAK_ROLE", role, 1);
        if (bootimg) setenv("VBFQ_IMAGE", bootimg, 1);
        if (bootimg) {                       /* private NVS per child  */
            char nv[700];
            snprintf(nv, sizeof nv, "%s.nvs", bootimg);
            setenv("VSIM_NVS_PATH", nv, 1);
        }
        if (logpath) setenv("SCN_SOAK_BOOTLOG", logpath, 1);
        execl("/proc/self/exe", "scn_soak", NULL);
        _exit(127);
    }
    if (pid <= 0) return -1;
    int stt = 0; pid_t w = 0;
    long t0 = (long)time(NULL);
    for (;;) {
        w = waitpid(pid, &stt, WNOHANG);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) break;      /* real wait error     */
        if ((long)time(NULL) - t0 > BOOTPROBE_TMO_S) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &stt, 0) < 0 && errno == EINTR) {}
            w = pid;                                   /* report 137   */
            break;
        }
        usleep(100000);
    }
    if (w != pid) return -2;
    if (WIFEXITED(stt))  return WEXITSTATUS(stt);
    if (WIFSIGNALED(stt)) return 128 + WTERMSIG(stt);
    return -3;
}

static int copy_file(const char *src, const char *dst)
{
    int fi = open(src, O_RDONLY);
    if (fi < 0) return -1;
    int fo = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fo < 0) { close(fi); return -1; }
    static char buf[1 << 20];
    for (;;) {
        ssize_t n = read(fi, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fi); close(fo); return n; }
        char *w = buf;
        while (n > 0) {
            ssize_t k = write(fo, w, n);
            if (k < 0 && errno == EINTR) continue;
            if (k <= 0) goto err;
            n -= k; w += k;
        }
    }
err:
    close(fi); close(fo); return -1;
}

/* ============================ bootprobe =============================== */
/* Fresh process: real cold boot on a private copy of the soaked flash
 * image. Writes its own probe log; exit code = number of failed checks.*/
static int run_bootprobe(void)
{
    const char *bl = getenv("SCN_SOAK_BOOTLOG");
    if (bl) redirect_stdio(bl);
    int fails = 0;

    vsim_boot();
    vboard_clock_set_scale(150.0);
    vboard_clock_sleep_us(8 * 1000000LL);        /* boot settle */
    wait_console();

    unsigned long heap0, lu, lt;
    long long up = probe_status(&heap0, &lu, &lt);
    if (up < 0) { fails++; printf("PROBE status cmd failed\n"); }

    int n1 = probe_sensors();
    (void)cmd("status", "uptime", CMD_TMO_VMS);   /* flush stale replies */
    vboard_clock_sleep_us(8 * 1000000LL);
    memcpy(s_prev, s_cur, sizeof s_cur); s_n_prev = n1 > 0 ? n1 : 0;
    int n2 = probe_sensors();
    if (n1 <= 0 || n2 <= 0) { fails++; printf("PROBE sensors cmd failed\n"); }
    int grew = 0;
    if (n2 > 0) (void)check_growth(NULL, 0, &grew);
    /* only ~4 producers exist even when the suite is healthy — the
     * static-template-sink firmware bug starves the rest (see report) */
    if (n2 > 0 && grew < 2) { fails++; printf("PROBE only %d growing\n", grew); }

    /* log mount + walk — direct API, not the crash-prone console cmd   */
    itctx_t it;
    int we = log_walk(&it);
    if (we != 0) { fails++; printf("PROBE iterate rc=%d\n", we); }
    if (it.total_rec == 0) { fails++; printf("PROBE ring empty\n"); }

    /* one decision cycle on demand — loop alive after reboot           */
    int rc = cmd("score", "prob", CMD_TMO_VMS);
    if (rc < 0) { fails++; printf("PROBE score rc=%d\n", rc); }

    printf("PROBE_RESULT fails=%d sensors=%d growing=%d uptime=%lld "
           "recs=%lu order=%d\n", fails, n2, grew, up, it.total_rec,
           it.monotonic);
    /* fflush(stdout) only — fflush(NULL) would block on stdin's FILE
     * lock: the REPL thread holds it inside fgets(stdin)->read().     */
    fflush(stdout); fflush(stderr);
    _exit(fails);       /* skip atexit — detached fw threads may hang  */
}

/* ========================= logtail crash probe ======================== */
/* Demonstrates the host-shim free() bug via the real console command.  */
static int run_logtailcrash(void)
{
    const char *bl = getenv("SCN_SOAK_BOOTLOG");
    if (bl) redirect_stdio(bl);
    vsim_boot();
    vboard_clock_set_scale(150.0);
    vboard_clock_sleep_us(6 * 1000000LL);
    printf("PROBE about to run 'logtail 20 flash' — expect free() abort\n");
    fflush(stdout); fflush(stderr);   /* never fflush(NULL) — stdin lock */
    char big[1 << 16];
    /* generous deadline: vq_log_iterate can wait seconds of wall time
     * behind the pipeline's append mutex before the free() path runs */
    int rc = vsim_cmd("logtail 20 flash", big, sizeof big, 4000000);
    printf("PROBE logtail rc=%d (no crash — shim must interpose free)\n%s\n",
           rc, big);
    fflush(stdout); fflush(stderr);
    _exit(0);
}

/* ============================== main ================================== */
int main(void)
{
    const char *role = getenv("SCN_SOAK_ROLE");
    if (role && strcmp(role, "bootprobe") == 0)    return run_bootprobe();
    if (role && strcmp(role, "logtailcrash") == 0) return run_logtailcrash();

    make_paths();
    signal(SIGPIPE, SIG_IGN);            /* survive "| head" harnesses */
    {   struct sigaction sa = {0};
        sa.sa_sigaction = segv_dump;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS,  &sa, NULL); }

    /* Instances race on sim/build/scn_soak_* (image unlink+create).
     * Serialize on a lock file rather than corrupt a sibling's run.    */
    {   char lk[660];
        snprintf(lk, sizeof lk, "%s/scn_soak.lock", s_builddir);
        int lfd = open(lk, O_WRONLY | O_CREAT, 0644);
        if (lfd >= 0 && flock(lfd, LOCK_EX) < 0)
            perror("flock");
    }

    /* optional env overrides (default = full spec'd soak) */
    { const char *e = getenv("SCN_SOAK_SCALE");   if (e) s_scale   = atof(e); }
    { const char *e = getenv("SCN_SOAK_HOURS");   if (e) s_hours   = atoi(e); }
    { const char *e = getenv("SCN_SOAK_REBOOTS"); if (e) s_reboots = atoi(e); }
    if (s_scale <= 0) s_scale = TIME_SCALE;
    if (s_hours <= 0) s_hours = SOAK_HOURS;

    /* dedicated board images so parallel scenario runs can't collide */
    char img[600], nvs[600], bootlog[640], bootimg[640];
    snprintf(img, sizeof img, "%s/scn_soak_flash.bin", s_builddir);
    snprintf(nvs, sizeof nvs, "%s/scn_soak_nvs.bin", s_builddir);
    setenv("VBFQ_IMAGE", img, 1);
    setenv("VSIM_NVS_PATH", nvs, 1);
    unlink(img); unlink(nvs); unlink(s_fwlog);

    redirect_stdio(s_fwlog);
    con("SCN soak start — build=%s scale=%.0fx hours=%d\n",
        s_builddir, s_scale, s_hours);

    fwstats_t fw = {0};

    /* ---------------- 1. boot + baseline ---------------------------- */
    vboard_vbat_set_mv(3900);
    vsim_boot();
    vboard_clock_set_scale(s_scale);
    vsim_advance_us(6 * 1000000LL);
    CHK(wait_console() == 0, "console REPL never answered at boot");
    fw_scan(&fw, s_fwlog);

    unsigned long heap0 = 0, heap6 = 0, heap12 = 0, heap24 = 0;
    unsigned long spi0 = 0, spi6 = 0, spi12 = 0, spi24 = 0;
    unsigned long lu, lt;
    long long up0 = probe_status(&heap0, &lu, &lt);
    CHK(up0 >= 0, "status command failed at baseline");
    long long up_prev = up0;
    CHK(last_of("log_store") && !strstr(last_of("log_store"), "absent"),
        "log_store absent at baseline");
    CHK(last_of("vcell=3.9") != NULL, "vcell ~= 3.9 V expected");
    NOTE("baseline: uptime=%lld heap_int=%lu spiram=%lu log=%lu/%lu rss=%ld",
         up0, heap0, spi0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
         lu, lt, vmrss_kb());
    unsigned long spiram_min0 = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

    CHK(probe_sensors() > 10, "expected >10 sensors, got %d", s_n_cur);
    int base_healthy = 0, base_prod = 0;
    for (int i = 0; i < s_n_cur; i++) {
        if (s_cur[i].healthy == 1) base_healthy++;
        if (s_cur[i].samples > 0) base_prod++;
    }
    NOTE("baseline sensors: %d rows, %d healthy, %d producing",
         s_n_cur, base_healthy, base_prod);
    for (int i = 0; i < s_n_cur; i++)
        NOTE("  %-16s samples=%-10lu err=%-6lu health=%d",
             s_cur[i].name, s_cur[i].samples, s_cur[i].errs, s_cur[i].healthy);
    CHK(base_healthy >= 12, "too few healthy sensors at baseline: %d",
        base_healthy);
    sens_commit();

    /* quiet the known noise + most INFO chatter so the 24 h log stays
     * scannable and cheap to produce; counters already captured above. */
    esp_log_level_set_default(ESP_LOG_WARN);
    esp_log_level_set("vq_pipe", ESP_LOG_INFO);   /* recovered lines     */
    esp_log_level_set("vq_dec",  ESP_LOG_INFO);   /* score/cycle-skip    */
    esp_log_level_set("vq_log",  ESP_LOG_INFO);   /* mounted:            */
    esp_log_level_set("as7341", ESP_LOG_ERROR);
    esp_log_level_set("afe4900", ESP_LOG_ERROR);
    { const char *dt = getenv("SCN_SOAK_DEBUG_TAG");
      if (dt && *dt) esp_log_level_set(dt, ESP_LOG_DEBUG); }

    /* ---------------- 2-4. the soak --------------------------------- */
    int kill_done = 0, jitter_done = 0;
    unsigned long kill_base = 0;       /* imu samples at silence start */
    unsigned long heap_mark[3] = {0, 0, 0}, spi_mark[3] = {0, 0, 0};
    long rss0 = vmrss_kb();
    /* phase anchors scale with soak length; defaults hit spec'd hours */
    int kill_h  = (s_hours >= 4) ? (s_hours * 3) / 8 : -1;   /* h9  @24h */
    int jit_on  = (s_hours * 7) / 12;                        /* h14 @24h */
    int jit_off = jit_on + 3;                                /* h17 @24h */
    int h_m1 = s_hours / 4, h_m2 = s_hours / 2, h_m3 = s_hours;
    int stuck_total = 0;
    char dead_names[512] = {0};    /* producers that never delivered   */
    int  dead_reported[MAX_SENS] = {0};

    for (int h = 1; h <= s_hours; h++) {
        /* kill the IMU for KILL_MINUTES part-way through the soak */
        if (h == kill_h && !kill_done) {
            NOTE("[h%02d] i2c_silence 0x%02X for %d min", h, KILL_ADDR,
                 KILL_MINUTES);
            unsigned long f0 = fw.kill_faults;
            {   const sens_t *im = sens_find(s_cur, s_n_cur, KILL_NAME);
                if (im) kill_base = im->samples; }
            g_vb_faults.i2c_silence_addr = KILL_ADDR;
            for (int m = 0; m < KILL_MINUTES; m++)
                vsim_advance_us(MIN_US);
            g_vb_faults.i2c_silence_addr = 0;
            fw_scan(&fw, s_fwlog);
            kill_done = 1;
            NOTE("  silence window: +%lu %s faults, +%lu restart-fail",
                 fw.kill_faults - f0, KILL_NAME, fw.restart_fail);
            CHK(fw.kill_faults > f0,
                "no SENSOR_FAULT for %s during 10-min device kill",
                KILL_NAME);
            vsim_advance_us(HOUR_US - KILL_MINUTES * MIN_US);
        } else {
            vsim_advance_us(HOUR_US);
        }

        /* clock-jitter segment (knob is a no-op in the sim — reported) */
        if (h == jit_on && !jitter_done) {
            g_vb_faults.clock_jitter_ppm = JITTER_PPM;
            jitter_done = 1;
            NOTE("[h%02d] clock_jitter_ppm=%d on", h, JITTER_PPM);
        }
        if (h == jit_off && jitter_done == 1) {
            g_vb_faults.clock_jitter_ppm = 0;
            jitter_done = 2;
            NOTE("[h%02d] clock_jitter_ppm off", h);
        }

        fw_scan(&fw, s_fwlog);

        /* per-chunk probes */
        const char *exempt = (kill_done && h <= kill_h + 1) ? KILL_NAME : NULL;
        unsigned long heapi = 0, lu2 = 0, lt2 = 0;
        long long up = probe_status(&heapi, &lu2, &lt2);
        CHK(up > up_prev, "uptime not monotonic: %lld -> %lld", up_prev, up);
        if (up > up_prev) up_prev = up;
        int n = probe_sensors();
        CHK(n > 10, "sensors table shrank to %d rows at h%02d", n, h);
        int stuck = (n > 0) ? check_growth(exempt,
                                 h == s_hours || (h % 6) == 0, NULL)
                          : 0;
        CHK(stuck == 0, "%d producers stuck at h%02d", stuck, h);
        stuck_total += stuck;
        /* dead-on-arrival: rate>0, never a sample after several chunks */
        if (n > 0) {
            for (int i2 = 0; i2 < n && i2 < MAX_SENS; i2++) {
                if (s_cur[i2].rate > 0.05f && s_cur[i2].samples == 0 &&
                    s_cur[i2].dead_chunks >= 4 && !dead_reported[i2]) {
                    dead_reported[i2] = 1;
                    if (dead_names[0]) strncat(dead_names, ",",
                                    sizeof(dead_names)-strlen(dead_names)-1);
                    strncat(dead_names, s_cur[i2].name,
                            sizeof(dead_names)-strlen(dead_names)-1);
                    NOTE("[h%02d] DEAD-PRODUCER %s rate=%.1f health=%d "
                         "(0 samples since baseline)", h, s_cur[i2].name,
                         (double)s_cur[i2].rate, s_cur[i2].healthy);
                }
            }
            sens_commit();
        }

        if (h == h_m1) { heap_mark[0] = heapi; spi_mark[0] =
                         heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
        if (h == h_m2) { heap_mark[1] = heapi; spi_mark[1] =
                         heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
        if (h == h_m3) { heap_mark[2] = heapi; spi_mark[2] =
                         heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

        if ((h % 3) == 0 || h == s_hours) {
            NOTE("[h%02d] up=%lld heap=%lu spi=%lu log=%luKB "
                 "nonmono=%lu invsz=%lu escNP=%lu fault=%lu dec=%lu/%lu/%lu "
                 "rss=%ld tmo=%lu", h, up, heapi,
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM), lu2 / 1024,
                 fw.nonmono, fw.invalid_size, fw.esc_nopersist,
                 fw.sensor_fault, fw.dec_scored, fw.dec_escal, fw.dec_skip,
                 vmrss_kb(), s_cmd_timeouts);
        }

        /* IMU recovery check — one chunk after restore */
        if (h == kill_h + 1 && kill_done && n > 0) {
            const sens_t *imu = sens_find(s_cur, s_n_cur, KILL_NAME);
            /* health flips during retry churn — accept health==1 OR a
             * resumed (growing) sample counter as recovery evidence   */
            CHK(imu && (imu->healthy == 1 || imu->samples > kill_base),
                KILL_NAME " not recovered after kill+restore "
                "(health=%d samples=%lu base=%lu)",
                imu ? imu->healthy : -9, imu ? imu->samples : 0,
                kill_base);
            CHK(fw.recovered > 0,
                "no 'recovered — streaming' after kill");
        }
    }
    heap6  = heap_mark[0] ? heap_mark[0] : heap0;
    heap12 = heap_mark[1] ? heap_mark[1] : heap0;
    heap24 = heap_mark[2] ? heap_mark[2] : heap0;
    spi6   = spi_mark[0]; spi12 = spi_mark[1]; spi24 = spi_mark[2];

    /* ---------------- 3. leak verdict -------------------------------- */
    NOTE("heap-int trend: boot=%lu %dh=%lu %dh=%lu %dh=%lu (B)",
         heap0, h_m1, heap6, h_m2, heap12, h_m3, heap24);
    NOTE("spiram   trend: boot=%lu %dh=%lu %dh=%lu %dh=%lu (B)",
         spi0, h_m1, spi6, h_m2, spi12, h_m3, spi24);
    NOTE("spiram min-free watermark: %lu B (start %lu)",
         heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM), spiram_min0);
    long rss_end = vmrss_kb();
    NOTE("rss: start=%ldKB end=%ldKB", rss0, rss_end);
    /* continuous decline = leak candidate; lazy first-use allocs make
     * boot->mid dips legitimate — judge the LAST segment hardest.      */
    CHK(!(heap6 < heap0 && heap12 < heap6 && heap24 < heap12 &&
          (heap0 - heap24) > 8192),
        "internal heap declined monotonically %lu -> %lu B", heap0, heap24);
    CHK(heap24 + 65536 >= heap0,
        "internal heap lost >64KB over soak (%lu -> %lu)", heap0, heap24);
    if (spi12 && spi24) {
        CHK(spi24 + 262144 >= spi12,
            "SPIRAM still falling late in soak (%dh=%lu -> %dh=%lu)",
            h_m2, spi12, h_m3, spi24);
        CHK(spi24 > 262144,
            "SPIRAM nearly exhausted at soak end (%lu B free)", spi24);
    }
    CHK(rss_end <= rss0 + 200 * 1024,
        "VmRSS grew >200MB (%ld -> %ld KB)", rss0, rss_end);

    /* ---------------- 5. log ring wrapped + ordered ------------------ */
    {
        itctx_t it;
        int we = log_walk(&it);
        CHK(we == 0, "vq_log_iterate rc=%d at soak end", we);
        CHK(it.total_rec > 0, "log ring holds no records");
        CHK(it.monotonic, "record ids not monotonic oldest->newest");
        NOTE("log ring: %lu records (%lu EVENT), id %lu -> %lu, "
             "t %lld -> %lld ms%s",
             it.total_rec, it.n_rec,
             (unsigned long)it.first_id, (unsigned long)it.last_id,
             (long long)(it.first_t / 1000), (long long)(it.last_t / 1000),
             it.monotonic ? "" : "  <OUT-OF-ORDER>");
        /* wrap evidence: ring id range exceeds a full-erase capacity or
         * used bytes == ring capacity means rotation has happened      */
        unsigned long lu2 = 0, lt2 = 0;
        (void)probe_status(NULL, &lu2, &lt2);
        NOTE("log_store used=%lu/%lu B", lu2, lt2);
        if (lt2 && s_hours >= 20)
            CHK(lu2 > lt2 / 2, "ring nowhere near wrap after %dh",
                s_hours);
        else if (lt2)
            NOTE("short soak: wrap check deferred "
                 "(%lu/%lu B used)", lu2, lt2);
    }
    CHK(fw.prog_fail == 0, "%lu flash program failures", fw.prog_fail);
    CHK(fw.psram_fail == 0, "%lu 'PSRAM alloc failed' during soak",
        fw.psram_fail);

    /* ---------------- 5b. decision evidence -------------------------- */
    NOTE("decisions: scored=%lu escalated=%lu skipped=%lu "
         "(esc persisted-0=%lu, target cycles ~%d)",
         fw.dec_scored, fw.dec_escal, fw.dec_skip, fw.esc_nopersist,
         s_hours * 20);
    CHK(fw.dec_scored + fw.dec_escal + fw.dec_skip >=
            (unsigned long)(s_hours * 15),
        "decision loop stalled: only %lu cycles in %dh",
        fw.dec_scored + fw.dec_escal + fw.dec_skip, s_hours);
    if (fw.dec_escal && fw.esc_nopersist == fw.dec_escal) {
        NOTE("NOTE: EVERY escalation failed durable persist "
             "(blob > VQ_LOG_MAX_PAYLOAD) — invalid-size drop is total");
    }

    /* ---------------- 6. reboot cycles ------------------------------- */
    for (int i = 0; i < s_reboots; i++) {
        snprintf(bootimg, sizeof bootimg, "%s/scn_soak_flash_boot%d.bin",
                 s_builddir, i);
        snprintf(bootlog, sizeof bootlog, "%s/scn_soak_boot%d.log",
                 s_builddir, i);
        unlink(bootlog);
        CHK(copy_file(img, bootimg) == 0, "boot %d: flash copy failed", i);
        NOTE("reboot %d/%d (cold fork+exec on a snapshot of soaked flash)",
             i + 1, s_reboots);
        int crc = run_child("bootprobe", bootimg, bootlog);
        CHK(crc == 0, "bootprobe %d failed (rc=%d)", i, crc);
        /* mount forensics: journal must be readable — no rescan        */
        fwstats_t bf = {0};
        fw_scan(&bf, bootlog);
        CHK(bf.mounts > 0, "boot %d: no 'mounted:' line", i);
        CHK(bf.journal_bad == 0 && bf.full_rescan == 0,
            "boot %d: journal unreadable/full rescan (mount degraded)", i);
        if (bf.mounts) NOTE("  %s", bf.last_mount);
        if (bf.corrupt)  NOTE("  boot %d: %lu corrupt records skipped",
                              i, bf.corrupt);
        unlink(bootimg);
    }

    /* ---------------- 6b. logtail crash documentation ---------------- */
    if (s_reboots > 0) {
        snprintf(bootimg, sizeof bootimg, "%s/scn_soak_flash_ltc.bin",
                 s_builddir);
        snprintf(bootlog, sizeof bootlog, "%s/scn_soak_ltc.log",
                 s_builddir);
        unlink(bootlog);
        (void)copy_file(img, bootimg);
        int crc = run_child("logtailcrash", bootimg, bootlog);
        /* classify from the log too: the abort may strike the console
         * task before the marker, giving a bare timeout on the wire   */
        int saw_abort = 0, saw_recs = 0;
        {   FILE *lf = fopen(bootlog, "r");
            if (lf) { char lb[512];
                      while (fgets(lb, sizeof lb, lf)) {
                          if (strstr(lb, "invalid pointer") ||
                              strstr(lb, "free()") ||
                              strstr(lb, "SIGABRT") ||
                              strstr(lb, "Aborted")) saw_abort = 1;
                          if (strstr(lb, "stored events") ||
                              strstr(lb, "(binary payload)")) saw_recs = 1;
                      }
                      fclose(lf); }
        }
        NOTE("logtail-flash child rc=%d %s (abort_evidence=%d recs=%d)",
             crc,
             (crc == 134 || crc == 6 || crc == 139 || crc == 11 ||
              saw_abort)
                 ? "SIGABRT/SEGV — free() on caps-heap ptr, host-shim "
                   "gap; legal on real ESP-IDF"
             : (saw_recs ? "completed without crash"
                         : "timed out waiting on log mutex — inconclusive"),
             saw_abort, saw_recs);
        CHK(crc == 134 || crc == 6 || crc == 139 || crc == 11 ||
            crc == 0 || saw_abort,
            "logtail child died oddly (rc=%d)", crc);
        unlink(bootimg);
    }

    /* ---------------- 7. final checks -------------------------------- */
    CHK(dead_names[0] == 0,
        "producers never delivered a single sample: %s", dead_names);
    {
        unsigned long heapi = 0, lu2 = 0, lt2 = 0;
        long long up = probe_status(&heapi, &lu2, &lt2);
        CHK(up > 0, "final status failed");
        int n = probe_sensors();
        CHK(n > 10, "final sensors rows=%d", n);
        int ok = 0;
        for (int i2 = 0; i2 < n; i2++) if (s_cur[i2].healthy == 1) ok++;
        CHK(ok >= 12, "final healthy sensors=%d", ok);
        for (int i2 = 0; i2 < n; i2++)
            if (s_cur[i2].samples == 0 && s_cur[i2].healthy != -1)
                NOTE("  never-produced: %s (health %d, err %lu)",
                     s_cur[i2].name, s_cur[i2].healthy, s_cur[i2].errs);
        int rc = cmd("selftest decision_loop", "decision_loop", CMD_TMO_VMS);
        CHK(rc == 0, "selftest decision_loop rc=%d", rc);
        CHK(strstr(s_out, "PASS") != NULL,
            "selftest decision_loop did not pass: %.200s", s_out);
    }

    fw_scan(&fw, s_fwlog);
    NOTE("--- fw telemetry (%dh + %d reboots) ---", s_hours, s_reboots);
    NOTE("nonmono=%lu bad_ch=%lu invalid_size_appends=%lu "
         "logdrop_lines=%lu (+%lu samples) corrupt=%lu",
         fw.nonmono, fw.bad_ch, fw.invalid_size, fw.logdrop,
         fw.dropped_s, fw.corrupt);
    NOTE("sensor_fault=%lu recovered=%lu restart_fail=%lu "
         "stuck_events=%d",
         fw.sensor_fault, fw.recovered, fw.restart_fail, stuck_total);
    NOTE("decisions scored=%lu escal=%lu (persisted-0=%lu) skip=%lu | "
         "noise led_nomem=%lu grant_lost=%lu | aborts=%lu | "
         "cmd_timeouts=%lu",
         fw.dec_scored, fw.dec_escal, fw.esc_nopersist, fw.dec_skip,
         fw.led_nomem, fw.grant_lost, fw.aborts, s_cmd_timeouts);
    NOTE("last escalation: %.140s", fw.last_escal);
    NOTE("SCN soak RESULT: checks=%d fails=%d", s_checks, g_vsim_failures);
    /* _exit, not return: exit() runs stdio teardown which deadlocks on
     * stdin's FILE lock (the REPL thread holds it in fgets->read).     */
    fflush(stdout); fflush(stderr);
    _exit(g_vsim_failures);
}
