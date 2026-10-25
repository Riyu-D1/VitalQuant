/* vq_drv_mic.h — IM69D130 PDM MEMS microphone driver (U25, fix_v22).
 *
 * Hardware (board_pins.h truth):
 *   CLK    = IO40 out  — PDM clock, 2.048 MHz (IM69D130 legal 1.0–3.25 MHz)
 *   DAT    = IO41 in   — 1-bit PDM data
 *   SELECT = GND       — mic answers the LEFT slot
 *   VDD    = +3V3 always-on domain; 100 nF decap at the part
 *
 * The part is FITTED per BOM but probe-optional: a PDM mic has no bus
 * address, no WHO_AM_I and no ACK — there is no true probe. "Probe" here
 * means capture-and-check-non-silence on demand: a short DMA capture is
 * scanned for a non-flat stream (a live sigma-delta modulator always
 * dithers the LSBs; an unfitted/dead mic or a pinned DATA line yields a
 * constant stream). Absence is a SOFT condition: the sensor registers
 * required=false and probe() may return ESP_ERR_NOT_FOUND, which the
 * pipeline tolerates (vq_sensor.h contract).
 *
 * The mic is NOT a streaming sensor. It provides optional on-demand
 * "chest acoustics" block captures via vq_mic_capture(); start() only
 * arms this on-demand mode — nothing ever streams to the sample sink
 * continuously. After each successful public capture one VQ_CH_MIC block
 * summary sample (RMS in LSB, stamped at capture start) is emitted to the
 * registered sink, if any.
 *
 * vq_mic_sleep(): channel disable stops the PDM clock (the IM69D130
 * enters its low-power state when CLK stops), then IO40 is re-routed
 * from the I2S peripheral to the plain GPIO output and driven low —
 * deterministic off, not dependent on the I2S idle level. IO41 (DATA)
 * is left as the input it already is.
 *
 * Non-medical research prototype — engineering-grade acoustics only.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decimated PCM output rate (samples/s). Buffer sizing for captures:
 * bytes = seconds * VQ_MIC_PCM_RATE_HZ * sizeof(int16_t).              */
#define VQ_MIC_PCM_RATE_HZ   16000u

/* Register sensor "im69d130" (channel VQ_CH_MIC, required=false) in the
 * pipeline registry. Idempotent-ish: safe to call once at boot.        */
esp_err_t vq_mic_register(void);

/* Self-test "mic_pdm": ~1 s capture → RMS + peak-peak non-silence
 * heuristic → text report. NOTE in the report: a silent room may
 * legitimately read low — a low RMS alone is not a failure; only a
 * perfectly flat stream (pp ≈ 0) or a dead capture is.               */
esp_err_t vq_mic_selftest(char *report, size_t len);

/* On-demand block capture. Fills pcm[] with up to `max` samples of
 * 16-bit mono PCM @ VQ_MIC_PCM_RATE_HZ; *n gets the count actually
 * captured (may be short on timeout). Caller supplies the buffer —
 * use a PSRAM allocation for multi-second captures.
 * Enables the clock for the duration of the call only, including an
 * IM69D130 start-up + HP settle window whose data is discarded.       */
esp_err_t vq_mic_capture(float seconds, int16_t *pcm, size_t max, size_t *n);

/* RMS of a PCM block in LSB units (0.0 on empty input).               */
float     vq_mic_rms(const int16_t *pcm, size_t n);

/* Deepest available sleep for pre-sleep prep: I2S channel disabled
 * (clock stops → mic power-down) + IO40 driven low as plain GPIO.
 * The channel stays allocated; the next capture un-parks IO40.        */
esp_err_t vq_mic_sleep(void);

#ifdef __cplusplus
}
#endif
