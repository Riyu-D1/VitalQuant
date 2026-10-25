/* esp_rom_gpio.h — host-sim shim for ESP32-S3 ROM GPIO-matrix routines.
 *
 * Record-only: the connections are stored so other shim code (or a
 * scenario) can introspect the firmware's GPIO-matrix programming, but
 * no real signal routing exists on the host.  Board-level behaviour
 * still flows through the normal gpio shim / vboard GPIO paths.
 *
 * Signal indices come from soc/gpio_sig_map.h (e.g. SIG_GPIO_OUT_IDX);
 * a gpio_num of GPIO_MATRIX_CONST_{ZERO,ONE}_INPUT connects a constant.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* soc/gpio_pins.h values for ESP32-S3 (defined here so the header is
 * self-contained — the real ROM header pulls soc/gpio_pins.h).        */
#ifndef GPIO_MATRIX_CONST_ONE_INPUT
#define GPIO_MATRIX_CONST_ONE_INPUT   (0x38)
#endif
#ifndef GPIO_MATRIX_CONST_ZERO_INPUT
#define GPIO_MATRIX_CONST_ZERO_INPUT  (0x3C)
#endif

/* Configure IO pad as a plain GPIO (disconnects direct IO_MUX path so
 * the pin is driven through the GPIO matrix).                        */
void esp_rom_gpio_pad_select_gpio(uint32_t iopad_num);

/* Enable internal pull-up only.                                      */
void esp_rom_gpio_pad_pullup_only(uint32_t iopad_num);

/* Release pad hold.                                                  */
void esp_rom_gpio_pad_unhold(uint32_t gpio_num);

/* Set pad drive strength 0..3 (~5/10/20/40 mA).                      */
void esp_rom_gpio_pad_set_drv(uint32_t iopad_num, uint32_t drv);

/* Connect a GPIO (or a CONST_*_INPUT pseudo-pin) to a peripheral
 * input signal `signal_idx`; inv inverts the level seen by the signal. */
void esp_rom_gpio_connect_in_signal(uint32_t gpio_num, uint32_t signal_idx,
                                    bool inv);

/* Connect peripheral output `signal_idx` to a GPIO; SIG_GPIO_OUT_IDX
 * disconnects all peripherals so the GPIO driver owns the pad.        */
void esp_rom_gpio_connect_out_signal(uint32_t gpio_num, uint32_t signal_idx,
                                     bool out_inv, bool oen_inv);

/* ---- sim introspection (NOT part of IDF — test/shim helpers) ------ */
/* Last output-signal index routed to `gpio_num` (or -1).             */
int  esp_shim_gpio_out_signal(int gpio_num);
/* Last GPIO routed to peripheral input `signal_idx` (or -1).         */
int  esp_shim_gpio_in_signal_gpio(int signal_idx);
/* Whether pad_select_gpio() ran for this pad.                        */
int  esp_shim_gpio_pad_is_gpio(int gpio_num);

#ifdef __cplusplus
}
#endif
