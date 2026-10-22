/* adc_cali_scheme.h — host-sim stand-in for esp_adc/adc_cali_scheme.h
 * (IDF v5.4.2).
 *
 * Real silicon exposes the schemes via a per-target adc_cali_schemes.h:
 * on ESP32-S3 only CURVE_FITTING is supported.  The host model burns no
 * eFuses, so BOTH flags are 1 here and both create/delete entry points
 * exist — firmware still picks curve fitting first (the `#if CURVE`
 * branch precedes `#elif LINE`, identical to target).
 */
#pragma once

#include <stdint.h>
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Supported-scheme flags (real S3: curve only — see header comment).   */
#define ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED    1
#define ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED     1

/*---------------------------------------------------------------
            Curve Fitting Calibration Scheme
---------------------------------------------------------------*/
typedef struct {
    adc_unit_t unit_id;         ///< ADC unit
    adc_channel_t chan;         ///< ADC channel, for chips with SOC_ADC_CALIB_CHAN_COMPENS_SUPPORTED, calibration can be per channel
    adc_atten_t atten;          ///< ADC attenuation
    adc_bitwidth_t bitwidth;    ///< ADC raw output bitwidth
} adc_cali_curve_fitting_config_t;

/**
 * @brief Create a Curve Fitting calibration scheme
 *
 * @param[in]  config  Initial configurations
 * @param[out] handle  ADC calibration handle
 *
 * @return
 *        - ESP_OK:                On success
 *        - ESP_ERR_INVALID_ARG:   Invalid argument
 *        - ESP_ERR_NO_MEM:        No enough memory
 */
esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *config, adc_cali_handle_t *ret_handle);

/**
 * @brief Delete the Curve Fitting calibration scheme handle
 *
 * @param[in] handle ADC calibration handle
 *
 * @return
 *        - ESP_OK:                On success
 *        - ESP_ERR_INVALID_ARG:   Invalid argument
 */
esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t handle);

/*---------------------------------------------------------------
            Line Fitting Calibration Scheme
---------------------------------------------------------------*/
/**
 * @brief Type of calibration value used in line fitting scheme characterization
 */
typedef enum {
    ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_VREF = 0,        ///< Characterization based on reference voltage stored in eFuse
    ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_TP = 1,          ///< Characterization based on Two Point values stored in eFuse
    ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF = 2,      ///< Characterization based on default reference voltage
} adc_cali_line_fitting_efuse_val_t;

typedef struct {
    adc_unit_t unit_id;         ///< ADC unit
    adc_atten_t atten;          ///< ADC attenuation
    adc_bitwidth_t bitwidth;    ///< ADC raw output bitwidth
    /* NB: no .default_vref — that field exists only on the
     * CONFIG_IDF_TARGET_ESP32 variant of this struct.           */
} adc_cali_line_fitting_config_t;

/**
 * @brief Create a Line Fitting calibration scheme
 *
 * @param[in]  config  Initial configurations
 * @param[out] handle  ADC calibration handle
 *
 * @return
 *        - ESP_OK:                On success
 *        - ESP_ERR_INVALID_ARG:   Invalid argument
 *        - ESP_ERR_NO_MEM:        No enough memory
 */
esp_err_t adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t *config, adc_cali_handle_t *ret_handle);

/**
 * @brief Delete the Line Fitting calibration scheme handle
 *
 * @param[in] handle ADC calibration handle
 *
 * @return
 *        - ESP_OK:                On success
 *        - ESP_ERR_INVALID_ARG:   Invalid argument
 */
esp_err_t adc_cali_delete_scheme_line_fitting(adc_cali_handle_t handle);

#ifdef __cplusplus
}
#endif
