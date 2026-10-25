/* pipeline.h — sensor orchestration: registry walk, per-driver tasks,
 * sample fan-out to (a) feature windows, (b) log_store, (c) batch upload.
 *
 * Design: one task per streaming AFE (DRDY/ADC_RDY/INT-driven), one task
 * for the 0.2-2 Hz I2C cadence group, all pushing stamped samples to the
 * shared hub. Absent optional devices degrade gracefully.
 */
#pragma once
#include "vq_types.h"
#include "vq_sensor.h"
#include "features.h"

vq_err_t vq_pipeline_init(void);
vq_err_t vq_pipeline_start(void);    /* spawn per-driver + cadence tasks */
vq_err_t vq_pipeline_stop(void);     /* graceful stop before sleep       */

/* Subscribe to the stamped sample stream (features, logger, uploader). */
vq_err_t vq_pipeline_subscribe(vq_sample_sink_t sink, void *ctx);

/* Window access for features + escalation bundles: last N ms of a chan. */
size_t   vq_pipeline_window(vq_channel_id_t ch, int64_t window_us,
                            vq_sample_t *out, size_t max);
/* Per-channel ring capacity: PPG/ECG keep 60 s @ ≤500 sps in PSRAM.    */
#define VQ_WIN_PPG_S    60
#define VQ_WIN_ECG_S    60
#define VQ_WIN_SLOW_S   300

/* Jitter metric for acceptance §11.7: DRDY/ADC_RDY edge interval stats. */
typedef struct { float mean_us, jitter_us; uint32_t n; } vq_edge_stats_t;
void     vq_pipeline_edge_stats(vq_channel_id_t ch, vq_edge_stats_t *out);
