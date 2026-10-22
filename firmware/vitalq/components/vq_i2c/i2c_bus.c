/* i2c_bus.c — vq_i2c: the single I2C0 master on fix_v22.
 *
 * Implements components/vq/include/i2c_bus.h against the NEW
 * esp_driver_i2c master API (driver/i2c_master.h, ESP-IDF >= 5.2).
 * ESP-IDF 5.4, C99. The header is the contract — do not change call
 * signatures here; fix the header if the contract itself changes.
 *
 * ====================== 100 kHz BUDGET RULE ======================
 * fix_v22 routes ONE I2C bus to 14 nodes through 2.2k pull-ups
 * (R19/R20); measured rise time tr ~= 503 ns fails the 400 kHz spec, so
 * 100 kHz is a HARD limit — enforced by the _Static_assert below. Do
 * not raise it; change the board header only with a schematic change.
 * The 1.8 V island (AS7341 @0x39, MLX90632 @0x3A) is reached through
 * the PCA9306 and is the SAME logical bus: ONE mutex serialises every
 * transaction, probe or transfer, no exceptions or fast paths.
 *
 * At 100 kHz one byte costs ~90 us on the wire and 11+ addresses share
 * it. Device drivers MUST therefore:
 *   - batch register reads (multi-byte auto-increment where offered),
 *   - keep each transaction short — this file caps every transfer at
 *     VQ_I2C_XFER_TIMEOUT_MS, the single tuning point below,
 *   - never poll faster than the sensor profile rates.
 * Total scheduled traffic must fit the hw-brief section 7 budget; if a
 * driver needs more wire time the budget changes, not this frequency.
 * ===============================================================
 */
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "i2c_bus.h"        /* contract — lives in the vq component   */
#include "board_pins.h"     /* VQ_I2C_PORT/SDA/SCL/FREQ board truth   */

static const char *TAG = "vq_i2c";

/* ---- single tuning point -----------------------------------------------
 * VQ_I2C_XFER_TIMEOUT_MS caps EVERY data-plane transaction: 100 ms at
 * 100 kHz is > 8000 wire-bits, far above any legal transfer on this
 * board — anything longer is a wedged slave, not a slow one. Tune here
 * only; per-driver overrides are not offered on purpose.             */
#define VQ_I2C_XFER_TIMEOUT_MS    100     /* per-transaction cap (ms)     */
#define VQ_I2C_PROBE_TIMEOUT_MS   50      /* ACK probe cap (ms)           */
#define VQ_I2C_DEV_CACHE_MAX      16      /* cached 7-bit device handles  */
#define VQ_I2C_ADDR_MAX_7BIT      0x7Fu
#define VQ_I2C_SCAN_FIRST         0x08u   /* reserved-space edge          */
#define VQ_I2C_SCAN_LAST          0x77u   /* first reserved addr is 0x78  */

/* HARD frequency cap — fails the build if board_pins.h ever asks for
 * more than the fix_v22 RC budget allows (see rule above).           */
_Static_assert(VQ_I2C_FREQ_HZ <= 100000u,
               "fix_v22 I2C hard cap is 100 kHz: 2.2k pulls, tr ~503 ns "
               "fails the 400 kHz rise-time spec");

/* ---- module state ------------------------------------------------------
 * Device handles are cached per 7-bit address on first use: the board
 * carries 11+ addresses, so a small fixed table beats add/remove churn.
 * The table is only touched while holding s_lock — no separate guard. */
typedef struct {
    uint8_t                 addr7;
    i2c_master_dev_handle_t handle;
} dev_slot_t;

static i2c_master_bus_handle_t s_bus;       /* NULL until init succeeds */
static SemaphoreHandle_t       s_lock;      /* ONE mutex — whole bus    */
static dev_slot_t              s_devs[VQ_I2C_DEV_CACHE_MAX];
static size_t                  s_dev_used;

/* Bus counters for the 24 h error-rate acceptance metric. GNU __atomic
 * builtins: real atomics (Xtensa S32C1I) while staying strict-C99.
 * xactions counts every attempted wire transaction; errors is the
 * subset that returned != ESP_OK. Writers always hold s_lock, readers
 * are lock-free relaxed loads.                                        */
static uint32_t s_xactions;
static uint32_t s_errors;

/* ---- internals --------------------------------------------------------- */

/* Take the single bus mutex. Fails fast if init never ran. */
static esp_err_t bus_lock(void)
{
    if (s_bus == NULL || s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;       /* call vq_i2c_init() first */
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);  /* infinite wait: cannot fail */
    return ESP_OK;
}

static void account_xaction(esp_err_t err)
{
    __atomic_fetch_add(&s_xactions, 1u, __ATOMIC_RELAXED);
    if (err != ESP_OK) {
        __atomic_fetch_add(&s_errors, 1u, __ATOMIC_RELAXED);
    }
}

/* Resolve the cached device handle, adding the device on first use.
 * Caller MUST hold s_lock. 7-bit addressing, bus-capped speed.       */
static esp_err_t dev_for_addr(uint8_t addr7, i2c_master_dev_handle_t *out)
{
    if (addr7 > VQ_I2C_ADDR_MAX_7BIT) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < s_dev_used; i++) {
        if (s_devs[i].addr7 == addr7) {
            *out = s_devs[i].handle;
            return ESP_OK;
        }
    }
    if (s_dev_used >= VQ_I2C_DEV_CACHE_MAX) {
        return ESP_ERR_NO_MEM;              /* raise VQ_I2C_DEV_CACHE_MAX */
    }
    const i2c_device_config_t dcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr7,
        .scl_speed_hz    = VQ_I2C_FREQ_HZ,  /* hard-capped — see _Static_assert */
        .scl_wait_us     = 0,               /* driver default stretch timeout    */
        .flags = { .disable_ack_check = false },
    };
    i2c_master_dev_handle_t h = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dcfg, &h);
    if (err != ESP_OK) {
        return err;
    }
    s_devs[s_dev_used].addr7  = addr7;
    s_devs[s_dev_used].handle = h;
    s_dev_used++;
    *out = h;
    return ESP_OK;
}

/* One lock, one accounting path: every data-plane helper funnels
 * through here — lock, resolve the (cached) device, run exactly one
 * driver transfer, count it, unlock. ESP_ERR_TIMEOUT / ESP_FAIL flow up
 * unchanged so drivers degrade gracefully per the contract.          */
typedef esp_err_t (*xfer_fn_t)(i2c_master_dev_handle_t dev, void *ctx);

static esp_err_t run_xfer(uint8_t addr7, xfer_fn_t fn, void *ctx)
{
    esp_err_t err = bus_lock();
    if (err != ESP_OK) {
        return err;
    }
    i2c_master_dev_handle_t dev = NULL;
    err = dev_for_addr(addr7, &dev);
    if (err == ESP_OK) {
        err = fn(dev, ctx);
        account_xaction(err);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "xfer dev 0x%02x: %s", addr7, esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

typedef struct {
    const uint8_t *wbuf;
    size_t         wlen;
    uint8_t       *rbuf;
    size_t         rlen;
} xfer_args_t;

static esp_err_t do_transmit(i2c_master_dev_handle_t dev, void *ctx)
{
    const xfer_args_t *a = (const xfer_args_t *)ctx;
    return i2c_master_transmit(dev, a->wbuf, a->wlen, VQ_I2C_XFER_TIMEOUT_MS);
}

static esp_err_t do_receive(i2c_master_dev_handle_t dev, void *ctx)
{
    const xfer_args_t *a = (const xfer_args_t *)ctx;
    return i2c_master_receive(dev, a->rbuf, a->rlen, VQ_I2C_XFER_TIMEOUT_MS);
}

static esp_err_t do_transmit_receive(i2c_master_dev_handle_t dev, void *ctx)
{
    const xfer_args_t *a = (const xfer_args_t *)ctx;
    return i2c_master_transmit_receive(dev, a->wbuf, a->wlen,
                                       a->rbuf, a->rlen,
                                       VQ_I2C_XFER_TIMEOUT_MS);
}

/* ---- contract ---------------------------------------------------------- */

vq_err_t vq_i2c_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;                      /* idempotent per contract  */
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    const i2c_master_bus_config_t bcfg = {
        .i2c_port          = VQ_I2C_PORT,
        .sda_io_num        = VQ_I2C_SDA_GPIO,   /* GPIO8 */
        .scl_io_num        = VQ_I2C_SCL_GPIO,   /* GPIO9 */
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,                 /* standard spike filter */
        .intr_priority     = 0,                 /* driver default        */
        .trans_queue_depth = 0,                 /* synchronous path only */
        .flags = {
            .enable_internal_pullup = false,    /* R19/R20 2.2k fitted   */
            .allow_pd               = false,    /* no light-sleep domain  */
        },
    };
    esp_err_t err = i2c_new_master_bus(&bcfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        s_bus = NULL;
        return err;
    }
    ESP_LOGI(TAG, "I2C0 master up: SDA=%d SCL=%d @ %lu Hz (external 2.2k)",
             (int)VQ_I2C_SDA_GPIO, (int)VQ_I2C_SCL_GPIO,
             (unsigned long)VQ_I2C_FREQ_HZ);
    return ESP_OK;
}

vq_err_t vq_i2c_probe(uint8_t addr7)
{
    if (addr7 > VQ_I2C_ADDR_MAX_7BIT) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bus_lock();
    if (err != ESP_OK) {
        return err;
    }
    /* A probe is a wire transaction too — same mutex, same stats. */
    err = i2c_master_probe(s_bus, addr7, VQ_I2C_PROBE_TIMEOUT_MS);
    account_xaction(err);
    xSemaphoreGive(s_lock);
    return err;                             /* ESP_OK iff the addr ACKed */
}

size_t vq_i2c_scan(uint8_t *out, size_t max)
{
    if (out == NULL || max == 0 || s_bus == NULL) {
        return 0;
    }
    size_t n = 0;
    for (uint16_t a = VQ_I2C_SCAN_FIRST; a <= VQ_I2C_SCAN_LAST && n < max; a++) {
        /* probe takes the mutex per address, so a scan yields the bus
         * between addresses instead of hogging it for 112 probes.     */
        if (vq_i2c_probe((uint8_t)a) == ESP_OK) {
            out[n++] = (uint8_t)a;
        }
    }
    return n;
}

vq_err_t vq_i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xfer_args_t a = { .wbuf = data, .wlen = len };
    return run_xfer(addr, do_transmit, &a);
}

vq_err_t vq_i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xfer_args_t a = { .rbuf = data, .rlen = len };
    return run_xfer(addr, do_receive, &a);
}

vq_err_t vq_i2c_write_read(uint8_t addr, const uint8_t *wdata, size_t wlen,
                           uint8_t *rdata, size_t rlen)
{
    if (wdata == NULL || wlen == 0 || rdata == NULL || rlen == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xfer_args_t a = {
        .wbuf = wdata, .wlen = wlen,
        .rbuf = rdata, .rlen = rlen,
    };
    return run_xfer(addr, do_transmit_receive, &a);
}

vq_err_t vq_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len)
{
    if (len > 0 && data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* reg + payload must leave in ONE START..STOP frame — stage
     * [reg|data] into a single buffer for i2c_master_transmit.
     * len == 0 is legal: a bare register-pointer write.              */
    uint8_t *frame = (uint8_t *)malloc(len + 1);
    if (frame == NULL) {
        return ESP_ERR_NO_MEM;
    }
    frame[0] = reg;
    if (len > 0) {
        memcpy(&frame[1], data, len);
    }
    xfer_args_t a = { .wbuf = frame, .wlen = len + 1 };
    esp_err_t err = run_xfer(addr, do_transmit, &a);
    free(frame);
    return err;
}

vq_err_t vq_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    /* "write register pointer, repeated-start read" — the SMBus pattern
     * is exactly i2c_master_transmit_receive via vq_i2c_write_read.  */
    return vq_i2c_write_read(addr, &reg, 1, data, len);
}

void vq_i2c_stats(uint32_t *xactions, uint32_t *errors)
{
    if (xactions != NULL) {
        *xactions = __atomic_load_n(&s_xactions, __ATOMIC_RELAXED);
    }
    if (errors != NULL) {
        *errors = __atomic_load_n(&s_errors, __ATOMIC_RELAXED);
    }
}

void vq_i2c_stats_reset(void)
{
    __atomic_store_n(&s_xactions, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_errors,   0u, __ATOMIC_RELAXED);
}
