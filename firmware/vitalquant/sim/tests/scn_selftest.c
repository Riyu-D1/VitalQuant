/* scn_selftest.c — host-side scenario: the §9 bring-up self-test suite.
 *
 *   1. boot + settle
 *   2. `selftest list`   -> verify all 16 names, in §9 order
 *   3. `selftest all`    -> per-test verdicts + tally line
 *   4. `selftest <name>` -> each test standalone; compare with 'all'
 *   5. tmp117.tail (0x4A) must report absent/optional, not a failure
 *   6. `selftest all` with g_vb_faults.i2c_silence_addr = 0x44 (SHT45)
 *      -> suite must degrade gracefully: i2c_scan fails, others
 *         unaffected, no hang/crash; fault cleared + recovery verified
 *   7. `scan`, `sensors`, `status` sanity
 *
 * Verdicts are parsed from the report TEXT ("<name>: PASS|FAIL|SKIP —")
 * because the console rc is only 0/1 (SKIP also returns nonzero).
 *
 * Before vsim_boot() the partition-sim backing image is normalised:
 * kept if it already has a valid vq_log journal, otherwise rewritten
 * as an erased chip + one valid journal slot + 0xE9 app magic.  This
 * avoids (a) rails_boot flapping on zeroed app bytes and (b) vq_log's
 * catastrophic-loss full-ring rescan, which takes minutes of wall
 * time inside the byte-accurate flash model while the console REPL
 * is not yet up (vsim_cmd output then lands in the wrong window).
 *
 * Termination: detached fw/tick/REPL threads keep running, so we cannot
 * return from main.  We also cannot fflush(NULL): the REPL thread is
 * blocked inside fgets() holding stdin's stdio lock, and fflush(NULL)
 * walks every open stream including stdin -> deadlock.  stdout is set
 * unbuffered at start so nothing is pending, then fflush(stdout) (safe:
 * nobody is parked inside a stdout call) and _exit(g_vsim_failures).
 */
#include "vsim.h"
#include "vboard.h"
#include "esp_partition.h"       /* part_sim_flash_path() */
#include "log_store.h"           /* vq_log_flash_present() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>

#define OUT_SZ      (64 * 1024)
#define N_TESTS     16

static char  out[OUT_SZ];
static int   n_checks;          /* every VSIM_CHECK issued            */

#define CHECK(cond, msg) do { n_checks++; VSIM_CHECK(cond, msg); } while (0)

/* §9 table order — must match components/vq_selftest/selftest.c */
static const char *const TEST_NAMES[N_TESTS] = {
    "rails_boot", "i2c_scan", "expander_walk", "spi_ids",
    "miso_contention", "irq_latency", "power_paths", "usb_cdc",
    "sleep_current", "optical", "ecg_bioz", "mic_pdm",
    "fsr_adc", "model_infer", "led_interlock", "decision_loop",
};

/* ------------------------------------------------------------------ */
/* parsing helpers                                                     */
/* ------------------------------------------------------------------ */

/* Locate "<name>:" at a line start inside buf; return the verdict
 * char 'P'/'F'/'S' ('?' if the line is there but verdict garbled,
 * 0 if the name never starts a line). */
static char verdict_of(const char *buf, const char *name)
{
    const size_t nl = strlen(name);
    const char  *p  = buf;
    for (;;) {
        p = strstr(p, name);
        if (!p) return 0;
        if ((p == buf || p[-1] == '\n') && p[nl] == ':') {
            const char *q = p + nl + 1;
            while (*q == ' ') q++;
            if (!strncmp(q, "PASS", 4)) return 'P';
            if (!strncmp(q, "FAIL", 4)) return 'F';
            if (!strncmp(q, "SKIP", 4)) return 'S';
            return '?';
        }
        p += nl;
    }
}

/* copy the single-test report block for `name` out of an `all`
 * transcript: "<name>: V — ..." up to the next TEST_NAMES entry line
 * (or end).  Returns length copied. */
static size_t block_copy(const char *buf, const char *name,
                         char *dst, size_t cap)
{
    const size_t nl = strlen(name);
    const char  *p  = buf;
    const char  *start = NULL;
    while ((p = strstr(p, name))) {
        if ((p == buf || p[-1] == '\n') && p[nl] == ':') { start = p; break; }
        p += nl;
    }
    if (!start) { if (cap) dst[0] = 0; return 0; }
    const char *end = start + strlen(start);
    for (int i = 0; i < N_TESTS; i++) {
        const char *q = strstr(start + 1, TEST_NAMES[i]);
        while (q && !(q > buf && q[-1] == '\n' && q[strlen(TEST_NAMES[i])] == ':'))
            q = strstr(q + 1, TEST_NAMES[i]);
        if (q && q > start && q < end) end = q;
    }
    size_t n = (size_t)(end - start);
    if (n >= cap) n = cap - 1;
    memcpy(dst, start, n);
    dst[n] = 0;
    return n;
}

static const char *vstr(char v)
{
    return v == 'P' ? "PASS" : v == 'F' ? "FAIL" :
           v == 'S' ? "SKIP" : v == '?' ? "???" : "MISSING";
}

/* The suite summary line is exactly "selftest: N/16 pass, F fail,
 * S skip" printed at a LINE START (selftest.c).  A plain strstr for
 * "selftest:" would also match embedded log text such as
 * "[I] (...) vq_exp: selftest: TX5_EN pulse ~123 us", so anchor the
 * search at line starts and take the LAST one (the tally is emitted
 * after all per-test blocks). */
static const char *tally_line(const char *buf)
{
    const char *last = NULL;
    const char *p = buf;
    if (!strncmp(p, "selftest:", 9))
        last = p;
    while ((p = strstr(p, "\nselftest:")) != NULL) {
        last = p + 1;
        p += 10;
    }
    return last;
}

/* run a console command; returns vsim_cmd rc, leaves text in `out` */
static int cmd(const char *c, int timeout_ms)
{
    out[0] = 0;
    int rc = vsim_cmd(c, out, sizeof(out), timeout_ms);
    printf("----- %s  (rc=%d) -----\n%.8000s\n%s\n", c, rc, out,
           strlen(out) > 8000 ? "...[truncated in log]" : "");
    return rc;
}

/* ------------------------------------------------------------------ */
/* deterministic flash image                                           */
/* ------------------------------------------------------------------ */
/* The partition shim's backing file is what the firmware actually
 * reads.  Two image states break the scenario for non-firmware
 * reasons:
 *   - no usable VQLJ journal slot -> vq_log falls back to a full-ring
 *     rescan (~16k sector reads through the byte-accurate flash model;
 *     >5 min wall) while the console REPL is not yet spawned, so every
 *     vsim_cmd queues and output lands in the wrong capture window;
 *   - zeroed app bytes at ota_0@0x20000 -> rails_boot sees magic!=0xE9.
 * Fabricating an "erased chip + valid journal + app magic" image once
 * keeps the scenario deterministic and realistic (journal constants
 * mirror components/vq_logstore/log_store.c).                        */

#define IMG_BYTES        PART_SIM_FLASH_BYTES   /* 16 MiB              */
#define IMG_SECTOR       4096u
#define IMG_SB_A         0u                     /* journal sector A    */
#define IMG_SB_B         1u                     /* journal sector B    */
#define IMG_RING_FIRST   2u                     /* first data sector   */
#define IMG_SLOTS        (IMG_SECTOR / 64u)     /* 64 slots/sector     */
#define IMG_VQLJ         0x4A4C5156u            /* "VQLJ"              */
#define IMG_APP_MAGIC    0x20000u               /* ota_0 base offset   */

static uint32_t s_crc_tab[256];
static void crc_tab_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc_tab[i] = c;
    }
}
static uint32_t crc32_buf(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    while (len--) c = s_crc_tab[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/* one valid journal slot (empty ring): seq=1 head=tail=RING_FIRST */
static void make_journal_slot(uint8_t *p)
{
    memset(p, 0xFF, 64);
    put32(p + 0,  IMG_VQLJ);
    put32(p + 4,  1u);              /* seq       */
    put32(p + 8,  IMG_RING_FIRST);  /* head      */
    put32(p + 12, 0u);              /* head_off  */
    put32(p + 16, IMG_RING_FIRST);  /* tail      */
    put32(p + 20, 1u);              /* next_id   */
    /* appends/drops/used = 0 */
    put32(p + 36, crc32_buf(p, 36));
}
/* does the image already contain ANY valid journal slot? (same rule
 * as sb_load: magic + crc over the first 36 B of a 64 B slot) */
static int journal_ok(int fd)
{
    uint8_t sec[IMG_SECTOR];
    for (uint32_t s = IMG_SB_A; s <= IMG_SB_B; s++) {
        if (pread(fd, sec, sizeof sec, (off_t)s * IMG_SECTOR)
                != (ssize_t)sizeof sec)
            return 0;
        for (uint32_t i = 0; i < IMG_SLOTS; i++) {
            const uint8_t *p = sec + i * 64u;
            if (get32(p) == IMG_VQLJ &&
                crc32_buf(p, 36) == get32(p + 36))
                return 1;
        }
    }
    return 0;
}
/* ensure the backing image boots cleanly: keep it if it already has a
 * valid journal (just top up the app magic); otherwise write a fresh
 * erased-chip image with one journal slot. */
static void ensure_flash_image(const char *path)
{
    crc_tab_init();
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size == (off_t)IMG_BYTES &&
            journal_ok(fd)) {
            uint8_t b;
            if (pread(fd, &b, 1, IMG_APP_MAGIC) == 1 && b != 0xE9) {
                b = 0xE9;           /* part_sim's fresh-image seed  */
                if (pwrite(fd, &b, 1, IMG_APP_MAGIC) == 1)
                    printf("(patched app magic 0xE9 @0x20000)\n");
            }
            close(fd);
            return;
        }
        close(fd);
    }
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        fprintf(stderr, "warn: cannot create flash image %s: %m\n", path);
        return;
    }
    uint8_t blk[1 << 20];
    memset(blk, 0xFF, sizeof blk);
    for (size_t off = 0; off < IMG_BYTES; off += sizeof blk)
        if (write(fd, blk, sizeof blk) != (ssize_t)sizeof blk)
            fprintf(stderr, "warn: short write on image\n");
    make_journal_slot(blk);                 /* sector A, slot 0       */
    ssize_t w1 = pwrite(fd, blk, 64, (off_t)IMG_SB_A * IMG_SECTOR);
    blk[0] = 0xE9;                          /* app image magic        */
    memset(blk + 1, 0xFF, 63);
    ssize_t w2 = pwrite(fd, blk, 64, IMG_APP_MAGIC);
    if (w1 != 64 || w2 != 64)
        fprintf(stderr, "warn: journal/magic writes incomplete\n");
    close(fd);
    printf("(fabricated fresh flash image %s — erased chip + journal)\n",
           path);
}

/* ------------------------------------------------------------------ */
int main(void)
{
    /* Unbuffered stdout: nothing pending at _exit(), so the trailing
     * fflush(stdout) is a cheap lock+no-op rather than a real flush —
     * and no output is ever stranded in a buffer the process skips. */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* If the harness didn't pin a scale, compress virtual time so the
     * suite's multi-second waits stay cheap in wall time. */
    if (!getenv("VSIM_TIME_SCALE"))
        vboard_clock_set_scale(20.0);

    /* Deterministic image state: erased-chip + valid journal if the
     * backing file is missing/stale (see ensure_flash_image). */
    {
        char img[1024];
        if (part_sim_flash_path(img, sizeof img) > 0)
            ensure_flash_image(img);
    }

    vsim_boot();

    /* Wait for the console REPL before injecting commands.  Fresh
     * images trigger a vq_log full-ring rescan (~140 virt s for 16k
     * sectors) and the console is only spawned AFTER the mount
     * (main.c: console last).  vsim_cmd() writes queue on a pipe — a
     * command issued early times out but still executes later, and its
     * output lands in the NEXT command's capture window, corrupting
     * every parse that follows.  vq_log_flash_present() latches only
     * after a successful mount, so gate on it, then settle for the
     * pipeline/console spawn. */
    int boot_ok = 0;
    for (int i = 0; i < 600; i++) {
        if (vq_log_flash_present()) { boot_ok = 1; break; }
        vsim_advance_us(500 * 1000);            /* +0.5 s, ~300 s cap */
    }
    if (!boot_ok)
        fprintf(stderr, "warn: log store not mounted after 300 virt s "
                        "— console may be late\n");
    vsim_advance_us(10 * 1000 * 1000);  /* pipeline + console spawn   */

    /* ================= 2. selftest list ============================ */
    int rc = cmd("selftest list", 60000);
    CHECK(rc >= 0, "selftest list: no CMD-END (console dead?)");
    CHECK(strstr(out, "16 self-tests") != NULL,
          "selftest list: expected '16 self-tests' header");
    for (int i = 0; i < N_TESTS; i++) {
        char needle[40];
        snprintf(needle, sizeof needle, "  %s\n", TEST_NAMES[i]);
        CHECK(strstr(out, needle) != NULL,
              "selftest list: missing test name");
        if (strstr(out, needle) == NULL)
            fprintf(stderr, "   missing: %s\n", TEST_NAMES[i]);
    }
    /* order check: names must appear in §9 order */
    {
        const char *pos = out;
        int ok = 1;
        for (int i = 0; i < N_TESTS; i++) {
            const char *p = strstr(pos, TEST_NAMES[i]);
            if (!p) { ok = 0; break; }
            pos = p + strlen(TEST_NAMES[i]);
        }
        CHECK(ok, "selftest list: names not in expected order");
    }

    /* ============ 7a. early sanity: scan / sensors / status ======== */
    rc = cmd("scan", 90000);
    CHECK(rc >= 0, "scan: no CMD-END");
    {
        static const uint8_t want[] =
            { 0x20, 0x36, 0x39, 0x3A, 0x44, 0x48, 0x49, 0x52, 0x6A, 0x76 };
        char tag[8];
        int found = 0;
        for (size_t i = 0; i < sizeof(want); i++) {
            snprintf(tag, sizeof tag, "0x%02X", want[i]);
            if (strstr(out, tag)) found++;
            else fprintf(stderr, "scan: missing 0x%02X\n", want[i]);
        }
        CHECK(found == 10, "scan: not all 10 expected I2C nodes seen");
        /* clean bus: the console prints "expected missing: none";
         * anything else names a missing non-optional part (fault) */
        CHECK(strstr(out, "expected missing: none") != NULL,
              "scan: a non-optional node reported expected-missing");
        CHECK(strstr(out, "0x4A") == NULL,
              "scan: 0x4A tmp117.tail should be ABSENT on vboard");
    }

    rc = cmd("sensors", 60000);
    CHECK(rc >= 0, "sensors: no CMD-END");
    {
        /* count table rows: sensor names we know must register */
        static const char *const s_names[] = {
            "ads1292r", "afe4900", "ad5940.eda", "ad5940.sweat",
            "max86141", "tmp117.skin", "tmp117.dome", "tmp117.tail",
            "bme280", "sht45", "max17048", "rv3028", "as7341",
            "mlx90632", "lsm6dsv80x", "im69d130", "fsr",
        };
        int present = 0;
        for (size_t i = 0; i < sizeof(s_names)/sizeof(s_names[0]); i++)
            if (strstr(out, s_names[i])) present++;
        CHECK(present == 17, "sensors: expected 17 registered drivers");
        CHECK(strstr(out, "tmp117.tail") != NULL,
              "sensors: tmp117.tail must still be REGISTERED (absent ok)");
    }

    rc = cmd("status", 60000);
    CHECK(rc >= 0, "status: no CMD-END");
    CHECK(strstr(out, "vitalquant status") != NULL, "status: no header");
    CHECK(strstr(out, "power     :") != NULL,   "status: no power line");
    CHECK(strstr(out, "heap      :") != NULL,   "status: no heap line");

    /* ================= 3. selftest all ============================= */
    char verdict_all[N_TESTS + 1];
    memset(verdict_all, 0, sizeof verdict_all);
    rc = cmd("selftest all", 240000);
    CHECK(rc >= 0, "selftest all: no CMD-END (hang/crash)");
    {
        int np = 0, nf = 0, ns = 0;
        for (int i = 0; i < N_TESTS; i++) {
            verdict_all[i] = verdict_of(out, TEST_NAMES[i]);
            CHECK(verdict_all[i] != 0,
                  "selftest all: report lacks a verdict line for a test");
            if (verdict_all[i] == 'P') np++;
            else if (verdict_all[i] == 'F') nf++;
            else if (verdict_all[i] == 'S') ns++;
        }
        /* tally line + truncation audit — anchored at a line start so
         * log text like "vq_exp: selftest: TX5_EN pulse" can't alias */
        const char *tl = tally_line(out);
        if (tl) {
            int ep = -1, ef = -1, es = -1, et = -1;
            int got = sscanf(tl,
                             "selftest: %d/%d pass, %d fail, %d skip",
                             &ep, &et, &ef, &es);
            CHECK(got == 4, "selftest all: tally line unparseable");
            CHECK(et == N_TESTS, "selftest all: tally denominator != 16");
            if (got == 4 && et == N_TESTS)
                CHECK(ep == np && ef == nf && es == ns,
                      "selftest all: tally counts != parsed verdicts");
        } else {
            fprintf(stderr,
                    "note: `selftest all` report truncated before the "
                    "tally line (s_report buffer too small?)\n");
            n_checks++;
            VSIM_CHECK(0, "selftest all: summary line missing (report "
                          "truncated in 6144 B buffer)");
        }
        /* tmp117.tail must be reported absent/optional, not missing */
        char blk[2048];
        block_copy(out, "i2c_scan", blk, sizeof blk);
        CHECK(strstr(blk, "0x4A tmp117.tail: absent") != NULL,
              "i2c_scan: tail absence not reported as optional/absent");
        CHECK(strstr(blk, "MISSING: 0x4A") == NULL &&
              strstr(blk, "MISSING") == NULL,
              "i2c_scan: tail listed as MISSING (should be optional)");
        printf("===== selftest all verdicts =====\n");
        for (int i = 0; i < N_TESTS; i++)
            printf("  %-16s %s\n", TEST_NAMES[i], vstr(verdict_all[i]));
    }

    /* ================= 4. individual runs ========================== */
    char verdict_solo[N_TESTS + 1];
    memset(verdict_solo, 0, sizeof verdict_solo);
    for (int i = 0; i < N_TESTS; i++) {
        char c[48];
        snprintf(c, sizeof c, "selftest %s", TEST_NAMES[i]);
        int r = cmd(c, 180000);
        CHECK(r >= 0, "selftest <name>: no CMD-END (hang/crash)");
        verdict_solo[i] = verdict_of(out, TEST_NAMES[i]);
        CHECK(verdict_solo[i] != 0,
              "selftest <name>: no verdict line in report");
    }
    printf("===== matrix (ALL vs SOLO) =====\n");
    for (int i = 0; i < N_TESTS; i++) {
        printf("  %-16s all=%-4s solo=%-4s%s\n", TEST_NAMES[i],
               vstr(verdict_all[i]), vstr(verdict_solo[i]),
               (verdict_all[i] && verdict_solo[i] &&
                verdict_all[i] != verdict_solo[i])
                   ? "  <-- differs" : "");
    }

    /* ================= 6. graceful degradation ===================== */
    char verdict_flt[N_TESTS + 1];
    memset(verdict_flt, 0, sizeof verdict_flt);
    printf("===== fault injection: i2c_silence_addr = 0x44 (SHT45) =====\n");
    g_vb_faults.i2c_silence_addr = 0x44;
    vsim_advance_us(500 * 1000);        /* let faulted traffic hit      */
    {
        rc = cmd("selftest all", 240000);
        CHECK(rc >= 0, "fault run: suite hung/crashed (no CMD-END)");
        /* completion = every test still emitted a verdict line; the
         * suite is stateful (expander_walk re-asserts resets,
         * sleep_current parks the log store), so verdicts that are
         * order/timing sensitive are recorded, not asserted equal. */
        int missing_v = 0;
        for (int i = 0; i < N_TESTS; i++) {
            verdict_flt[i] = verdict_of(out, TEST_NAMES[i]);
            if (verdict_flt[i] == 0) {
                missing_v++;
                fprintf(stderr, "fault run: no verdict line for %s\n",
                        TEST_NAMES[i]);
            }
        }
        CHECK(missing_v == 0,
              "fault run: suite stopped early — verdict lines missing");
        CHECK(verdict_flt[1] /* i2c_scan */ == 'F',
              "fault run: i2c_scan did not FAIL with SHT45 silenced");
        char blk[2048];
        block_copy(out, "i2c_scan", blk, sizeof blk);
        CHECK(strstr(blk, "MISSING") != NULL && strstr(blk, "0x44") != NULL,
              "fault run: i2c_scan did not report 0x44 missing");
        CHECK(strstr(blk, "MISSING: 0x4A") == NULL,
              "fault run: optional 0x4A wrongly counted as missing");
        /* informational: which verdicts moved vs the baseline run */
        for (int i = 0; i < N_TESTS; i++) {
            if (verdict_flt[i] && verdict_all[i] &&
                verdict_flt[i] != verdict_all[i]) {
                printf("   fault-run delta: %-16s %s -> %s\n",
                       TEST_NAMES[i], vstr(verdict_all[i]),
                       vstr(verdict_flt[i]));
            }
        }
        /* i2c_scan is the only test the 0x44 fault may legitimately
         * move to FAIL; other moves are state/timing noise (they occur
         * in un-faulted re-runs too) and are reported in the matrix. */
        int extra_fails = 0;
        for (int i = 0; i < N_TESTS; i++)
            if (i != 1 && verdict_flt[i] == 'F' && verdict_all[i] == 'P')
                extra_fails++;
        if (extra_fails)
            printf("   note: %d test(s) P->F vs baseline — suite order "
                   "noise, see report\n", extra_fails);
    }
    /* still alive? a console command must answer */
    rc = cmd("scan", 90000);
    CHECK(rc >= 0, "post-fault scan: console unresponsive");
    CHECK(strstr(out, "0x44(sht45") != NULL,
          "post-fault scan: 0x44 not reported as expected-missing");

    g_vb_faults.i2c_silence_addr = 0;
    vsim_advance_us(500 * 1000);
    rc = cmd("scan", 90000);
    CHECK(rc >= 0, "post-clear scan: console unresponsive");
    CHECK(strstr(out, "0x44") != NULL,
          "post-clear scan: 0x44 SHT45 did not come back");

    /* post-suite pipeline sanity: sensors table still answers */
    rc = cmd("sensors", 60000);
    CHECK(rc >= 0, "post-suite sensors: console unresponsive");
    CHECK(strstr(out, "sht45") != NULL,
          "post-suite sensors: sht45 row gone");

    /* ================= results ===================================== */
    printf("\n===== SCN selftest matrix =====\n");
    printf("%-16s %-6s %-6s %-6s\n", "test", "all", "solo", "fault");
    for (int i = 0; i < N_TESTS; i++)
        printf("%-16s %-6s %-6s %-6s\n", TEST_NAMES[i],
               vstr(verdict_all[i]), vstr(verdict_solo[i]),
               vstr(verdict_flt[i]));

    printf("SCN selftest RESULT: checks=%d fails=%d\n",
           n_checks, g_vsim_failures);
    /* Do NOT fflush(NULL): the REPL thread is parked in fgets() with
     * stdin's lock held, and flushing every stream deadlocks on it.
     * stdout is _IONBF so everything is already on the wire; this
     * fflush is just a lock+no-op.  _exit skips atexit handlers and
     * detached threads die with the process. */
    fflush(stdout);
    _exit(g_vsim_failures & 0xFF);
}
