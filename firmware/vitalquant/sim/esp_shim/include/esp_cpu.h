/* Host-sim esp_cpu.h — CPU helpers.  The "cycle counter" is derived
 * from the virtual clock at the 240 MHz core frequency, so it wraps
 * every ~17.9 s exactly like a real 32-bit CCOUNT register does. */
#ifndef ESP_CPU_H
#define ESP_CPU_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cycle counter: vboard_now_us() * 240 (CPU runs at 240 MHz on the
 *  target config).  Returns a wrapping uint32 like the real register. */
uint32_t esp_cpu_get_cycle_count(void);

/** Always false in the sim — no JTAG debugger attached. */
bool esp_cpu_in_ocd_debug_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_CPU_H */
