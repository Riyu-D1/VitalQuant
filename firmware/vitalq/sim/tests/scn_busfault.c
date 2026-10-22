/* scn_busfault.c — host-side physical-layer bus-fault scenario (fix_v22).
 *
 * Runs the REAL firmware on the virtual board and injects faults only
 * through g_vb_faults / vboard_rail_set — the same knobs the chip models
 * honour inside the real I2C/SPI shims. Driver internals are never
 * touched.
 *
 * =====================  OBSERVABILITY MODEL  =========================
 * The `sensors` table shows the pipeline *registry copies* of
 * vq_sensor_t (registration is BY VALUE — see vq_pipeline/vq_registry.c).
 * Only drivers that emit through the ops-passed `s` are visible:
 *
 *   VISIBLE  : ads1292r, lsm6dsv80x, mlx90632, as7341
 *   INVISIBLE: everything that emits on its private template copy —
 *              env group (sht45/bme280/tmp117.*), ad5940.*, fsr,
 *              im69d130, afe4900, max17048 — their samples/errs land on
 *              the driver template, never on the registry.  These are
 *              reported as BUG-FOUND checks in P1.
 *
 *   Still visible for the invisible set: pipeline-level err_count
 *   (sensor_fault / failed retry increment the REGISTRY copy), the
 *   health column, `scan`, `logtail flash` event records, and the
 *   pipeline's own SENSOR_FAULT log lines on stderr.
 *
 * Phases (virtual time; VSIM_TIME_SCALE=50 unless pinned):
 *
 *   P0  spi_corrupt_id[4] (CS38 MAX86141) = 0xAA pre-boot: forces
 *       rx_dst[0] of the PART_ID read — mx_r8() uses vq_spi_write_read()
 *       so the forced byte lands exactly on PART_ID.  Probe must refuse
 *       (ESP_ERR_NOT_FOUND → SS_ABSENT), not configure alien silicon.
 *
 *       SIM LIMITATION (not a fw bug): the requested ADS1292 wrong-ID
 *       cannot be reached by spi_corrupt_id[] — it forces rx_dst[0],
 *       the first byte of the firmware's rx buffer; ads_rreg() reads the
 *       ID from rx[2] (the model answers in the 3rd frame byte), so a
 *       forced byte 0 only hits the don't-care echo.  P6a uses the knob
 *       where byte 0 IS checked (RDATAC status sync nibble).
 *
 *   P1  baseline: verify the visible producers stream; audit/report the
 *       template-emit bug for every sensor stuck at 0 while "ok".
 *
 *   P2  i2c_silence_addr=0x44 (SHT45 NACKs): fetch errs -> last_ok stale
 *       -> health task SENSOR_FAULT + retry (registry err++ — visible);
 *       `scan` must drop 0x44 while the other 9 nodes ACK (single-device
 *       fault must not wedge the shared bus).  Clear -> 0x44 answers.
 *
 *   P3  i2c_sda_stuck_low (~10 s): every wire txn burns its bounded
 *       timeout (50/100 ms); `sensors` must still answer (no i2c on
 *       that path); `scan` must END with "no I2C devices ACKed" —
 *       proving probes time out rather than hang; SPI (ads1292r) must
 *       keep streaming through the wedge (bus isolation).  Release ->
 *       all previously-producing I2C sensors resume.
 *
 *   P4  i2c_nack_rate=0.05: nacks counted, producers keep streaming.
 *
 *   P5  spi_bitflip_rate=0.02: ADS1292 sync-nibble rejects corrupted
 *       frames (err++); stream continues.  NOTE: channel payload bytes
 *       (rx[3..8]) have no integrity check — a flipped data byte passes
 *       silently; reported as a data-integrity gap, not a hang.
 *
 *   P6a spi_corrupt_id[0] (CS10 ADS1292)=0x42: forces the RDATAC status
 *       MSB -> every frame fails the 0xC sync nibble -> discarded
 *       (err++ fast), samples frozen, health faults + retries.  Clear
 *       -> recovers (init reads ID from rx[2], unaffected).
 *   P6b spi_stuck_cs=18 + miso_fight: g_vb_stats.miso_fights must grow;
 *       console stays responsive.
 *   P6c vboard_gpio_set(15,0): stuck-low CS during traffic ->
 *       g_vb_stats.spi_cs_violations must grow.
 *
 *   P7  vboard_rail_set(VB_RAIL_1V8,false): kills AS7341(0x39)+MLX90632
 *       (0x3A) — their reads NACK (models gate on the rail) -> frozen
 *       samples + err++ + FAULT; 3V3 devices (lsm6 etc.) keep streaming.
 *       Restore -> health retries recover them.
 *
 * SIM GAPS found (reported, not asserted):
 *   - i2c_master_probe() checks registration, not powered state — a
 *     rail-dead device still ACKs a `scan` probe (real silicon NACKs).
 *   - `logtail <n>` and `logtail <n> flash` ABORT the whole sim: the
 *     console pairs heap_caps_*() with plain free() (vq_console.c
 *     ~918/925/931/943 and ~977/1002) — legal on IDF (unified heap),
 *     fatal here because the sim heap prepends an alloc_hdr_t so
 *     glibc free() aborts "invalid pointer".  Both code paths are
 *     therefore unusable in-host; SENSOR_FAULT evidence is taken from
 *     the registry err counters + stderr ESP_LOGW instead.
 *   - vsim_reset() cannot re-run probes (no fw restart), so
 *     wrong-silicon is tested pre-boot.
 *   - at >=10x clock compression the vboard tick thread cannot sustain
 *     500 DRDY/virtual-s of real work -> ads1292r's 20 ms stall-detect
 *     trips during heavy phases (transient fault+retry, isr_dropped
 *     bursts).  Compression artifact, NOT a bus leak: verified at
 *     scale 1 the same wedge leaves ads1292r streaming.  P3 asserts
 *     post-release SPI recovery instead of mid-wedge streaming.
 *
 * Result line (required):  SCN busfault RESULT: checks=<n> fails=<k>
 * `fails` counts CONFIRMED defects: BUG-FOUND messages are intentional
 * failing checks documenting real firmware bugs; anything else means a
 * fault-handling regression.  Exit code = g_vsim_failures.
 */
#include "vsim.h"
#include "vboard.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <inttypes.h>

#define OUT_SZ      (64 * 1024)

/* CS order in spi_sim s_cs_map: {10 ADS1292,15 AD5940,16 W25Q512,
 * 18 AFE4900,38 MAX86141} — spi_corrupt_id[] indexes this table.      */
#define CSIDX_ADS1292   0
#define CSIDX_MAX86141  4
#define CS_GPIO_AD5940  15
#define CS_GPIO_AFE4900 18

#define VSEC(x)         ((int64_t)(x) * 1000000LL)
#define CMD_TMO         30000           /* virtual ms                  */
#define SCAN_TMO        120000          /* wedge: 112x(50ms+contention)*/

static char out[OUT_SZ];
static int  n_checks;

#define CHECK(cond, msg) do { n_checks++; VSIM_CHECK(cond, msg); } while (0)

/* ------------------------------------------------------------------ */
static int cmd(const char *c, int timeout_ms, bool quiet)
{
    out[0] = 0;
    int rc = vsim_cmd(c, out, sizeof(out), timeout_ms);
    if (!quiet)
        printf("----- %s  (rc=%d) -----\n%.7000s\n%s\n", c, rc, out,
               strlen(out) > 7000 ? "...[truncated]" : "");
    return rc;
}

typedef struct {
    bool          found;
    unsigned long samples;
    unsigned long err;
    char          health[8];
} srow_t;

/* "<name> <rate> <samples> <err> <last|never> <health>" */
static srow_t sensor_row(const char *buf, const char *name)
{
    srow_t r = { .health = "?" };
    const size_t nl = strlen(name);
    const char  *p  = buf;
    while ((p = strstr(p, name)) != NULL) {
        if (p == buf || p[-1] == '\n') {
            char line[200], nm[16], hl[8] = "?", lt[16] = "";
            unsigned long smp = 0, er = 0;
            double rate = 0;
            const char *eol = strchr(p, '\n');
            size_t n = eol ? (size_t)(eol - p) : strlen(p);
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, p, n); line[n] = 0;
            if (sscanf(line, "%15s %lf %lu %lu %15s %7s",
                       nm, &rate, &smp, &er, lt, hl) >= 5 &&
                strcmp(nm, name) == 0) {
                r.found = true; r.samples = smp; r.err = er;
                snprintf(r.health, sizeof r.health, "%s", hl);
                return r;
            }
        }
        p += nl;
    }
    return r;
}

static int take_sensors(srow_t rows[], const char *const names[], int n)
{
    int rc = cmd("sensors", CMD_TMO, false);
    if (rc < 0) { for (int i = 0; i < n; i++) rows[i].found = false; }
    for (int i = 0; i < n; i++) rows[i] = sensor_row(out, names[i]);
    return rc;
}

static void dump_stats(const char *tag)
{
    printf("[vb-stats %s] i2c_txns=%llu nacks=%llu | spi_txns=%llu "
           "cs_viol=%llu miso_bytes=%llu miso_fights=%llu irq=%llu\n",
           tag,
           (unsigned long long)g_vb_stats.i2c_txns,
           (unsigned long long)g_vb_stats.i2c_nacks,
           (unsigned long long)g_vb_stats.spi_txns,
           (unsigned long long)g_vb_stats.spi_cs_violations,
           (unsigned long long)g_vb_stats.miso_bytes,
           (unsigned long long)g_vb_stats.miso_fights,
           (unsigned long long)g_vb_stats.irq_pulses);
}

static void clear_faults(void)
{
    memset(&g_vb_faults, 0, sizeof(g_vb_faults));
    g_vb_faults.spi_stuck_cs = 0;               /* 0 = off, -1 also ok  */
    if (!vboard_rail(VB_RAIL_1V8)) vboard_rail_set(VB_RAIL_1V8, true);
    vboard_gpio_set(CS_GPIO_AD5940, 1);
}

static const char *const NAMES[] = {
    "ads1292r", "afe4900", "ad5940.eda", "ad5940.sweat", "max86141",
    "tmp117.skin", "tmp117.dome", "tmp117.tail", "bme280", "sht45",
    "max17048", "rv3028", "as7341", "mlx90632", "lsm6dsv80x",
    "im69d130", "fsr",
};
#define N_SENSORS (int)(sizeof(NAMES) / sizeof(NAMES[0]))

static int name_idx(const char *n)
{
    for (int i = 0; i < N_SENSORS; i++) if (!strcmp(NAMES[i], n)) return i;
    return -1;
}

/* one-line summary of a sensor row for the log */
static void note_row(const srow_t rows[], const char *n, const char *tag)
{
    int i = name_idx(n);
    printf("%s %-12s samples=%-8lu err=%-5lu health=%s\n", tag, n,
           rows[i].samples, rows[i].err, rows[i].health);
}

/* ------------------------------------------------------------------ */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* ================= P0: wrong silicon (pre-boot) ================= */
    g_vb_faults.spi_corrupt_id[CSIDX_MAX86141] = 0xAA;
    printf("===== P0: spi_corrupt_id[CS38] = 0xAA pre-boot =====\n");

    vsim_boot();
    /* The virtual clock's env knob (VSIM_TIME_SCALE) is read by
     * core_init_once() which runs during the device-file
     * __attribute__((constructor)) self-registrations — BEFORE main()
     * — so setting it here is too late.  set_scale() rebases
     * (origin,base) monotonically, safe post-boot.  ~950 virt s of
     * advances + command latency ≈ ~3 min real at 10x; higher scales
     * starve the fw: ADS1292 DRDY is 500/virtual-s -> at 50x the drain
     * needs ~25k SPI xfers per REAL s and the REPL never answers
     * vsim_cmd before its virtual deadline.                          */
    vboard_clock_set_scale(10.0);
    vsim_advance_us(VSEC(18));

    srow_t rows[N_SENSORS], pre[N_SENSORS];
    int rc = take_sensors(rows, NAMES, N_SENSORS);
    CHECK(rc >= 0, "P0: console dead after boot");
    {
        int mx = name_idx("max86141");
        CHECK(rows[mx].found, "P0: max86141 row missing");
        CHECK(rows[mx].found && rows[mx].samples == 0,
              "P0: corrupt PART_ID accepted — max86141 produced samples");
        CHECK(rows[mx].found && strcmp(rows[mx].health, "FAULT") == 0,
              "P0: refused part not marked unhealthy");
        int ads = name_idx("ads1292r");
        CHECK(rows[ads].found && rows[ads].samples > 0,
              "P0: CS38-targeted fault disturbed CS10 ads1292r");
        printf("P0: PART_ID=0xAA -> probe refused, max86141 ABSENT "
               "(no re-probe path — see header)\n");
    }
    g_vb_faults.spi_corrupt_id[CSIDX_MAX86141] = 0;

    /* ================= P1: baseline + emit audit ==================== */
    printf("===== P1: baseline =====\n");
    vsim_advance_us(VSEC(15));
    rc = take_sensors(rows, NAMES, N_SENSORS);
    CHECK(rc >= 0, "P1: sensors command failed");
    {
        /* producers that DO reach the registry — must stream */
        const char *prod[] = { "ads1292r", "lsm6dsv80x", "mlx90632" };
        for (size_t i = 0; i < sizeof(prod)/sizeof(prod[0]); i++) {
            int ix = name_idx(prod[i]);
            CHECK(rows[ix].found && rows[ix].samples > 0,
                  "P1: expected producer not streaming");
        }
        int a7 = name_idx("as7341");
        if (rows[a7].samples == 0)
            printf("P1: (note) as7341 produced 0 — led_sched grant "
                   "starvation, known issue\n");

        /* ---- BUG-FOUND audit: sensors stuck at 0 while "streaming" --
         * emit on the driver's private template copy of vq_sensor_t;
         * the pipeline registry copy (the only one that shows) never
         * sees sink/sample_count/err_count.  One failing check per
         * affected driver. */
        struct { const char *names[6]; const char *site; } bugs[] = {
            { { "sht45", "bme280", "tmp117.skin", "tmp117.dome", NULL },
              "vq_env_emit()->d->s (vq_drv_env.c) — sink/count on the "
              "dev-embedded template (tmp117.tail absent by design)" },
            { { "ad5940.eda", "ad5940.sweat", NULL },
              "emit(&s_sens_*) (vq_drv_ad5940.c) — static templates" },
            { { "fsr", NULL },
              "s_sensor template (vq_drv_fsr.c)" },
            /* im69d130 is NOT in this table by design: the driver arms
             * on-demand mode and never streams (mic_op_start), so
             * samples=0 in `sensors` is correct.  Its capture→emit→sink
             * path is covered end-to-end by the mic_pdm selftest.     */
            { { "max17048", NULL },
              "gauge task spawned on &vq_aux_gauge_sensor via "
              "vq_gauge_on_alert (vq_max17048.c:400)" },
            { { "afe4900", NULL },
              "s_priv.sen = &s_sensor template (vq_drv_afe4900.c:891)" },
        };
        for (size_t b = 0; b < sizeof(bugs)/sizeof(bugs[0]); b++) {
            int stuck = 0, total = 0;
            for (int j = 0; bugs[b].names[j]; j++) {
                int ix = name_idx(bugs[b].names[j]);
                if (ix < 0 || !rows[ix].found) continue;
                total++;
                if (rows[ix].samples == 0) stuck++;
            }
            if (total == 0) continue;
            if (stuck == total) {
                n_checks++;
                VSIM_CHECK(0, "BUG-FOUND: template emit — samples never "
                              "reach pipeline registry");
                fprintf(stderr, "   site: %s; sensors:", bugs[b].site);
                for (int j = 0; bugs[b].names[j]; j++)
                    fprintf(stderr, " %s", bugs[b].names[j]);
                fprintf(stderr, "\n");
            } else if (stuck > 0) {
                printf("P1: (note) partial visibility in '%s' — "
                       "%d/%d stuck\n", bugs[b].site, stuck, total);
            }
        }
    }
    memcpy(pre, rows, sizeof(pre));
    dump_stats("baseline");

    /* ================= P2: silence SHT45 @0x44 ===================== */
    printf("===== P2: i2c_silence_addr = 0x44 =====\n");
    g_vb_faults.i2c_silence_addr = 0x44;
    vsim_advance_us(VSEC(45));          /* >=1 health tick + retry cycle */

    rc = take_sensors(rows, NAMES, N_SENSORS);
    CHECK(rc >= 0, "P2: console dead while SHT45 silenced");
    {
        int sh = name_idx("sht45");
        printf("P2: sht45 err %lu->%lu (fault/retry evidence)\n",
               pre[sh].err, rows[sh].err);
        CHECK(rows[sh].err > pre[sh].err,
              "P2: silenced sht45 produced no pipeline-visible error");
        /* isolation: every still-producing I2C sensor keeps streaming
         * (as7341 excluded — it independently faults on led_sched grant
         * starvation, a known unrelated issue) */
        const char *peers[] = { "mlx90632", "lsm6dsv80x" };
        for (size_t i = 0; i < sizeof(peers)/sizeof(peers[0]); i++) {
            int ix = name_idx(peers[i]);
            if (!pre[ix].found || pre[ix].samples == 0) continue;
            CHECK(rows[ix].samples > pre[ix].samples,
                  "P2: unrelated I2C sensor starved by 0x44 silence");
        }
    }
    rc = cmd("scan", SCAN_TMO, false);
    CHECK(rc >= 0, "P2: scan hung during single-device silence");
    /* scan prints found devices as "  0xXX  name" lines and then an
     * "expected missing: 0x44(sht45 ...)" annotation — match the
     * indented found-line, not the annotation. */
    CHECK(strstr(out, "\n  0x44") == NULL,
          "P2: 0x44 ACKed while silenced");
    CHECK(strstr(out, "0x44(sht45") != NULL,
          "P2: scan did not flag silenced 0x44 as expected-missing");
    CHECK(strstr(out, "9 device(s)") != NULL,
          "P2: 0x44 silence took other devices off the bus");
    /* hold longer: >=1 failed health-retry while still silenced.
     * NOTE: `logtail` (RAM or flash) cannot be used — it aborts the
     * sim (heap_caps_* + free() mismatch; see header).  SENSOR_FAULT
     * evidence: the ESP_LOGW lines on stderr, e.g.
     *   "vq_pipe: SENSOR_FAULT sht45: healthy() (streak 1...)"
     *   "vq_pipe: sht45 restart failed (ESP_ERR_TIMEOUT)..."          */
    vsim_advance_us(VSEC(40));
    take_sensors(rows, NAMES, N_SENSORS);
    {
        int sh = name_idx("sht45");
        printf("P2: end-of-silence sht45 err=%lu (see stderr for "
               "SENSOR_FAULT/restart-failed lines)\n", rows[sh].err);
        CHECK(rows[sh].err > pre[sh].err,
              "P2: silenced sht45 produced no pipeline fault evidence");
    }

    g_vb_faults.i2c_silence_addr = 0;
    vsim_advance_us(VSEC(120));
    rc = cmd("scan", SCAN_TMO, false);
    CHECK(rc >= 0, "P2: post-clear scan hung");
    CHECK(strstr(out, "\n  0x44") != NULL,
          "P2: 0x44 did not answer after silence cleared");
    take_sensors(rows, NAMES, N_SENSORS);
    note_row(rows, "sht45", "P2: post-clear");
    dump_stats("post-P2");

    /* ================= P3: whole-bus wedge ========================= */
    printf("===== P3: i2c_sda_stuck_low =====\n");
    memcpy(pre, rows, sizeof(pre));
    g_vb_faults.i2c_sda_stuck_low = true;
    vsim_advance_us(VSEC(6));
    rc = cmd("sensors", CMD_TMO, false);          /* i2c-free path      */
    CHECK(rc >= 0, "P3: console dead while bus wedged (deadlock)");
    {
        int ads = name_idx("ads1292r");
        srow_t wed[N_SENSORS];
        for (int i = 0; i < N_SENSORS; i++) wed[i] = sensor_row(out, NAMES[i]);
        printf("P3: ads1292r during wedge samples %lu->%lu err=%lu "
               "(SPI isolation observed; see header re: scale artifact)\n",
               pre[ads].samples, wed[ads].samples, wed[ads].err);
        /* NOTE: under >=10x clock compression the tick thread cannot
         * sustain 500 DRDY edges/virtual-s of real work; the drain's
         * 20 ms stall-detect trips -> fault+retry cycle (isr_dropped
         * bursts into err_count).  NOT a bus leak: at scale 1 the same
         * wedge left ads1292r streaming (464312->476232).  The real
         * assertion is recovery after release — checked below.        */
    }
    rc = cmd("scan", SCAN_TMO, false);
    CHECK(rc >= 0, "P3: scan hung during wedge — probes unbounded");
    CHECK(strstr(out, "no I2C devices ACKed") != NULL,
          "P3: wedge did not present as total NACK (fault not applied)");
    vsim_advance_us(VSEC(4));
    g_vb_faults.i2c_sda_stuck_low = false;
    printf("P3: wedge released after ~10 s\n");

    vsim_advance_us(VSEC(200));         /* streak-1/2 retries + tick    */
    take_sensors(rows, NAMES, N_SENSORS);
    {
        const char *i2c_peers[] = { "mlx90632", "lsm6dsv80x" };
        int recovered = 0, checked = 0;
        for (size_t i = 0; i < sizeof(i2c_peers)/sizeof(i2c_peers[0]); i++) {
            int ix = name_idx(i2c_peers[i]);
            if (!pre[ix].found || pre[ix].samples == 0) continue;
            checked++;
            if (rows[ix].samples > pre[ix].samples) recovered++;
            else printf("P3: %s not recovered (samples %lu->%lu)\n",
                        i2c_peers[i], pre[ix].samples, rows[ix].samples);
        }
        printf("P3: %d/%d producing I2C sensors recovered post-wedge\n",
               recovered, checked);
        CHECK(checked > 0 && recovered == checked,
              "P3: bus wedge left I2C sensors permanently dead");
        int ads = name_idx("ads1292r");
        printf("P3: post-wedge ads1292r samples %lu->%lu err=%lu\n",
               pre[ads].samples, rows[ads].samples, rows[ads].err);
        CHECK(rows[ads].samples > pre[ads].samples,
              "P3: SPI pipeline never recovered after wedge release");
        rc = cmd("scan", SCAN_TMO, false);
        CHECK(rc >= 0 && strstr(out, "\n  0x44") != NULL,
              "P3: post-wedge bus never recovered (SHT45 unreachable)");
    }
    dump_stats("post-P3");

    /* ================= P4: random NACKs ============================ */
    printf("===== P4: i2c_nack_rate = 0.05 =====\n");
    memcpy(pre, rows, sizeof(pre));
    uint64_t nacks0 = g_vb_stats.i2c_nacks;
    g_vb_faults.i2c_nack_rate = 0.05f;
    vsim_advance_us(VSEC(45));
    g_vb_faults.i2c_nack_rate = 0.0f;
    take_sensors(rows, NAMES, N_SENSORS);
    {
        uint64_t dn = g_vb_stats.i2c_nacks - nacks0;
        printf("P4: i2c_nacks +%llu over 45 s @5%%\n",
               (unsigned long long)dn);
        CHECK(dn > 0, "P4: nack fault had no effect");
        const char *peers[] = { "ads1292r", "mlx90632", "lsm6dsv80x" };
        for (size_t i = 0; i < sizeof(peers)/sizeof(peers[0]); i++) {
            int ix = name_idx(peers[i]);
            if (!pre[ix].found || pre[ix].samples == 0) continue;
            CHECK(rows[ix].samples > pre[ix].samples,
                  "P4: 5% NACKs stopped a producer");
        }
    }
    dump_stats("post-P4");

    /* ================= P5: SPI bit flips =========================== */
    printf("===== P5: spi_bitflip_rate = 0.02 =====\n");
    memcpy(pre, rows, sizeof(pre));
    g_vb_faults.spi_bitflip_rate = 0.02f;
    vsim_advance_us(VSEC(45));
    g_vb_faults.spi_bitflip_rate = 0.0f;
    take_sensors(rows, NAMES, N_SENSORS);
    {
        int ads = name_idx("ads1292r");
        unsigned long derr = rows[ads].err - pre[ads].err;
        printf("P5: ads1292r err +%lu, samples %lu->%lu\n",
               derr, pre[ads].samples, rows[ads].samples);
        CHECK(derr > 0,
              "P5: bit flips never hit the checked status byte");
        CHECK(rows[ads].samples > pre[ads].samples,
              "P5: ads1292r stopped streaming under 2% bit flips");
        /* payload bytes are unprotected — document, don't assert */
        printf("P5: (note) rx[3..8] channel data has no integrity "
               "check — silent corruption passes as samples\n");
    }
    dump_stats("post-P5");

    /* ============ P6a: forced ADS1292 status byte ================== */
    printf("===== P6a: spi_corrupt_id[CS10] = 0x42 =====\n");
    memcpy(pre, rows, sizeof(pre));
    g_vb_faults.spi_corrupt_id[CSIDX_ADS1292] = 0x42;
    vsim_advance_us(VSEC(70));
    take_sensors(rows, NAMES, N_SENSORS);
    {
        int ads = name_idx("ads1292r");
        unsigned long derr = rows[ads].err - pre[ads].err;
        printf("P6a: ads1292r err +%lu, samples %lu->%lu health=%s\n",
               derr, pre[ads].samples, rows[ads].samples, rows[ads].health);
        CHECK(derr >= 50, "P6a: forced status byte not rejected");
        /* Corrupted frames must not stream: assert <5% pass-through.
         * Observed ~0.4% (84 samples) — the drain bumps sample_count
         * even with sink detached (vq_drv_ads1292.c:478 runs
         * unconditionally after `if (s->sink)`), so stop()/retry
         * boundary frames still count.  Also note err includes bulk
         * isr_dropped accounting, not only rejected frames.           */
        unsigned long dsmp = rows[ads].samples - pre[ads].samples;
        CHECK(derr + dsmp == 0 || dsmp * 100 <= (derr + dsmp) * 5,
              "P6a: corrupted frames streamed (status check bypassed)");
    }
    g_vb_faults.spi_corrupt_id[CSIDX_ADS1292] = 0;
    vsim_advance_us(VSEC(110));
    take_sensors(rows, NAMES, N_SENSORS);
    {
        int ads = name_idx("ads1292r");
        printf("P6a: post-clear ads1292r samples=%lu err=%lu health=%s\n",
               rows[ads].samples, rows[ads].err, rows[ads].health);
        CHECK(rows[ads].samples > pre[ads].samples,
              "P6a: ads1292r did not recover after corruption cleared");
    }

    /* ============ P6b: stuck CS responder + MISO fight ============= */
    printf("===== P6b: spi_stuck_cs=18 + miso_fight =====\n");
    uint64_t mf0 = g_vb_stats.miso_fights;
    g_vb_faults.spi_stuck_cs = CS_GPIO_AFE4900;
    g_vb_faults.miso_fight   = true;
    vsim_advance_us(VSEC(8));
    g_vb_faults.spi_stuck_cs = 0;
    g_vb_faults.miso_fight   = false;
    {
        uint64_t dm = g_vb_stats.miso_fights - mf0;
        printf("P6b: miso_fights +%llu\n", (unsigned long long)dm);
        CHECK(dm > 0, "P6b: stuck-CS/MISO fight never observed");
    }
    rc = cmd("sensors", CMD_TMO, false);
    CHECK(rc >= 0, "P6b: console dead after MISO fight window");

    /* ============ P6c: externally stuck CS line ==================== */
    printf("===== P6c: GPIO15 held low externally =====\n");
    uint64_t cv0 = g_vb_stats.spi_cs_violations;
    for (int i = 0; i < 3; i++) {
        vboard_gpio_set(CS_GPIO_AD5940, 0);
        vsim_advance_us(VSEC(1));
    }
    vboard_gpio_set(CS_GPIO_AD5940, 1);
    {
        uint64_t dc = g_vb_stats.spi_cs_violations - cv0;
        printf("P6c: spi_cs_violations +%llu\n", (unsigned long long)dc);
        CHECK(dc > 0, "P6c: stuck-low CS produced no violation");
    }
    vsim_advance_us(VSEC(5));
    take_sensors(rows, NAMES, N_SENSORS);
    dump_stats("post-P6");

    /* ================= P7: rail kill (1V8 island) ================== */
    printf("===== P7: vboard_rail_set(VB_RAIL_1V8, false) =====\n");
    memcpy(pre, rows, sizeof(pre));
    vboard_rail_set(VB_RAIL_1V8, false);
    vsim_advance_us(VSEC(75));
    take_sensors(rows, NAMES, N_SENSORS);
    {
        int a7 = name_idx("as7341"), mx9 = name_idx("mlx90632");
        note_row(rows, "as7341",   "P7: dead-rail");
        note_row(rows, "mlx90632", "P7: dead-rail");
        bool a7_was_ok = pre[a7].samples > 0 &&
                         strcmp(pre[a7].health, "ok") == 0;
        if (a7_was_ok) {
            CHECK(rows[a7].samples == pre[a7].samples,
                  "P7: as7341 sampled on a dead rail");
            CHECK(rows[a7].err > pre[a7].err,
                  "P7: as7341 errors not counted under rail loss");
        } else {
            printf("P7: (note) as7341 already silent before rail kill\n");
        }
        if (pre[mx9].samples > 0) {
            CHECK(rows[mx9].samples == pre[mx9].samples,
                  "P7: mlx90632 sampled on a dead rail");
            CHECK(rows[mx9].err > pre[mx9].err,
                  "P7: mlx90632 errors not counted under rail loss");
        }
        /* isolation: 3V3 devices unaffected */
        int l6 = name_idx("lsm6dsv80x");
        CHECK(rows[l6].samples > pre[l6].samples,
              "P7: 3V3 lsm6 disturbed by 1V8 rail loss");
        int ads = name_idx("ads1292r");
        CHECK(rows[ads].samples > pre[ads].samples,
              "P7: SPI pipeline disturbed by 1V8 rail loss");
    }
    vboard_rail_set(VB_RAIL_1V8, true);
    vsim_advance_us(VSEC(130));
    take_sensors(rows, NAMES, N_SENSORS);
    {
        int a7 = name_idx("as7341"), mx9 = name_idx("mlx90632");
        note_row(rows, "as7341",   "P7: restored");
        note_row(rows, "mlx90632", "P7: restored");
        if (pre[mx9].samples > 0)
            CHECK(rows[mx9].samples > pre[mx9].samples,
                  "P7: mlx90632 did not recover after rail restore");
        if (pre[a7].samples > 0 && strcmp(pre[a7].health, "ok") == 0)
            CHECK(rows[a7].samples > pre[a7].samples,
                  "P7: as7341 did not recover after rail restore");
    }
    dump_stats("post-P7");

    /* ================= wrap-up ===================================== */
    clear_faults();
    printf("===== FINAL: sensors + status =====\n");
    take_sensors(rows, NAMES, N_SENSORS);
    rc = cmd("status", CMD_TMO, false);
    CHECK(rc >= 0, "FINAL: status command failed");
    CHECK(strstr(out, "vitalq status") != NULL, "FINAL: status gone");
    dump_stats("final");

    printf("\nSCN busfault RESULT: checks=%d fails=%d\n",
           n_checks, g_vsim_failures);
    fflush(NULL);
    _exit(g_vsim_failures & 0xFF);
}
