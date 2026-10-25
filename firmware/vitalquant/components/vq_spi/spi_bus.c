/* spi_bus.c — vq_spi: the single SPI2/FSPI master on fix_v22.
 *
 * Implements components/vq/include/spi_bus.h against the GPSPI master
 * API (driver/spi_master.h). ESP-IDF 5.4, C99. The header is the
 * contract — do not change call signatures here.
 *
 * ==================== SHARED-MISO HARD RULES ====================
 * All five slaves share one MISO line (CS_MAX86141/IO38 also gates the
 * U26 level shifter), so the bus dies if two chip-selects ever go low
 * together. Enforcement is BY CONSTRUCTION:
 *   - ONE FreeRTOS mutex (s_lock) serialises every transfer; there is
 *     no transfer path that does not hold it.
 *   - CS is driven by THIS FILE only: devices are attached with
 *     spics_io_num = -1, so the IDF driver never touches a CS pin.
 *     cs_low()/cs_high() are the only code that asserts/deasserts.
 *   - s_cs_owner is a volatile token set while any CS is low; cs_low()
 *     checks it on entry and ESP_EARLY_LOGEs a violation — under the
 *     mutex that can only fire if out-of-band code drove a CS pin.
 * All CS idle HIGH (external 10k pull-ups; we hold them driven high).
 * ================================================================
 *
 * Clock policy: requested clock_hz is CLAMPED to the per-device cap in
 * board_pins.h (VQ_SPI_*_HZ) — MAX86141/AFE4900/AD5940 <= 8 MHz,
 * ADS1292R <= 4 MHz (mode 1, >=1 us CS setup/hold enforced for its
 * decode timing), W25Q512 <= 40 MHz. Requesting 0 selects the cap.
 *
 * MISO float self-test (vq_spi_miso_float_check): IO13 is the native
 * FSPI-Q iomux pin, so the bus driver routed it through the IO_MUX
 * (bus_iomux_pins_set_quad in spi_common.c). This file restores it the
 * same way — gpio_iomux_in(gpio, spi_periph_signal[host].spiq_in) to
 * re-bypass the GPIO matrix into FSPIQ_IN_IDX, then gpio_iomux_out()
 * to reselect the peripheral function — plus a verbatim write-back of
 * the saved IO_MUX word to restore pull bits exactly.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"        /* esp_rom_delay_us */
#include "esp_rom_gpio.h"       /* matrix-path restore (fallback only) */
#include "soc/soc.h"            /* REG_READ / REG_WRITE */
#include "soc/gpio_periph.h"    /* GPIO_PIN_MUX_REG[] */
#include "soc/spi_periph.h"     /* spi_periph_signal — driver's own map */

#include "spi_bus.h"            /* contract — lives in the vq component */
#include "board_pins.h"         /* VQ_SPI_* / VQ_CS_* board truth       */

static const char *TAG = "vq_spi";

/* ---- single tuning points ------------------------------------------- */
#define VQ_SPI_MAX_XFER_BYTES   4096    /* bus max_transfer_sz (4 KB)    */
#define VQ_SPI_MISO_SETTLE_US   100     /* float-check settle delay      */
#define VQ_SPI_ADS1292_CS_US    1       /* decode setup/hold floor       */
#define VQ_SPI_CS_CYCLES_MAX    16      /* cs_ena_* field width          */

/* HARD caps — fail the build if board_pins.h ever relaxes the limits
 * the fix_v22 silicon allows (see contract header §"hard rules").    */
_Static_assert(VQ_SPI_ADS1292_HZ  <=  4u*1000*1000, "ADS1292 cap 4 MHz");
_Static_assert(VQ_SPI_AFE4900_HZ  <=  8u*1000*1000, "AFE4900 cap 8 MHz");
_Static_assert(VQ_SPI_AD5940_HZ   <=  8u*1000*1000, "AD5940 cap 8 MHz");
_Static_assert(VQ_SPI_MAX86141_HZ <=  8u*1000*1000, "MAX86141 cap 8 MHz");
_Static_assert(VQ_SPI_FLASH_HZ    <= 40u*1000*1000, "W25Q512 cap 40 MHz");
_Static_assert(VQ_SPI_HOST == SPI2_HOST, "board uses FSPI/SPI2 host");

/* ---- module state ----------------------------------------------------
 * Handle pool: drivers may legitimately hold two handles on one device
 * id (e.g. a gentle probe-speed handle plus a run-speed handle — see
 * ads1292 and max86141). The ESP-IDF device handle is the value handed
 * back by vq_spi_add() and passed into every transfer call.  Slots are
 * only touched while holding s_lock — no separate guard.             */
typedef struct {
    spi_device_handle_t handle;   /* == the opaque void* we return      */
    vq_spi_dev_t        dev;      /* device id (clock-cap lookup)       */
    gpio_num_t          cs;       /* CS GPIO driven manually by us      */
    uint8_t             pre_us;   /* CS low -> first SCK setup delay    */
    uint8_t             post_us;  /* last SCK -> CS high hold delay     */
    bool                used;
} dev_slot_t;

#define VQ_SPI_MAX_HANDLES    (VQ_SPI_DEV_COUNT * 2)
static dev_slot_t       s_dev[VQ_SPI_MAX_HANDLES];
static SemaphoreHandle_t s_lock;                     /* ONE mutex — whole bus */
static volatile const dev_slot_t *s_cs_owner;        /* non-NULL while any CS is low */
static volatile bool    s_inited;

/* Every CS on the board — asserted HIGH at init so the shared MISO is
 * released before any device attaches. Board code may already have set
 * them; re-asserting here makes vq_spi_init self-sufficient.         */
static const gpio_num_t s_all_cs[] = {
    VQ_CS_ADS1292_GPIO,
    VQ_CS_AD5940_GPIO,
    VQ_CS_FLASH_GPIO,
    VQ_CS_AFE4900_GPIO,
    VQ_CS_MAX86141_GPIO,
};
#define VQ_SPI_CS_N     (sizeof(s_all_cs) / sizeof(s_all_cs[0]))
_Static_assert(VQ_SPI_CS_N == VQ_SPI_DEV_COUNT, "one CS per device");

/* Per-device clock caps, indexed by vq_spi_dev_t (enum order).       */
static const uint32_t s_clk_cap[VQ_SPI_DEV_COUNT] = {
    [VQ_SPI_DEV_ADS1292]  = VQ_SPI_ADS1292_HZ,
    [VQ_SPI_DEV_AFE4900]  = VQ_SPI_AFE4900_HZ,
    [VQ_SPI_DEV_AD5940]   = VQ_SPI_AD5940_HZ,
    [VQ_SPI_DEV_FLASH]    = VQ_SPI_FLASH_HZ,
    [VQ_SPI_DEV_MAX86141] = VQ_SPI_MAX86141_HZ,
};

/* Bus counters — same scheme as vq_i2c: GNU __atomic builtins are real
 * atomics on Xtensa (S32C1I) while staying strict-C99. Writers hold
 * s_lock; readers are lock-free relaxed loads.                      */
static uint32_t s_xactions;
static uint32_t s_errors;

/* ---- internals ------------------------------------------------------- */

static void account_xaction(esp_err_t err)
{
    __atomic_fetch_add(&s_xactions, 1u, __ATOMIC_RELAXED);
    if (err != ESP_OK) {
        __atomic_fetch_add(&s_errors, 1u, __ATOMIC_RELAXED);
    }
}

static void account_error(void)
{
    __atomic_fetch_add(&s_errors, 1u, __ATOMIC_RELAXED);
}

static esp_err_t bus_lock(void)
{
    if (!s_inited || s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;           /* call vq_spi_init() first */
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);      /* infinite wait: cannot fail */
    return ESP_OK;
}

/* Drive every CS HIGH. Called with s_lock held (or pre-RTOS at init). */
static void cs_all_high(void)
{
    for (size_t i = 0; i < VQ_SPI_CS_N; i++) {
        gpio_set_level(s_all_cs[i], 1);
    }
}

/* Assert this device's CS. Caller MUST hold s_lock.
 * The owner-token check is the belt behind the mutex: if another CS is
 * somehow low (out-of-band GPIO write, or a bug where a slot's CS pin
 * aliases another device's), we log loudly, count it, and carry on —
 * aborting mid-bring-up would hide the evidence. */
static void cs_low(const dev_slot_t *d)
{
    if (s_cs_owner != NULL && s_cs_owner != d) {
        ESP_EARLY_LOGE(TAG, "CS violation: IO%d still owned while asserting IO%d",
                       (int)s_cs_owner->cs, (int)d->cs);
        account_error();
    }
    s_cs_owner = d;
    gpio_set_level(d->cs, 0);
    if (d->pre_us) {
        esp_rom_delay_us(d->pre_us);    /* ADS1292 needs >=1us before SCK */
    }
}

/* Release this device's CS. Caller MUST hold s_lock. */
static void cs_high(const dev_slot_t *d)
{
    if (d->post_us) {
        esp_rom_delay_us(d->post_us);   /* hold CS low after last SCK     */
    }
    gpio_set_level(d->cs, 1);
    if (s_cs_owner == d) {
        s_cs_owner = NULL;
    } else {
        /* Should be impossible under s_lock — same story as cs_low(). */
        ESP_EARLY_LOGE(TAG, "CS violation: releasing IO%d not owned", (int)d->cs);
        account_error();
        s_cs_owner = NULL;
    }
}

/* Map a handle (the void* we returned from vq_spi_add) to its slot.
 * Caller holds s_lock or accepts a racy-but-validated lookup — slots
 * change only under s_lock.                                        */
static dev_slot_t *find_slot(const void *handle)
{
    if (handle == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < VQ_SPI_MAX_HANDLES; i++) {
        if (s_dev[i].used && (void *)s_dev[i].handle == handle) {
            return &s_dev[i];
        }
    }
    return NULL;
}

/* cs_ena_pretrans/posttrans are in SCK bit-cycles (0..16). We drive CS
 * manually so these are belt-and-braces hardware padding, but keep them
 * consistent with the microsecond policy anyway.                    */
static uint8_t us_to_cycles(uint8_t us, uint32_t hz)
{
    uint32_t c = (uint32_t)(((uint64_t)us * hz + 999999u) / 1000000u);
    return (c > VQ_SPI_CS_CYCLES_MAX) ? VQ_SPI_CS_CYCLES_MAX : (uint8_t)c;
}

/* One polling leg under an already-asserted CS. tx or rx may be NULL
 * (tx==NULL clocks dummy bits for pure reads). */
static esp_err_t poll_leg(const dev_slot_t *d, const uint8_t *tx,
                          uint8_t *rx, size_t len)
{
    spi_transaction_t t = {
        .length    = len * 8u,          /* bits                          */
        .rxlength  = len * 8u,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_polling_transmit(d->handle, &t);
    account_xaction(err);
    return err;
}

/* ---- contract -------------------------------------------------------- */

vq_err_t vq_spi_init(void)
{
    if (s_inited) {
        return ESP_OK;                  /* idempotent per contract      */
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    /* CS HIGH *before* the bus comes up: preload the output latch with
     * 1 then enable the driver — glitch-free even if board_init never
     * ran. All five CS, unconditionally re-asserted.                 */
    for (size_t i = 0; i < VQ_SPI_CS_N; i++) {
        gpio_set_level(s_all_cs[i], 1);
        gpio_set_direction(s_all_cs[i], GPIO_MODE_OUTPUT);
    }

    const spi_bus_config_t bus = {
        .mosi_io_num     = VQ_SPI_MOSI_GPIO,   /* IO11 */
        .miso_io_num     = VQ_SPI_MISO_GPIO,   /* IO13 — native FSPI-Q   */
        .sclk_io_num     = VQ_SPI_SCLK_GPIO,   /* IO12 */
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .data4_io_num    = -1,
        .data5_io_num    = -1,
        .data6_io_num    = -1,
        .data7_io_num    = -1,
        .max_transfer_sz = VQ_SPI_MAX_XFER_BYTES,
        .flags           = 0,
        .intr_flags      = 0,
    };
    /* SPI_DMA_CH_AUTO: FLASH page traffic wants >64 B bursts; the 5.4
     * driver bounce-buffers non-DMA-capable tx/rx automatically, and
     * spi_device_polling_transmit works regardless of DMA presence.   */
    esp_err_t err = spi_bus_initialize(VQ_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        account_error();
        return err;
    }
    s_inited = true;
    ESP_LOGI(TAG, "SPI2 master up: MOSI=%d MISO=%d SCK=%d, 5x CS held high",
             (int)VQ_SPI_MOSI_GPIO, (int)VQ_SPI_MISO_GPIO,
             (int)VQ_SPI_SCLK_GPIO);
    return ESP_OK;
}

void *vq_spi_add(const vq_spi_dev_cfg_t *cfg)
{
    if (cfg == NULL || cfg->dev >= VQ_SPI_DEV_COUNT || !s_inited) {
        return NULL;
    }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(cfg->cs_gpio)) {
        return NULL;
    }

    /* Clamp the request to the device cap; 0 means "cap". */
    uint32_t clk = cfg->clock_hz;
    if (clk == 0 || clk > s_clk_cap[cfg->dev]) {
        clk = s_clk_cap[cfg->dev];
    }
    /* ADS1292 decode logic needs >=1us CS setup and hold — enforce the
     * floor even if the caller asked for less.                       */
    uint8_t pre_us  = cfg->cs_pre_us;
    uint8_t post_us = cfg->cs_post_us;
    if (cfg->dev == VQ_SPI_DEV_ADS1292) {
        if (pre_us  < VQ_SPI_ADS1292_CS_US) pre_us  = VQ_SPI_ADS1292_CS_US;
        if (post_us < VQ_SPI_ADS1292_CS_US) post_us = VQ_SPI_ADS1292_CS_US;
    }

    const spi_device_interface_config_t dcfg = {
        .mode            = (uint8_t)(cfg->mode & 0x3),
        .clock_speed_hz  = (int)clk,
        .spics_io_num    = -1,              /* CS is ours — see rules   */
        .queue_size      = 1,               /* polling API requirement  */
        .cs_ena_pretrans  = us_to_cycles(pre_us,  clk),
        .cs_ena_posttrans = us_to_cycles(post_us, clk),
    };

    esp_err_t err = bus_lock();
    if (err != ESP_OK) {
        return NULL;
    }
    dev_slot_t *s = NULL;
    for (size_t i = 0; i < VQ_SPI_MAX_HANDLES; i++) {
        if (!s_dev[i].used) { s = &s_dev[i]; break; }
    }
    if (s == NULL) {
        xSemaphoreGive(s_lock);
        return NULL;                    /* handle pool exhausted        */
    }
    /* Re-assert this CS as driven-high output before the device can
     * ever see the bus — safe even if board code did it already.     */
    gpio_set_level(cfg->cs_gpio, 1);
    gpio_set_direction(cfg->cs_gpio, GPIO_MODE_OUTPUT);

    err = spi_bus_add_device(VQ_SPI_HOST, &dcfg, &s->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device dev=%d: %s",
                 (int)cfg->dev, esp_err_to_name(err));
        account_error();
        xSemaphoreGive(s_lock);
        return NULL;
    }
    s->cs      = cfg->cs_gpio;
    s->dev     = cfg->dev;
    s->pre_us  = pre_us;
    s->post_us = post_us;
    s->used    = true;
    xSemaphoreGive(s_lock);
    return (void *)s->handle;
}

void vq_spi_remove(void *handle)
{
    if (bus_lock() != ESP_OK) {
        return;
    }
    dev_slot_t *s = find_slot(handle);
    if (s != NULL) {
        gpio_set_level(s->cs, 1);       /* park it before detach        */
        spi_bus_remove_device(s->handle);
        memset(s, 0, sizeof(*s));
    }
    xSemaphoreGive(s_lock);
}

vq_err_t vq_spi_xfer(void *handle, const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (len == 0 || len > VQ_SPI_MAX_XFER_BYTES || (tx == NULL && rx == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bus_lock();
    if (err != ESP_OK) {
        return err;
    }
    dev_slot_t *s = find_slot(handle);
    if (s == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    spi_transaction_t t = {
        .length    = len * 8u,          /* full duplex                  */
        .rxlength  = len * 8u,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    cs_low(s);
    err = spi_device_transmit(s->handle, &t);
    cs_high(s);
    xSemaphoreGive(s_lock);
    account_xaction(err);
    return err;
}

vq_err_t vq_spi_write_read(void *handle, const uint8_t *tx, size_t txlen,
                           uint8_t *rx, size_t rxlen)
{
    if ((txlen > 0 && tx == NULL) || (rxlen > 0 && rx == NULL) ||
        (txlen == 0 && rxlen == 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (txlen > VQ_SPI_MAX_XFER_BYTES || rxlen > VQ_SPI_MAX_XFER_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bus_lock();
    if (err != ESP_OK) {
        return err;
    }
    dev_slot_t *s = find_slot(handle);
    if (s == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    /* CS held across TX-then-RX: one transaction cannot split its
     * phases, so two polling legs under our manual CS do the job with
     * explicit timing — no HW CS toggling exists to interfere.        */
    cs_low(s);
    err = ESP_OK;
    if (txlen > 0) {
        err = poll_leg(s, tx, NULL, txlen);
    }
    if (err == ESP_OK && rxlen > 0) {
        err = poll_leg(s, NULL, rx, rxlen);
    }
    cs_high(s);
    xSemaphoreGive(s_lock);
    return err;
}

vq_err_t vq_spi_reg_write(void *handle, const uint8_t *cmd, size_t cmdlen,
                          const uint8_t *data, size_t dlen)
{
    if (cmd == NULL || cmdlen == 0 || (dlen > 0 && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cmdlen > VQ_SPI_MAX_XFER_BYTES || dlen > VQ_SPI_MAX_XFER_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bus_lock();
    if (err != ESP_OK) {
        return err;
    }
    dev_slot_t *s = find_slot(handle);
    if (s == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    /* [cmd | data] must ship under ONE CS — two TX legs, CS held low. */
    cs_low(s);
    err = poll_leg(s, cmd, NULL, cmdlen);
    if (err == ESP_OK && dlen > 0) {
        err = poll_leg(s, data, NULL, dlen);
    }
    cs_high(s);
    xSemaphoreGive(s_lock);
    return err;
}

vq_err_t vq_spi_reg_read(void *handle, const uint8_t *cmd, size_t cmdlen,
                         uint8_t *data, size_t dlen)
{
    /* Command-out then data-in under one CS == vq_spi_write_read. */
    return vq_spi_write_read(handle, cmd, cmdlen, data, dlen);
}

int vq_spi_miso_float_check(gpio_pull_mode_t pull)
{
    if (bus_lock() != ESP_OK) {
        return -1;
    }
    cs_all_high();                      /* all slaves off the shared MISO */

    /* Park IO13 as a plain GPIO input with the requested pull. */
    const uint32_t saved_mux = REG_READ(GPIO_PIN_MUX_REG[VQ_SPI_MISO_GPIO]);
    gpio_set_direction(VQ_SPI_MISO_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(VQ_SPI_MISO_GPIO, pull);
    esp_rom_delay_us(VQ_SPI_MISO_SETTLE_US);
    const int level = gpio_get_level(VQ_SPI_MISO_GPIO);

    /* Restore MISO to the SPI function. IO13 is the native FSPI-Q iomux
     * pin, so replicate the bus driver's own hookup (spi_common.c
     * bus_iomux_pins_set_quad): bypass the GPIO matrix into FSPIQ_IN_IDX,
     * reselect the peripheral function + peripheral OE control, then
     * write back the saved IO_MUX word (pull + fun_sel bits verbatim).
     * The matrix fallback below keeps this correct if the pin ever
     * moves off the native pad.                                       */
    if ((int)VQ_SPI_MISO_GPIO == spi_periph_signal[VQ_SPI_HOST].spiq_iomux_pin) {
        gpio_iomux_in(VQ_SPI_MISO_GPIO,
                      (uint32_t)spi_periph_signal[VQ_SPI_HOST].spiq_in);
        gpio_iomux_out(VQ_SPI_MISO_GPIO,
                       spi_periph_signal[VQ_SPI_HOST].func, false);
    } else {
        gpio_set_direction(VQ_SPI_MISO_GPIO, GPIO_MODE_INPUT);
        esp_rom_gpio_pad_select_gpio(VQ_SPI_MISO_GPIO);
        esp_rom_gpio_connect_in_signal(VQ_SPI_MISO_GPIO,
                                       (uint32_t)spi_periph_signal[VQ_SPI_HOST].spiq_in,
                                       false);
    }
    REG_WRITE(GPIO_PIN_MUX_REG[VQ_SPI_MISO_GPIO], saved_mux);

    xSemaphoreGive(s_lock);
    return level;
}

void vq_spi_stats(uint32_t *xactions, uint32_t *errors)
{
    if (xactions != NULL) {
        *xactions = __atomic_load_n(&s_xactions, __ATOMIC_RELAXED);
    }
    if (errors != NULL) {
        *errors = __atomic_load_n(&s_errors, __ATOMIC_RELAXED);
    }
}

void vq_spi_stats_reset(void)
{
    __atomic_store_n(&s_xactions, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_errors,   0u, __ATOMIC_RELAXED);
}
