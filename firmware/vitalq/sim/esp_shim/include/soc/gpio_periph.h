/* soc/gpio_periph.h — host-sim GPIO peripheral tables (ESP32-S3).
 *
 * GPIO_PIN_MUX_REG[] maps pad number -> simulated IO_MUX register
 * address (DR_REG_IO_MUX_BASE + 4*n); reads/writes flow through the
 * shim register file in rom/rom.c so REG_READ(REG_WRITE()) round-trips
 * work exactly like hardware save/restore of the pad-mux word.
 */
#pragma once

#include <stdint.h>
#include "soc/soc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ESP32-S3 has pads GPIO0..GPIO48 (22-25 absent). */
#ifndef SOC_GPIO_PIN_COUNT
#define SOC_GPIO_PIN_COUNT      (49)
#endif
#ifndef SOC_GPIO_VALID_GPIO_MASK
#define SOC_GPIO_VALID_GPIO_MASK \
    (0x1FFFFFFFFFFFFULL & ~(0ULL | (1ULL<<22) | (1ULL<<23) | (1ULL<<24) | (1ULL<<25)))
#endif
#ifndef SOC_GPIO_VALID_OUTPUT_GPIO_MASK
#define SOC_GPIO_VALID_OUTPUT_GPIO_MASK (SOC_GPIO_VALID_GPIO_MASK)
#endif

/* IO_MUX register address for each pad — 0 for absent pads matches the
 * real table (reads return 0 via the reg file).                      */
extern const uint32_t GPIO_PIN_MUX_REG[SOC_GPIO_PIN_COUNT];

/* Pads with RTC/hold capability — sim keeps it zeroed. */
extern const uint32_t GPIO_HOLD_MASK[SOC_GPIO_PIN_COUNT];

#ifdef __cplusplus
}
#endif
