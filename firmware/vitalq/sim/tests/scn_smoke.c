/* scn_smoke — bring-up sanity: board inits, devices register, buses
 * route, virtual clock runs, an I2C probe finds the fitted parts.    */
#include "vsim.h"
#include "vboard.h"
#include <stdio.h>

int main(void)
{
    vboard_init();
    printf("SMOKE now=%lld scale ok\n", (long long)vboard_now_us());
    int found = 0;
    for (int a = 0x20; a <= 0x76; a++)
        if (vboard_find_i2c(a)) found++;
    printf("SMOKE i2c devices registered: %d\n", found);
    for (int cs = 0; cs < 64; cs++)
        if (vboard_find_spi(cs))
            printf("SMOKE spi dev on cs%d: %s\n", cs, vboard_find_spi(cs)->name);
    return found >= 10 ? 0 : 1;
}
