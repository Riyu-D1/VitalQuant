/* Host-sim esp_system.h — reset reason, restart, heap stats, chip info.
 *
 * esp_restart() does NOT loop the process: it prints a marker line and
 * exits with code 42 so scenario harnesses can distinguish a clean
 * firmware-requested reboot from a crash (non-zero, non-42 exits).
 */
#ifndef ESP_SYSTEM_H
#define ESP_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_chip_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_RST_UNKNOWN    = 0,  /* reset reason can not be determined          */
    ESP_RST_POWERON    = 1,  /* reset due to power-on event                 */
    ESP_RST_EXT        = 2,  /* reset by external pin (not applicable for ESP32) */
    ESP_RST_SW         = 3,  /* software reset via esp_restart              */
    ESP_RST_PANIC      = 4,  /* software reset due to exception/panic       */
    ESP_RST_INT_WDT    = 5,  /* interrupt watchdog reset                    */
    ESP_RST_TASK_WDT   = 6,  /* task watchdog reset                         */
    ESP_RST_WDT        = 7,  /* other watchdogs reset                       */
    ESP_RST_DEEPSLEEP  = 8,  /* reset after exiting deep sleep mode         */
    ESP_RST_BROWNOUT   = 9,  /* brownout reset (software or hardware)       */
    ESP_RST_SDIO       = 10, /* reset over SDIO                             */
    ESP_RST_USB        = 11, /* reset by USB peripheral                     */
    ESP_RST_JTAG       = 12, /* reset by JTAG                               */
    ESP_RST_EFUSE      = 13, /* reset due to efuse error                    */
    ESP_RST_PWR_GLITCH = 14, /* reset due to power glitch                   */
    ESP_RST_CPU_LOCKUP = 15, /* reset due to CPU lock up                    */
} esp_reset_reason_t;

/**
 * "Restart" the simulated chip: prints a marker to stderr and exits the
 * process with code 42.  Scenarios detect this as a clean reboot.
 */
void esp_restart(void) __attribute__((noreturn));

/** Reason of the last reset — power-on unless the deep-sleep shim or a
 *  scenario set another reason via esp_shim_set_reset_reason(). */
esp_reset_reason_t esp_reset_reason(void);

/** Free heap across all sim pools, in bytes (internal + SPIRAM). */
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_free_internal_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);

/** Fill an esp_chip_info_t describing the simulated ESP32-S3. */
void esp_chip_info(esp_chip_info_t *out_chip_info);

/** Abort the program with an error message (real IDF entry point). */
void esp_system_abort(const char *details) __attribute__((noreturn));

/* ---- Sim-only extension (not real IDF): let the deep-sleep path or a
 * scenario record the wake/reset cause before the next boot. -------- */
void esp_shim_set_reset_reason(esp_reset_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* ESP_SYSTEM_H */
