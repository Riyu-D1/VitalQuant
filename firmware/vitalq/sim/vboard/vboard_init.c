/* vboard_init.c — scenario boot/shutdown for the virtual fix_v22 board.
 *
 * vboard_init() brings up the idempotent core (rng, env clock scale,
 * GPIO idle levels, rail defaults — see vboard.c), then populates the
 * default device population: every model provides a weak
 * vbdev_*_register() entry point so models can land independently —
 * absent links resolve to NULL and are skipped, present ones call
 * vboard_register() (which dedups by name, so re-init is safe).
 * Finally the tick/IRQ engine is started at the default period.
 */
#include "vboard.h"

#include <stddef.h>

extern void vboard__init_core(void);    /* pthread_once'd core (vboard.c) */

/* Device model entry points — weak so each model file is optional.    */
#define WEAK_REG(name) __attribute__((weak)) extern void vbdev_##name##_register(void)

WEAK_REG(tca6408);
WEAK_REG(max17048);
WEAK_REG(tmp117);
WEAK_REG(sht45);
WEAK_REG(bme280);
WEAK_REG(lsm6);
WEAK_REG(rv3028);
WEAK_REG(as7341);
WEAK_REG(mlx90632);
WEAK_REG(ads1292);
WEAK_REG(afe4900);
WEAK_REG(ad5940);
WEAK_REG(max86141);
WEAK_REG(w25q512);
WEAK_REG(mic);
WEAK_REG(fsr);

typedef void (*vbdev_reg_fn_t)(void);
static vbdev_reg_fn_t const s_registrars[] = {
    &vbdev_tca6408_register,
    &vbdev_max17048_register,
    &vbdev_tmp117_register,
    &vbdev_sht45_register,
    &vbdev_bme280_register,
    &vbdev_lsm6_register,
    &vbdev_rv3028_register,
    &vbdev_as7341_register,
    &vbdev_mlx90632_register,
    &vbdev_ads1292_register,
    &vbdev_afe4900_register,
    &vbdev_ad5940_register,
    &vbdev_max86141_register,
    &vbdev_w25q512_register,
    &vbdev_mic_register,
    &vbdev_fsr_register,
};

void vboard_init(void)
{
    vboard__init_core();                 /* rng/env/gpio/rails/faults   */
    for (size_t i = 0; i < sizeof s_registrars / sizeof s_registrars[0]; i++)
        if (s_registrars[i])             /* weak link absent -> NULL    */
            s_registrars[i]();
    vboard_tick_start(0);                /* default 500µs virtual tick  */
}

void vboard_shutdown(void)
{
    vboard_tick_stop();                  /* join tick thread, flush IRQs */
}
