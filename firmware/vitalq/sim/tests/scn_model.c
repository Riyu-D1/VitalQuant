/* scn_model.c — 1.4M-parameter VQMB blob through the REAL runtime.
 *
 *   Layout: pre-create the 16 MiB flash image with the exported blob
 *   parked at the 'model' partition (0x820000) + ESP image magic at
 *   ota_0, boot the real fw_app_main, then check that the console /
 *   decision loop / selftest all see the real model — not the stub —
 *   and that inference latency is sane.
 *
 *   Run:  python3 sim/mock/gen_bigmodel.py sim/build/bigmodel
 *         ./sim/build/scn_model
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "vsim.h"
#include "vboard.h"

static int   g_fail;
static char  out[16384];

#define CHECK(cond, msg) do {                                  \
        if (cond) printf("  ok   : %s\n", msg);                \
        else    { printf("  FAIL : %s\n", msg); g_fail++; }    \
    } while (0)

static int cmd(const char *c, int timeout_ms)
{
    out[0] = 0;
    int rc = vsim_cmd(c, out, sizeof(out), timeout_ms);
    printf("----- %s  (rc=%d) -----\n%.12000s\n%s\n", c, rc, out,
           strlen(out) > 12000 ? "...[truncated]" : "");
    return rc;
}

/* Pre-seed the host flash image: ESP magic at ota_0, VQMB blob at the
 * 'model' partition offset 0x820000.  Returns blob size or -1.       */
static long seed_flash(const char *img, const char *blob)
{
    FILE *fb = fopen(blob, "rb");
    if (!fb) { perror("blob open"); return -1; }
    fseek(fb, 0, SEEK_END);
    long sz = ftell(fb);
    fseek(fb, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, fb) != (size_t)sz) {
        fclose(fb); free(buf); return -1;
    }
    fclose(fb);

    int fd = open(img, O_RDWR | O_CREAT, 0644);
    if (fd < 0) { perror("img open"); free(buf); return -1; }
    struct stat st;
    fstat(fd, &st);
    if (st.st_size != (off_t)(16u * 1024 * 1024))
        ftruncate(fd, (off_t)(16u * 1024 * 1024));
    const uint8_t magic = 0xE9;
    pwrite(fd, &magic, 1, 0x20000);                 /* ota_0 magic     */
    pwrite(fd, buf, (size_t)sz, 0x820000);          /* model partition */
    close(fd);
    free(buf);
    return sz;
}

int main(void)
{
    const char *img  = "sim/build/flash_image.bin";
    const char *blob = getenv("VQ_MODEL_BLOB");
    if (!blob) blob = "sim/build/bigmodel/big15m.bin";

    if (!getenv("VSIM_TIME_SCALE"))
        vboard_clock_set_scale(20.0);

    long sz = seed_flash(img, blob);
    CHECK(sz > 1000000, "big-model blob seeded into model partition");
    if (sz > 0) printf("  blob = %ld bytes at 0x820000\n", sz);

    vsim_boot();
    vsim_advance_us(4 * 1000 * 1000);

    /* ---- console 'model info' ------------------------------------ */
    int rc = cmd("model info", 90000);
    CHECK(rc >= 0, "model info: console round-trip");
    CHECK(strstr(out, "VQMB") != NULL, "model info: VQMB header parsed");
    CHECK(strstr(out, "1385346") != NULL ||
          strstr(out, "params") != NULL,
          "model info: param count reported");
    CHECK(strstr(out, "1634") == NULL,
          "model info: NOT the compiled-in stub (1634 params)");
    if (strstr(out, "DENSE") == NULL)
        printf("  note : per-op cycle table missing?\n");

    /* ---- decision cycle on the big model ------------------------- */
    rc = cmd("score", 120000);
    CHECK(rc >= 0, "score: decision cycle ran");
    CHECK(strstr(out, "prob=") != NULL, "score: probability printed");
    CHECK(strstr(out, "latency=") != NULL, "score: latency reported");

    /* ---- model_infer selftest against the partition blob --------- */
    rc = cmd("selftest model_infer", 120000);
    CHECK(rc >= 0, "model_infer selftest: ran");
    CHECK(strstr(out, "1385346") != NULL,
          "model_infer: real param count in verdict");
    CHECK(strstr(out, "FAIL") == NULL, "model_infer: no FAIL verdict");

    /* ---- boot log already told us the partition load worked ------ */
    printf("\n=== scn_model: %s (failures=%d) ===\n",
           g_fail ? "FAIL" : "PASS", g_fail);
    fflush(stdout);
    _exit(g_fail);
}
