/* fsr_src.h — board-side FSR402 + divider signal source for the host sim.
 *
 * NOT an ESP API — this is the vboard device model the esp_shim ADC
 * oneshot layer (sim/esp_shim/periph/adc_sim.c) reads through, and the
 * knob set scenarios use to steer the contact channel.
 *
 * Front end (fix_v22 schematic, mirrored by vq_drv_fsr):
 *   +3V3 --[ FSR402 ]--+-- FSR_ADC = IO1 (ADC1_CH0)
 *                      |
 *                    [R8 10k]
 *                      |
 *                     GND
 * Divider node rises with pressure: rest/open FSR ~MOhm -> ~0 counts,
 * firm press ~sub-kOhm -> ~3.3 V -> ~4095 counts (12-bit).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef SIM_HOST_BUILD
#ifdef __cplusplus
extern "C" {
#endif

/* Register the "fsr402" vboard_dev.  Idempotent — also invoked by an
 * init constructor in fsr_src.c, so explicit calls are optional glue for
 * harnesses that populate the board by hand. */
void vbdev_fsr_register(void);

/* Applied pressure, ordinal 0..1000 (clamped): 0 = pad open/resting,
 * ~300 light touch, ~500 seated, 1000 = full press.  Maps log-linearly
 * onto the FSR's resistance range — NOT newtons/kPa (the firmware's
 * channel contract is ordinal too, no SI units). */
void vbdev_fsr_set_pressure(int level01k);

/* Current ADC raw counts at the divider node (0..4095), with a few-LSB
 * conversion-noise dither.  Reads ~0 when the +3V3 rail is down.
 * Consumed by adc_sim.c for ADC1_CH0 / IO1. */
int  vbdev_fsr_raw_counts(void);

#ifdef __cplusplus
}
#endif
#endif /* SIM_HOST_BUILD */
