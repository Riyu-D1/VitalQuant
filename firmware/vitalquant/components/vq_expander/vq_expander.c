/* vq_expander.c — TCA6408 8-bit I2C GPIO expander @ VQ_ADDR_TCA6408 (0x20).
 *
 * Implements components/vq/include/expander.h — the pin contract and the
 * "~RESET = ESP_EN ⇒ all pins revert to inputs on every ESP reset" rule
 * live in that header and in board_pins.h (vq_exp_pin_t). This file only
 * moves the power-domain switches and charger status lines.
 *
 * Init ordering note (deviates from the header's doc order on purpose):
 * the TCA6408 power-on output latch is 0xFF, so flipping CONFIG to
 * outputs first would drive every domain HIGH for one I2C transaction
 * (~0.3 ms @ 100 kHz) before the 0x00 write landed. We preload
 * Output=0x00, then Polarity=0x00, then Config=0x60 — the first level
 * every output drives is the safe LOW.
 *
 * Degraded mode: if the 0x20 probe (or any init bus write) fails,
 * s_present stays false. vq_exp_init() returns the real bus error; all
 * vq_exp_* calls then return ESP_ERR_INVALID_STATE and the bool
 * accessors return safe values (outputs "off", vbus "no", charging
 * "no"). Re-calling vq_exp_init() retries the probe — no extra teardown.
 *
 * EXP_INT = IO3: open-drain, active-low, external 10k pull-up (R53) so
 * NO internal pulls are enabled. IO3 is a strap pin — configured
 * input-only, never driven. The NEGEDGE ISR gives a counting semaphore;
 * exp_int_task debounces 5 ms, reads the Input Port (the read also
 * clears the TCA6408 INT condition), then calls cb(ctx, mask) from task
 * context. If the line is still low after a read, an input moved again
 * during the debounce and it re-reads (bounded).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "vq_types.h"
#include "board_pins.h"
#include "i2c_bus.h"
#include "expander.h"

static const char *TAG = "vq_exp";

/* ---- TCA6408 register map --------------------------------------------- */
#define EXP_REG_INPUT     0x00   /* Input Port      — live pin levels    */
#define EXP_REG_OUTPUT    0x01   /* Output Port     — r/w output latch   */
#define EXP_REG_POLARITY  0x02   /* Polarity Inv.   — kept 0x00          */
#define EXP_REG_CONFIG    0x03   /* 1 = input (POR: 0xFF)                */

#define EXP_CFG_VALUE     0x60u  /* P5,P6 inputs; P0-P4,P7 outputs       */
#define EXP_INPUT_MASK    0x60u  /* bits expected to read back as inputs */
#define EXP_SAFE_OUTPUTS  0x00u  /* all domains off / resets asserted    */

/* ---- EXP_INT (IO3) debounce task -------------------------------------- */
#define EXP_INT_DEBOUNCE_MS   5        /* spec: 5 ms debounce in task   */
#define EXP_INT_TASK_STACK    2560     /* bytes                          */
#define EXP_INT_TASK_PRIO     (tskIDLE_PRIORITY + 6)
#define EXP_INT_READ_TRIES    3        /* reads per wake while ~INT low  */
#define EXP_INT_SEM_MAX       4        /* coalesces bursts of edges      */

static SemaphoreHandle_t  s_mux;        /* serialises out-shadow RMW    */
static SemaphoreHandle_t  s_int_sem;    /* ISR -> exp_int_task kick     */
static TaskHandle_t       s_int_task;
static vq_exp_int_cb_t    s_cb;
static void              *s_cb_ctx;
static volatile bool      s_present;    /* probed + configured          */
static bool               s_isr_service;/* gpio_install_isr_service done */
static bool               s_isr_added;
static uint8_t            s_out_shadow; /* last byte written to 0x01    */
static uint8_t            s_in_shadow;  /* last byte read from 0x00     */

/* ------------------------------------------------------------------ */
static bool exp_is_output(vq_exp_pin_t pin)
{
    unsigned p = (unsigned)pin;
    return p <= 4u || p == (unsigned)VQ_EXP_CHG_DIS;   /* P0-P4, P7 */
}

static vq_err_t exp_rd(uint8_t reg, uint8_t *val)
{
    return vq_i2c_read_reg(VQ_ADDR_TCA6408, reg, val, 1);
}

static vq_err_t exp_wr(uint8_t reg, uint8_t val)
{
    return vq_i2c_write_reg(VQ_ADDR_TCA6408, reg, &val, 1);
}

/* Write the output latch from the shadow; on bus error resync the
 * shadow from the device so get_out never reports a phantom state.   */
static vq_err_t exp_commit_outputs(void)
{
    vq_err_t err = exp_wr(EXP_REG_OUTPUT, s_out_shadow);
    if (err != ESP_OK) {
        uint8_t back;
        if (exp_rd(EXP_REG_OUTPUT, &back) == ESP_OK) {
            s_out_shadow = back;
        }
    }
    return err;
}

/* ---- EXP_INT path --------------------------------------------------- */
static void IRAM_ATTR exp_int_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    if (s_int_sem != NULL) {
        xSemaphoreGiveFromISR(s_int_sem, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static void exp_int_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(s_int_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(EXP_INT_DEBOUNCE_MS));
        while (xSemaphoreTake(s_int_sem, 0) == pdTRUE) {
            /* coalesce edges that arrived during the debounce window */
        }
        if (!s_present) {
            continue;
        }
        /* Reading the Input Port clears the TCA6408 INT latch. If the
         * line is still low afterwards, an input toggled again — re-read
         * (bounded) so a stuck-low ~INT can't mask future edges. */
        for (int i = 0; i < EXP_INT_READ_TRIES; i++) {
            uint8_t mask = 0;
            if (exp_rd(EXP_REG_INPUT, &mask) != ESP_OK) {
                ESP_LOGW(TAG, "INT: input-port read failed (try %d)", i + 1);
                vTaskDelay(pdMS_TO_TICKS(EXP_INT_DEBOUNCE_MS));
                continue;
            }
            xSemaphoreTake(s_mux, portMAX_DELAY);
            s_in_shadow = mask;
            xSemaphoreGive(s_mux);
            vq_exp_int_cb_t cb = s_cb;
            if (cb != NULL) {
                cb(s_cb_ctx, mask);
            }
            if (gpio_get_level(VQ_EXP_INT_GPIO) != 0) {
                break;            /* ~INT released — line is idle-high */
            }
        }
    }
}

/* GPIO3 setup + ISR service + task. Idempotent; callable from both
 * vq_exp_init() and vq_exp_on_int() — every piece is created/registered
 * once and guarded, so repeat calls just re-unmask the interrupt.     */
static vq_err_t exp_int_infra_init(void)
{
    if (s_int_sem == NULL) {
        s_int_sem = xSemaphoreCreateCounting(EXP_INT_SEM_MAX, 0);
        if (s_int_sem == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_isr_service) {
        esp_err_t err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;                    /* e.g. ESP_ERR_NO_MEM       */
        }
        s_isr_service = true;              /* INVALID_STATE: already up */
    }
    const gpio_config_t gc = {
        .pin_bit_mask  = 1ULL << VQ_EXP_INT_GPIO,
        .mode          = GPIO_MODE_INPUT,  /* strap pin — never a output */
        .pull_up_en    = GPIO_PULLUP_DISABLE,     /* R53 10k external   */
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_NEGEDGE,/* open-drain, active-low   */
    };
    esp_err_t err = gpio_config(&gc);
    if (err != ESP_OK) {
        return err;
    }
    if (s_int_task == NULL) {
        if (xTaskCreate(exp_int_task, "exp_int", EXP_INT_TASK_STACK, NULL,
                        EXP_INT_TASK_PRIO, &s_int_task) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_isr_added) {
        err = gpio_isr_handler_add(VQ_EXP_INT_GPIO, exp_int_isr, NULL);
        if (err != ESP_OK) {
            return err;
        }
        s_isr_added = true;
    }
    gpio_intr_enable(VQ_EXP_INT_GPIO);     /* unmask — spec's "arm INT"  */
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
vq_err_t vq_exp_init(void)
{
    if (s_mux == NULL) {
        s_mux = xSemaphoreCreateMutex();
        if (s_mux == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    vq_err_t err = vq_i2c_probe(VQ_ADDR_TCA6408);
    if (err != ESP_OK) {
        s_present = false;
        ESP_LOGW(TAG, "TCA6408 no-ACK @0x20 (%s) — expander degraded",
                 esp_err_to_name(err));
        return err;                  /* real error to caller; API safe */
    }

    /* 1. Preload output latch LOW before any pin becomes an output:
     *    the POR latch is 0xFF — config-first would glitch all domains
     *    HIGH for one transaction. */
    s_out_shadow = EXP_SAFE_OUTPUTS;
    err = exp_wr(EXP_REG_OUTPUT, s_out_shadow);
    if (err != ESP_OK) {
        goto fail;
    }
    /* 2. Polarity: explicit 0x00 (POR default, but cheap insurance if
     *    init ever re-runs on a live expander). */
    err = exp_wr(EXP_REG_POLARITY, 0x00);
    if (err != ESP_OK) {
        goto fail;
    }
    /* 3. Directions: P5,P6 inputs / P0-P4,P7 outputs. */
    err = exp_wr(EXP_REG_CONFIG, EXP_CFG_VALUE);
    if (err != ESP_OK) {
        goto fail;
    }
    /* 4. Snapshot inputs — this read also clears any pending INT. */
    err = exp_rd(EXP_REG_INPUT, &s_in_shadow);
    if (err != ESP_OK) {
        goto fail;
    }
    s_present = true;

    /* 5. Unmask EXP_INT (IO3 negedge). Device stays usable if this
     *    fails — report it but don't un-present the expander. */
    err = exp_int_infra_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "EXP_INT arm failed (%s) — INT path dead, "
                 "expander ops still work", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "TCA6408 up: out=0x%02x cfg=0x%02x in=0x%02x "
             "(VBUS=%d CHG_STAT=%d)", s_out_shadow, EXP_CFG_VALUE,
             s_in_shadow, (s_in_shadow >> VQ_EXP_VBUS_DET) & 1,
             (s_in_shadow >> VQ_EXP_CHG_STAT) & 1);
    return ESP_OK;

fail:
    s_present = false;
    ESP_LOGW(TAG, "TCA6408 config failed (%s) — expander degraded",
             esp_err_to_name(err));
    return err;
}

/* ------------------------------------------------------------------ */
vq_err_t vq_exp_set(vq_exp_pin_t pin, bool high)
{
    if (!exp_is_output(pin)) {
        return ESP_ERR_INVALID_ARG;    /* P5/P6 are inputs             */
    }
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    vq_err_t err;
    xSemaphoreTake(s_mux, portMAX_DELAY);
    if (high) {
        s_out_shadow |=  (uint8_t)(1u << (unsigned)pin);
    } else {
        s_out_shadow &= (uint8_t)~(1u << (unsigned)pin);
    }
    err = exp_commit_outputs();
    xSemaphoreGive(s_mux);
    return err;
}

bool vq_exp_get_out(vq_exp_pin_t pin)
{
    if (!exp_is_output(pin) || !s_present) {
        return false;                  /* safe default: "off"          */
    }
    xSemaphoreTake(s_mux, portMAX_DELAY);
    uint8_t v = s_out_shadow;
    xSemaphoreGive(s_mux);
    return ((v >> (unsigned)pin) & 1u) != 0;
}

vq_err_t vq_exp_read_inputs(uint8_t *mask)
{
    if (mask == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_present) {
        *mask = s_in_shadow;           /* last known (0 until first ok) */
        return ESP_ERR_INVALID_STATE;
    }
    vq_err_t err = exp_rd(EXP_REG_INPUT, mask);
    if (err == ESP_OK) {
        xSemaphoreTake(s_mux, portMAX_DELAY);
        s_in_shadow = *mask;
        xSemaphoreGive(s_mux);
    }
    return err;
}

bool vq_exp_in(vq_exp_pin_t pin)
{
    if ((unsigned)pin > 7u) {
        return false;
    }
    uint8_t mask;
    if (vq_exp_read_inputs(&mask) == ESP_OK) {
        return ((mask >> (unsigned)pin) & 1u) != 0;
    }
    return ((s_in_shadow >> (unsigned)pin) & 1u) != 0;   /* last known */
}

bool vq_exp_vbus_present(void)
{
    if (!s_present) {
        return false;
    }
    return vq_exp_in(VQ_EXP_VBUS_DET);           /* P6 high = USB in    */
}

bool vq_exp_charging(void)
{
    if (!s_present) {
        return false;                  /* unknown ≠ charging — safe    */
    }
    return !vq_exp_in(VQ_EXP_CHG_STAT);          /* P5 low  = charging  */
}

vq_err_t vq_exp_charge_inhibit(bool inhibit)
{
    return vq_exp_set(VQ_EXP_CHG_DIS, inhibit);  /* P7 high = inhibit   */
}

vq_err_t vq_exp_on_int(vq_exp_int_cb_t cb, void *ctx)
{
    /* Stored even while degraded — a later successful vq_exp_init()
     * re-arms the GPIO path and picks this callback up. */
    s_cb     = cb;
    s_cb_ctx = ctx;
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    return exp_int_infra_init();       /* idempotent                    */
}

/* ------------------------------------------------------------------ */
/* Self-test: walk every output pin high→low with register read-back,
 * then confirm P5/P6 are still configured and readable as inputs.
 * Returns a bitmask of the pins that failed (bit N = pin N); 0 = pass.
 *
 * PRECONDITIONS (bring-up context only, see selftest.h §expander_walk):
 *  - every downstream domain is OFF: no AFE conversion, no LED chain
 *    enabled, USB charge either absent or tolerant of a ~ms inhibit;
 *  - every walked pin is restored LOW at the end (the power-off state).
 *
 * P3 (TX5_EN) is walked with back-to-back writes — no read between them.
 * A <50 µs high pulse is physically impossible at the 100 kHz I2C hard
 * limit (one write ≈ 300 µs); the high interval is the data-byte window
 * of the second write, ~230 µs, far below the TX_5V boost soft-start
 * (ms scale) so no 5 V rail can develop. The trade-off, noted for the
 * acceptance report: P3's HIGH state is verified by the write ACKs and
 * the LOW read-back only — a mid-pulse read-back would triple the pulse.
 */
uint8_t vq_exp_selftest_walk(void)
{
    static const uint8_t out_pins[] = {
        VQ_EXP_AFE_RESETZ,   /* P0 */
        VQ_EXP_ADS_PWDN,     /* P1 */
        VQ_EXP_AD5940_RST,   /* P2 */
        VQ_EXP_TX5_EN,       /* P3 — short-pulse variant below          */
        VQ_EXP_IR_GATE,      /* P4 */
        VQ_EXP_CHG_DIS,      /* P7 */
    };
    uint8_t fails = 0;

    if (!s_present) {
        return 0xFF;                   /* degraded: everything fails    */
    }

    xSemaphoreTake(s_mux, portMAX_DELAY);
    for (size_t i = 0; i < sizeof(out_pins); i++) {
        const uint8_t pin = out_pins[i];
        const uint8_t bit = (uint8_t)(1u << pin);
        uint8_t rd = 0;

        if (pin == VQ_EXP_TX5_EN) {
            /* high then low, back-to-back; verify latch afterwards.   */
            s_out_shadow |= bit;
            vq_err_t e1 = exp_wr(EXP_REG_OUTPUT, s_out_shadow);
            const int64_t t0 = esp_timer_get_time();
            s_out_shadow &= (uint8_t)~bit;
            vq_err_t e2 = exp_wr(EXP_REG_OUTPUT, s_out_shadow);
            const int64_t pulse_us = esp_timer_get_time() - t0;
            ESP_LOGI(TAG, "selftest: TX5_EN pulse ~%lld us",
                     (long long)pulse_us);
            vq_err_t e3 = exp_rd(EXP_REG_OUTPUT, &rd);
            if (e1 != ESP_OK || e2 != ESP_OK || e3 != ESP_OK ||
                (rd & bit) != 0u) {
                fails |= bit;
            }
            continue;
        }

        /* P0,P1,P2,P4,P7: high → read-back verify → low → verify.      */
        s_out_shadow |= bit;
        if (exp_wr(EXP_REG_OUTPUT, s_out_shadow) != ESP_OK ||
            exp_rd(EXP_REG_OUTPUT, &rd) != ESP_OK || (rd & bit) == 0u) {
            fails |= bit;
        }
        s_out_shadow &= (uint8_t)~bit;
        if (exp_wr(EXP_REG_OUTPUT, s_out_shadow) != ESP_OK ||
            exp_rd(EXP_REG_OUTPUT, &rd) != ESP_OK || (rd & bit) != 0u) {
            fails |= bit;
        }
    }
    /* All walked pins are LOW again (s_out_shadow bits cleared above —
     * that is the restore step; safe power-off state is preserved).   */

    /* P5/P6 must still be inputs and the input path must answer.       */
    uint8_t cfg = 0;
    if (exp_rd(EXP_REG_CONFIG, &cfg) != ESP_OK ||
        (cfg & EXP_INPUT_MASK) != EXP_INPUT_MASK) {
        fails |= EXP_INPUT_MASK;                 /* flag bits 5,6       */
    }
    uint8_t in = 0;
    if (exp_rd(EXP_REG_INPUT, &in) != ESP_OK) {
        fails |= EXP_INPUT_MASK;
    } else {
        s_in_shadow = in;                        /* also clears ~INT    */
    }
    xSemaphoreGive(s_mux);

    ESP_LOGI(TAG, "selftest walk: %s (fails=0x%02x, in=0x%02x)",
             fails ? "FAIL" : "pass", fails, s_in_shadow);
    return fails;
}
