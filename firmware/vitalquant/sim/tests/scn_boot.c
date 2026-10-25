/* scn_boot — boot the real firmware on the virtual board, drive the
 * console: help -> a probe command -> one selftest suite.            */
#include "vsim.h"
#include "vboard.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    vsim_boot();
    vboard_clock_sleep_us(3 * 1000 * 1000);   /* let init settle   */

    char out[16384];
    int rc = vsim_cmd("help", out, sizeof(out), 8000);
    printf("=== help rc=%d ===\n%.3000s\n", rc, out);

    rc = vsim_cmd("selftest list", out, sizeof(out), 15000);
    printf("=== selftest list rc=%d ===\n%.2000s\n", rc, out);

    _exit(g_vsim_failures & 0xFF);
}
