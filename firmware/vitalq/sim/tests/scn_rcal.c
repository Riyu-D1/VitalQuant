#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "vsim.h"
#include "vboard.h"
int main(void) {
    vboard_clock_set_scale(2.0);
    vsim_boot();
    vsim_advance_us(4 * 1000 * 1000);
    char out[16384];
    int rc = vsim_cmd("selftest ecg_bioz", out, sizeof(out), 120000);
    printf("rc=%d\n%.4000s\n", rc, out);
    _exit(0);
}
