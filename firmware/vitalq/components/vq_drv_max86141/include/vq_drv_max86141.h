/* vq_drv_max86141.h — MAX86141ENP+ optical AFE driver (satellite PPG tail).
 *
 * Hardware notes (fix_v22, pin_audit_r7/U22 + hw_v2 profile):
 *  - Part lives on the J10 tail domain; the schematic net labels say
 *    "CS_MAX86178" / "MAX86178_INT" — LEGACY names. The fitted part is a
 *    MAX86141ENP+ (SPI optical PPG AFE, dual PD-capable; we use PD1 only —
 *    PD2_IN is unconnected on the board).
 *  - SPI mode 0, <= 8 MHz hard cap (VQ_SPI_MAX86141_HZ). CS = IO38, which is
 *    ALSO the active-low OE of the U26 SN74AXC2T245 MISO level shifter —
 *    SDO only drives while CS is low. All transfers therefore go through
 *    spi_bus.h (CS idle high, one CS at a time).
 *  - INT = IO39, open-drain active-low, R113 10k pull-up to +3V3.
 *  - LED1/2/3 cathodes drive the off-board SFH7050A emitters (530/660/940 nm)
 *    on J10; common anode LED_AN = TX_5V through R114 (0R). ALL LED on-time
 *    is declared to the LED scheduler as VQ_LED_MAX86141 (~200 mA class).
 *
 * Sample contract: each FIFO exposure word is emitted as one vq_sample_t on
 * VQ_CH_PPG_SAT with t_us reconstructed from the INT-edge anchor (back-filled
 * per in-batch index → every sample carries VQ_SAMPLE_F_ESTIMATED_TS).
 * Phase identification is by the fixed 4-word exposure order
 * LED1(530)/LED2(660)/LED3(940)/AMBIENT and by the private flag bit below.
 *
 * This is an engineering perfusion/PPG channel for a research prototype —
 * no medical claims.
 */
#pragma once
#include "vq_types.h"

/* Driver-private vq_sample_t.flags bit (0x01/0x02/0x04 are owned by
 * vq_types.h): set on the direct-ambient phase words of the satellite
 * time-mux so consumers can deinterleave LED vs ambient phases. */
#define VQ_MX86141_F_AMBIENT   0x08

/* Register this driver with the sensor registry. Safe to call once at boot
 * before the pipeline walks the registry. */
esp_err_t vq_max86141_register(void);

/* Bring-up self-test (hw brief §9 "spi_ids"/"optical" coverage for this
 * part): PART_ID @1 MHz and @8 MHz, FIFO fill/drain + tag sanity, INT line
 * assert/release check, and a minimum-current LED pulse with PD response
 * measurement. Fills a human-readable report into buf.
 * Returns ESP_ERR_INVALID_STATE if called while streaming, ESP_ERR_NOT_FOUND
 * if the part does not answer (tail population is optional). */
esp_err_t vq_max86141_selftest(char *buf, size_t len);

/* Read the PART_ID register (0xFF). Uses the low-speed probe handle.
 * Expected 0x25 (MAX86141); 0x24 (MAX86140) is accepted as family. */
esp_err_t vq_max86141_read_partid(uint8_t *id);
