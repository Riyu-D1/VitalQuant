/* esp_partition.h — host-sim shim of the ESP-IDF partition API.
 *
 * On silicon this API sits over the MMIO flash cache + a partition
 * table parsed from flash @0x8000.  In the host sim it is backed by a
 * single 16 MiB image file (sim/build/flash_image.bin — lazily created,
 * sparse) and an in-shim table that mirrors firmware/vitalq/partitions.csv:
 *
 *    nvs      data 0x02  @ 0x009000  size 0x6000
 *    otadata  data 0x00  @ 0x00f000  size 0x2000
 *    phy_init data 0x01  @ 0x011000  size 0x1000
 *    ota_0    app  0x10  @ 0x020000  size 0x400000   (4 MiB)
 *    ota_1    app  0x11  @ 0x420000  size 0x400000   (4 MiB)
 *    model    data 0x40  @ 0x820000  size 0x400000   (4 MiB, mmap'd weights)
 *    storage  data 0x81  @ 0xc20000  size 0x3d0000   (fat)
 *    coredump data 0x03  @ 0xff0000  size 0x10000    (ends at 16 MiB)
 *
 * Documented deltas vs silicon:
 *  - NOR program restrictions are NOT enforced: esp_partition_write is a
 *    plain pwrite (host bytes can go 0->1 as well as 1->0).
 *  - The image is sparse: regions never written are filesystem holes.
 *    esp_partition_read*() synthesises 0xFF (NOR-erased) for holes where
 *    the host fs supports SEEK_DATA/SEEK_HOLE; otherwise holes read 0x00.
 *  - esp_partition_mmap() is a REAL mmap(2) (PROT_READ, MAP_SHARED) of
 *    the image — zero-copy, so mmap'ing the 4 MiB 'model' partition does
 *    not eat heap.  Caveat: kernel zero pages back holes, so a partition
 *    that was never injected reads 0x00 through the mapping even though
 *    esp_partition_read() reports 0xFF.  Scenario flow stays faithful:
 *    place the VQMB blob at 0x820000 (part_sim_inject / dd) before boot.
 */
#ifndef ESP_PARTITION_H
#define ESP_PARTITION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* esp_err_t + the ESP_* codes this shim returns.  Prefer the shared
 * shim esp_err.h when it is on the include path; the individually
 * guarded fallbacks keep this header self-contained either way (same
 * pattern vboard.h uses). */
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

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_PARTITION_TYPE_APP  = 0x00,
    ESP_PARTITION_TYPE_DATA = 0x01,
} esp_partition_type_t;

typedef enum {
    /* app subtypes */
    ESP_PARTITION_SUBTYPE_APP_FACTORY = 0x00,
    ESP_PARTITION_SUBTYPE_APP_OTA_MIN = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_0   = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_1   = 0x11,
    ESP_PARTITION_SUBTYPE_APP_OTA_2   = 0x12,
    ESP_PARTITION_SUBTYPE_APP_OTA_3   = 0x13,
    ESP_PARTITION_SUBTYPE_APP_OTA_4   = 0x14,
    ESP_PARTITION_SUBTYPE_APP_OTA_5   = 0x15,
    ESP_PARTITION_SUBTYPE_APP_OTA_6   = 0x16,
    ESP_PARTITION_SUBTYPE_APP_OTA_7   = 0x17,
    ESP_PARTITION_SUBTYPE_APP_OTA_8   = 0x18,
    ESP_PARTITION_SUBTYPE_APP_OTA_9   = 0x19,
    ESP_PARTITION_SUBTYPE_APP_OTA_10  = 0x1A,
    ESP_PARTITION_SUBTYPE_APP_OTA_11  = 0x1B,
    ESP_PARTITION_SUBTYPE_APP_OTA_12  = 0x1C,
    ESP_PARTITION_SUBTYPE_APP_OTA_13  = 0x1D,
    ESP_PARTITION_SUBTYPE_APP_OTA_14  = 0x1E,
    ESP_PARTITION_SUBTYPE_APP_OTA_15  = 0x1F,
    ESP_PARTITION_SUBTYPE_APP_TEST    = 0x20,
    /* data subtypes */
    ESP_PARTITION_SUBTYPE_DATA_OTA       = 0x00,
    ESP_PARTITION_SUBTYPE_DATA_PHY       = 0x01,
    ESP_PARTITION_SUBTYPE_DATA_NVS       = 0x02,
    ESP_PARTITION_SUBTYPE_DATA_COREDUMP  = 0x03,
    ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS  = 0x04,
    ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM  = 0x05,
    ESP_PARTITION_SUBTYPE_DATA_UNDEFINED = 0x06,
    /* vendor range */
    ESP_PARTITION_SUBTYPE_DATA_VQMODEL   = 0x40,  /* 'model' — VQMB blob */
    ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD  = 0x80,
    ESP_PARTITION_SUBTYPE_DATA_FAT       = 0x81,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS    = 0x82,
    ESP_PARTITION_SUBTYPE_DATA_LITTLEFS  = 0x83,

    ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;

typedef struct {
    esp_partition_type_t    type;
    esp_partition_subtype_t subtype;
    uint32_t address;             /* absolute offset in the 16 MiB image */
    uint32_t size;
    char     label[17];           /* NUL-terminated, <= 16 chars        */
    bool     encrypted;
} esp_partition_t;

typedef struct esp_partition_iterator_opaque_ *esp_partition_iterator_t;

typedef enum {
    ESP_PARTITION_MMAP_DATA = 0,   /* data window (SPIFFS/model weights) */
    ESP_PARTITION_MMAP_INST = 1,   /* instruction window (OTA verify)    */
} esp_partition_mmap_memory_t;

typedef uint32_t esp_partition_mmap_handle_t;

/* ----------------------- enumeration ------------------------------- */

/* Snapshot iterator over the in-shim table.  subtype/label act as
 * filters: ESP_PARTITION_SUBTYPE_ANY (or NULL label) matches all.
 * Caller must esp_partition_iterator_release() it; walking to the end
 * with esp_partition_next() frees it automatically. */
esp_partition_iterator_t esp_partition_find(esp_partition_type_t type,
                                            esp_partition_subtype_t subtype,
                                            const char *label);
const esp_partition_t   *esp_partition_find_first(esp_partition_type_t type,
                                                  esp_partition_subtype_t subtype,
                                                  const char *label);
const esp_partition_t   *esp_partition_get(esp_partition_iterator_t iterator);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t iterator);
void                     esp_partition_iterator_release(esp_partition_iterator_t iterator);

/* True when *partition matches a row of the in-shim table (type,
 * subtype, address, size, label). */
bool esp_partition_verify(const esp_partition_t *partition);

/* ----------------------- access ------------------------------------ */

/* Bounds-checked against the partition extent.  read vs read_raw (and
 * write vs write_raw) are identical in the sim — the raw/non-raw split
 * is a cache-coherency detail of the silicon flash driver. */
esp_err_t esp_partition_read (const esp_partition_t *partition,
                              size_t src_offset, void *dst, size_t size);
esp_err_t esp_partition_write(const esp_partition_t *partition,
                              size_t dst_offset, const void *src, size_t size);
esp_err_t esp_partition_read_raw (const esp_partition_t *partition,
                                  size_t src_offset, void *dst, size_t size);
esp_err_t esp_partition_write_raw(const esp_partition_t *partition,
                                  size_t dst_offset, const void *src, size_t size);

/* offset and size must be multiples of the 4 KiB erase block; the range
 * becomes 0xFF. */
esp_err_t esp_partition_erase_range(const esp_partition_t *partition,
                                    size_t offset, size_t size);

/* Real mmap(2) of the image region (PROT_READ, MAP_SHARED).  offset is
 * partition-relative and need not be page-aligned; out_ptr is biased by
 * the alignment delta.  Handles are recycled from a fixed pool. */
esp_err_t esp_partition_mmap(const esp_partition_t *partition,
                             uint32_t offset, uint32_t size,
                             esp_partition_mmap_memory_t memory,
                             const void **out_ptr,
                             esp_partition_mmap_handle_t *out_handle);
void      esp_partition_munmap(esp_partition_mmap_handle_t handle);

/* SHA-256 over the whole partition extent (logical contents, holes
 * counted as 0xFF) — mirrors esp_partition_get_sha256(). */
esp_err_t esp_partition_get_sha256(const esp_partition_t *partition,
                                   uint8_t *sha_256);

/* ------------------- host-sim only helpers ------------------------- */

#define PART_SIM_FLASH_BYTES (16u * 1024u * 1024u)   /* 16 MiB image   */
#define PART_SIM_ERASE_BLOCK 0x1000u                 /* 4 KiB sector   */

/* Writes the backing-image path into buf.  Resolution order:
 *   $VSIM_FLASH_IMAGE            — explicit file path
 *   $SIM_HOST_BUILD              — treated as the build dir (<dir>/flash_image.bin)
 *   <dir-of-part_sim.c>/../build/flash_image.bin  — __FILE__-derived
 *   "sim/build/flash_image.bin"  — cwd-relative fallback
 * Returns the path length (>= 0) or -1 on bad args/truncation. */
int       part_sim_flash_path(char *buf, size_t len);

/* Persistently pwrite() len bytes at an ABSOLUTE flash offset — the
 * pre-boot hook for placing a model blob at 0x820000, pre-seeding nvs,
 * laying down corrupt data for recovery tests, ... */
esp_err_t part_sim_inject(uint32_t offset, const void *data, size_t len);

/* Whole-chip erase: the image becomes logically all-0xFF again
 * (re-sparsed where the fs supports hole detection). */
esp_err_t part_sim_erase_all(void);

/* Drop the cached fd so the next call re-opens the image — used by
 * vsim_reset between reboots; harmless to call any time. */
void      part_sim_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_PARTITION_H */
