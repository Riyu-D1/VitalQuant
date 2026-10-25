/* esp_sleep.h — host-sim shim of ESP-IDF esp_sleep.h (ESP32-S3 / fix_v22).
 *
 * Semantics are NOT silicon.  Arming calls are RECORDED into
 * g_vsim_sleep (vsim.h) so a scenario can assert exactly what the
 * firmware asked for, and esp_deep_sleep_start() parks the calling
 * thread on a condvar instead of killing the process.  A scenario
 * releases it via vsim_trigger_wakeup() / vsim_sleep_wake_with_gpio();
 * the shim then:
 *     a) calls a scenario-registered reboot fn, else
 *     b) longjmps into the armed reboot checkpoint
 *        (g_vsim_sleep_reboot_jb, set by whoever runs fw_app_main), else
 *     c) _exit(0) so the harness can inspect the recorded state.
 *
 * Wake causes are sticky across the "reboot" — the process survives, so
 * esp_sleep_get_wakeup_cause() inside the re-run fw_app_main returns the
 * injected source, matching RTC-domain behaviour on real silicon.
 *
 * RTC-capable wakeup GPIOs on S3 are 0..21 (RTC_GPIO0..21); that is the
 * range esp_sleep_is_valid_wakeup_gpio() accepts.
 */
#ifndef ESP_SLEEP_H
#define ESP_SLEEP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- esp_err_t: real shim header when present, minimal fallback otherwise.
 * The fallback typedef is the same pattern vboard.h uses (ESP_ERR_T_DECLARED
 * guard); a duplicate `typedef int esp_err_t` is legal C11 anyway.        */
#if defined(__has_include)
#  if __has_include("esp_err.h")
#    include "esp_err.h"
#  endif
#endif
#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#endif
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL (-1)
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM          0x101
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG     0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE   0x103
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND       0x105
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED   0x106
#endif
#ifndef ESP_ERR_TIMEOUT
#define ESP_ERR_TIMEOUT         0x107
#endif
#ifndef ESP_ERR_NOT_ALLOWED
#define ESP_ERR_NOT_ALLOWED     0x10D
#endif

/* gpio_num_t: real type from the gpio shim (driver/gpio.h) when on the
 * include path — always true in the sim build (board_pins.h needs it);
 * prototypes fall back to int so this header also compiles standalone.  */
#if defined(__has_include)
#  if __has_include("driver/gpio.h")
#    include "driver/gpio.h"
#    define VSIM_SLEEP_GPIO_T gpio_num_t
#  endif
#endif
#ifndef VSIM_SLEEP_GPIO_T
#define VSIM_SLEEP_GPIO_T int
#endif

/* S3 RTC-GPIO range: only GPIO0..21 can wake from deep sleep. */
#define VSIM_RTC_GPIO_MAX 21

/* Sleep modes. */
typedef enum {
    ESP_SLEEP_MODE_LIGHT_SLEEP = 0,
    ESP_SLEEP_MODE_DEEP_SLEEP  = 1,
} esp_sleep_mode_t;

/* Wakeup source — enum order matches IDF esp_sleep.h. */
typedef enum {
    ESP_SLEEP_WAKEUP_UNDEFINED = 0,
    ESP_SLEEP_WAKEUP_ALL,           /* pool bit, never returned as cause */
    ESP_SLEEP_WAKEUP_EXT0,
    ESP_SLEEP_WAKEUP_EXT1,
    ESP_SLEEP_WAKEUP_TIMER,
    ESP_SLEEP_WAKEUP_TOUCHPAD,
    ESP_SLEEP_WAKEUP_ULP,
    ESP_SLEEP_WAKEUP_GPIO,
    ESP_SLEEP_WAKEUP_UART,
    ESP_SLEEP_WAKEUP_WIFI,
    ESP_SLEEP_WAKEUP_COCPU,
    ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG,
    ESP_SLEEP_WAKEUP_BT,
} esp_sleep_source_t;

/* EXT1 modes valid on ESP32-S3 (no per-pin level mixing on S3 —
 * SOC_PM_SUPPORT_EXT1_WAKEUP_MODE_PER_PIN is absent there). */
typedef enum {
    ESP_EXT1_WAKEUP_ALL_LOW  = 0,   /* wake when ALL selected pins are low  */
    ESP_EXT1_WAKEUP_ANY_LOW  = 1,   /* wake when ANY selected pin is low    */
    ESP_EXT1_WAKEUP_ANY_HIGH = 2,   /* wake when ANY selected pin is high   */
} esp_sleep_ext1_wakeup_mode_t;

/* esp_deep_sleep_enable_gpio_wakeup() levels. */
typedef enum {
    ESP_GPIO_WAKEUP_GPIO_LOW  = 0,
    ESP_GPIO_WAKEUP_GPIO_HIGH = 1,
} esp_deep_sleep_gpio_wake_up_mode_t;

/* Power domains for esp_sleep_pd_config() — enum order matches IDF. */
typedef enum {
    ESP_PD_DOMAIN_RTC_PERIPH,
    ESP_PD_DOMAIN_RTC8M,
    ESP_PD_DOMAIN_RTC_SLOW_MEM,
    ESP_PD_DOMAIN_RTC_FAST_MEM,
    ESP_PD_DOMAIN_XTAL,
    ESP_PD_DOMAIN_RTC8M_D256,
    ESP_PD_DOMAIN_MODEM,
    ESP_PD_DOMAIN_CPU,
    ESP_PD_DOMAIN_VDDSDIO,
    ESP_PD_DOMAIN_MAX
} esp_sleep_pd_domain_t;

typedef enum {
    ESP_PD_OPTION_OFF,
    ESP_PD_OPTION_ON,
    ESP_PD_OPTION_AUTO,
} esp_sleep_pd_option_t;

/* ------------------------------------------------------------------ */
/* Wakeup-source arming — all RECORD into g_vsim_sleep, never sleep.   */
/* ------------------------------------------------------------------ */
esp_err_t esp_sleep_enable_ext0_wakeup(VSIM_SLEEP_GPIO_T gpio_num, int level);
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t io_mask,
                                          esp_sleep_ext1_wakeup_mode_t mode);
/* IDF-deprecated name kept for source compatibility. */
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t io_mask,
                                       esp_sleep_ext1_wakeup_mode_t mode)
    __attribute__((deprecated));
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
esp_err_t esp_sleep_enable_touchpad_wakeup(void);
esp_err_t esp_sleep_enable_ulp_wakeup(void);
esp_err_t esp_sleep_enable_gpio_wakeup(void);
esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t gpio_pin_mask,
                                            esp_deep_sleep_gpio_wake_up_mode_t mode);
esp_err_t esp_sleep_enable_uart_wakeup(int uart_num);
esp_err_t esp_sleep_enable_wifi_wakeup(void);
esp_err_t esp_sleep_enable_bt_wakeup(void);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source);

/* True for RTC-capable pins: S3 GPIO0..21. */
bool esp_sleep_is_valid_wakeup_gpio(VSIM_SLEEP_GPIO_T gpio_num);

/* ------------------------------------------------------------------ */
/* Sleep entry                                                         */
/* ------------------------------------------------------------------ */
/* Light sleep is modelled as a no-op that just records; returns ESP_OK. */
esp_err_t esp_light_sleep_start(void);

/* Deep sleep: record -> park on condvar -> wake -> reboot-path/exit.
 * Never returns to the firmware caller (noreturn, like IDF).          */
void esp_deep_sleep_start(void) __attribute__((noreturn));

/* ------------------------------------------------------------------ */
/* Post-wake queries                                                   */
/* ------------------------------------------------------------------ */
esp_sleep_source_t esp_sleep_get_wakeup_cause(void);
uint64_t esp_sleep_get_ext1_wakeup_status(void);   /* mask of pin(s) that woke */
uint64_t esp_sleep_get_gpio_wakeup_status(void);
esp_err_t esp_sleep_pd_config(esp_sleep_pd_domain_t domain,
                              esp_sleep_pd_option_t option);

/* ------------------------------------------------------------------ */
/* Host-sim control surface (SIM_HOST_BUILD only — never firmware code) */
/* ------------------------------------------------------------------ */
#ifdef SIM_HOST_BUILD
#include <setjmp.h>

/* Reboot checkpoint: the thread that runs fw_app_main (typically the one
 * vsim_boot spawns) can arm this so a deep-sleep wake longjmps back:
 *
 *     if (setjmp(g_vsim_sleep_reboot_jb) == 0)
 *         g_vsim_sleep_reboot_armed = 1;
 *     fw_app_main();                  // first boot AND post-wake reboot
 *
 * setjmp/longjmp must be same-thread; the jmp target lives with the
 * frame that called setjmp, so arm it on the firmware thread itself.   */
extern jmp_buf      g_vsim_sleep_reboot_jb;
extern volatile int g_vsim_sleep_reboot_armed;

/* Alternative reboot hook — when set it is called instead of the
 * longjmp/exit fallback chain (e.g. re-run fw_app_main).  If it returns,
 * the shim _exit(0)s.                                               */
typedef void (*vsim_reboot_fn_t)(void);
void vsim_sleep_set_reboot_fn(vsim_reboot_fn_t fn);

/* Wake triggers — callable from any thread (scenario / vboard_irq).  */
void vsim_trigger_wakeup(int gpio);          /* RTC gpio that woke    */
void vsim_trigger_wakeup_timer(void);        /* timer-source wake     */
void vsim_trigger_wakeup_force(esp_sleep_source_t cause, uint64_t gpio_mask);

/* Scenario introspection. */
bool     vsim_sleep_parked(void);            /* fw blocked in deep_sleep_start */
int      vsim_sleep_woke_from_deep(void);    /* sticky: for esp_reset_reason() */
uint64_t vsim_sleep_timer_us(void);          /* armed timer, 0 = none          */
void     vsim_sleep_state_clear(void);       /* zero g_vsim_sleep + causes     */
#endif /* SIM_HOST_BUILD */

#ifdef __cplusplus
}
#endif

#endif /* ESP_SLEEP_H */
