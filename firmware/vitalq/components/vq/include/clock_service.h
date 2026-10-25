/* clock_service.h — single esp_timer monotonic domain + wall-clock anchor.
 *
 * All sample timestamps are esp_timer µs. Wall clock comes from SNTP when
 * Wi-Fi is up; the RV-3028 RTC (0x52, INT NOT wired to ESP — poll flags,
 * no backup cell per R115) is an optional absolute anchor, probed at boot.
 * Beyond max_unsynced_s without an anchor, clock is flagged unreliable.
 */
#pragma once
#include "vq_types.h"

struct timespec;

vq_err_t vq_clock_init(void);

int64_t  vq_clock_us(void);            /* esp_timer monotonic µs        */
/* Wall clock (UTC). ok=false → unreliable (unsynced too long). */
bool     vq_clock_wall(struct timespec *out);
int64_t  vq_clock_wall_us(bool *ok);   /* wall µs or -1                 */

/* SNTP: call after Wi-Fi connects; re-arms every sync_interval_s. */
vq_err_t vq_clock_sntp_start(void);
bool     vq_clock_synced(void);

/* RTC anchor: probed at init; if present, read wall time + poll flags.
 * Present-flag is public because RTC presence is a BOM question (D5). */
bool     vq_clock_rtc_present(void);
vq_err_t vq_clock_rtc_read(struct timespec *out);

/* Clock-anchor record for batches: {wall_time ↔ monotonic_us}. */
typedef struct { int64_t wall_us; int64_t mono_us; bool reliable; } vq_clock_anchor_t;
vq_clock_anchor_t vq_clock_anchor(void);
