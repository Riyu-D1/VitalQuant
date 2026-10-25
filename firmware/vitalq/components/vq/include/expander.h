/* expander.h — TCA6408 I2C GPIO expander @ 0x20.
 *
 * ~RESET is wired to ESP_EN: every ESP reset returns all 8 pins to INPUT,
 * and the board's external pulls then hold every power domain OFF — that
 * is the safe default. After boot, firmware must re-assert directions +
 * outputs explicitly (expander_init does this from a shadow register).
 *
 * P0 AFE4900_RESETZ (out, low=reset)   P4 IR_GATE   (out, high=IR on)
 * P1 ADS1292_PWDN   (out, low=pwdn)    P5 CHG_STAT  (in, low=charging)
 * P2 AD5940_RESET   (out, low=reset)   P6 VBUS_DET  (in, high=USB present)
 * P3 TX5_EN         (out, low=boost off) P7 CHG_DIS (out, high=inhibit)
 *
 * EXP_INT (IO3) is open-drain active-low; it's also a strap pin — input only.
 */
#pragma once
#include "vq_types.h"
#include "board_pins.h"

/* Init: read inputs, program config (P0-P4,P7 out / P5,P6 in), drive the
 * safe power-off pattern (all domain controls at default-low), arm INT. */
vq_err_t vq_exp_init(void);

/* Output control — only valid on P0-P4, P7; shadowed, read-modify-write safe. */
vq_err_t vq_exp_set(vq_exp_pin_t pin, bool high);
bool     vq_exp_get_out(vq_exp_pin_t pin);          /* shadow copy */
/* Inputs — reads the live input register. */
vq_err_t vq_exp_read_inputs(uint8_t *mask);
bool     vq_exp_in(vq_exp_pin_t pin);               /* single input bit   */

/* Convenience accessors */
bool     vq_exp_vbus_present(void);                 /* P6 */
bool     vq_exp_charging(void);                     /* P5 low = charging  */
vq_err_t vq_exp_charge_inhibit(bool inhibit);       /* P7 */

/* Register callback for EXP_INT falling edge (called from task ctx). */
typedef void (*vq_exp_int_cb_t)(void *ctx, uint8_t input_mask);
vq_err_t vq_exp_on_int(vq_exp_int_cb_t cb, void *ctx);

/* Self-test: walk each output pin and read back; returns bitmask of pins
 * that failed read-back. */
uint8_t  vq_exp_selftest_walk(void);
