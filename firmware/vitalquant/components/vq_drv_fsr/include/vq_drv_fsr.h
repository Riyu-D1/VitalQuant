/* vq_drv_fsr.h — FSR402 force-sensing-resistor driver + status LED helper.
 * fix_v22 board (ESP32-S3), hw brief §7 channel "contact" @ 20 Hz.
 *
 * ---------------------------------------------------------------------------
 * FSR analog front end (fix_v22 schematic):
 *   J3 pad1 = +3V3 ──[ FSR402 ]──●── FSR_ADC = IO1 (ADC1_CH0)
 *                                │
 *                               [R8 10k]
 *                                │
 *                               GND
 *   Divider node rises with pressure: rest/open FSR ~MOhm -> ~0 counts,
 *   firm press ~1 kOhm -> ~3.0 V. 12-bit, ADC_ATTEN_DB_12 full scale.
 *
 * CHANNEL CONTRACT — VQ_CH_CONTACT ("contact"):
 *   Sample value is an ORDINAL level {0,1,2,3} = vq_fsr_level_t
 *   {off, light, seated, excessive}. NO SI UNITS: the divider is not
 *   calibrated to force or pressure — the bands are engineering
 *   thresholds for contact-quality gating only, never Newtons or kPa.
 *   Downstream consumers must not present the value as a measurement.
 *
 * Sampling: start() runs the driver's own 20 Hz esp_timer (per the v2
 * contract, NOT the pipeline cadence task). Samples are stamped with
 * esp_timer_get_time() AT the adc_oneshot_read() — that read *is* the
 * acquisition; there is no data-ready edge or FIFO, so no
 * VQ_SAMPLE_F_ESTIMATED_TS.
 *
 * The FSR is a passive divider — "absent" (pads open) is electrically
 * identical to level OFF and is reported as such, never as a fault.
 *
 * Boot wiring (done by the app/pipeline, not this header):
 *   vq_fsr_register();  // once, before vq_pipeline_init()
 *   vq_status_led_init();  // main may also just call vq_status_led(true)
 * ---------------------------------------------------------------------------
 */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ordinal contact levels emitted on VQ_CH_CONTACT. Order only —
 * off < light < seated < excessive. NOT SI units (see contract above). */
typedef enum {
    VQ_FSR_LEVEL_OFF = 0,      /* pad open / resting (~<150 counts)      */
    VQ_FSR_LEVEL_LIGHT,        /* light touch                            */
    VQ_FSR_LEVEL_SEATED,       /* normal seated contact                  */
    VQ_FSR_LEVEL_EXCESSIVE,    /* pressed harder than seated band        */
    VQ_FSR_LEVEL_COUNT
} vq_fsr_level_t;

/* Name used when wiring vq_fsr_selftest into the §9 suite (selftest.h). */
#define VQ_FSR_SELFTEST_NAME "fsr_adc"

/* Register sensor descriptor "fsr" (channel VQ_CH_CONTACT, 20 Hz,
 * required=false) into the vq_sensor registry. Idempotent. Call once
 * during boot before vq_pipeline_init(). */
esp_err_t vq_fsr_register(void);

/* Interactive selftest — wires as vq_test_t{name="fsr_adc", run=...}.
 * Reads raw counts at rest, lights the status LED as the "press now"
 * cue, waits ~2.5 s, reads again. Report prints rest/pressed counts
 * (+mV if adc_cali is available), the delta, and both ordinal bands.
 * Purely informational — rest vs pressed is a human-judged check. */
esp_err_t vq_fsr_selftest(char *report, size_t len);

/* One-shot raw ADC counts (0..4095) for console/debug. Lazy-inits the
 * ADC if the pipeline hasn't run init() yet. */
esp_err_t vq_fsr_raw(int *counts);

/* ---------------- status LED (same component, same header) ------------
 * IO17 = VQ_STATUS_LED_GPIO, active-high through R88 150R (~7-9 mA).
 * main() uses it for boot/alive signaling; vq_fsr_selftest() uses it as
 * the "press now" cue. Deep-sleep pin holds are power_mgr's job (§4.4);
 * the driver's sleep() op parks the LED dark + driven-low. */

/* Configure IO17 as output, drive low, create the blink timer.
 * Idempotent; vq_status_led()/blink() lazy-init on first call. */
esp_err_t vq_status_led_init(void);

/* Solid on/off; cancels any blink in progress. */
void      vq_status_led(bool on);

/* Periodic blink via esp_timer (no LEDC — keep it simple; intended for
 * ~2 Hz max). period_ms = full cycle; duty_pct = ON share, 1..99.
 * duty 0 -> solid off, 100 -> solid on, period 0 -> off. */
void      vq_status_led_blink(uint32_t period_ms, uint8_t duty_pct);

#ifdef __cplusplus
}
#endif
