/* vq_sensor.h — driver interface, v1 SensorDriver contract carried to C.
 *
 * Lifecycle: probe() → init() → start() → read via sink → stop()/sleep().
 * Drivers NEVER busy-poll; data-ready lines go through timestamped ISRs
 * per the FIFO timestamp contract (vq_types.h).
 *
 * Every driver registers a descriptor via vq_sensor_register(); the
 * pipeline iterates the registry. Missing/degraded hardware must never
 * be fatal: probe() returning ESP_ERR_NOT_FOUND → pipeline marks absent
 * and continues (RTC/mic/J9 tail are probe-optional per BOM ambiguity).
 */
#pragma once
#include "vq_types.h"

typedef struct vq_sensor vq_sensor_t;

typedef struct {
    const char *name;                 /* "ads1292r", "tmp117.skin" ...  */

    /* Probe only — cheap ID/ACK check; must not disturb the device.    */
    vq_err_t (*probe)(vq_sensor_t *s);
    /* Full configure; device left ready but not streaming.             */
    vq_err_t (*init)(vq_sensor_t *s);
    /* Begin streaming at cfg.rate_hz; samples → sink with contract ts. */
    vq_err_t (*start)(vq_sensor_t *s);
    /* Stop streaming; lowest-power state short of full sleep.          */
    vq_err_t (*stop)(vq_sensor_t *s);
    /* Deepest available sleep for pre-deep-sleep prep (§4.4 checklist).*/
    vq_err_t (*sleep)(vq_sensor_t *s);
    /* Health: false → pipeline emits VQ_EVT_SENSOR_FAULT + backoff.    */
    bool     (*healthy)(vq_sensor_t *s);

    const vq_channel_id_t *channels;  /* channels produced              */
    size_t                 n_channels;
    float                  rate_hz;   /* from profile (§7 table)        */
    bool                   required;  /* false → absent is tolerated    */
} vq_sensor_ops_t;

struct vq_sensor {
    const vq_sensor_ops_t *ops;
    vq_sample_sink_t       sink;
    void                  *sink_ctx;
    void                  *priv;      /* driver-private state           */
    uint32_t               err_count;
    uint32_t               sample_count;
    int64_t                last_t_us;
};

/* Registry — populated by each driver component's vq_<name>_register(). */
#define VQ_SENSOR_MAX 24
vq_err_t   vq_sensor_register(vq_sensor_t *s);
vq_sensor_t *vq_sensors(size_t *count);
vq_sensor_t *vq_sensor_find(const char *name);

/* Helper for drivers: latch esp_timer in a GPIO ISR, keep an
 * {edge_us ↔ device-sample-index} anchor for FIFO-pop reconstruction.   */
typedef struct {
    volatile int64_t edge_us;
    volatile int32_t edge_seq;        /* increments per edge            */
    int32_t          seq_anchor;      /* device seq at last edge        */
    float            rate_hz;         /* for per-index ts back-off      */
} vq_ts_anchor_t;

void vq_ts_anchor_edge(vq_ts_anchor_t *a);               /* call in ISR  */
int64_t vq_ts_anchor_stamp(const vq_ts_anchor_t *a, int32_t dev_seq);
