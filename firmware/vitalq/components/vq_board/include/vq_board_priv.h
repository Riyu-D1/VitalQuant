/* vq_board_priv.h — module-private constants for the vq_board component.
 * NOT a public API: include/ exists only because idf_component_register
 * names it; no other component may rely on anything in this file.
 *
 * fix_v22 expected memory map — ESP32-S3-WROOM-1-N16R8:
 *   16 MB quad SPI flash @ 3.3 V (VDD_SPI strap IO45 must read 0 at boot)
 *    8 MB OCTAL PSRAM       (CONFIG_SPIRAM_MODE_OCT)
 */
#pragma once
#include <stdint.h>

#define VQ_BOOT_FLASH_BYTES_EXPECT   0x01000000UL   /* 16 MB quad  */
#define VQ_BOOT_PSRAM_BYTES_EXPECT   0x00800000UL   /*  8 MB octal */
