/* esp_flash.h — host-sim shim of the ESP-IDF SPI flash chip-driver API.
 *
 * The canonical implementation lives in esp_shim/flash/esp_flash_sim.c
 * (file-backed 16 MiB image, offset == flash address, erased = 0xFF);
 * esp_shim/rom/rom.c supplies the remaining API surface declared below.
 * This header defines the shared contract for both.
 *
 * Sim device: 16 MiB NOR, JEDEC id reported by esp_flash_sim.c.
 */
#ifndef ESP_FLASH_H
#define ESP_FLASH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* esp_err_t + codes — use the shared shim esp_err.h when present      */
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
#ifndef ESP_ERR_INVALID_MAC
#define ESP_ERR_INVALID_MAC       0x10B
#endif
#ifndef ESP_ERR_NOT_FINISHED
#define ESP_ERR_NOT_FINISHED      0x10C
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------- geometry ------------------------------- */
#ifndef ESP_FLASH_SIM_SIZE_BYTES
#define ESP_FLASH_SIM_SIZE_BYTES  (16u * 1024u * 1024u)   /* 16 MiB    */
#endif
#ifndef SPI_FLASH_SEC_SIZE
#define SPI_FLASH_SEC_SIZE        4096u                   /* 4 KiB     */
#endif
#ifndef SPI_FLASH_SUBSEC_SIZE
#define SPI_FLASH_SUBSEC_SIZE     SPI_FLASH_SEC_SIZE
#endif
#ifndef SPI_FLASH_MMU_PAGE_SIZE
#define SPI_FLASH_MMU_PAGE_SIZE   (64u * 1024u)
#endif
#ifndef SPI_FLASH_ERASE_MAX_TIME_US
#define SPI_FLASH_ERASE_MAX_TIME_US (60u * 1000u)
#endif

/* Flash io modes — enum values match real IDF esp_flash_io_mode_t. */
typedef enum {
    SPI_FLASH_QIO = 0,        /* Quad I/O, address & data 4-bit         */
    SPI_FLASH_QOUT,           /* Quad I/O, data only                    */
    SPI_FLASH_DIO,            /* Dual I/O, address & data 2-bit         */
    SPI_FLASH_DOUT,           /* Dual I/O, data only                    */
    SPI_FLASH_FASTRD,         /* Fast Read                              */
    SPI_FLASH_SLOWRD,         /* Normal read                            */
    SPI_FLASH_OPI_STR,        /* Octal I/O, STR                         */
    SPI_FLASH_OPI_DTR,        /* Octal I/O, DTR                         */
    SPI_FLASH_READ_MODE_MAX,
} esp_flash_io_mode_t;

/* Chip handle.  In the sim this is a plain data record — the impl in
 * esp_flash_sim.c keys off .size/.device_id and uses .host_ctx for the
 * backing image; esp_flash_default_chip is the 16 MiB internal flash. */
typedef struct esp_flash_t {
    void                *host_ctx;       /* sim image context          */
    esp_flash_io_mode_t  read_mode;
    uint32_t             size;           /* chip size in bytes         */
    uint32_t             device_id;      /* JEDEC-style id             */
    bool                 busy;           /* suspended-for-calibration  */
    uint8_t              hpm_dummy_ena;
    uint8_t              reserved_dummy;
} esp_flash_t;

extern esp_flash_t *esp_flash_default_chip;

/* Region used by write-protect bookkeeping. */
typedef struct {
    uint32_t start;
    uint32_t size;
} esp_flash_region_t;

typedef struct esp_flash_enc_mode_t_ *esp_flash_enc_mode_t;

/* ------------------------- lifecycle ------------------------------ */
esp_err_t esp_flash_init(esp_flash_t *chip);
bool      esp_flash_chip_driver_initialized(const esp_flash_t *chip);

esp_err_t esp_flash_read_id(esp_flash_t *chip, uint32_t *out_id);
/* Sim convenience (not in real IDF): raw JEDEC id of the chip. */
uint32_t  esp_flash_get_id(esp_flash_t *chip);

esp_err_t esp_flash_get_size(esp_flash_t *chip, uint32_t *out_size);
esp_err_t esp_flash_get_physical_size(esp_flash_t *chip,
                                      uint32_t *flash_size);
esp_err_t esp_flash_read_unique_chip_id(esp_flash_t *chip,
                                        uint64_t *out_id);

/* ------------------------- data path ------------------------------ */
/* All offsets/counts are bytes, bounds-checked against the 16 MiB
 * image.  esp_flash_write goes straight to the backing image (the sim
 * does not model NOR 1->0 program restrictions — see esp_flash_sim.c). */
esp_err_t esp_flash_read(esp_flash_t *chip, void *buffer,
                         uint32_t address, uint32_t length);
esp_err_t esp_flash_write(esp_flash_t *chip, const void *buffer,
                          uint32_t address, uint32_t length);

/* start/len must be SPI_FLASH_SEC_SIZE-aligned; region becomes 0xFF. */
esp_err_t esp_flash_erase_region(esp_flash_t *chip,
                                 uint32_t start, uint32_t len);
esp_err_t esp_flash_erase_chip(esp_flash_t *chip);

/* ------------------------- protection ----------------------------- */
/* Sim chip reports no protected regions; set/get are recorded stubs. */
esp_err_t esp_flash_get_chip_write_protect(esp_flash_t *chip,
                                           bool *write_protected);
esp_err_t esp_flash_set_chip_write_protect(esp_flash_t *chip,
                                           bool write_protect);
esp_err_t esp_flash_get_protectable_regions(const esp_flash_t *chip,
                                            const esp_flash_region_t **out_regions,
                                            uint32_t *out_num_regions);
esp_err_t esp_flash_get_protected_region(esp_flash_t *chip,
                                         const esp_flash_region_t *region,
                                         bool *out_protected);
esp_err_t esp_flash_set_protected_region(esp_flash_t *chip,
                                         const esp_flash_region_t *region,
                                         bool protect);

/* ------------------------- encryption ----------------------------- */
/* Flash encryption is not modelled: encrypted ops alias the plain
 * read/write paths (scenarios keep plaintext). */
esp_err_t esp_flash_write_encrypted(esp_flash_t *chip, uint32_t address,
                                    const void *buffer, uint32_t length);
esp_err_t esp_flash_read_encrypted(esp_flash_t *chip, uint32_t address,
                                   void *out_buffer, uint32_t length);
esp_err_t esp_flash_encryption_enabled(void);

/* Mode helpers — always report plaintext-disabled in the sim. */
bool esp_flash_encryption_cfg_check(esp_flash_enc_mode_t mode, bool enabled);
bool esp_flash_encryption_check_mode_supported(esp_flash_enc_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* ESP_FLASH_H */
