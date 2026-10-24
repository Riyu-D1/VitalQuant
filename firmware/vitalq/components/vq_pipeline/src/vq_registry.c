/* vq_registry.c — vq_pipeline owns the vq_sensor.h registry contract:
 *
 *   vq_sensor_register() / vq_sensors() / vq_sensor_find()
 *   vq_channel_name()          (vq_types.h)
 *   vq_ts_anchor_edge/stamp()  (vq_sensor.h ISR timestamp anchors)
 *
 * REGISTRATION IS BY VALUE: vq_sensor_register() copies the caller's
 * descriptor into the registry pool so vq_sensors() can hand the pipeline a
 * contiguous array. Driver ops MUST therefore use the `vq_sensor_t *s`
 * argument they are invoked with — it is the registry copy, not the driver's
 * static instance. sink/sink_ctx/err_count/sample_count/last_t_us all live
 * on the registry copy; the driver's static struct is only a template.
 *
 * Registration order is preserved: vq_pipeline_init() calls the driver
 * register functions in a fixed order (SPI AFEs first, then I2C groups,
 * optional devices last) so registry index == deterministic probe order.
 */
#include <string.h>
#include "vq_sensor.h"
#include "vq_types.h"
#include "esp_timer.h"

static vq_sensor_t s_pool[VQ_SENSOR_MAX];
static size_t      s_pool_n;

vq_err_t vq_sensor_register(vq_sensor_t *s)
{
    if (s == NULL || s->ops == NULL || s->ops->name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < s_pool_n; i++) {
        if (strcmp(s_pool[i].ops->name, s->ops->name) == 0) {
            return ESP_ERR_INVALID_STATE;      /* duplicate name */
        }
    }
    if (s_pool_n >= VQ_SENSOR_MAX) {
        return ESP_ERR_NO_MEM;
    }
    s_pool[s_pool_n] = *s;                     /* copy — see header note */
    s_pool_n++;
    return ESP_OK;
}

vq_sensor_t *vq_sensors(size_t *count)
{
    if (count) {
        *count = s_pool_n;
    }
    return s_pool;
}

vq_sensor_t *vq_sensor_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < s_pool_n; i++) {
        if (s_pool[i].ops->name && strcmp(s_pool[i].ops->name, name) == 0) {
            return &s_pool[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Channel ids → platform registry names (hw_v3.yaml `outputs:` names  */
/* where registered; engineering names where "pending registration").  */
/* ------------------------------------------------------------------ */
static const char *const s_chan_names[VQ_CH_COUNT] = {
    [VQ_CH_ECG_II]        = "ecg.ii",
    [VQ_CH_RESP]          = "resp.waveform",
    [VQ_CH_PPG_RED]       = "ppg.red",
    [VQ_CH_PPG_IR]        = "ppg.ir",
    [VQ_CH_PPG_SAT]       = "ppg.satellite",
    [VQ_CH_EDA]           = "eda.level",        /* AD5940 Z-proxy, µS-ish raw */
    [VQ_CH_BIOZ]          = "bioz.mag",         /* |Z| less 51k series R       */
    [VQ_CH_SWEAT_LACTATE] = "sweat.lactate",    /* RESEARCH-GRADE only         */
    [VQ_CH_SPEC_F1]       = "spec.f1",
    [VQ_CH_SPEC_F2]       = "spec.f2",
    [VQ_CH_SPEC_F3]       = "spec.f3",
    [VQ_CH_SPEC_F4]       = "spec.f4",
    [VQ_CH_SPEC_F5]       = "spec.f5",
    [VQ_CH_SPEC_F6]       = "spec.f6",
    [VQ_CH_SPEC_F7]       = "spec.f7",
    [VQ_CH_SPEC_F8]       = "spec.f8",
    [VQ_CH_SPEC_CLEAR]    = "spec.clear",
    [VQ_CH_SPEC_NIR]      = "spec.nir",
    [VQ_CH_SPEC_FLICKER]  = "spec.flicker",
    [VQ_CH_TISSUE_STO2]   = "tissue.sto2",      /* derived index, not oximetry */
    [VQ_CH_TEMP_SKIN]     = "temp.skin",        /* TMP117 U11 @0x48            */
    [VQ_CH_TEMP_DOME]     = "temp.dome",        /* TMP117 U20 @0x49 (ZHF pair) */
    [VQ_CH_TEMP_DISTAL]   = "temp.distal",      /* TMP117 J9 tail @0x4A        */
    [VQ_CH_TEMP_OBJ]      = "temp.object",      /* MLX90632                    */
    [VQ_CH_TEMP_AMB]      = "temp.ambient",     /* MLX90632 die                */
    [VQ_CH_SKIN_RH]       = "skin.rh",          /* SHT45                       */
    [VQ_CH_SKIN_TEMP]     = "skin.temp",        /* SHT45                       */
    [VQ_CH_ENV_TEMP]      = "env.temp",         /* BME280                      */
    [VQ_CH_ENV_RH]        = "env.rh",
    [VQ_CH_ENV_PRESS]     = "env.press",
    [VQ_CH_ACC_X]         = "motion.accel_x",
    [VQ_CH_ACC_Y]         = "motion.accel_y",
    [VQ_CH_ACC_Z]         = "motion.accel_z",
    [VQ_CH_GYR_X]         = "motion.gyro_x",
    [VQ_CH_GYR_Y]         = "motion.gyro_y",
    [VQ_CH_GYR_Z]         = "motion.gyro_z",
    [VQ_CH_CONTACT]       = "contact.level",    /* FSR ordinal, no SI          */
    [VQ_CH_BATT_V]        = "sys.battery_v",
    [VQ_CH_BATT_SOC]      = "sys.battery_soc",
    [VQ_CH_MIC]           = "mic.audio",        /* IM69D130 block channel      */
};

const char *vq_channel_name(vq_channel_id_t ch)
{
    if (ch < 0 || ch >= VQ_CH_COUNT || s_chan_names[ch] == NULL) {
        return "unknown";
    }
    return s_chan_names[ch];
}

/* ------------------------------------------------------------------ */
/* FIFO-timestamp anchors — drivers latch the esp_timer µs inside their */
/* DRDY/ADC_RDY/INT ISR, then stamp each FIFO-popped sample by its      */
/* device sequence index (vq_types.h contract).                        */
/* ------------------------------------------------------------------ */
void vq_ts_anchor_edge(vq_ts_anchor_t *a)
{
    if (a == NULL) {
        return;
    }
    a->edge_us = esp_timer_get_time();   /* ISR-safe */
    a->edge_seq++;
}

int64_t vq_ts_anchor_stamp(const vq_ts_anchor_t *a, int32_t dev_seq)
{
    if (a == NULL) {
        return esp_timer_get_time();
    }
    /* edge_us is 64-bit on a 32-bit core — read until the seq guard shows
     * no edge raced through the read.                                   */
    int64_t edge;
    int32_t seq_anchor;
    float   rate;
    int32_t guard;
    do {
        guard      = a->edge_seq;
        edge       = a->edge_us;
        seq_anchor = a->seq_anchor;
        rate       = a->rate_hz;
    } while (guard != a->edge_seq);
    if (edge == 0) {
        return esp_timer_get_time();     /* no edge latched yet */
    }
    if (rate <= 0.0f) {
        return edge;
    }
    return edge + (int64_t)((double)(dev_seq - seq_anchor) * 1.0e6 /
                            (double)rate);
}
