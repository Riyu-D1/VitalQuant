/* esp_efuse.h — host-sim shim for the ESP-IDF eFuse API (ESP32-S3).
 *
 * Model: 11 blocks (BLK0..BLK10) x 256 bits of simulated eFuse RAM.
 * Every fuse reads 0 by default EXCEPT the factory MAC programmed into
 * BLK1 (see rom/efuse_mac.c) — matching the fix_v22 bench expectation:
 *   VDD_SPI_FORCE=0 (strap decides -> 3.3 V), STRAP_JTAG_SEL=0,
 *   DIS_USB_JTAG=0, DIS_USB_SERIAL_JTAG=0.
 * Field descriptors live in esp_efuse_table.h; the reader walks the
 * descriptor arrays bit-faithfully.
 *
 * Field addressing: {block, bit_start, bit_count}; bits are numbered
 * 0..255 inside a block, LSB-first into the destination buffer.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* esp_err_t: prefer the shim's esp_err.h; identical fallback. */
#if defined(__has_include)
#  if __has_include("esp_err.h")
#    include "esp_err.h"
#    define ESP_ERR_T_DECLARED
#  endif
#endif
#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#endif
#ifndef ESP_OK
#define ESP_OK                   0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL                 (-1)
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG      0x102
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED    0x106
#endif

/* eFuse error base + codes (same numbering as IDF). */
#define ESP_ERR_EFUSE                         0x1600
#define ESP_OK_EFUSE_CNT                     (ESP_ERR_EFUSE + 0x01)
#define ESP_ERR_EFUSE_CNT_IS_FULL            (ESP_ERR_EFUSE + 0x02)
#define ESP_ERR_EFUSE_REPEATED_PROG          (ESP_ERR_EFUSE + 0x03)
#define ESP_ERR_CODING                       (ESP_ERR_EFUSE + 0x04)
#define ESP_ERR_NOT_ENOUGH_UNUSED_KEY_BLOCKS (ESP_ERR_EFUSE + 0x05)
#define ESP_ERR_DAMAGED_READING              (ESP_ERR_EFUSE + 0x06)

#ifdef __cplusplus
extern "C" {
#endif

/* eFuse blocks (ESP32-S3).  Guarded so a sibling esp_efuse_chip.h can
 * coexist if it defines the same enum.                              */
#ifndef ESP_EFUSE_BLOCK_T_DECLARED
#define ESP_EFUSE_BLOCK_T_DECLARED
typedef enum {
    EFUSE_BLK0                 = 0,   /**< REPEAT_DATA */
    EFUSE_BLK1                 = 1,   /**< MAC_SPI_8M_SYS */
    EFUSE_BLK2                 = 2,   /**< SYS_DATA_PART1 */
    EFUSE_BLK_SYS_DATA_PART1   = 2,
    EFUSE_BLK3                 = 3,   /**< USER_DATA */
    EFUSE_BLK_USER_DATA        = 3,
    EFUSE_BLK4                 = 4,   /**< KEY0 */
    EFUSE_BLK_KEY0             = 4,
    EFUSE_BLK5                 = 5,   /**< KEY1 */
    EFUSE_BLK_KEY1             = 5,
    EFUSE_BLK6                 = 6,   /**< KEY2 */
    EFUSE_BLK_KEY2             = 6,
    EFUSE_BLK7                 = 7,   /**< KEY3 */
    EFUSE_BLK_KEY3             = 7,
    EFUSE_BLK8                 = 8,   /**< KEY4 */
    EFUSE_BLK_KEY4             = 8,
    EFUSE_BLK9                 = 9,   /**< KEY5 */
    EFUSE_BLK_KEY5             = 9,
    EFUSE_BLK_KEY_MAX          = 10,
    EFUSE_BLK10                = 10,  /**< SYS_DATA_PART2 */
    EFUSE_BLK_SYS_DATA_PART2   = 10,
    EFUSE_BLK_MAX
} esp_efuse_block_t;
#endif /* ESP_EFUSE_BLOCK_T_DECLARED */

/* Type definition for an eFuse field descriptor. */
typedef struct {
    esp_efuse_block_t   efuse_block: 8; /**< Block of eFuse */
    uint8_t             bit_start;      /**< Start bit [0..255] */
    uint16_t            bit_count;      /**< Length of bit field */
} esp_efuse_desc_t;

/* ROM log scheme (kept for completeness). */
typedef enum {
    ESP_EFUSE_ROM_LOG_ALWAYS_ON,
    ESP_EFUSE_ROM_LOG_ON_GPIO_LOW,
    ESP_EFUSE_ROM_LOG_ON_GPIO_HIGH,
    ESP_EFUSE_ROM_LOG_ALWAYS_OFF
} esp_efuse_rom_log_scheme_t;

/* Read `field` bits into `dst` (LSB-first packing, up to dst_size_bits). */
esp_err_t esp_efuse_read_field_blob(const esp_efuse_desc_t* field[],
                                    void* dst, size_t dst_size_bits);

/* Read a single-bit field as a bool. */
bool      esp_efuse_read_field_bit(const esp_efuse_desc_t *field[]);

/* Count programmed ("1") bits of `field`. */
esp_err_t esp_efuse_read_field_cnt(const esp_efuse_desc_t* field[],
                                   size_t* out_cnt);

/* Number of bits used by `field` (sum of desc entries). */
int       esp_efuse_get_field_size(const esp_efuse_desc_t* field[]);

/* Write support — the sim block RAM is writable but the fix_v22 board
 * contract treats eFuses as read-only; these return NOT_SUPPORTED to
 * catch any firmware path that tries to burn fuses.                 */
esp_err_t esp_efuse_write_field_blob(const esp_efuse_desc_t* field[],
                                     const void* src, size_t src_size_bits);
esp_err_t esp_efuse_write_field_cnt(const esp_efuse_desc_t* field[],
                                    size_t cnt);
esp_err_t esp_efuse_write_field_bit(const esp_efuse_desc_t* field[]);
esp_err_t esp_efuse_set_write_protect(esp_efuse_block_t blk);
esp_err_t esp_efuse_set_read_protect(esp_efuse_block_t blk);

/* Register-level access used by a few drivers/utilities. */
uint32_t  esp_efuse_read_reg(esp_efuse_block_t blk, unsigned int num_reg);
esp_err_t esp_efuse_write_reg(esp_efuse_block_t blk, unsigned int num_reg,
                              uint32_t val);
uint32_t  esp_efuse_get_pkg_ver(void);

/* ---- sim helpers (NOT part of IDF) -------------------------------- */
/* Burn bits into the sim eFuse array (test-side knob, e.g. fuse
 * STRAP_JTAG_SEL to watch the board_init warning path fire).         */
esp_err_t esp_shim_efuse_burn(uint32_t blk, uint32_t bit_start,
                              uint32_t bit_count, uint64_t value);
/* Reset all fuse words to 0 + factory MAC (as at power-on).          */
void      esp_shim_efuse_reset(void);

#ifdef __cplusplus
}
#endif
