/* nvs_flash.h — host-sim NVS init/erase API (subset of ESP-IDF
 * nvs_flash.h).
 *
 * The sim has a single NVS store; partition labels are accepted for
 * API compatibility and ignored.  nvs_flash_init() loads the snapshot
 * log (sim/build/nvs.bin) into memory; nvs_flash_erase() drops the
 * store and deletes the file.
 */
#ifndef NVS_FLASH_H
#define NVS_FLASH_H

#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_init_partition(const char *partition_label);
esp_err_t nvs_flash_deinit(void);
esp_err_t nvs_flash_deinit_partition(const char *partition_label);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_flash_erase_partition(const char *part_name);

#ifdef __cplusplus
}
#endif

#endif /* NVS_FLASH_H */
