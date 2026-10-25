/* vboard — virtual fix_v22 board for host-side firmware testing.
 *
 * This is the CONTRACT between the device models (sim/vboard/devices),
 * the ESP-IDF shim bus layer (sim/esp_shim), and the scenario tests
 * (sim/tests).  Everything speaks the firmware's real transaction
 * level: I2C write/read frames, full-duplex SPI byte streams, GPIO
 * levels and interrupt edges, power-rail state, a virtual clock.
 *
 * Nothing here pretends to be silicon-accurate; models are
 * register-plausible and protocol-faithful enough to exercise the real
 * driver code paths.  Datasheet-uncertain encodings carry the same
 * TODO(datasheet) caveat as the firmware.
 */
#ifndef VBOARD_H
#define VBOARD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- *
 *  Virtual clock — the ONLY time source.  esp_timer, FreeRTOS ticks,
 *  vTaskDelay and device-model async behaviour all derive from it.
 *  Default scale = 1.0 (wall clock).  Scenarios may set
 *  VSIM_TIME_SCALE (env) or call vboard_clock_set_scale() — e.g. 200x
 *  compresses a 24 h soak into ~7 min of wall time.
 * --------------------------------------------------------------- */
int64_t  vboard_now_us(void);            /* virtual µs since sim start */
void     vboard_clock_set_scale(double x);
void     vboard_clock_sleep_us(int64_t virt_us);  /* sleep scaled */
/* Periodic tick driving async device behaviour (DRDY, FIFO fill…). */
void     vboard_tick_start(int64_t period_us);    /* spawn tick thread */
void     vboard_tick_stop(void);

/* --------------------------------------------------------------- *
 *  GPIO — every pin the firmware may touch.  Models assert lines by
 *  calling vboard_gpio_set(); the shim's gpio_get_level reads them.
 *  Firmware "outputs" arrive via vboard_gpio_from_firmware() (called
 *  by the gpio shim) so coupled logic (e.g. CS gating the MAX86141
 *  level shifter) can react.
 * --------------------------------------------------------------- */
void     vboard_gpio_from_firmware(int gpio, int level);   /* ESP -> board */
int      vboard_gpio_get(int gpio);                        /* board/ESP read */
void     vboard_gpio_set(int gpio, int level);             /* board -> ESP  */
void     vboard_gpio_pull(int gpio, int level);            /* internal pull */
/* Deliver an interrupt edge to the firmware (runs registered ISRs
 * on the tick thread — real concurrent FromISR semantics).        */
void     vboard_irq_pulse(int gpio, int level /*0/1; -1 = both edges*/);
typedef void (*vboard_gpio_out_cb_t)(int gpio, int level, void *ctx);
void     vboard_on_gpio_out(int gpio, vboard_gpio_out_cb_t cb, void *ctx);

/* --------------------------------------------------------------- *
 *  Power rails (§4.1).  Device models may query them (e.g. a sensor
 *  answers only when its rail is up); scenarios drive VBUS/VBAT and
 *  watch TX5.
 * --------------------------------------------------------------- */
typedef enum {
    VB_RAIL_VBUS = 0,   /* USB in — scenario-controlled              */
    VB_RAIL_VBAT,       /* cell voltage drives VBAT_SYS              */
    VB_RAIL_3V3,        /* TPS63802 — always on while VBAT_SYS ok    */
    VB_RAIL_3V3_ANA,    /* tied via R95                              */
    VB_RAIL_1V8,        /* TPS7A2018                                 */
    VB_RAIL_TX5,        /* TPS61240 — follows TCA6408 P3             */
    VB_RAIL__N
} vboard_rail_t;
void     vboard_rail_set(vboard_rail_t r, bool on);
bool     vboard_rail(vboard_rail_t r);
void     vboard_vbat_set_mv(int mv);       /* drives gauge model + cutoff */
int      vboard_vbat_mv(void);
typedef void (*vboard_rail_cb_t)(vboard_rail_t r, bool on, void *ctx);
void     vboard_on_rail(vboard_rail_t r, vboard_rail_cb_t cb, void *ctx);

/* --------------------------------------------------------------- *
 *  Device model interface.  A model registers itself with the bus it
 *  sits on.  The shim layer NEVER calls models directly — it goes
 *  through vboard_i2c_* / vboard_spi_* so bus-level faults (nack
 *  storms, stuck SDA, MISO fights) apply to every device uniformly.
 * --------------------------------------------------------------- */
/* esp_err_t is defined in the shim; forward-declare here so device
 * models can include just vboard.h.                                */
#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#define VB_ESP_OK            0
#define VB_ESP_ERR_TIMEOUT  0x107
#define VB_ESP_FAIL         (-1)
#endif

typedef struct vboard_dev vboard_dev_t;
struct vboard_dev {
    const char *name;
    /* I2C (7-bit addr; 0 = not on I2C) */
    uint8_t    i2c_addr;
    esp_err_t (*i2c_write)(vboard_dev_t *d, const uint8_t *data, size_t len);
    esp_err_t (*i2c_read )(vboard_dev_t *d, uint8_t *data, size_t len);
    /* SPI (CS = gpio number; -1 = not on SPI). xfer is full-duplex.  */
    int        spi_cs;
    esp_err_t (*spi_xfer)(vboard_dev_t *d, const uint8_t *tx,
                          uint8_t *rx, size_t len);
    /* Async behaviour: DRDY toggling, conversion engines, FIFOs.
     * Called by the tick thread every vboard_tick period.          */
    void     (*tick)(vboard_dev_t *d, int64_t now_us);
    /* Warm/hard reset — models must fall back to power-on state.   */
    void     (*reset)(vboard_dev_t *d);
    void      *ctx;
};
void         vboard_register(vboard_dev_t *dev);
vboard_dev_t*vboard_find_i2c(uint8_t addr);
vboard_dev_t*vboard_find_spi(int cs_gpio);
vboard_dev_t*vboard_find_name(const char *name); /* e.g. "tca6408"    */
void         vboard_reset_all(void);       /* ESP_EN pulse → every reset()*/

/* --------------------------------------------------------------- *
 *  Fault injection — "try to break it" knobs.  All default benign.
 * --------------------------------------------------------------- */
typedef struct {
    /* I2C */
    float    i2c_nack_rate;      /* P(random NACK on any txn)         */
    uint8_t  i2c_silence_addr;   /* this addr NACKs always (0=off)    */
    bool     i2c_sda_stuck_low;  /* bus wedged                        */
    /* SPI */
    float    spi_bitflip_rate;   /* P(flip one bit in RX stream)      */
    int      spi_stuck_cs;       /* this CS answers even when idle…   */
    bool     miso_fight;         /* second device drives MISO too     */
    uint8_t  spi_corrupt_id[8];  /* per-CS forced ID byte (0 = off)   */
    /* System */
    int      heap_fail_after;    /* N heap allocs then fail (-1 off)  */
    bool     wifi_link_down;
    bool     cloud_drop;         /* http client sees conn refused/rst */
    int      clock_jitter_ppm;   /* drift applied to esp_timer        */
} vboard_faults_t;
extern vboard_faults_t g_vb_faults;

/* Scenario helpers (test-side). */
typedef struct {
    uint64_t i2c_txns, i2c_nacks, spi_txns, spi_cs_violations;
    uint64_t miso_bytes, miso_fights, irq_pulses;
    int64_t  tx5_on_us_total, led_overlap_violations;
} vboard_stats_t;
extern vboard_stats_t g_vb_stats;
void     vboard_stats_reset(void);

/* Init/teardown for a scenario process.  board_init wires the default
 * device population + rail couplings; tests may re-register models.  */
void     vboard_init(void);
void     vboard_shutdown(void);

/* Convenience for device models: read current expander pin state via
 * the registered TCA6408 model (returns -1 if absent).              */
int      vboard_exp_pin(int p);            /* 0/1, -1 unknown        */
void     vboard_exp_set_input(int p, int level); /* board -> P5/P6   */

#endif /* VBOARD_H */
