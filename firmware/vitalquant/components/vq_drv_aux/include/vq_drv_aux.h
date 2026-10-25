/* vq_drv_aux.h — always-on auxiliary I2C devices for fix_v22 / hw_v3.
 *
 *   name       addr  role
 *   rv3028     0x52  RV-3028-C7 RTC (U24) — absolute wall-clock anchor +
 *                    polled flags. FITTED per fix_v22 BOM but registered
 *                    probe-optional (D5 ambiguity; clock_service tolerates
 *                    absence). RTC_INT goes to TP28 ONLY — no interrupt
 *                    reaches the ESP — so all flag handling is polled.
 *                    VBACKUP is grounded via R115 (no backup cell): every
 *                    +3V3 cold start is a POR and wall time is lost;
 *                    it survives only a warm ESP reset. Produces NO
 *                    sample channels — channels=NULL, n_channels=0 is
 *                    intentional: it is a clock resource, registered only
 *                    so the pipeline probe/init/health bookkeeping covers
 *                    it like every other board device.
 *   max17048   0x36  MAX17048G+ fuel gauge (U17) — powered from VBAT:
 *                    ALWAYS-ON whenever a cell is attached, and it IS the
 *                    battery-cutoff sensor + deep-sleep wake source
 *                    (ALRT → GAUGE_ALRT IO6, open-drain, R74 10k pull to
 *                    +3V3; power_mgr arms it as an EXT1 wake). Channels
 *                    batt_v + batt_soc at 0.2 Hz. Because of this role
 *                    the IC is never put to sleep — see sleep() note in
 *                    vq_max17048.c.
 *
 * Both devices live on the single 100 kHz I2C0 bus — all traffic goes
 * through the mutexed vq_i2c_* helpers (i2c_bus.h).
 *
 * RESEARCH PROTOTYPE — engineering telemetry only; no medical claims.
 */
#pragma once
#include "vq_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* registration + bring-up self test                                    */
/* ------------------------------------------------------------------ */

/* Register both aux descriptors with the pipeline registry
 * (vq_sensor_register). Idempotent; call once before pipeline start.
 * Absent hardware is handled per-device in probe(), never here. */
esp_err_t vq_aux_register(void);

/* §9-style self test: gauge VERSION/VCELL/SOC readback (vs DMM at J2),
 * alert threshold write/readback, ALRT line state; RTC seconds-advance +
 * STATUS flag poll + unix conversion. Fills `report` (always
 * NUL-terminated); ESP_OK when every REQUIRED device passes (the RTC is
 * optional — absent is reported, not failed). */
esp_err_t vq_aux_selftest(char *report, size_t len);

/* ------------------------------------------------------------------ */
/* MAX17048 fuel gauge (0x36, VBAT-powered)                             */
/* ------------------------------------------------------------------ */

/* STATUS register (0x1A) alert-cause flags — low byte, write-0-to-clear. */
#define VQ_GAUGE_ST_RI   0x01u   /* reset indicator (POR'd / unconfigured) */
#define VQ_GAUGE_ST_VH   0x02u   /* VCELL above VALRT.MAX                  */
#define VQ_GAUGE_ST_VL   0x04u   /* VCELL below VALRT.MIN                  */
#define VQ_GAUGE_ST_VR   0x08u   /* voltage reset (battery-swap detect)    */
#define VQ_GAUGE_ST_HD   0x10u   /* SOC crossed the ATHD empty threshold   */
#define VQ_GAUGE_ST_SC   0x20u   /* 1% SOC change (only when ALSC set)     */

/* Live VCELL/SOC read — works whenever the chip ACKs, independent of
 * streaming state (power_mgr polls this for the cut-off decision).
 *   *vcell_v → volts (78.125 µV/LSB ADC average)
 *   *soc_pct → percent (SOC reg / 256)
 * Either pointer may be NULL. */
esp_err_t vq_gauge_read(float *vcell_v, float *soc_pct);

/* CRATE (0x16): signed, 0.208 %/hr per LSB — positive = charging. */
esp_err_t vq_gauge_crate(float *pct_per_hr);

/* Program the alert block used by the power state machine:
 *   VALRT (0x14)  VCELL.MIN≈3.2 V / MAX≈4.3 V window (20 mV/LSB — see
 *                 the TODO(datasheet) note in vq_max17048.c)
 *   CONFIG.ATHD   SOC-low alert ≈ VQ_SOC_LOW_PCT (5 %) → STATUS.HD
 *   STATUS + ALRT flag left cleared on success (ready to edge).
 * Idempotent; called by init and may be re-called any time the device
 * is present (power_mgr calls it directly). */
esp_err_t vq_gauge_config_alerts(void);

/* Read STATUS alert-cause flags (VQ_GAUGE_ST_*). Call BEFORE
 * vq_gauge_clear_alert() — clearing STATUS is what de-asserts the line. */
esp_err_t vq_gauge_alert_status(uint8_t *flags);

/* Service an alert: clear pending STATUS cause bits then the master
 * CONFIG.ALRT flag → the open-drain ALRT line releases high and the next
 * threshold crossing can edge again. */
esp_err_t vq_gauge_clear_alert(void);

/* Arm the ALRT line: IO6 input, NEGEDGE interrupt; cb runs in TASK
 * context (the gauge's own low-priority task — created on demand so the
 * path works even when the sensor is not streaming). cb should call
 * vq_gauge_alert_status() then vq_gauge_clear_alert(). Pass NULL to
 * detach the handler. A level already held low at install still
 * dispatches once (a level line cannot re-edge). */
esp_err_t vq_gauge_on_alert(void (*cb)(void *), void *ctx);

/* MODE.QuickStart — re-seeds OCV/SOC estimate from instantaneous VCELL.
 * USE SPARINGLY: datasheet warns it can jump SOC badly under load or on
 * an unrelaxed cell. Kept for console/debug only — never auto-issued
 * (POR already includes a quick-start; QSTRT pin is grounded in hw). */
esp_err_t vq_gauge_quickstart(void);

/* True after probe/init saw (ver & 0xFFF0) == 0x0010. */
bool      vq_gauge_present(void);

/* Diagnostics: ALRT negedges latched by the ISR (armed or not). */
uint32_t  vq_gauge_alert_edges(void);

/* ------------------------------------------------------------------ */
/* RV-3028-C7 RTC (0x52) — wall-clock anchor, polled (INT not wired)    */
/* ------------------------------------------------------------------ */

/* STATUS register (0x0E) flags — write-0-to-clear (bit7 is read-only). */
#define VQ_RTC_ST_PORF   0x01u   /* power-on flag: set at every VDD POR —
                                * with VBACKUP grounded this means
                                * "wall time was lost"                  */
#define VQ_RTC_ST_EVF    0x02u   /* external event (EVI is grounded)     */
#define VQ_RTC_ST_AF     0x04u   /* alarm flag                           */
#define VQ_RTC_ST_TF     0x08u   /* countdown-timer flag                 */
#define VQ_RTC_ST_UF     0x10u   /* periodic time-update flag            */
#define VQ_RTC_ST_BSF    0x20u   /* backup switchover flag               */
#define VQ_RTC_ST_CLKF   0x40u   /* clock-output interrupt flag          */
#define VQ_RTC_ST_EEBUSY 0x80u   /* EEPROM busy (read-only)              */
#define VQ_RTC_ST_FLAGS  0x7Fu   /* every latchable flag, no EEBUSY      */

/* Civil time: year 2000-2099, month 1-12, day 1-31, weekday 0-6
 * (firmware convention 0=Sunday — the chip's weekday is a free-running
 * 3-bit counter with no datasheet-fixed day mapping), hour 0-23
 * (this driver forces 24 h mode), minute/second 0-59. */
typedef struct {
    uint16_t year;
    uint8_t  month, day, weekday, hour, minute, second;
} vq_rtc_time_t;

/* Presence: true after probe/init ACK'd the part (U24 is BOM-fitted but
 * probe-optional — absent must never be fatal). */
bool      vq_rtc_present(void);

/* Read the calendar block (0x00-0x06, one burst) + decode.
 *   *t      → decoded civil fields (always filled on ESP_OK)
 *   *valid  → false when the time can't be trusted: PORF was latched
 *             since last init/set (power lost — no backup cell) or a
 *             set-time has never happened; may be NULL.
 * Returns ESP_ERR_INVALID_RESPONSE on out-of-range decode (bus-timeout
 * garbage is possible on a stalled transaction — the part NAKs after
 * ~950 ms and a later read returns 0xFF). */
esp_err_t vq_rtc_read_time(vq_rtc_time_t *t, bool *valid);

/* Write the calendar block (console set-time path). Validates ranges,
 * forces 24 h mode, clears PORF afterwards. */
esp_err_t vq_rtc_set_time(const vq_rtc_time_t *t);

/* Wall-clock seconds since 1970-01-01 UTC. Software civil<->unix
 * conversion is canonical here (deterministic — no reliance on the
 * chip's separate HW UNIX counter at 0x1B-0x1E). 32-bit wraps at 2106. */
esp_err_t vq_rtc_read_unix(uint32_t *unix_s, bool *valid);
esp_err_t vq_rtc_set_unix(uint32_t unix_s);
uint32_t  vq_rtc_to_unix(const vq_rtc_time_t *t);      /* pure conversion */
void      vq_rtc_from_unix(uint32_t unix_s, vq_rtc_time_t *t);

/* Flag poll helpers — the only interrupt path this board has for the RTC.
 *   vq_rtc_status()       → raw STATUS byte (VQ_RTC_ST_*)
 *   vq_rtc_clear_flags()  → clear the named flags (write-0-to-clear)   */
esp_err_t vq_rtc_status(uint8_t *flags);
esp_err_t vq_rtc_clear_flags(uint8_t flags);

/* True when a valid clock has been set since last POR (convenience). */
bool      vq_rtc_time_valid(void);

#ifdef __cplusplus
}
#endif
