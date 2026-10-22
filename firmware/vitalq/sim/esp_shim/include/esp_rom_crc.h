/* esp_rom_crc.h — host-sim shim for the ESP32-S3 ROM CRC routines.
 *
 * Semantics are bit-identical to the silicon ROM code (and to IDF's
 * reference implementation in esp_rom/patches/esp_rom_crc.c):
 *
 *     out = ~CRC_inner(~crc_arg, buf, len)
 *
 * i.e. the 'crc' argument is the *complemented* running residue — the
 * function complements it on entry and complements the result on exit.
 * Consequence: esp_rom_crc32_le(0, buf, len) == zlib crc32(0, buf, len),
 * and results chain transparently:
 *
 *     crc = esp_rom_crc32_le(0,    buf0, len0);
 *     crc = esp_rom_crc32_le(crc,  buf1, len1);   // continued CRC
 *
 * Polynomials (per esp_rom_crc.h upstream doc):
 *   CRC-8        0x07       (le: reflected 0xE0)
 *   CRC16-CCITT  0x1021     (le: reflected 0x8408)
 *   CRC32        0x04C11DB7 (le: reflected 0xEDB88320)
 *
 *   "_le" = refin/refout true (reflected), "_be" = non-reflected.
 * To compute a named CRC algorithm: crc = ~esp_rom_crcXX_yy(~init, b, l)^xorout
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t esp_rom_crc32_le(uint32_t crc, uint8_t const *buf, uint32_t len);
uint32_t esp_rom_crc32_be(uint32_t crc, uint8_t const *buf, uint32_t len);
uint16_t esp_rom_crc16_le(uint16_t crc, uint8_t const *buf, uint32_t len);
uint16_t esp_rom_crc16_be(uint16_t crc, uint8_t const *buf, uint32_t len);
uint8_t  esp_rom_crc8_le (uint8_t  crc, uint8_t const *buf, uint32_t len);
uint8_t  esp_rom_crc8_be (uint8_t  crc, uint8_t const *buf, uint32_t len);

#ifdef __cplusplus
}
#endif
