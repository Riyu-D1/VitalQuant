/* esp_pm.h — host-sim shim of ESP-IDF esp_pm.h (ESP32-S3 / fix_v22).
 *
 * Power management has no host-silicon meaning here; this is a faithful
 * API surface that RECORDS configuration and honours lock refcounts so
 * firmware code paths (configure, acquire/release around busy sections)
 * compile and run.  A scenario can inspect via esp_pm_dump_locks().
 */
#ifndef ESP_PM_H
#define ESP_PM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

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
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG 0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 0x101
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Lock types — enum order matches IDF esp_pm.h. */
typedef enum {
    ESP_PM_CPU_FREQ_MAX = 0,   /* CPU at max_freq_mhz while held        */
    ESP_PM_APB_FREQ_MAX,       /* APB at max while held                 */
    ESP_PM_NO_LIGHT_SLEEP,     /* forbid automatic light sleep           */
} esp_pm_lock_type_t;

#define ESP_PM_LOCK_TYPE_MAX  ESP_PM_NO_LIGHT_SLEEP

/* PM configuration — target-generic shape; matches esp_pm_config_esp32s3_t. */
typedef struct {
    int  max_freq_mhz;
    int  min_freq_mhz;
    bool light_sleep_enable;
} esp_pm_config_esp32s3_t;
typedef esp_pm_config_esp32s3_t esp_pm_config_t;

/* Opaque lock handle. */
typedef struct esp_pm_lock *esp_pm_lock_handle_t;

/* Record a PM configuration (no clocks to drive on host). */
esp_err_t esp_pm_configure(const void *config);
esp_err_t esp_pm_get_configuration(void *config);

/* Lock lifecycle — refcounts are real (a held lock is visible to
 * esp_pm_dump_locks) even though they gate nothing on host.           */
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t lock_type, int arg,
                             const char *name, esp_pm_lock_handle_t *out_handle);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_dump_locks(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* ESP_PM_H */
