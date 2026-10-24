/* esp_ota_ops.h — host-sim shim of the ESP-IDF OTA API.
 *
 * In the sim:
 *   esp_ota_get_running_partition()    -> ota_0 @0x20000 (4 MiB)
 *   esp_ota_get_next_update_partition()-> ota_1 (or the non-running slot)
 *   esp_ota_set_boot_partition()       -> recorded, ESP_OK
 *   esp_ota_get_boot_partition()       -> recorded choice or running
 *   esp_ota_mark_app_valid_cancel_rollback / invalid_rollback_and_reboot
 *                                      -> ESP_OK (no bootloader state)
 *
 * esp_partition_t comes from the shared esp_partition.h shim when it is
 * on the include path; the guarded fallback keeps this header usable on
 * its own — the fallback layout matches esp_partition.h exactly.
 */
#ifndef ESP_OTA_OPS_H
#define ESP_OTA_OPS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* esp_err_t — shared shim esp_err.h when present                      */
/* ------------------------------------------------------------------ */
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
#ifndef ESP_FAIL
#define ESP_FAIL (-1)
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM            0x101
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG       0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE     0x103
#endif
#ifndef ESP_ERR_INVALID_SIZE
#define ESP_ERR_INVALID_SIZE      0x104
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND         0x105
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED     0x106
#endif
#ifndef ESP_ERR_TIMEOUT
#define ESP_ERR_TIMEOUT           0x107
#endif
#ifndef ESP_ERR_INVALID_RESPONSE
#define ESP_ERR_INVALID_RESPONSE  0x108
#endif
#ifndef ESP_ERR_INVALID_CRC
#define ESP_ERR_INVALID_CRC       0x109
#endif
#ifndef ESP_ERR_INVALID_VERSION
#define ESP_ERR_INVALID_VERSION   0x10A
#endif

/* OTA-specific error codes (real IDF: components/app_update/include). */
#ifndef ESP_ERR_OTA_BASE
#define ESP_ERR_OTA_BASE                     0x1500
#endif
#ifndef ESP_ERR_OTA_PARTITION_CONFLICT
#define ESP_ERR_OTA_PARTITION_CONFLICT      (ESP_ERR_OTA_BASE + 0x00)
#endif
#ifndef ESP_ERR_OTA_SELECT_INFO_INVALID
#define ESP_ERR_OTA_SELECT_INFO_INVALID     (ESP_ERR_OTA_BASE + 0x01)
#endif
#ifndef ESP_ERR_OTA_VALIDATE_FAILED
#define ESP_ERR_OTA_VALIDATE_FAILED         (ESP_ERR_OTA_BASE + 0x02)
#endif
#ifndef ESP_ERR_OTA_SMALL_SEC_VER
#define ESP_ERR_OTA_SMALL_SEC_VER           (ESP_ERR_OTA_BASE + 0x03)
#endif
#ifndef ESP_ERR_OTA_ROLLBACK_FAILED
#define ESP_ERR_OTA_ROLLBACK_FAILED         (ESP_ERR_OTA_BASE + 0x04)
#endif
#ifndef ESP_ERR_OTA_NOTHING_FOUND
#define ESP_ERR_OTA_NOTHING_FOUND           (ESP_ERR_OTA_BASE + 0x05)
#endif
#ifndef ESP_ERR_OTA_ROLLBACK_INVALID_STATE
#define ESP_ERR_OTA_ROLLBACK_INVALID_STATE  (ESP_ERR_OTA_BASE + 0x06)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* esp_partition_t                                                    */
/* ------------------------------------------------------------------ */
#if defined(__has_include)
#  if __has_include("esp_partition.h")
#    include "esp_partition.h"
#    define ESP_OTA_OPS_HAVE_ESP_PARTITION_H 1
#  endif
#endif

#ifndef ESP_OTA_OPS_HAVE_ESP_PARTITION_H
/* Fallback minimal partition view — MUST match esp_partition.h. */
typedef enum {
    ESP_PARTITION_TYPE_APP  = 0x00,
    ESP_PARTITION_TYPE_DATA = 0x01,
} esp_partition_type_t;

typedef enum {
    ESP_PARTITION_SUBTYPE_APP_OTA_0   = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_1   = 0x11,
    ESP_PARTITION_SUBTYPE_APP_OTA_MIN = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_MAX = 0x1F,
    ESP_PARTITION_SUBTYPE_DATA_OTA    = 0x00,
    ESP_PARTITION_SUBTYPE_DATA_VQMODEL = 0x40,
    ESP_PARTITION_SUBTYPE_DATA_FAT    = 0x81,
    ESP_PARTITION_SUBTYPE_DATA_COREDUMP = 0x03,
    ESP_PARTITION_SUBTYPE_ANY         = 0xff,
} esp_partition_subtype_t;

typedef struct {
    esp_partition_type_t    type;
    esp_partition_subtype_t subtype;
    uint32_t address;             /* absolute offset in the 16 MiB image */
    uint32_t size;
    char     label[17];
    bool     encrypted;
} esp_partition_t;
#endif

#define esp_ota_get_app_ota_partition_count() 16

#define esp_ota_get_app_partition_subtype(i) \
    ((esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_MIN + ((i) & 0xf)))

/* ------------------------------------------------------------------ */
/* OTA API                                                            */
/* ------------------------------------------------------------------ */
typedef uint32_t esp_ota_handle_t;

/* OTA_DATA states for checking operability of the app — real values
 * from bootloader_support esp_flash_partitions.h.                    */
typedef enum {
    ESP_OTA_IMG_NEW             = 0x0U,         /* first boot monitor    */
    ESP_OTA_IMG_PENDING_VERIFY  = 0x1U,         /* first boot happened   */
    ESP_OTA_IMG_VALID           = 0x2U,         /* confirmed workable    */
    ESP_OTA_IMG_INVALID         = 0x3U,         /* confirmed non-workable */
    ESP_OTA_IMG_ABORTED         = 0x4U,         /* verify aborted        */
    ESP_OTA_IMG_UNDEFINED       = 0xFFFFFFFFU,  /* undefined             */
} esp_ota_img_states_t;

const esp_partition_t *esp_ota_get_boot_partition(void);
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition,
                                            void *app_desc);   /* esp_app_desc_t * */
esp_err_t esp_ota_get_boot_partition_description(const esp_partition_t *partition,
                                                 void *app_desc);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition,
                                      esp_ota_img_states_t *ota_state);
const esp_partition_t *esp_ota_get_last_invalid_partition(void);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
const esp_partition_t *esp_ota_get_next_update_partition_from(const esp_partition_t *start_from);
esp_err_t esp_ota_erase_last_boot_app_partition(void);
int       esp_ota_check_rollback_is_possible(void);

/* OTA write session — sim forwards to esp_partition_write over the
 * target slot; the running partition is rejected like on silicon.    */
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size,
                        esp_ota_handle_t *out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_write_with_offset(esp_ota_handle_t handle, const void *data,
                                    size_t size, uint32_t offset);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* ESP_OTA_OPS_H */
