/* adc_sim.c — host model of the ESP32-S3 ADC oneshot driver +
 * calibration schemes.
 *
 * Channel ownership: reads are served by the board-side signal sources
 * in sim/vboard/devices/ — today that is ADC1_CH0 / IO1 wired to the
 * FSR402 divider model (fsr_src.c).  Any other channel reads as the
 * pin's digital level scaled to counts (an analog read of a driven GPIO
 * saturates near rail/ground), plus a little dither.
 *
 * IO<->channel map is the real ESP32-S3 truth table:
 *   ADC1 CH0..CH9 <-> GPIO1..10,  ADC2 CH0..CH9 <-> GPIO11..20.
 *
 * Cali: the host burns no eFuses, so both schemes succeed and convert
 * linearly 0..full-scale mV (3100 mV at ADC_ATTEN_DB_12 — the nominal
 * design point, not a measured silicon curve).
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "vboard.h"
#include "devices/fsr_src.h"        /* vbdev_fsr_raw_counts() */

#define ADC_SIM_NUM_UNITS      2
#define ADC_SIM_NUM_CHAN       10
#define ADC_SIM_MAX_RAW_BITS   12      /* S3 SAR is 12-bit             */

/* ESP32-S3 adc_channel_io_map: unit -> channel -> GPIO (soc/adc_periph.c) */
static const int8_t s_adc_io_map[ADC_SIM_NUM_UNITS][ADC_SIM_NUM_CHAN] = {
    { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 },        /* ADC1: GPIO1..10        */
    { 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 }/* ADC2: GPIO11..20       */
};

struct adc_oneshot_unit_ctx_t {
    adc_unit_t            unit_id;
    adc_oneshot_clk_src_t clk_src;
    adc_ulp_mode_t        ulp_mode;
    struct {
        bool           configured;
        adc_atten_t    atten;
        adc_bitwidth_t bitwidth;
    } chan[ADC_SIM_NUM_CHAN];
    uint32_t            rng;          /* shared conversion noise       */
    bool                alive;
};

/* One handle per unit at a time — the real driver returns
 * ESP_ERR_NOT_FOUND on a second claim of an in-use unit.            */
static adc_oneshot_unit_handle_t s_units[ADC_SIM_NUM_UNITS];

static uint32_t adc_noise_next(struct adc_oneshot_unit_ctx_t *u)
{
    uint32_t x = u->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    u->rng = x;
    return x;
}

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config,
                               adc_oneshot_unit_handle_t *ret_unit)
{
    if (!init_config || !ret_unit) {
        return ESP_ERR_INVALID_ARG;
    }
    if (init_config->unit_id < 0 ||
        init_config->unit_id >= ADC_SIM_NUM_UNITS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_units[init_config->unit_id] != NULL) {
        return ESP_ERR_NOT_FOUND;       /* peripheral already in use   */
    }
    adc_oneshot_unit_handle_t h = calloc(1, sizeof(*h));
    if (!h) {
        return ESP_ERR_NO_MEM;
    }
    h->unit_id  = init_config->unit_id;
    h->clk_src  = init_config->clk_src;
    h->ulp_mode = init_config->ulp_mode;
    h->rng      = 0xADC5u + (uint32_t)init_config->unit_id;
    h->alive    = true;
    s_units[init_config->unit_id] = h;
    *ret_unit = h;
    return ESP_OK;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle,
                                     adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *config)
{
    if (!handle || !handle->alive || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (channel < 0 || channel >= ADC_SIM_NUM_CHAN) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->chan[channel].configured = true;
    handle->chan[channel].atten      = config->atten;
    handle->chan[channel].bitwidth   = config->bitwidth;
    return ESP_OK;
}

/* Raw counts for one channel — the board-side source model.  ADC1_CH0
 * is the FSR divider; every other channel is a driven/floating GPIO
 * read through the analog mux (saturated near rail or ground) with a
 * few-LSB noise dither. */
static int adc_sim_read_raw(struct adc_oneshot_unit_ctx_t *h,
                            adc_channel_t chan)
{
    int raw;
    if (h->unit_id == ADC_UNIT_1 && chan == ADC_CHANNEL_0) {
        raw = vbdev_fsr_raw_counts();
    } else {
        int io   = s_adc_io_map[h->unit_id][chan];
        int lvl  = vboard_gpio_get(io);
        int base = (lvl > 0) ? 4050 : 40;
        int dith = (int)(adc_noise_next(h) % 9u) - 4;
        raw = base + dith;
        if (raw < 0) raw = 0;
        if (raw > 4095) raw = 4095;
    }
    /* Honour the configured output width (default = 12-bit max). */
    int bits = h->chan[chan].configured &&
               h->chan[chan].bitwidth >= ADC_BITWIDTH_9
             ? (int)h->chan[chan].bitwidth : ADC_SIM_MAX_RAW_BITS;
    int max  = (1 << bits) - 1;
    if (bits < ADC_SIM_MAX_RAW_BITS) {
        raw >>= (ADC_SIM_MAX_RAW_BITS - bits);      /* real part truncates */
    }
    if (raw > max) raw = max;
    return raw;
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle,
                           adc_channel_t chan, int *out_raw)
{
    if (!handle || !handle->alive || !out_raw) {
        return ESP_ERR_INVALID_ARG;
    }
    if (chan < 0 || chan >= ADC_SIM_NUM_CHAN) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_raw = adc_sim_read_raw(handle, chan);
    return ESP_OK;                       /* the virtual SAR never times out */
}

esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t handle)
{
    if (!handle || !handle->alive) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->alive = false;
    if (s_units[handle->unit_id] == handle) {
        s_units[handle->unit_id] = NULL;
    }
    free(handle);
    return ESP_OK;
}

esp_err_t adc_oneshot_io_to_channel(int io_num,
                                    adc_unit_t * const unit_id,
                                    adc_channel_t * const channel)
{
    if (io_num < 0 || io_num > 48) {    /* GPIO_IS_VALID_GPIO          */
        return ESP_ERR_INVALID_ARG;
    }
    if (!unit_id || !channel) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int u = 0; u < ADC_SIM_NUM_UNITS; u++) {
        for (int c = 0; c < ADC_SIM_NUM_CHAN; c++) {
            if (s_adc_io_map[u][c] == io_num) {
                *unit_id = (adc_unit_t)u;
                *channel = (adc_channel_t)c;
                return ESP_OK;
            }
        }
    }
    return ESP_ERR_NOT_FOUND;           /* valid GPIO, no ADC pad      */
}

esp_err_t adc_oneshot_channel_to_io(adc_unit_t unit_id,
                                    adc_channel_t channel,
                                    int * const io_num)
{
    if (unit_id < 0 || unit_id >= ADC_SIM_NUM_UNITS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (channel < 0 || channel >= ADC_SIM_NUM_CHAN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!io_num) {
        return ESP_ERR_INVALID_ARG;
    }
    *io_num = s_adc_io_map[unit_id][channel];
    return ESP_OK;
}

esp_err_t adc_oneshot_get_calibrated_result(adc_oneshot_unit_handle_t handle,
                                            adc_cali_handle_t cali_handle,
                                            adc_channel_t chan,
                                            int *cali_result)
{
    if (!cali_result) {
        return ESP_ERR_INVALID_ARG;
    }
    int raw = 0;
    esp_err_t err = adc_oneshot_read(handle, chan, &raw);
    if (err != ESP_OK) {
        return err;
    }
    return adc_cali_raw_to_voltage(cali_handle, raw, cali_result);
}

/* ------------------------------------------------------------------ *
 *  Calibration schemes — linear full-scale map, no eFuse curves.       *
 * ------------------------------------------------------------------ */

struct adc_cali_scheme_t {
    adc_cali_scheme_ver_t ver;
    adc_unit_t            unit_id;
    adc_channel_t         chan;      /* curve-fitting only             */
    adc_atten_t           atten;
    adc_bitwidth_t        bitwidth;
    bool                  alive;
};

/* Nominal full-scale at the pin per attenuation (design points, mV). */
static int cali_full_scale_mv(adc_atten_t atten)
{
    switch (atten) {
    case ADC_ATTEN_DB_0:   return 950;
    case ADC_ATTEN_DB_2_5: return 1250;
    case ADC_ATTEN_DB_6:   return 2200;
    case ADC_ATTEN_DB_12:
    default:               return 3100;   /* DB_11 is the same enum    */
    }
}

esp_err_t adc_cali_check_scheme(adc_cali_scheme_ver_t *scheme_mask)
{
    if (!scheme_mask) {
        return ESP_ERR_INVALID_ARG;
    }
    *scheme_mask = (adc_cali_scheme_ver_t)(ADC_CALI_SCHEME_VER_CURVE_FITTING |
                                           ADC_CALI_SCHEME_VER_LINE_FITTING);
    return ESP_OK;
}

static esp_err_t cali_create(adc_cali_scheme_ver_t ver, adc_unit_t unit,
                             adc_channel_t chan, adc_atten_t atten,
                             adc_bitwidth_t bw, adc_cali_handle_t *ret)
{
    if (!ret) {
        return ESP_ERR_INVALID_ARG;
    }
    if (unit < 0 || unit >= ADC_SIM_NUM_UNITS ||
        chan < 0 || chan >= ADC_SIM_NUM_CHAN) {
        return ESP_ERR_INVALID_ARG;
    }
    adc_cali_handle_t h = calloc(1, sizeof(*h));
    if (!h) {
        return ESP_ERR_NO_MEM;
    }
    h->ver      = ver;
    h->unit_id  = unit;
    h->chan     = chan;
    h->atten    = atten;
    h->bitwidth = (bw == ADC_BITWIDTH_DEFAULT) ? ADC_BITWIDTH_12 : bw;
    h->alive    = true;
    *ret = h;
    return ESP_OK;
}

esp_err_t adc_cali_create_scheme_curve_fitting(
    const adc_cali_curve_fitting_config_t *config, adc_cali_handle_t *ret_handle)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    return cali_create(ADC_CALI_SCHEME_VER_CURVE_FITTING, config->unit_id,
                       config->chan, config->atten, config->bitwidth,
                       ret_handle);
}

esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t handle)
{
    if (!handle || !handle->alive ||
        handle->ver != ADC_CALI_SCHEME_VER_CURVE_FITTING) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->alive = false;
    free(handle);
    return ESP_OK;
}

esp_err_t adc_cali_create_scheme_line_fitting(
    const adc_cali_line_fitting_config_t *config, adc_cali_handle_t *ret_handle)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    return cali_create(ADC_CALI_SCHEME_VER_LINE_FITTING, config->unit_id,
                       ADC_CHANNEL_0, config->atten, config->bitwidth,
                       ret_handle);
}

esp_err_t adc_cali_delete_scheme_line_fitting(adc_cali_handle_t handle)
{
    if (!handle || !handle->alive ||
        handle->ver != ADC_CALI_SCHEME_VER_LINE_FITTING) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->alive = false;
    free(handle);
    return ESP_OK;
}

esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t handle, int raw,
                                  int *voltage)
{
    if (!handle || !handle->alive || !voltage) {
        return ESP_ERR_INVALID_ARG;
    }
    int bits = (handle->bitwidth >= ADC_BITWIDTH_9 &&
                handle->bitwidth <= ADC_BITWIDTH_13)
             ? (int)handle->bitwidth : ADC_SIM_MAX_RAW_BITS;
    int max  = (1 << bits) - 1;
    if (raw < 0)    raw = 0;
    if (raw > max)  raw = max;
    *voltage = raw * cali_full_scale_mv(handle->atten) / max;
    return ESP_OK;
}
