/* power_mgr.h — battery/charge state machine + deep-sleep governor.
 *
 * Cell is ASSUMED UNPROTECTED (open question Q1) — firmware enforces the
 * cut-off:  GAUGE_ALRT / VCELL <= 3.2 V → stop LEDs+RF, save, deep sleep
 * (wake on gauge alert / VBUS). <= 3.0 V → emergency shutdown now.
 * TX_5V boost never runs below 3.3 V. Board can't disconnect the cell —
 * standby ~110 µA keeps draining; that's logged, not hidden.
 *
 * USB budget: 200 mA charge + system can exceed 500 mA → during heavy
 * bursts (Wi-Fi TX + LED window) firmware inhibits charge via CHG_DIS.
 *
 * Thermal: >~40 °C board → throttle Wi-Fi/LED duty; >45 °C → charge-inhibit
 * hard loads (charger TS self-limits too).
 */
#pragma once
#include "vq_types.h"

typedef enum {
    VQ_PWR_NORMAL = 0,
    VQ_PWR_CONSERVE,     /* SOC low or VCELL < ~3.45 V: reduce duty     */
    VQ_PWR_LOW_CUTOFF,   /* <=3.2 V: LED/RF off, preparing deep sleep   */
    VQ_PWR_EMERGENCY,    /* <=3.0 V: immediate shutdown                 */
} vq_pwr_state_t;

typedef struct {
    float vcell_v, soc_pct, charge_rate;
    bool  vbus, charging, charge_inhibited;
    float board_temp_c;              /* warmest TMP117/ESP sensor        */
    vq_pwr_state_t state;
} vq_power_status_t;

vq_err_t vq_power_init(void);
void     vq_power_status(vq_power_status_t *out);
vq_pwr_state_t vq_power_state(void);

/* Permission gates — LED scheduler and net layer ask before bursts.     */
bool     vq_power_led_ok(uint32_t pulse_mA);   /* respects budget+state  */
bool     vq_power_wifi_burst_ok(void);         /* false during LED win..*/
void     vq_power_burst_begin(uint32_t est_mA);/* marks heavy burst     */
void     vq_power_burst_end(void);

/* Deep sleep: park every sensor per §4.4, hold CS/LED/MIC_CLK, arm EXT1
 * (GAUGE_ALRT IO6, EXP_INT IO3, optional LSM6 IO4), enter. Non-returning
 * unless wake tests run on host — use vq_power_enter_sleep().          */
void     vq_power_enter_deep_sleep(void) __attribute__((noreturn));
vq_err_t vq_power_prepare_sleep(void);  /* the §4.4 checklist, testable  */

/* Callback on state transitions (LED kill, net backoff, decision skip).*/
typedef void (*vq_pwr_cb_t)(vq_pwr_state_t state, void *ctx);
vq_err_t vq_power_on_change(vq_pwr_cb_t cb, void *ctx);
