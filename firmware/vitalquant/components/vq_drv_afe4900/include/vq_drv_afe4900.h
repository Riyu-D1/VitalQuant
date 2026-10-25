/* vq_drv_afe4900.h — AFE4900YZR (U6) PPG/ECG AFE driver, fix_v22 board.
 *
 * Duty on this board: PPG only (SFH7072 optical front-end). Channels
 * ppg.red / ppg.ir at the 200 Hz profile rate — engineering perfusion
 * proxies, never SpO2 or any clinical claim.
 *
 * Hardware truth (pin_audit_r6/U6 + board_pins.h):
 *   SPI mode 0, <=8 MHz; SEN = CS_AFE4900 (IO18); SDOUT → R90 → IO13.
 *   MOSI/SCK share the I2C_DAT/I2C_CLK balls — the part is in SPI mode
 *   only while I2C_SPI_SEL reads high (tied to +3V3_ANA on fix_v22;
 *   see VQ_AFE4900_SPI_ENABLE in vq_drv_afe4900.c for the polarity
 *   escape hatch — OPEN QUESTION tracked there).
 *   ADC_RDY = IO21 (active-high pulse; timestamped in ISR).
 *   RESETZ  = TCA6408 P0 (default low = reset/power-down).
 *   LED cathodes TX1..TX4 → SFH7072 G1/G2/R/IR; anodes on TX_5V.
 *   ALL LED on-time is owned by led_sched (VQ_LED_AFE4900 chain).
 */
#pragma once
#include "vq_types.h"

/* Register this driver with the sensor registry. Idempotent; returns
 * ESP_OK (or the error from vq_sensor_register). Does not touch the bus —
 * the part is probed later via the vq_sensor_ops lifecycle.            */
esp_err_t vq_afe4900_register(void);

/* Human-readable self-test report (NUL-terminated into buf):
 *   1 MHz + 8 MHz register readback verify, register write/readback,
 *   one LED minimum-current pulse inside a led_sched slot + PD response.
 * Returns ESP_OK when every stage passes, ESP_FAIL/ESP_ERR_* otherwise.
 * buf is always filled (report includes per-stage PASS/FAIL/SKIP).     */
esp_err_t vq_afe4900_selftest(char *buf, size_t len);

/* Diagnostic single-register read (8-bit address space; reg is
 * truncated to 0..0xFF). For console/debug use — not on the hot path. */
esp_err_t vq_afe4900_diag_read(uint16_t reg, uint32_t *val);
