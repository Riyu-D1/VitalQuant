/* Host-sim esp_psram.h — the fix_v22 module carries 8 MB of octal PSRAM
 * (CONFIG_SPIRAM_SIZE in the sim sdkconfig).  The SPIRAM pool itself is
 * enforced by esp_heap_caps; these are just the info queries. */
#ifndef ESP_PSRAM_H
#define ESP_PSRAM_H

#include <stddef.h>
#include <stdbool.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the attached PSRAM in bytes — 8 MiB on ESP32-S3-N16R8. */
size_t esp_psram_get_size(void);

/** Always true in the sim — PSRAM is "initialized" before app boot. */
bool esp_psram_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_PSRAM_H */
