/* temperature_sensor.h — host-sim stand-in for
 * driver/temperature_sensor.h (IDF v5.4.2).
 *
 * The "sensor" is the virtual board's die/board temperature: reads
 * return g_vb_board_c, which scenarios steer through
 * vb_tsens_set_celsius().  Sim implementation: periph/tsens_sim.c.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief temperature sensor clock source — opaque int in the sim
 *        (hal/temperature_sensor_types.h folds it to a soc_periph type
 *        on real silicon; the value is recorded, never used).
 */
typedef int temperature_sensor_clk_src_t;
#define TEMPERATURE_SENSOR_CLK_SRC_DEFAULT   0

/**
 * @brief Type of temperature sensor driver handle
 */
typedef struct temperature_sensor_obj_t *temperature_sensor_handle_t;

/**
 * @brief Configuration of measurement range for the temperature sensor.
 */
typedef struct {
    int range_min;   /**< the minimum value of the temperature you want to test */
    int range_max;   /**< the maximum value of the temperature you want to test */
    temperature_sensor_clk_src_t clk_src;  /**< the clock source of the temperature sensor. */
    struct {
        uint32_t allow_pd;  /**< If set, the driver will backup/restore the temperature sensor registers before/after entering/exist sleep mode.
                                 By this approach, the system can power off temperature sensor's power domain.
                                 This can save power, but at the expense of more RAM being consumed */
    } flags;                /**< Temperature sensor config flags */
} temperature_sensor_config_t;

/**
 * @brief temperature_sensor_config_t default constructor
 */
#define TEMPERATURE_SENSOR_CONFIG_DEFAULT(min, max)    \
    {                                                  \
        .range_min = min,                              \
        .range_max = max,                              \
        .clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT, \
    }

/**
 * @brief Install temperature sensor driver
 *
 * @param tsens_config Pointer to config structure.
 * @param ret_tsens Return the pointer of temperature sensor handle.
 * @return
 *      - ESP_OK if succeed
 *      - ESP_ERR_INVALID_ARG if arguments are bad
 *      - ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t temperature_sensor_install(const temperature_sensor_config_t *tsens_config, temperature_sensor_handle_t *ret_tsens);

/**
 * @brief Uninstall the temperature sensor driver
 *
 * @param tsens The handle created by `temperature_sensor_install()`.
 * @return
 *      - ESP_OK if succeed.
 */
esp_err_t temperature_sensor_uninstall(temperature_sensor_handle_t tsens);

/**
 * @brief Enable the temperature sensor
 *
 * @param tsens The handle created by `temperature_sensor_install()`.
 * @return
 *     - ESP_OK Success
 *     - ESP_ERR_INVALID_STATE if temperature sensor is enabled already.
 */
esp_err_t temperature_sensor_enable(temperature_sensor_handle_t tsens);

/**
 * @brief Disable temperature sensor
 *
 * @param tsens The handle created by `temperature_sensor_install()`.
 * @return
 *     - ESP_OK Success
 *     - ESP_ERR_INVALID_STATE if temperature sensor is not enabled yet.
 */
esp_err_t temperature_sensor_disable(temperature_sensor_handle_t tsens);

/**
 * @brief Read temperature sensor data that is converted to degrees Celsius.
 * @note  Should not be called from interrupt.
 *
 * @param tsens The handle created by `temperature_sensor_install()`.
 * @param out_celsius The measure output value.
 * @return
 *     - ESP_OK Success
 *     - ESP_ERR_INVALID_ARG invalid arguments
 *     - ESP_ERR_INVALID_STATE Temperature sensor is not enabled yet.
 */
esp_err_t temperature_sensor_get_celsius(temperature_sensor_handle_t tsens, float *out_celsius);

#ifdef SIM_HOST_BUILD
/* ------------------------- host-sim hooks --------------------------- *
 * The board/die temperature the virtual tsens reports.  Default 36.5 C
 * (body-worn device at skin).  Scenarios set it directly or via the
 * setter to drive the vq_power thermal rules (>40 C conserve,
 * >45 C charge-inhibit).
 */
extern float g_vb_board_c;
void         vb_tsens_set_celsius(float celsius);
#endif /* SIM_HOST_BUILD */

#ifdef __cplusplus
}
#endif
