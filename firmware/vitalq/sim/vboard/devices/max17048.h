/* max17048.h — host-sim model exports for the MAX17048G+ fuel gauge
 * (I2C 0x36, VBAT-powered — the board's battery-cutoff sensor).
 *
 * Contract header for the model in sim/vboard/devices/max17048.c; the
 * vboard core includes this for registration and scenario control.
 */
#ifndef VBDEV_MAX17048_H
#define VBDEV_MAX17048_H

#ifdef __cplusplus
extern "C" {
#endif

/* Register the model with the vboard core (name "max17048", addr 0x36).
 * Call once from vboard_init's default device population.             */
void vbdev_max17048_register(void);

/* Scenario override of the SOC register (0x04), percent.
 *   pct >= 0 → SOC reads back pct (VCELL stays live off vboard_vbat_mv);
 *   pct <  0 → resume the auto map (linear 3.3 V→0 % / 4.2 V→100 %).
 * A firmware QuickStart (MODE 0x06 bit14) also drops the override —
 * the IC re-seeds SOC from instantaneous VCELL.                      */
void vbdev_gauge_set_soc_pct(float pct);

#ifdef __cplusplus
}
#endif

#endif /* VBDEV_MAX17048_H */
