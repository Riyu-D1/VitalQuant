/* vq_types.h — shared sample/result/event types. Carries the v1
 * sensor_driver.h FIFO TIMESTAMP CONTRACT into v2:
 *
 *   Every vq_sample_t.t_us is the esp_timer monotonic µs taken at
 *   ACQUISITION (the data-ready edge latched in the ISR, then offset per
 *   in-FIFO index where the device supplies a sample counter) — never at
 *   FIFO pop, never at transmit. Streaming AFEs keep independent sample
 *   clocks; cross-device alignment is by t_us only.
 *
 * No medical claims anywhere: channel ids and event wording stay
 * engineering-grade ("early-warning score trend"), never diagnostic.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

typedef esp_err_t vq_err_t;   /* ESP_OK, ESP_ERR_* */

/* Channel ids — stable strings matching the platform channel registry. */
typedef enum {
    VQ_CH_ECG_II = 0,
    VQ_CH_RESP,
    VQ_CH_PPG_RED,
    VQ_CH_PPG_IR,
    VQ_CH_PPG_SAT,        /* satellite PPG (MAX86141 on J10)           */
    VQ_CH_EDA,
    VQ_CH_BIOZ,
    VQ_CH_SWEAT_LACTATE,  /* RESEARCH-GRADE — never a clinical output  */
    VQ_CH_SPEC_F1, VQ_CH_SPEC_F2, VQ_CH_SPEC_F3, VQ_CH_SPEC_F4,
    VQ_CH_SPEC_F5, VQ_CH_SPEC_F6, VQ_CH_SPEC_F7, VQ_CH_SPEC_F8,
    VQ_CH_SPEC_CLEAR, VQ_CH_SPEC_NIR, VQ_CH_SPEC_FLICKER,
    VQ_CH_TISSUE_STO2,    /* derived index — NOT an oximetry number    */
    VQ_CH_TEMP_SKIN,      /* U11 TMP117                                 */
    VQ_CH_TEMP_DOME,      /* U20 TMP117 (ZHF-style pair → trend only)   */
    VQ_CH_TEMP_DISTAL,    /* J9 tail TMP117 — optional                  */
    VQ_CH_TEMP_OBJ,       /* MLX90632 object                            */
    VQ_CH_TEMP_AMB,       /* MLX90632 ambient                           */
    VQ_CH_SKIN_RH,        /* SHT45 RH on skin face                      */
    VQ_CH_SKIN_TEMP,      /* SHT45 temp                                 */
    VQ_CH_ENV_TEMP, VQ_CH_ENV_RH, VQ_CH_ENV_PRESS,  /* BME280           */
    VQ_CH_ACC_X, VQ_CH_ACC_Y, VQ_CH_ACC_Z,
    VQ_CH_GYR_X, VQ_CH_GYR_Y, VQ_CH_GYR_Z,
    VQ_CH_CONTACT,        /* FSR ordinal level, no SI units             */
    VQ_CH_BATT_V, VQ_CH_BATT_SOC,
    VQ_CH_MIC,            /* IM69D130 block channel                     */
    VQ_CH_COUNT
} vq_channel_id_t;

const char *vq_channel_name(vq_channel_id_t ch);   /* "ppg.red", ...    */

/* The Sample contract (v1 → v2). t_us = acquisition stamp, per contract. */
typedef struct {
    vq_channel_id_t channel;
    int64_t         t_us;   /* esp_timer µs AT ACQUISITION — see contract */
    float           value;  /* SI where defined; raw counts otherwise     */
    uint8_t         flags;  /* VQ_SAMPLE_F_*                              */
} vq_sample_t;

#define VQ_SAMPLE_F_QUALITY_BAD   0x01   /* driver-side SQI rejection     */
#define VQ_SAMPLE_F_SYNTH         0x02   /* self-test / injected data     */
#define VQ_SAMPLE_F_ESTIMATED_TS  0x04   /* t_us interpolated, not edged  */

/* Device-registered sample sink. Pipeline gives each driver a queue; the
 * driver task drains its device FIFO and pushes stamped samples.          */
typedef void (*vq_sample_sink_t)(void *ctx, const vq_sample_t *samples, size_t n);

/* Event ids for the firmware event log + status endpoint (engineering
 * wording only — no clinical claims).                                     */
typedef enum {
    VQ_EVT_BOOT = 0,
    VQ_EVT_SENSOR_FAULT,
    VQ_EVT_I2C_ERR,
    VQ_EVT_SPI_ERR,
    VQ_EVT_LOW_BATTERY,
    VQ_EVT_EMERGENCY_SHUTDOWN,
    VQ_EVT_VBUS_IN,
    VQ_EVT_VBUS_OUT,
    VQ_EVT_CHARGE_STATE,
    VQ_EVT_MODEL_SCORED,      /* prob + latency logged                  */
    VQ_EVT_ESCALATION_SENT,   /* local anomaly → cloud bundle           */
    VQ_EVT_ESCALATION_RESULT, /* cloud advisory received                */
    VQ_EVT_UPLOAD_OK,
    VQ_EVT_UPLOAD_FAIL,
    VQ_EVT_DEEP_SLEEP_ENTER,
    VQ_EVT_WATCHDOG_WARN,
} vq_event_id_t;

/* Batch window sizes used across components (bytes / counts). */
#define VQ_MODEL_INPUT_MAX       4096   /* max floats for one input tensor */
#define VQ_FEATURE_DIM           40     /* features.h vector length        */
#define VQ_ESC_CHANNELS_MAX      8      /* raw channels in escalation      */
