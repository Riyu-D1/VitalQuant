/* scn_faults.c — deliberate breakage: the firmware must degrade, not die.
 *
 *   Injects each board fault class, lets traffic flow, checks the
 *   console is still alive + the system recovers when the fault lifts.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

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
    printf("----- %s (rc=%d) -----\n%.3000s\n", c, rc, out);
    return rc;
}

static void clear_faults(void)
{
    memset(&g_vb_faults, 0, sizeof g_vb_faults);
}

int main(void)
{
    if (!getenv("VSIM_TIME_SCALE"))
        vboard_clock_set_scale(20.0);

    vsim_boot();
    vsim_advance_us(4 * 1000 * 1000);
    int rc = cmd("status", 20000);
    CHECK(rc >= 0, "baseline status OK");

    /* ---- 1. I2C NACK storm ---------------------------------------- */
    g_vb_faults.i2c_nack_rate = 0.6f;
    vsim_advance_us(3 * 1000 * 1000);
    rc = cmd("scan", 30000);
    CHECK(rc >= 0, "console alive through i2c nack storm");
    g_vb_faults.i2c_nack_rate = 0.0f;
    vsim_advance_us(2 * 1000 * 1000);
    rc = cmd("sensors", 30000);
    CHECK(rc >= 0, "post-nack sensors cmd responds");

    /* ---- 2. SDA stuck low (wedged bus) ----------------------------- */
    g_vb_faults.i2c_sda_stuck_low = true;
    vsim_advance_us(2 * 1000 * 1000);
    rc = cmd("status", 30000);
    CHECK(rc >= 0, "console alive with SDA stuck");
    g_vb_faults.i2c_sda_stuck_low = false;

    /* ---- 3. SPI bit flips ------------------------------------------ */
    g_vb_faults.spi_bitflip_rate = 0.15f;
    vsim_advance_us(3 * 1000 * 1000);
    rc = cmd("selftest spi_ids", 60000);
    printf("  spi_ids under bitflip: %s\n",
           strstr(out, "FAIL") ? "degraded (expected-ish)" : "held");
    CHECK(rc >= 0, "console alive through spi bitflips");
    clear_faults();

    /* ---- 4. heap exhaustion ---------------------------------------- */
    g_vb_faults.heap_fail_after = 0;
    vsim_advance_us(1 * 500 * 1000);
    rc = cmd("status", 30000);
    CHECK(rc >= 0, "survives heap_fail_after=0");
    g_vb_faults.heap_fail_after = -1;

    /* ---- 5. wifi link down ----------------------------------------- */
    g_vb_faults.wifi_link_down = true;
    rc = cmd("wifi status", 20000);
    CHECK(rc >= 0, "wifi status rc under link-down");
    g_vb_faults.wifi_link_down = false;

    /* ---- 6. recovery check ----------------------------------------- */
    vsim_advance_us(5 * 1000 * 1000);
    rc = cmd("sensors", 30000);
    CHECK(rc >= 0 && strstr(out, "streaming") != NULL,
          "pipeline still streaming after fault storm");
    rc = cmd("selftest i2c_scan", 60000);
    CHECK(rc >= 0 && strstr(out, "PASS") != NULL,
          "i2c_scan PASS after faults lifted");

    printf("\n=== scn_faults: %s (failures=%d) ===\n",
           g_fail ? "FAIL" : "PASS", g_fail);
    fflush(stdout);
    _exit(g_fail);
}
