/* vq_env_priv.h — internals shared by the env drivers and the cadence
 * core (vq_drv_env.c). NOT installed: component-private contract.
 *
 * Device model: every env device is one vq_env_dev_t. Its embedded
 * vq_sensor_t is registered with the pipeline; the generic ops
 * (vq_env_op_*) translate pipeline callbacks into the driver vtable
 * below, and the shared cadence task drives trigger/fetch.
 *
 * Concurrency contract (32-bit Xtensa — int64 is NOT atomic):
 *   - `cmd` and `state` are plain volatile ints (atomic word ops).
 *   - All int64 scheduling fields are written ONLY by the cadence task.
 *   - `last_ok_us` is additionally read by healthy() from the pipeline
 *     task → both sides take vq_env_mux (defined in vq_drv_env.c).
 */
#pragma once
#include "vq_sensor.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

/* Delay ≥ ms, safe at any configTICK_RATE (pdMS_TO_TICKS can round a
 * short delay down to 0 ticks). The +1 tick of slack is immaterial on
 * these probe/init/selftest/fetch-retry paths.                        */
static inline void vq_env_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}

typedef struct vq_env_dev vq_env_dev_t;

/* Driver vtable — one per device TYPE.
 *   probe    cheap ID/ACK check; must not disturb the device.
 *            Any ACK-stage failure → ESP_ERR_NOT_FOUND (absent).
 *   init     full configure, device left in standby.
 *   trigger  start ONE conversion, return immediately.
 *   fetch    runs conv_wait_ms after trigger; reads result and emits
 *            every produced channel via vq_env_emit() under t_acq_us.
 *   standby  lowest-power idle state for stop().
 *   sleep    deepest available state for pre-deep-sleep.
 *   selftest ID + one conversion; writes a short text fragment to buf.
 *            Must leave the device in standby afterwards.            */
typedef struct {
    vq_err_t (*probe)(vq_env_dev_t *d);
    vq_err_t (*init)(vq_env_dev_t *d);
    vq_err_t (*trigger)(vq_env_dev_t *d);
    vq_err_t (*fetch)(vq_env_dev_t *d);
    vq_err_t (*standby)(vq_env_dev_t *d);
    vq_err_t (*sleep)(vq_env_dev_t *d);
    vq_err_t (*selftest)(vq_env_dev_t *d, char *buf, size_t len);
    uint32_t conv_wait_ms;          /* trigger → fetch latency        */
} vq_env_drv_t;

enum {                          /* vq_env_dev_t.state */
    ENV_ST_IDLE = 0,            /* init'd/stopped/sleeping — no polls */
    ENV_ST_STREAM,              /* streaming: due checks active       */
    ENV_ST_FETCH,               /* conversion armed, awaiting fetch   */
};

enum {                          /* vq_env_dev_t.cmd — ops → task */
    ENV_CMD_NONE = 0,
    ENV_CMD_START,
    ENV_CMD_STOP,
    ENV_CMD_SLEEP,
};

struct vq_env_dev {
    vq_sensor_t          s;             /* descriptor — MUST be first */
    vq_sensor_t         *s_reg;         /* registry-owned copy (post-   */
                                        /* vq_sensor_register): sink/   */
                                        /* counters live THERE, not in  */
                                        /* the template above.          */
    const vq_env_drv_t  *drv;
    volatile int         cmd;           /* ops → cadence task         */
    volatile int         state;
    int64_t              period_us;     /* = 1e6 / rate_hz            */
    int64_t              next_due_us;   /* next trigger deadline      */
    int64_t              fetch_at_us;   /* pending fetch deadline     */
    int64_t              t_acq_us;      /* READ-START stamp (header)  */
    int64_t              last_ok_us;    /* last successful fetch      */
};

/* ---- provided by vq_drv_env.c -------------------------------------- */
extern portMUX_TYPE vq_env_mux;         /* guards last_ok_us          */

/* Emit one stamped sample to the pipeline sink (t_acq_us contract) and
 * update s.sample_count / s.last_t_us. */
void     vq_env_emit(vq_env_dev_t *d, vq_channel_id_t ch, float value);

/* Generic vq_sensor_ops used by every env descriptor. */
vq_err_t vq_env_op_probe(vq_sensor_t *s);
vq_err_t vq_env_op_init(vq_sensor_t *s);
vq_err_t vq_env_op_start(vq_sensor_t *s);
vq_err_t vq_env_op_stop(vq_sensor_t *s);
vq_err_t vq_env_op_sleep(vq_sensor_t *s);
bool     vq_env_op_healthy(vq_sensor_t *s);

/* ---- device tables (one file per device type) ----------------------- */
vq_env_dev_t **vq_tmp117_devs(size_t *n);   /* skin, dome, tail         */
vq_env_dev_t  *vq_bme280_dev(void);
vq_env_dev_t  *vq_sht45_dev(void);
