/* selftest.h — hw brief §9 bring-up suite. Each test is a console
 * command plus `selftest all`; results print pass/fail + measured values
 * over USB CDC, and feed the acceptance report (§11.3).
 */
#pragma once
#include "vq_types.h"

typedef struct {
    const char *name;                 /* "rails", "i2c_scan", ...        */
    vq_err_t (*run)(char *report, size_t len);  /* fills report text     */
} vq_test_t;

const vq_test_t *vq_selftests(size_t *count);
vq_err_t  vq_selftest_run(const char *name, char *report, size_t len);
vq_err_t  vq_selftest_run_all(char *report, size_t len);

/* Required test names (brief §9):
 *   rails_boot      reset reason, strap readback (VDD_SPI=3.3V), flash
 *                   16 MB, PSRAM 8 MB octal, ≥1 MB PSRAM march, flash CRC
 *   i2c_scan        0x20,36,39,3A,44,48,49,52,6A,76 (+opt 0x4A) + IDs
 *   expander_walk   P0-P4,P7 walk + P5/P6 track VBUS
 *   spi_ids         per-device ID @1 MHz then operating clock
 *   miso_contention all CS high → MISO floats (pull-up vs pull-down)
 *   irq_latency     trigger each ready/INT line, measure edge latency
 *   power_paths     TX_5V on/off (ONE chain min current), gauge vs DMM,
 *                   charger state w/ VBUS in/out, CHG_DIS
 *   usb_cdc         enumeration + post-deep-sleep reconnect
 *   sleep_current   everything parked per §4.4 → measure at J2
 *   optical         one chain at a time, PD response, AS7341 dark/light,
 *                   NIR730 via AD5940 GPIO2
 *   ecg_bioz        ADS1292 test signal + lead-off; AD5940 RCAL
 *   mic_pdm         1 s capture → RMS
 *   fsr_adc         rest vs pressed raw counts
 *   model_infer     blob CRC, arena fit, golden-vector check
 *   led_interlock   scheduler trace shows zero overlap, duty <=1%
 *   decision_loop   features→model→(forced) escalation path dry-run
 */
