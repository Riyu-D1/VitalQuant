/* scn_cloud.c — decision → cloud escalation over REAL TCP.
 *
 *   Spawns sim/mock/server.py on 127.0.0.1:g_vsim_cloud_port, steers
 *   the firmware's esp_http_client shim at it, then exercises the full
 *   path: wifi up → cloud set → heartbeat → forced decision escalation
 *   → batch flush — and verifies the mock actually received real JSON
 *   with an Authorization header.
 *
 *   Run: ./sim/build/scn_cloud   (writes sim/build/cloud_rx/*)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "vsim.h"
#include "vboard.h"

#define CLOUD_PORT 8499
#define OUTDIR     "sim/build/cloud_rx"

static int   g_fail;
static char  out[16384];
static pid_t g_srv = -1;

#define CHECK(cond, msg) do {                                  \
        if (cond) printf("  ok   : %s\n", msg);                \
        else    { printf("  FAIL : %s\n", msg); g_fail++; }    \
    } while (0)

static int cmd(const char *c, int timeout_ms)
{
    out[0] = 0;
    int rc = vsim_cmd(c, out, sizeof(out), timeout_ms);
    printf("----- %s  (rc=%d) -----\n%.8000s\n%s\n", c, rc, out,
           strlen(out) > 8000 ? "...[truncated]" : "");
    return rc;
}

static int file_has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[4096];
    int found = 0;
    while (fgets(buf, sizeof buf, f))
        if (strstr(buf, needle)) { found = 1; break; }
    fclose(f);
    return found;
}

int main(void)
{
    if (!getenv("VSIM_TIME_SCALE"))
        vboard_clock_set_scale(20.0);

    /* ---- spawn the mock cloud ------------------------------------ */
    system("mkdir -p " OUTDIR " && rm -f " OUTDIR "/received.jsonl "
           OUTDIR "/stats.json");
    g_srv = fork();
    if (g_srv == 0) {
        execlp("python3", "python3", "sim/mock/server.py",
               "--port", "8499", "--outdir", OUTDIR, NULL);
        _exit(127);
    }
    CHECK(g_srv > 0, "mock cloud spawned");
    usleep(800 * 1000);                          /* let it bind       */
    g_vsim_cloud_port = CLOUD_PORT;

    /* ---- boot + configure ---------------------------------------- */
    vsim_boot();
    vsim_advance_us(4 * 1000 * 1000);

    int rc = cmd("wifi set testssid testpass", 20000);
    CHECK(rc >= 0 && strstr(out, "failed") == NULL, "wifi creds stored");
    rc = cmd("wifi connect", 30000);
    CHECK(rc >= 0, "wifi connect attempted");
    vsim_advance_us(2 * 1000 * 1000);
    rc = cmd("wifi status", 15000);
    CHECK(rc >= 0, "wifi status rc");

    rc = cmd("cloud set http://cloud.invalid/vq devkey-TEST-001", 20000);
    CHECK(rc >= 0 && strstr(out, "configured") != NULL, "cloud configured");

    /* ---- heartbeat (smallest real POST) --------------------------- */
    /* TX5/wifi-burst interlock defers POSTs behind LED activity; the
     * drain task retries pended requests every 30 s virtual, but under
     * sustained LED churn the gate can stay shut for minutes — re-issue
     * periodically to give the heartbeat fresh 5 s gate windows.       */
    rc = cmd("cloud heartbeat", 30000);
    CHECK(rc >= 0, "heartbeat command ran");
    for (int i = 0; i < 12; i++) {
        vsim_advance_us(30 * 1000 * 1000);
        rc = cmd("cloud stats", 15000);
        if (strstr(out, "ok=0 fail=0") == NULL) break;
        if ((i % 3) == 2) cmd("cloud heartbeat", 30000);
    }

    /* ---- forced escalation → /v1/edge/escalation ------------------ */
    rc = cmd("score", 120000);
    CHECK(rc >= 0, "score cycle ran");
    vsim_advance_us(10 * 1000 * 1000);

    rc = cmd("cloud flush 8", 60000);
    CHECK(rc >= 0, "flush ran");
    for (int i = 0; i < 8; i++) {
        vsim_advance_us(35 * 1000 * 1000);
        rc = cmd("cloud stats", 15000);
        if (strstr(out, "ok=0 fail=0") == NULL) break;
    }
    CHECK(rc >= 0, "cloud stats rc");
    if (strstr(out, "ok="))
        printf("  stats: %.80s\n", strstr(out, "ok="));

    /* ---- server-side verification -------------------------------- */
    usleep(400 * 1000);
    int got_esc   = file_has(OUTDIR "/received.jsonl", "escalation");
    int got_hb    = file_has(OUTDIR "/received.jsonl", "heartbeat");
    int got_auth  = file_has(OUTDIR "/stats.json", "devkey-TEST-001");
    int got_batch = file_has(OUTDIR "/received.jsonl", "ingest");
    CHECK(got_esc || got_batch, "mock cloud received a real POST body");
    /* which POST type lands is scheduler-dependent: the TX5/wifi-burst
     * interlock (design intent — shared ~700 mA rail) can keep the gate
     * shut for minutes under sim LED churn, so heartbeat and escalation
     * race. Both paths funnel through the same do_post → either reaching
     * the mock proves transport + auth end-to-end.                    */
    CHECK(got_hb || got_esc || got_batch,
          "a POST (heartbeat or escalation) reached the mock");
    CHECK(got_auth, "Authorization: Bearer devkey-TEST-001 seen");

    printf("\n=== scn_cloud: %s (failures=%d) ===\n",
           g_fail ? "FAIL" : "PASS", g_fail);
    fflush(stdout);
    if (g_srv > 0) kill(g_srv, SIGTERM), waitpid(g_srv, NULL, 0);
    _exit(g_fail);
}
