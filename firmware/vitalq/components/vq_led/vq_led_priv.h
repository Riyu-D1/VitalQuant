/* vq_led_priv.h — module-private constants for the LED slot scheduler.
 *
 * Nothing here is part of the public contract (include/led_sched.h in the
 * vq component). These are the fix_v22 board-specific margins for the
 * TPS61240 boost (TX_5V via TCA6408 P3) and the hard-interlock slot engine.
 */
#pragma once

/* ---------------- TX_5V boost timing -------------------------------------
 * TPS61240: fixed 5.0 V, EN driven from TCA6408 P3 through R25. Enabling is
 * an I2C transaction (~300-450 us at the bus's 100 kHz hard limit) plus a
 * ~100 us rail settle before the LED anode rail is trustworthy.           */
#define VQ_LED_TX5_WARMUP_US    100      /* rail settle after EN goes high   */
#define VQ_LED_TX5_I2C_US       450      /* TCA6408 write @100 kHz + slack   */
#define VQ_LED_TX5_PREARM_US    (VQ_LED_TX5_WARMUP_US + VQ_LED_TX5_I2C_US)
/* Short tail after the last high-power hold is released — avoids toggling
 * the boost at slot boundaries when another hp slot is right behind.      */
#define VQ_LED_TX5_TAIL_US      1500
/* If the next scheduled hp-chain slot starts within this horizon, the
 * scheduler keeps TX_5V on through the gap instead of re-warming it — this
 * is what stops per-pulse boost toggling in pulse trains.                 */
#define VQ_LED_TX5_KEEP_US      30000

/* ---------------- slot engine ------------------------------------------- */
#define VQ_LED_SLOT_GUARD_US    25       /* dead time between slots          */
#define VQ_LED_POWER_RETRY_US   250000   /* re-check power gate cadence      */
#define VQ_LED_POOL             8        /* max outstanding requests         */
#define VQ_LED_MAX_ON_US        (10U*1000*1000)  /* sanity bound on a slot  */
#define VQ_LED_MIN_PERIOD_US    1000     /* floor for periodic requests      */

/* ---------------- hard duty rule -----------------------------------------
 * Global duty for >= VQ_LED_DUTY_CLASS_MA pulse requests must stay <= 1%
 * over a rolling 60 s window (fix_v22: TPS61240 supplies ~400 mA while all
 * chains together want ~900 mA). LIMIT = 1% of WINDOW = 600 ms of on-time
 * per minute, measured on REAL pulse edges, not requested widths.          */
#define VQ_LED_DUTY_CLASS_MA    300
#define VQ_LED_DUTY_WINDOW_US   (60LL*1000*1000)
#define VQ_LED_DUTY_LIMIT_US    600000LL             /* 1% of 60 s           */
/* Rolling window is kept as absolute-second buckets so pulse count is
 * unbounded; boundary buckets are counted whole (conservative).           */
#define VQ_LED_DUTY_BUCKETS     64
