/* mic_src.h — board-side IM69D130 (U25) signal source for the host sim.
 *
 * NOT an ESP API — this is the vboard device model the esp_shim I2S PDM
 * layer (sim/esp_shim/periph/i2s_sim.c) pulls PCM from, and the knob set
 * scenarios use to steer the acoustic channel.  The model produces the
 * decimated 16-bit mono PCM stream the real silicon would emit at the
 * driver's configured rate (16 kHz, VQ_MIC_PCM_RATE_HZ): a PDM->PCM
 * decimation model is out of scope — we synthesize PCM directly.
 *
 * Presence contract (mirrors vq_drv_mic.c): a live IM69D130 always
 * dithers the LSBs (sigma-delta self-noise), so the default output is a
 * small DC offset + a few-LSB noise floor — a flat stream means
 * power-down/absent and the driver's probe reports ESP_ERR_NOT_FOUND.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef SIM_HOST_BUILD
#ifdef __cplusplus
extern "C" {
#endif

/* Register the "im69d130" vboard_dev.  Idempotent — also invoked by an
 * init constructor in mic_src.c, so explicit calls are optional glue for
 * harnesses that populate the board by hand. */
void    vbdev_mic_register(void);

/* Inject a test tone: sine at `hz` with peak amplitude `amp` in LSB
 * (0..32767, clamped).  hz=0 or amp=0 leaves only the mic noise floor +
 * DC offset — a "silent room", which is still a LIVE stream. */
void    vbdev_mic_set_tone(float hz, float amp);

/* Model power state: true -> the part is powered down and outputs a
 * flat digital-silence stream (0s), false -> normal operation.  The I2S
 * shim additionally forces silence whenever it holds the MIC_CLK pin
 * low; this knob is for scenarios that model a dead/unclocked mic. */
void    vbdev_mic_set_sleep(bool sleeping);

/* Pull the next PCM sample (mono int16 at the modelled 16 kHz rate).
 * Called by the I2S PDM shim while the channel is RUNNING; may also be
 * sampled directly by scenario code. */
int16_t vbdev_mic_next_sample(void);

#ifdef __cplusplus
}
#endif
#endif /* SIM_HOST_BUILD */
