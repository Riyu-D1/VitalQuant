/* imu_feed.h — public surface of the sim/vboard/devices models built so
 * far: the LSM6DSV80X motion feed plus the registration hooks for the
 * device models in this directory.  There is no other public header —
 * the board population code (sim/vboard/vboard.c / board_init) includes
 * this to wire the default device set.
 */
#ifndef IMU_FEED_H
#define IMU_FEED_H

#include "vboard.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the device models on the virtual board.  Both are
 * idempotent and also self-register at load via a GNU constructor,
 * matching the other models in this directory.
 *
 *   lsm6    — LSM6DSV80X IMU,  I2C 0x6A, INT1 -> IO4 (push-pull, high)
 *   rv3028  — RV-3028-C7 RTC,  I2C 0x52, /INT unwired (TP28 only)     */
void vbdev_lsm6_register(void);
void vbdev_rv3028_register(void);

/* Motion feed for the LSM6 model: the accel/gyro vector the part
 * "senses" — every generated sample is the current feed quantised to
 * the configured full-scale.  Defaults at power-on: accel {0,0,+1} g
 * (static gravity, board face-up) and gyro {0,0,0} dps.
 * Pass NULL for either array to restore its default.  Units match the
 * firmware's registry convention (accel_g in g, gyro_dps in dps).    */
void vbimu_set_motion(const float accel_g[3], const float gyro_dps[3]);

/* Read back the currently fed vector (test convenience; may be NULL). */
void vbimu_get_motion(float accel_g[3], float gyro_dps[3]);

#ifdef __cplusplus
}
#endif

#endif /* IMU_FEED_H */
