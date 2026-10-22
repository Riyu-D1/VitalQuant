/* vq_drv_ad5940.h — AD5940 impedance AFE driver (U7, fix_v22).
 *
 * One physical AD5940 serves three logical channels via two registry
 * descriptors that share a single SPI device handle and one private
 * driver state:
 *
 *   "ad5940.eda"   — VQ_CH_EDA + VQ_CH_BIOZ, ~5 Hz each
 *                    (EDA = galvanic skin response site J6, 4-pad
 *                     kelvin-sensed; BioZ = electrode site J7, 4-wire)
 *   "ad5940.sweat" — VQ_CH_SWEAT_LACTATE, 1 Hz, required=false
 *                    RESEARCH-GRADE ONLY: the AD5940 low-power
 *                    potentiostat loop drives the J11 sweat electrode
 *                    (WE/RE/CE) and reports raw electrode current.
 *                    This is an engineering signal for lactate-sensor
 *                    characterisation — it is NOT a clinical or
 *                    validated lactate measurement and must not be
 *                    surfaced as one.
 *
 * SPI: SPI2 via vq_spi, mode 0, 8 MHz (self-test also probes at 1 MHz).
 * Reset: expander P2 (VQ_EXP_AD5940_RST), active low.
 * GPIO0 (AD5940) → ESP IO14, INTC0 interrupt output.
 * GPIO2 (AD5940) → NIR730 LED gate (Q4). GP2 has no plain-GPIO mux
 * option; it is driven through its SYNC function via SYNCEXTDEVICE.
 * No crystal fitted — internal HFOSC 32 MHz + LFOSC only.
 */
#pragma once
#include "vq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register both logical sensors ("ad5940.eda", "ad5940.sweat") with the
 * vq sensor registry.  Idempotent; safe to call from the driver table.  */
esp_err_t vq_ad5940_register(void);

/* Console/self-test entry.  Fills buf with a bounded text report covering:
 *   - CHIPID probe at 1 MHz and 8 MHz (expected 0x5500/0x5501/0x5502)
 *   - internal-oscillator status (no external crystal assumed)
 *   - RCAL channel measurement (DFT magnitude + derived ohms)
 *   - GPIO0 → IO14 interrupt loopback (sequencer-generated custom int)
 * Returns ESP_OK on write success (test results are in the text).        */
esp_err_t vq_ad5940_selftest(char *buf, size_t len);

/* Raw NIR730 gate: asserts/deasserts AD5940 GPIO2 (ball E1 → Q4 gate).
 * This is the hardware primitive ONLY — it toggles the gate immediately
 * and does not enforce LED scheduling.  Callers wanting an NIR pulse
 * must go through vq_ad5940_nir_submit()/vq_led_submit so led_sched
 * remains the single owner of the NIR-chain timing/priority/safety.
 * (The led_sched cb_on/cb_off callbacks registered by
 *  vq_ad5940_nir_submit() call this raw gate.)                           */
esp_err_t vq_ad5940_nir_gate(bool on);

/* Thin scheduler submission for the VQ_LED_NIR730 chain.
 * on_us = requested on-time, period_us = chain period; actual start
 * time is returned in *slot_start_us (may be NULL).  The submitted
 * callbacks assert/deassert the AD5940 gate — no direct callers
 * bypass the scheduler.                                                 */
esp_err_t vq_ad5940_nir_submit(uint32_t on_us, uint32_t period_us,
                               int64_t *slot_start_us);

/* Measure the external RCAL resistor network (RCAL0/RCAL1, J1 pins
 * B1/C1 — R97 etc., ~10 kΩ nominal).  *ohm receives the derived
 * resistance; on first call this anchors the running ratio reference,
 * later calls return RCAL_nom × mag/mag_ref (repeatability check).
 * NOTE: ohms are engineering units anchored to the BOM RCAL value —
 * absolute ohms require the DFT scale constant, TODO(datasheet).       */
esp_err_t vq_ad5940_run_rcal(float *ohm);

#ifdef __cplusplus
}
#endif
