/* driver/gpio.h — host-sim shim of the ESP-IDF GPIO driver (ESP32-S3).
 *
 * Backed by sim/vboard: firmware "output" levels are pushed through
 * vboard_gpio_from_firmware(), inputs read vboard_gpio_get(), and
 * interrupt edges are injected by the vboard tick thread via
 * gpio_sim_irq() — so FromISR paths really do run on a different
 * thread, just like on target.
 *
 * SAFETY TRIPWIRE: fix_v22 forbids driving the SPI-flash/PSRAM pins,
 * the native-USB pins and the non-RTC strap pins.  Any config/output
 * call on a forbidden pin prints "VB-GPIO-FORBIDDEN" on stderr and
 * bumps the counter returned by gpio_forbidden_count() (SIM_HOST_BUILD
 * only).  Strap pins (IO0/IO3/IO45/IO46) may be *configured as inputs*
 * freely — output-side use is a violation like any other.
 */
#ifndef SIM_DRIVER_GPIO_H
#define SIM_DRIVER_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* esp_err_t normally comes from the shim's esp_err.h; fall back to the
 * same self-declared pattern vboard.h uses when compiling standalone. */
#if defined(__has_include)
#if __has_include("esp_err.h")
#include "esp_err.h"
#endif
#endif
#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#endif
#ifndef ESP_OK
#define ESP_OK                 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL              (-1)
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG    0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE  0x103
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND      0x105
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED  0x106
#endif

/* Interrupt-allocation flags (canonical esp_intr_alloc.h values).
 * Defined here, guarded, because driver/gpio.h exposes them on target
 * and firmware takes them from this header alone.                    */
#ifndef ESP_INTR_FLAG_LEVEL1
#define ESP_INTR_FLAG_LEVEL1        (1<<1)
#endif
#ifndef ESP_INTR_FLAG_LEVEL2
#define ESP_INTR_FLAG_LEVEL2        (1<<2)
#endif
#ifndef ESP_INTR_FLAG_LEVEL3
#define ESP_INTR_FLAG_LEVEL3        (1<<3)
#endif
#ifndef ESP_INTR_FLAG_LEVEL4
#define ESP_INTR_FLAG_LEVEL4        (1<<4)
#endif
#ifndef ESP_INTR_FLAG_LEVEL5
#define ESP_INTR_FLAG_LEVEL5        (1<<5)
#endif
#ifndef ESP_INTR_FLAG_LEVEL6
#define ESP_INTR_FLAG_LEVEL6        (1<<6)
#endif
#ifndef ESP_INTR_FLAG_NMI
#define ESP_INTR_FLAG_NMI           (1<<7)
#endif
#ifndef ESP_INTR_FLAG_SHARED
#define ESP_INTR_FLAG_SHARED        (1<<8)
#endif
#ifndef ESP_INTR_FLAG_EDGE
#define ESP_INTR_FLAG_EDGE          (1<<9)
#endif
#ifndef ESP_INTR_FLAG_IRAM
#define ESP_INTR_FLAG_IRAM          (1<<10)
#endif
#ifndef ESP_INTR_FLAG_INTRDISABLED
#define ESP_INTR_FLAG_INTRDISABLED  (1<<11)
#endif

/* --------------------------------------------------------------- *
 *  Pin numbering — ESP32-S3 exposes GPIO0..21 and GPIO26..48.
 * --------------------------------------------------------------- */
typedef enum {
    GPIO_NUM_NC = -1,   /*!< not connected / sentinel */
    GPIO_NUM_0 = 0,   GPIO_NUM_1,   GPIO_NUM_2,   GPIO_NUM_3,
    GPIO_NUM_4,       GPIO_NUM_5,   GPIO_NUM_6,   GPIO_NUM_7,
    GPIO_NUM_8,       GPIO_NUM_9,   GPIO_NUM_10,  GPIO_NUM_11,
    GPIO_NUM_12,      GPIO_NUM_13,  GPIO_NUM_14,  GPIO_NUM_15,
    GPIO_NUM_16,      GPIO_NUM_17,  GPIO_NUM_18,  GPIO_NUM_19,
    GPIO_NUM_20,      GPIO_NUM_21,  GPIO_NUM_22,  GPIO_NUM_23,
    GPIO_NUM_24,      GPIO_NUM_25,  GPIO_NUM_26,  GPIO_NUM_27,
    GPIO_NUM_28,      GPIO_NUM_29,  GPIO_NUM_30,  GPIO_NUM_31,
    GPIO_NUM_32,      GPIO_NUM_33,  GPIO_NUM_34,  GPIO_NUM_35,
    GPIO_NUM_36,      GPIO_NUM_37,  GPIO_NUM_38,  GPIO_NUM_39,
    GPIO_NUM_40,      GPIO_NUM_41,  GPIO_NUM_42,  GPIO_NUM_43,
    GPIO_NUM_44,      GPIO_NUM_45,  GPIO_NUM_46,  GPIO_NUM_47,
    GPIO_NUM_48,
    GPIO_NUM_MAX
} gpio_num_t;

#define GPIO_PIN_COUNT 49

/* Validity macros (S3: pads 0-21 and 26-48 exist; 22-25 are not
 * bonded out).  RTC-capable pads are GPIO0-21.                       */
#define GPIO_IS_VALID_GPIO(gpio_num) \
    (((int)(gpio_num) >= 0) && \
     (((int)(gpio_num) < 22) || \
      ((int)(gpio_num) >= 26 && (int)(gpio_num) <= 48)))
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio_num) \
    (GPIO_IS_VALID_GPIO(gpio_num))          /* all S3 pads can output */
#define GPIO_IS_VALID_RTC_GPIO(gpio_num) \
    (((int)(gpio_num) >= 0) && ((int)(gpio_num) <= 21))

/* Per-pin bit-mask convenience (same names/values as IDF). */
#define GPIO_SEL_0   (1ULL<<0)
#define GPIO_SEL_1   (1ULL<<1)
#define GPIO_SEL_2   (1ULL<<2)
#define GPIO_SEL_3   (1ULL<<3)
#define GPIO_SEL_4   (1ULL<<4)
#define GPIO_SEL_5   (1ULL<<5)
#define GPIO_SEL_6   (1ULL<<6)
#define GPIO_SEL_7   (1ULL<<7)
#define GPIO_SEL_8   (1ULL<<8)
#define GPIO_SEL_9   (1ULL<<9)
#define GPIO_SEL_10  (1ULL<<10)
#define GPIO_SEL_11  (1ULL<<11)
#define GPIO_SEL_12  (1ULL<<12)
#define GPIO_SEL_13  (1ULL<<13)
#define GPIO_SEL_14  (1ULL<<14)
#define GPIO_SEL_15  (1ULL<<15)
#define GPIO_SEL_16  (1ULL<<16)
#define GPIO_SEL_17  (1ULL<<17)
#define GPIO_SEL_18  (1ULL<<18)
#define GPIO_SEL_19  (1ULL<<19)
#define GPIO_SEL_20  (1ULL<<20)
#define GPIO_SEL_21  (1ULL<<21)
#define GPIO_SEL_22  (1ULL<<22)
#define GPIO_SEL_23  (1ULL<<23)
#define GPIO_SEL_24  (1ULL<<24)
#define GPIO_SEL_25  (1ULL<<25)
#define GPIO_SEL_26  (1ULL<<26)
#define GPIO_SEL_27  (1ULL<<27)
#define GPIO_SEL_28  (1ULL<<28)
#define GPIO_SEL_29  (1ULL<<29)
#define GPIO_SEL_30  (1ULL<<30)
#define GPIO_SEL_31  (1ULL<<31)
#define GPIO_SEL_32  (1ULL<<32)
#define GPIO_SEL_33  (1ULL<<33)
#define GPIO_SEL_34  (1ULL<<34)
#define GPIO_SEL_35  (1ULL<<35)
#define GPIO_SEL_36  (1ULL<<36)
#define GPIO_SEL_37  (1ULL<<37)
#define GPIO_SEL_38  (1ULL<<38)
#define GPIO_SEL_39  (1ULL<<39)
#define GPIO_SEL_40  (1ULL<<40)
#define GPIO_SEL_41  (1ULL<<41)
#define GPIO_SEL_42  (1ULL<<42)
#define GPIO_SEL_43  (1ULL<<43)
#define GPIO_SEL_44  (1ULL<<44)
#define GPIO_SEL_45  (1ULL<<45)
#define GPIO_SEL_46  (1ULL<<46)
#define GPIO_SEL_47  (1ULL<<47)
#define GPIO_SEL_48  (1ULL<<48)
#define GPIO_PIN_MASK 0x1FFFFFFFFFFFFULL  /* (1ULL<<49)-1 */

/* --------------------------------------------------------------- *
 *  Mode / pull / interrupt enums — canonical IDF bit patterns.
 * --------------------------------------------------------------- */
#define GPIO_MODE_DEF_DISABLE (0)
#define GPIO_MODE_DEF_INPUT   (1<<0)
#define GPIO_MODE_DEF_OUTPUT  (1<<1)
#define GPIO_MODE_DEF_OD      (1<<2)

typedef enum {
    GPIO_MODE_DISABLE         = GPIO_MODE_DEF_DISABLE,                                          /*!< floating */
    GPIO_MODE_INPUT           = GPIO_MODE_DEF_INPUT,                                            /*!< input only */
    GPIO_MODE_OUTPUT          = GPIO_MODE_DEF_OUTPUT,                                           /*!< push-pull output */
    GPIO_MODE_OUTPUT_OD       = GPIO_MODE_DEF_OUTPUT | GPIO_MODE_DEF_OD,                        /*!< open-drain output */
    GPIO_MODE_INPUT_OUTPUT_OD = GPIO_MODE_DEF_INPUT  | GPIO_MODE_DEF_OUTPUT | GPIO_MODE_DEF_OD, /*!< open-drain I/O */
    GPIO_MODE_INPUT_OUTPUT    = GPIO_MODE_DEF_INPUT  | GPIO_MODE_DEF_OUTPUT,                    /*!< bidirectional */
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_DISABLE = 0x0,
    GPIO_PULLUP_ENABLE  = 0x1,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0x0,
    GPIO_PULLDOWN_ENABLE  = 0x1,
} gpio_pulldown_t;

typedef enum {
    GPIO_PULLUP_ONLY,        /*!< pull-up only               */
    GPIO_PULLDOWN_ONLY,      /*!< pull-down only             */
    GPIO_PULLUP_PULLDOWN,    /*!< both (bus-keeper style)    */
    GPIO_FLOATING,           /*!< neither                    */
} gpio_pull_mode_t;

typedef enum {
    GPIO_INTR_DISABLE    = 0,
    GPIO_INTR_POSEDGE    = 1,
    GPIO_INTR_NEGEDGE    = 2,
    GPIO_INTR_ANYEDGE    = 3,
    GPIO_INTR_LOW_LEVEL  = 4,
    GPIO_INTR_HIGH_LEVEL = 5,
    GPIO_INTR_MAX,
} gpio_int_type_t;

typedef enum {
    GPIO_DRIVE_CAP_0       = 0,
    GPIO_DRIVE_CAP_1       = 1,
    GPIO_DRIVE_CAP_2       = 2,
    GPIO_DRIVE_CAP_3       = 3,
    GPIO_DRIVE_CAP_DEFAULT = 3,
    GPIO_DRIVE_CAP_MAX,
} gpio_drive_cap_t;

typedef void (*gpio_isr_t)(void *);
typedef void *gpio_isr_handle_t;

typedef struct {
    uint64_t         pin_bit_mask;  /*!< bit n = GPIO n               */
    gpio_mode_t      mode;          /*!< GPIO_MODE_*                  */
    gpio_pullup_t    pull_up_en;    /*!< GPIO_PULLUP_*                */
    gpio_pulldown_t  pull_down_en;  /*!< GPIO_PULLDOWN_*              */
    gpio_int_type_t  intr_type;     /*!< GPIO_INTR_*                  */
} gpio_config_t;

/* --------------------------------------------------------------- *
 *  Configuration
 * --------------------------------------------------------------- */
esp_err_t gpio_config(const gpio_config_t *pGPIOConfig);
esp_err_t gpio_reset_pin(gpio_num_t gpio_num);
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull);
esp_err_t gpio_pullup_en(gpio_num_t gpio_num);
esp_err_t gpio_pullup_dis(gpio_num_t gpio_num);
esp_err_t gpio_pulldown_en(gpio_num_t gpio_num);
esp_err_t gpio_pulldown_dis(gpio_num_t gpio_num);
esp_err_t gpio_set_drive_capability(gpio_num_t gpio_num, gpio_drive_cap_t strength);
esp_err_t gpio_get_drive_capability(gpio_num_t gpio_num, gpio_drive_cap_t *strength);
esp_err_t gpio_iomux_in(uint32_t gpio_num, uint32_t signal_idx);
esp_err_t gpio_iomux_out(uint8_t gpio_num, int func, bool oen_inv);

/* --------------------------------------------------------------- *
 *  Levels — set_level reports to vboard (device models may gate on
 *  it, e.g. CS gating a level shifter); get_level asks the board.
 * --------------------------------------------------------------- */
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
int       gpio_get_level(gpio_num_t gpio_num);

/* --------------------------------------------------------------- *
 *  Interrupts.  Edge delivery: the vboard tick thread calls
 *  gpio_sim_irq() which runs the registered handler on THAT thread —
 *  xQueueSendFromISR & friends get a genuinely concurrent caller.
 * --------------------------------------------------------------- */
esp_err_t gpio_set_intr_type(gpio_num_t gpio_num, gpio_int_type_t intr_type);
esp_err_t gpio_intr_enable(gpio_num_t gpio_num);
esp_err_t gpio_intr_disable(gpio_num_t gpio_num);
esp_err_t gpio_install_isr_service(int intr_alloc_flags);
void      gpio_uninstall_isr_service(void);
esp_err_t gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args);
esp_err_t gpio_isr_handler_remove(gpio_num_t gpio_num);
esp_err_t gpio_isr_register(void (*fn)(void *), void *arg,
                            int intr_alloc_flags, gpio_isr_handle_t *handle);

/* --------------------------------------------------------------- *
 *  Sleep / hold — recorded for the sleep shim to consume.
 * --------------------------------------------------------------- */
esp_err_t gpio_wakeup_enable(gpio_num_t gpio_num, gpio_int_type_t intr_type);
esp_err_t gpio_wakeup_disable(gpio_num_t gpio_num);
esp_err_t gpio_hold_en(gpio_num_t gpio_num);
esp_err_t gpio_hold_dis(gpio_num_t gpio_num);
void      gpio_deep_sleep_hold_en(void);
void      gpio_deep_sleep_hold_dis(void);
esp_err_t gpio_sleep_sel_en(gpio_num_t gpio_num);
esp_err_t gpio_sleep_sel_dis(gpio_num_t gpio_num);
esp_err_t gpio_sleep_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_sleep_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull);
esp_err_t gpio_sleep_hold_en(gpio_num_t gpio_num);
esp_err_t gpio_sleep_hold_dis(gpio_num_t gpio_num);
esp_err_t gpio_force_hold_all(void);
esp_err_t gpio_force_unhold_all(void);

/* --------------------------------------------------------------- *
 *  Sim-only surface (vboard core, sleep shim, scenario tests).
 * --------------------------------------------------------------- */
#ifdef SIM_HOST_BUILD
/* Edge injector — called BY the vboard tick thread (the C file lives
 * in sim/esp_shim/gpio/gpio_sim.c).  level: 0 = falling edge,
 * 1 = rising edge, -1 = both edges (delivered NEG then POS).        */
void      gpio_sim_irq(int gpio, int level);

/* Tripwire: number of forbidden/strap-output config calls so far.   */
uint64_t  gpio_forbidden_count(void);

/* Hold-state introspection for the sleep shim (vsim_sleep_state_t
 * holds[] is filled from these).                                     */
bool      gpio_sim_is_held(int gpio);
int       gpio_sim_hold_pins(int *gpios_out, int max);   /* → count   */
bool      gpio_sim_deep_sleep_hold(void);
int       gpio_sim_wakeup_type(int gpio);   /* gpio_int_type_t or -1   */

/* Misc recorded-state introspection for tests/debugging.            */
int       gpio_sim_mode(int gpio);          /* gpio_mode_t or -1       */
int       gpio_sim_pull(int gpio);          /* bit0=up bit1=down, -1   */
int       gpio_sim_iomux_in(int gpio);      /* in-signal or -1         */
int       gpio_sim_iomux_out(int gpio);     /* out-func or -1          */
void      gpio_sim_reset(void);             /* scenario teardown       */
#endif /* SIM_HOST_BUILD */

#ifdef __cplusplus
}
#endif

#endif /* SIM_DRIVER_GPIO_H */
