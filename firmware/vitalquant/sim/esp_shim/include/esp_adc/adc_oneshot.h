/* adc_oneshot.h — host-sim stand-in for esp_adc/adc_oneshot.h (IDF v5.4.2).
 *
 * The base ADC types IDF keeps in hal/adc_types.h are folded in here so
 * the shim needs no hal/ tree; names and values are identical to target.
 * adc_cali_scheme.h is pulled in at the bottom exactly like the real
 * header does, so including this file alone yields the full ADC API.
 *
 * Sim semantics (periph/adc_sim.c): one handle per ADC unit, IO<->channel
 * map per the ESP32-S3 truth table (ADC1 CH0-9 <-> GPIO1-10,
 * ADC2 CH0-9 <-> GPIO11-20); reads are served by the board-side signal
 * sources registered in sim/vboard/devices/.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_cali.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- base ADC types (IDF: hal/adc_types.h) -------------------------- */

/**
 * @brief ADC unit
 */
typedef enum {
    ADC_UNIT_1,        ///< SAR ADC 1
    ADC_UNIT_2,        ///< SAR ADC 2
} adc_unit_t;

/**
 * @brief ADC channels
 */
typedef enum {
    ADC_CHANNEL_0,     ///< ADC channel
    ADC_CHANNEL_1,     ///< ADC channel
    ADC_CHANNEL_2,     ///< ADC channel
    ADC_CHANNEL_3,     ///< ADC channel
    ADC_CHANNEL_4,     ///< ADC channel
    ADC_CHANNEL_5,     ///< ADC channel
    ADC_CHANNEL_6,     ///< ADC channel
    ADC_CHANNEL_7,     ///< ADC channel
    ADC_CHANNEL_8,     ///< ADC channel
    ADC_CHANNEL_9,     ///< ADC channel
} adc_channel_t;

/**
 * @brief ADC attenuation parameter. Different parameters determine the range of the ADC.
 */
typedef enum {
    ADC_ATTEN_DB_0   = 0,  ///<No input attenuation, ADC can measure up to approx. 1 V
    ADC_ATTEN_DB_2_5 = 1,  ///<The input voltage of ADC will be attenuated extending the range of measurement by about 2.5 dB
    ADC_ATTEN_DB_6   = 2,  ///<The input voltage of ADC will be attenuated extending the range of measurement by about 6 dB
    ADC_ATTEN_DB_12  = 3,  ///<The input voltage of ADC will be attenuated extending the range of measurement by about 12 dB
    ADC_ATTEN_DB_11  = ADC_ATTEN_DB_12,  ///<Deprecated alias, behaves the same as `ADC_ATTEN_DB_12`
} adc_atten_t;

typedef enum {
    ADC_BITWIDTH_DEFAULT = 0, ///< Default ADC output bits, max supported width will be selected
    ADC_BITWIDTH_9  = 9,      ///< ADC output width is 9Bit
    ADC_BITWIDTH_10 = 10,     ///< ADC output width is 10Bit
    ADC_BITWIDTH_11 = 11,     ///< ADC output width is 11Bit
    ADC_BITWIDTH_12 = 12,     ///< ADC output width is 12Bit
    ADC_BITWIDTH_13 = 13,     ///< ADC output width is 13Bit
} adc_bitwidth_t;

typedef enum {
    ADC_ULP_MODE_DISABLE = 0, ///< ADC ULP mode is disabled
    ADC_ULP_MODE_FSM     = 1, ///< ADC is controlled by ULP FSM
    ADC_ULP_MODE_RISCV   = 2, ///< ADC is controlled by ULP RISCV
} adc_ulp_mode_t;

/**
 * @brief Clock source type of oneshot mode (S3: RTC controller clock).
 *        Opaque int in the sim — the value is recorded, never used.
 */
typedef int adc_oneshot_clk_src_t;
#define ADC_ONESHOT_CLK_SRC_DEFAULT   0

/* ---- oneshot driver API --------------------------------------------- */

/**
 * @brief Type of ADC unit handle for oneshot mode
 */
typedef struct adc_oneshot_unit_ctx_t *adc_oneshot_unit_handle_t;

/**
 * @brief ADC oneshot driver initial configurations
 */
typedef struct {
    adc_unit_t unit_id;             ///< ADC unit
    adc_oneshot_clk_src_t clk_src;  ///< Clock source
    adc_ulp_mode_t ulp_mode;        ///< ADC controlled by ULP, see `adc_ulp_mode_t`
} adc_oneshot_unit_init_cfg_t;

/**
 * @brief ADC channel configurations
 */
typedef struct {
    adc_atten_t atten;              ///< ADC attenuation
    adc_bitwidth_t bitwidth;        ///< ADC conversion result bits
} adc_oneshot_chan_cfg_t;

/**
 * @brief Create a handle to a specific ADC unit
 *
 * @param[in]  init_config    Driver initial configurations
 * @param[out] ret_unit       ADC unit handle
 *
 * @return
 *        - ESP_OK:              On success
 *        - ESP_ERR_INVALID_ARG: Invalid arguments
 *        - ESP_ERR_NO_MEM:      No memory
 *        - ESP_ERR_NOT_FOUND:   The ADC peripheral to be claimed is already in use
 */
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config, adc_oneshot_unit_handle_t *ret_unit);

/**
 * @brief Set ADC oneshot mode required configurations
 *
 * @param[in] handle    ADC handle
 * @param[in] channel   ADC channel to be configured
 * @param[in] config    ADC configurations
 *
 * @return
 *        - ESP_OK:              On success
 *        - ESP_ERR_INVALID_ARG: Invalid arguments
 */
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel, const adc_oneshot_chan_cfg_t *config);

/**
 * @brief Get one ADC conversion raw result
 *
 * @param[in] handle    ADC handle
 * @param[in] chan      ADC channel
 * @param[out] out_raw  ADC conversion raw result
 *
 * @return
 *        - ESP_OK:                On success
 *        - ESP_ERR_INVALID_ARG:   Invalid arguments
 *        - ESP_ERR_TIMEOUT:       Timeout, the ADC result is invalid
 */
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw);

/**
 * @brief Delete the ADC unit handle
 *
 * @param[in] handle    ADC handle
 *
 * @return
 *        - ESP_OK:              On success
 *        - ESP_ERR_INVALID_ARG: Invalid arguments
 *        - ESP_ERR_NOT_FOUND:   The ADC peripheral to be disclaimed isn't in use
 */
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t handle);

/**
 * @brief Get ADC channel from the given GPIO number
 *
 * @param[in]  io_num     GPIO number
 * @param[out] unit_id    ADC unit
 * @param[out] channel    ADC channel
 *
 * @return
 *        - ESP_OK:              On success
 *        - ESP_ERR_INVALID_ARG: Invalid argument
 *        - ESP_ERR_NOT_FOUND:   The IO is not a valid ADC pad
 */
esp_err_t adc_oneshot_io_to_channel(int io_num, adc_unit_t * const unit_id, adc_channel_t * const channel);

/**
 * @brief Get GPIO number from the given ADC channel
 *
 * @param[in]  unit_id    ADC unit
 * @param[in]  channel    ADC channel
 * @param[out] io_num     GPIO number
 *
 * @return
 *       - ESP_OK:              On success
 *       - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t adc_oneshot_channel_to_io(adc_unit_t unit_id, adc_channel_t channel, int * const io_num);

/**
 * @brief Convenience function to get ADC calibrated result
 *
 * @param[in]  handle       ADC oneshot handle
 * @param[in]  cali_handle  ADC calibration handle
 * @param[in]  chan         ADC channel
 * @param[out] cali_result  Calibrated ADC result (in mV)
 *
 * @return
 *        - ESP_OK
 *        Other return errors from adc_oneshot_read() and adc_cali_raw_to_voltage()
 */
esp_err_t adc_oneshot_get_calibrated_result(adc_oneshot_unit_handle_t handle, adc_cali_handle_t cali_handle, adc_channel_t chan, int *cali_result);

/* Mirrors the real header: the calibration schemes ship with the unit
 * driver, so including adc_oneshot.h exposes the whole esp_adc API.   */
#include "esp_adc/adc_cali_scheme.h"

#ifdef __cplusplus
}
#endif
