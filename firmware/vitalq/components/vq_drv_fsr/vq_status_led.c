/* vq_status_led.c — board status LED on IO17 (VQ_STATUS_LED_GPIO).
 *
 * fix_v22: LED + R88 150R series to GND, active-high; ~7-9 mA at 3.3 V
 * drive — plain GPIO output, no LEDC (blink is ~2 Hz max, so a plain
 * esp_timer + gpio_set_level is plenty and keeps the LEDC channels free).
 *
 * Blink engine: one periodic esp_timer in TASK dispatch context at
 * tick = period/20 (clamped 5..200 ms); a phase accumulator compares
 * against period*duty/100 for the ON window. Exact edge times drift by
 * <1 tick — irrelevant for a status lamp.
 *
 * Concurrency: esp_timer_stop() only DISARMS the timer — an already-
 * dispatched cb can still run. So every GPIO write (cb and writers)
 * happens under the same mutex: ordering is strictly serialized and the
 * last writer wins. The cb writes NOTHING when not blinking — a stale
 * post-stop cb then can't override vq_status_led()'s solid level.
 * Writers call esp_timer_stop() BEFORE taking the lock; since the cb
 * only touches the lock, stop can never wait on us — no deadlock.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_pins.h"
#include "vq_drv_fsr.h"
#include "vq_drv_fsr_priv.h"

static const char *TAG = "vq_status_led";

#define VQ_LED_TICK_DIV      20u    /* ~20 ticks per blink period      */
#define VQ_LED_TICK_MIN_MS    5u
#define VQ_LED_TICK_MAX_MS  200u
#define VQ_LED_MIN_PERIOD_MS 40u    /* below this, force solid         */

typedef struct {
    esp_timer_handle_t tmr;
    SemaphoreHandle_t  lock;
    volatile bool      active;      /* blinking enabled                */
    volatile uint32_t  period_ms;
    volatile uint32_t  tick_ms;
    volatile uint32_t  phase_ms;
    volatile uint8_t   duty_pct;
    bool               inited;
} vq_led_state_t;

static vq_led_state_t s_led;

static void led_lock(void)
{
    if (!s_led.lock) {
        static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&mux);
        if (!s_led.lock) s_led.lock = xSemaphoreCreateMutex();
        portEXIT_CRITICAL(&mux);
    }
    if (s_led.lock) xSemaphoreTake(s_led.lock, portMAX_DELAY);
}

static void led_unlock(void)
{
    if (s_led.lock) xSemaphoreGive(s_led.lock);
}

static void led_timer_cb(void *arg)
{
    (void)arg;
    led_lock();
    /* Write ONLY while blinking. If a writer parked/set the LED between
     * dispatch and this lock acquire, active==false -> touch nothing. */
    if (s_led.active && s_led.duty_pct && s_led.period_ms) {
        s_led.phase_ms += s_led.tick_ms;
        while (s_led.phase_ms >= s_led.period_ms)
            s_led.phase_ms -= s_led.period_ms;
        bool on = s_led.phase_ms <
            (uint32_t)(((uint64_t)s_led.period_ms * s_led.duty_pct) / 100u);
        gpio_set_level(VQ_STATUS_LED_GPIO, on ? 1 : 0);
    }
    led_unlock();
}

esp_err_t vq_status_led_init(void)
{
    led_lock();
    if (s_led.inited) { led_unlock(); return ESP_OK; }

    const gpio_config_t g = {
        .pin_bit_mask = 1ULL << VQ_STATUS_LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,   /* push-pull, LED to GND  */
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&g);
    if (err == ESP_OK) err = gpio_set_level(VQ_STATUS_LED_GPIO, 0);
    if (err == ESP_OK && !s_led.tmr) {
        const esp_timer_create_args_t tcfg = {
            .callback        = &led_timer_cb,
            .name            = "stled",
            .dispatch_method = ESP_TIMER_TASK,
            .skip_unhandled_events = true,
        };
        err = esp_timer_create(&tcfg, &s_led.tmr);
    }
    if (err == ESP_OK) s_led.inited = true;
    led_unlock();
    return err;
}

void vq_status_led(bool on)
{
    if (vq_status_led_init() != ESP_OK) return;
    esp_timer_stop(s_led.tmr);        /* disarm; a stale cb writes nothing
                                         once active==false (see header) */
    led_lock();
    s_led.active = false;
    gpio_set_level(VQ_STATUS_LED_GPIO, on ? 1 : 0);
    led_unlock();
}

void vq_status_led_blink(uint32_t period_ms, uint8_t duty_pct)
{
    if (vq_status_led_init() != ESP_OK) return;
    if (duty_pct > 100u) duty_pct = 100u;
    if (duty_pct == 0u || period_ms == 0u) { vq_status_led(false); return; }
    if (duty_pct == 100u || period_ms < VQ_LED_MIN_PERIOD_MS) {
        vq_status_led(true);
        return;
    }

    uint32_t tick = period_ms / VQ_LED_TICK_DIV;
    if (tick < VQ_LED_TICK_MIN_MS) tick = VQ_LED_TICK_MIN_MS;
    if (tick > VQ_LED_TICK_MAX_MS) tick = VQ_LED_TICK_MAX_MS;

    esp_timer_stop(s_led.tmr);
    led_lock();
    s_led.period_ms = period_ms;
    s_led.duty_pct  = duty_pct;
    s_led.tick_ms   = tick;
    s_led.phase_ms  = 0;
    s_led.active    = true;
    gpio_set_level(VQ_STATUS_LED_GPIO, 1);   /* ON phase starts now    */
    led_unlock();
    esp_timer_start_periodic(s_led.tmr, (uint64_t)tick * 1000ULL);
}

/* Component-private: reached from the FSR sensor's sleep() op because
 * the pre-sleep walk only visits sensor ops. Stops blinking and leaves
 * IO17 driven LOW. gpio_hold_en / pad latching for deep sleep stays
 * with power_mgr (§4.4) — this only guarantees a dark, low pin. */
void vq_status_led_park(void)
{
    if (!s_led.inited) return;
    esp_timer_stop(s_led.tmr);
    led_lock();
    s_led.active = false;
    gpio_set_level(VQ_STATUS_LED_GPIO, 0);
    led_unlock();
    ESP_LOGD(TAG, "parked: IO%d low", (int)VQ_STATUS_LED_GPIO);
}
