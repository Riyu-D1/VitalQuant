/* tsens_sim.c — host model of the ESP32-S3 internal temperature sensor
 * (driver/temperature_sensor.h, IDF v5.4.2 surface).
 *
 * The die/board temperature lives in g_vb_board_c; scenarios steer it
 * with vb_tsens_set_celsius() to drive the vq_power thermal rules
 * (>40 C -> CONSERVE, >45 C -> charge inhibit, hysteresis bands).
 * get_celsius returns the variable verbatim while enabled — a real
 * part reports ~0.5 C granular noise; the sim keeps the exact value so
 * threshold tests are deterministic.
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "driver/temperature_sensor.h"

/* Board temperature in deg C.  36.5 = body-worn default (device at
 * skin); the power task also takes the warmest of the TMP117s.       */
float g_vb_board_c = 36.5f;

void vb_tsens_set_celsius(float celsius)
{
    g_vb_board_c = celsius;
}

struct temperature_sensor_obj_t {
    int   range_min;
    int   range_max;
    temperature_sensor_clk_src_t clk_src;
    bool  allow_pd;
    bool  enabled;
    bool  alive;
};

esp_err_t temperature_sensor_install(const temperature_sensor_config_t *tsens_config,
                                     temperature_sensor_handle_t *ret_tsens)
{
    if (!tsens_config || !ret_tsens) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tsens_config->range_min >= tsens_config->range_max) {
        return ESP_ERR_INVALID_ARG;     /* empty measurement range     */
    }
    temperature_sensor_handle_t h = calloc(1, sizeof(*h));
    if (!h) {
        return ESP_ERR_NO_MEM;
    }
    h->range_min = tsens_config->range_min;
    h->range_max = tsens_config->range_max;
    h->clk_src   = tsens_config->clk_src;
    h->allow_pd  = tsens_config->flags.allow_pd;
    h->enabled   = false;
    h->alive     = true;
    *ret_tsens = h;
    return ESP_OK;
}

esp_err_t temperature_sensor_uninstall(temperature_sensor_handle_t tsens)
{
    if (!tsens || !tsens->alive) {
        return ESP_ERR_INVALID_ARG;
    }
    tsens->alive   = false;
    tsens->enabled = false;
    free(tsens);
    return ESP_OK;
}

esp_err_t temperature_sensor_enable(temperature_sensor_handle_t tsens)
{
    if (!tsens || !tsens->alive) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tsens->enabled) {
        return ESP_ERR_INVALID_STATE;   /* real driver errors on re-enable */
    }
    tsens->enabled = true;
    return ESP_OK;
}

esp_err_t temperature_sensor_disable(temperature_sensor_handle_t tsens)
{
    if (!tsens || !tsens->alive) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!tsens->enabled) {
        return ESP_ERR_INVALID_STATE;   /* not enabled yet             */
    }
    tsens->enabled = false;
    return ESP_OK;
}

esp_err_t temperature_sensor_get_celsius(temperature_sensor_handle_t tsens,
                                         float *out_celsius)
{
    if (!tsens || !tsens->alive || !out_celsius) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!tsens->enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_celsius = g_vb_board_c;
    return ESP_OK;
}
