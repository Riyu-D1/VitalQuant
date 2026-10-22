/* vq_drv_imu.h — LSM6DSV80X (U14) 6-axis IMU driver, fix_v22.
 *
 * Device: ST LSM6DSV80X on I2C0 @ 0x6A — SDO/SA0 strapped to GND, CS tied
 * to +3V3 (I2C mode). INT1 → IO4, push-pull, active-high, no external
 * pull (board_pins.h VQ_LSM6_INT1_GPIO) → the driver enables the
 * internal pull-down so the line idles low.
 *
 * Streams channels acc_x/y/z + gyr_x/y/z at 52.0 Hz (required device —
 * motion gating feeds the model's alarm path). Units follow the
 * platform registry convention: accel in g, gyro in dps
 * (docs/02-research-notes: MotionSensor { accel_g[3], gyro_dps[3] }).
 *
 * NOTE on 52 Hz: the plain ODR table has no 52 Hz step (1.875/7.5/15/
 * 30/60/...). This part's high-accuracy-ODR mode does: OP_MODE_x = HAODR
 * + ODR nibble 0b0101 with HAODR_CFG.HAODR_SEL = 0b11 (band 3 = the
 * classic 13/26/52/104 table) → exactly 52.0 Hz, both accel and gyro.
 *
 * Batching: the device FIFO runs in FIFO mode with the watermark at 52
 * tagged 7-byte words = 26 accel+gyro sample pairs (~0.5 s @ 52 Hz);
 * INT1 is routed to FIFO-TH (+overrun). The drain task burst-reads the
 * tagged FIFO window, decodes per-sample tags, and stamps samples per
 * the vq_types.h FIFO contract: esp_timer latched at the INT1 edge,
 * per-frame back-off at 52 Hz through the registry anchor helper.
 * The on-chip timestamp engine stays OFF (esp_timer is the anchor) and
 * the ±80 g high-g channel is unused — ±8 g low-g covers motion gating.
 *
 * Deep sleep: vq_imu_arm_wake(true) configures the WAKE_UP event →
 * INT1 (latched, ~250 mg threshold) before ops.sleep() parks the part;
 * sleep() keeps the accel running at 15 Hz low-power so the wake engine
 * still works. EXT1 arming on IO4 itself is power_mgr's job (§4.4).
 *
 * Research prototype — engineering data only, no clinical claims.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the "lsm6dsv80x" sensor into the vq registry. Idempotent. */
esp_err_t vq_imu_register(void);

/* Bring-up self-test: WHO_AM_I (0x70 family), one 52 Hz ODR burst
 * (XLDA + data readback), INT1 edge delivery via INT1_DRDY_XL, and a
 * wake-arm register roundtrip. Fills a human-readable report.
 * Refuses while streaming. */
esp_err_t vq_imu_selftest(char *report, size_t len);

/* Arm/disarm the wake-on-motion path for deep sleep: configures
 * WAKE_UP_THS/DUR + INT1 routing (MD1_CFG.INT1_WU, latched LIR,
 * FUNCTIONS_ENABLE.INTERRUPTS_ENABLE) so motion asserts INT1 while
 * the part is parked. ops.sleep() applies the flag. */
esp_err_t vq_imu_arm_wake(bool on);

/* Motion energy for the feature layer: RMS of the accel vector
 * magnitude |a| in g over the last window_s seconds (1 s resolution,
 * clamps to the deepest buffered window — see IMU_MOT_WIN_S in
 * vq_drv_imu.c). ≈1 g at rest (static gravity included), matching the
 * features.c "mot_rms_g" convention. NAN until the first full second. */
float vq_imu_motion_rms(uint32_t window_s);

/* Seconds since the last detected motion (|a| rms > threshold in a
 * 1 s bin, or |ω| above the gyro spin threshold in one frame).
 * 0 while never sampled / moving right now. */
uint32_t vq_imu_still_s(void);

#ifdef __cplusplus
}
#endif
