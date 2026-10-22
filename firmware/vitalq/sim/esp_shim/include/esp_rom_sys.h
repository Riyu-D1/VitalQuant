/* esp_rom_sys.h — host-sim shim for ESP32-S3 ROM system routines.
 *
 * Only what the firmware + other shims need:
 *   esp_rom_delay_us() -> vboard_clock_sleep_us()  (virtual clock!)
 *   esp_rom_printf()   -> stdio (sim console = process stdout)
 *   cpu-ticks helpers  -> fixed 240 MHz model
 */
#pragma once

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pauses execution for `us` *virtual* microseconds — implemented on top
 * of vboard_clock_sleep_us() so VSIM_TIME_SCALE applies uniformly.     */
void esp_rom_delay_us(uint32_t us);

/* ROM printf — the on-target UART console maps to sim stdout. */
int  esp_rom_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  esp_rom_vprintf(const char *fmt, va_list ap);

/* Channel plumbing kept record-only: the sim console is stdio.       */
void esp_rom_install_channel_putc(int channel, void (*putc)(char c));
void esp_rom_output_to_channels(char c);
void esp_rom_install_uart_printf(void);

/* Fixed 240 MHz CPU model (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240).     */
uint32_t esp_rom_get_cpu_ticks_per_us(void);
void     esp_rom_set_cpu_ticks_per_us(uint32_t ticks_per_us);

/* Software reset — in sim this is a fatal diagnostic (firmware is
 * expected to go through esp_restart()).                             */
void esp_rom_software_reset_system(void);
void esp_rom_software_reset_cpu(int cpu_no);

#ifdef __cplusplus
}
#endif
