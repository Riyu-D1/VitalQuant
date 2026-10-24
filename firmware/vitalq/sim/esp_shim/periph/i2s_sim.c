/* i2s_sim.c — host model of the ESP-IDF v5.4 I2S channel driver,
 * PDM RX path only (the firmware's IM69D130 use).
 *
 * Model contract:
 *  - Handles walk REGISTERED -> READY (init_pdm_rx_mode) -> RUNNING
 *    (enable) -> READY (disable), exactly like the real driver.
 *  - enable() starts the PDM clock: the channel's clk GPIO is driven
 *    to the "clock running" level (1) through vboard_gpio_from_firmware,
 *    disable() drives it 0.  A read while the clk pin is held low —
 *    e.g. vq_mic_sleep() parked IO40 as a plain GPIO low — returns
 *    digital silence (flat stream == powered-down/absent mic).
 *  - PCM flows at the configured sample rate against the virtual clock
 *    (vboard_now_us): each read delivers min(pending, request) and
 *    blocks up to timeout_ms waiting for more; the pending backlog is
 *    capped at the configured DMA depth (older data is overrun-dropped,
 *    matching DMA ring behaviour).
 *  - Samples come from the board-side "im69d130" model
 *    (devices/mic_src.c): left slot = mic PCM, right slot = 0
 *    (SELECT pin is strapped low — mono left device only).
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "driver/gpio.h"

#include "vboard.h"
#include "devices/mic_src.h"        /* vbdev_mic_next_sample() */

#define I2S_SIM_MAX_CHAN     4
#define I2S_SIM_MAX_PORTS    2

typedef enum {
    I2S_ST_REGISTERED = 0,      /* allocated, not mode-initialized     */
    I2S_ST_READY,               /* initialized, clock stopped          */
    I2S_ST_RUNNING,             /* enabled, clock + DMA running        */
} i2s_sim_state_t;

struct i2s_channel_obj_t {
    i2s_port_t               port;
    i2s_role_t               role;
    i2s_dir_t                dir;
    i2s_comm_mode_t          mode;
    i2s_sim_state_t          state;

    uint32_t                 dma_desc_num;
    uint32_t                 dma_frame_num;

    /* PDM RX config */
    i2s_pdm_rx_clk_config_t  clk;
    i2s_pdm_rx_slot_config_t slot;
    i2s_pdm_rx_gpio_config_t gpio;

    /* stream position, in PCM frames (1 frame = 1 sample per active slot) */
    int64_t                  enable_us;    /* virtual time at RUNNING   */
    int64_t                  delivered;    /* frames pulled so far      */

    /* event callbacks (registered, never fired — no ISR model) */
    i2s_event_callbacks_t    cbs;
    void                    *user_data;
    i2s_chan_handle_t        pair_chan;    /* tx<->rx on shared port    */

    bool                     alive;
};

static i2s_chan_handle_t s_chans[I2S_SIM_MAX_CHAN];

static i2s_chan_handle_t i2s_sim_alloc(void)
{
    for (int i = 0; i < I2S_SIM_MAX_CHAN; i++) {
        if (!s_chans[i]) {
            i2s_chan_handle_t h = calloc(1, sizeof(*h));
            if (!h) {
                return NULL;
            }
            h->alive = true;
            s_chans[i] = h;
            return h;
        }
    }
    return NULL;
}

static bool i2s_sim_known(i2s_chan_handle_t h)
{
    for (int i = 0; i < I2S_SIM_MAX_CHAN; i++) {
        if (s_chans[i] == h) {
            return h && h->alive;
        }
    }
    return false;
}

static void i2s_sim_forget(i2s_chan_handle_t h)
{
    for (int i = 0; i < I2S_SIM_MAX_CHAN; i++) {
        if (s_chans[i] == h) {
            s_chans[i] = NULL;
            return;
        }
    }
}

esp_err_t i2s_new_channel(const i2s_chan_config_t *chan_cfg,
                          i2s_chan_handle_t *ret_tx_handle,
                          i2s_chan_handle_t *ret_rx_handle)
{
    if (!chan_cfg || (!ret_tx_handle && !ret_rx_handle)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (chan_cfg->id < 0 || chan_cfg->id >= I2S_NUM_AUTO) {
        return ESP_ERR_INVALID_ARG;         /* S3 has ports 0..1 only  */
    }
    i2s_port_t port = (chan_cfg->id == I2S_NUM_AUTO) ? I2S_NUM_0
                                                     : chan_cfg->id;

    i2s_chan_handle_t tx = NULL, rx = NULL;
    if (ret_tx_handle) {
        tx = i2s_sim_alloc();
        if (!tx) {
            return ESP_ERR_NOT_FOUND;
        }
        tx->port          = port;
        tx->role          = chan_cfg->role;
        tx->dir           = I2S_DIR_TX;
        tx->mode          = I2S_COMM_MODE_NONE;
        tx->state         = I2S_ST_REGISTERED;
        tx->dma_desc_num  = chan_cfg->dma_desc_num;
        tx->dma_frame_num = chan_cfg->dma_frame_num;
        *ret_tx_handle    = tx;
    }
    if (ret_rx_handle) {
        rx = i2s_sim_alloc();
        if (!rx) {
            if (tx) {
                tx->alive = false;
                i2s_sim_forget(tx);
                free(tx);
                *ret_tx_handle = NULL;
            }
            return ESP_ERR_NOT_FOUND;
        }
        rx->port          = port;
        rx->role          = chan_cfg->role;
        rx->dir           = I2S_DIR_RX;
        rx->mode          = I2S_COMM_MODE_NONE;
        rx->state         = I2S_ST_REGISTERED;
        rx->dma_desc_num  = chan_cfg->dma_desc_num;
        rx->dma_frame_num = chan_cfg->dma_frame_num;
        *ret_rx_handle    = rx;
    }
    if (tx) {
        tx->pair_chan = rx;
    }
    if (rx) {
        rx->pair_chan = tx;
    }
    return ESP_OK;
}

static int i2s_sim_frame_bytes(const i2s_chan_handle_t h)
{
    /* PDM is fixed 16-bit; mono = 1 slot/frame, stereo = 2. */
    int slots = (h->slot.slot_mode == I2S_SLOT_MODE_STEREO) ? 2 : 1;
    return slots * (int)sizeof(int16_t);
}

static int64_t i2s_sim_dma_backlog_frames(const i2s_chan_handle_t h)
{
    int64_t frames = (int64_t)h->dma_desc_num * (int64_t)h->dma_frame_num;
    if (frames <= 0) {
        frames = 6 * 240;                   /* the default-depth guard */
    }
    return frames;
}

/* Drive (or release) the channel's PDM clock pin through the
 * firmware-output path — vboard_gpio_get() then reports the level the
 * ESP is putting on the pad, which is what parks/un-parks interact
 * with.  Clock running = level 1, stopped = level 0. */
static void i2s_sim_drive_clk(const i2s_chan_handle_t h, int level)
{
    if ((int)h->gpio.clk >= 0) {
        vboard_gpio_from_firmware((int)h->gpio.clk, level);
    }
}

esp_err_t i2s_channel_init_pdm_rx_mode(i2s_chan_handle_t handle,
                                     const i2s_pdm_rx_config_t *pdm_rx_cfg)
{
    if (!handle || !handle->alive || !pdm_rx_cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->dir != I2S_DIR_RX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->state != I2S_ST_REGISTERED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (handle->port != I2S_NUM_0) {
        return ESP_ERR_NOT_SUPPORTED;   /* PDM RX is I2S0-only on S3   */
    }
    if (pdm_rx_cfg->clk_cfg.sample_rate_hz == 0 ||
        pdm_rx_cfg->slot_cfg.data_bit_width != I2S_DATA_BIT_WIDTH_16BIT) {
        return ESP_ERR_INVALID_ARG;     /* PDM is fixed 16-bit         */
    }
    handle->clk  = pdm_rx_cfg->clk_cfg;
    handle->slot = pdm_rx_cfg->slot_cfg;
    handle->gpio = pdm_rx_cfg->gpio_cfg;
    handle->mode = I2S_COMM_MODE_PDM;
    handle->state = I2S_ST_READY;
    i2s_sim_drive_clk(handle, 0);         /* peripheral owns pin, idle */
    return ESP_OK;
}

esp_err_t i2s_channel_enable(i2s_chan_handle_t handle)
{
    if (!handle || !handle->alive) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->state != I2S_ST_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    handle->state     = I2S_ST_RUNNING;
    handle->enable_us = vboard_now_us();
    handle->delivered = 0;
    i2s_sim_drive_clk(handle, 1);         /* PDM clock out -> mic wakes */
    return ESP_OK;
}

esp_err_t i2s_channel_disable(i2s_chan_handle_t handle)
{
    if (!handle || !handle->alive) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->state != I2S_ST_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    i2s_sim_drive_clk(handle, 0);         /* clock stops -> mic power-down */
    handle->state = I2S_ST_READY;
    return ESP_OK;
}

/* PCM frames the DMA engine has produced since enable() that the
 * caller hasn't pulled yet (clamped to the ring depth). */
static int64_t i2s_sim_pending_frames(i2s_chan_handle_t h, int64_t now_us)
{
    if (now_us < h->enable_us) {
        now_us = h->enable_us;
    }
    int64_t elapsed   = now_us - h->enable_us;
    int64_t produced  = elapsed * (int64_t)h->clk.sample_rate_hz / 1000000;
    int64_t backlog   = i2s_sim_dma_backlog_frames(h);
    int64_t pend      = produced - h->delivered;
    if (pend > backlog) {
        /* overrun: the DMA ring already dropped the oldest frames —
         * count them as delivered so pending stays bounded.        */
        h->delivered += (pend - backlog);
        pend = backlog;
    }
    return pend;
}

/* Pull one PCM frame into the caller buffer (little-endian int16).
 * clk-pin low -> digital silence (mic in power-down / absent). */
static void i2s_sim_pull_frame(i2s_chan_handle_t h, int16_t *dst)
{
    bool clk_low = ((int)h->gpio.clk >= 0) &&
                   (vboard_gpio_get((int)h->gpio.clk) == 0);
    int16_t s = clk_low ? 0 : vbdev_mic_next_sample();
    if (h->slot.slot_mode == I2S_SLOT_MODE_STEREO) {
        dst[0] = (h->slot.slot_mask & I2S_PDM_SLOT_LEFT) ? s : 0;
        dst[1] = (h->slot.slot_mask & I2S_PDM_SLOT_RIGHT) ? s : 0;
    } else {
        dst[0] = s;                     /* mono: the masked slot only  */
    }
}

esp_err_t i2s_channel_read(i2s_chan_handle_t handle, void *dest,
                           size_t size, size_t *bytes_read,
                           uint32_t timeout_ms)
{
    if (!handle || !handle->alive || !dest || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->dir != I2S_DIR_RX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->state != I2S_ST_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    const int    frame_b   = i2s_sim_frame_bytes(handle);
    int64_t      wait_left = (int64_t)timeout_ms * 1000;
    const int64_t frame_us = 1000000 / (int64_t)handle->clk.sample_rate_hz + 1;

    for (;;) {
        int64_t now   = vboard_now_us();
        int64_t pend  = i2s_sim_pending_frames(handle, now);
        int64_t want  = (int64_t)(size / (size_t)frame_b);
        int64_t take  = pend < want ? pend : want;
        size_t  done  = 0;
        int16_t *out  = (int16_t *)dest;

        for (int64_t i = 0; i < take; i++) {
            int16_t frame[2] = { 0, 0 };
            i2s_sim_pull_frame(handle, frame);
            out[done++] = frame[0];
            if (frame_b == (int)(2 * sizeof(int16_t))) {
                out[done++] = frame[1];
            }
        }
        handle->delivered += take;
        if (bytes_read) {
            *bytes_read = take * (size_t)frame_b;
        }
        if (take > 0 || timeout_ms == 0) {
            /* Non-blocking reads return whatever is pending — zero
             * bytes included (the driver drains stale DMA this way). */
            return ESP_OK;
        }
        /* Blocking path: wait for the next frame boundary, bounded by
         * the total timeout even if the virtual clock doesn't move. */
        if (wait_left <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        int64_t nap = frame_us < wait_left ? frame_us : wait_left;
        vboard_clock_sleep_us(nap);
        wait_left -= nap;
    }
}

esp_err_t i2s_channel_reconfig_pdm_rx_clock(i2s_chan_handle_t handle,
                                            const i2s_pdm_rx_clk_config_t *clk_cfg)
{
    if (!handle || !handle->alive || !clk_cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->mode != I2S_COMM_MODE_PDM || handle->dir != I2S_DIR_RX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->state != I2S_ST_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (clk_cfg->sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->clk = *clk_cfg;
    return ESP_OK;
}

esp_err_t i2s_channel_reconfig_pdm_rx_slot(i2s_chan_handle_t handle,
                                           const i2s_pdm_rx_slot_config_t *slot_cfg)
{
    if (!handle || !handle->alive || !slot_cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->mode != I2S_COMM_MODE_PDM || handle->dir != I2S_DIR_RX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->state != I2S_ST_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (slot_cfg->data_bit_width != I2S_DATA_BIT_WIDTH_16BIT) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->slot = *slot_cfg;
    return ESP_OK;
}

esp_err_t i2s_channel_reconfig_pdm_rx_gpio(i2s_chan_handle_t handle,
                                           const i2s_pdm_rx_gpio_config_t *gpio_cfg)
{
    if (!handle || !handle->alive || !gpio_cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->mode != I2S_COMM_MODE_PDM || handle->dir != I2S_DIR_RX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->state != I2S_ST_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    handle->gpio = *gpio_cfg;
    i2s_sim_drive_clk(handle, 0);         /* pins re-routed, still idle */
    return ESP_OK;
}

esp_err_t i2s_del_channel(i2s_chan_handle_t handle)
{
    if (!handle || !handle->alive || !i2s_sim_known(handle)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->state == I2S_ST_RUNNING) {
        return ESP_ERR_INVALID_STATE;   /* must disable first         */
    }
    handle->alive = false;
    i2s_sim_forget(handle);
    free(handle);
    return ESP_OK;
}

esp_err_t i2s_channel_get_info(i2s_chan_handle_t handle,
                               i2s_chan_info_t *chan_info)
{
    if (!handle || !handle->alive || !chan_info) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!i2s_sim_known(handle)) {
        return ESP_ERR_NOT_FOUND;
    }
    memset(chan_info, 0, sizeof(*chan_info));
    chan_info->id        = handle->port;
    chan_info->role      = handle->role;
    chan_info->dir       = handle->dir;
    chan_info->mode      = handle->mode;
    chan_info->pair_chan = handle->pair_chan;
    if (handle->state != I2S_ST_REGISTERED) {
        chan_info->total_dma_buf_size =
            handle->dma_desc_num * handle->dma_frame_num *
            (uint32_t)i2s_sim_frame_bytes(handle);
    }
    return ESP_OK;
}

esp_err_t i2s_channel_register_event_callback(i2s_chan_handle_t handle,
                                              const i2s_event_callbacks_t *callbacks,
                                              void *user_data)
{
    if (!handle || !handle->alive || !callbacks) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->state == I2S_ST_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    handle->cbs       = *callbacks;
    handle->user_data = user_data;
    return ESP_OK;
}

/* TX direction is declared for API parity; there is no TX model in
 * this sim — calls fail fast instead of silently dropping audio.     */
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t tx_handle,
                                   const void *src, size_t size,
                                   size_t *bytes_loaded)
{
    (void)src; (void)size;
    if (!tx_handle || !tx_handle->alive || !bytes_loaded) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tx_handle->dir != I2S_DIR_TX) {
        return ESP_ERR_INVALID_ARG;
    }
    *bytes_loaded = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t i2s_channel_write(i2s_chan_handle_t handle, const void *src,
                            size_t size, size_t *bytes_written,
                            uint32_t timeout_ms)
{
    (void)src; (void)size; (void)timeout_ms;
    if (!handle || !handle->alive) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->dir != I2S_DIR_TX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bytes_written) {
        *bytes_written = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
