/* board_init.h — fix_v22 boot bring-up for the WROOM N16R8.
 *
 * Order matters: strap check (IO45/IO46/IO3/IO0 read-back) → CS lines
 * high → buses → expander (all domains off) → everything else.
 * Asserts nothing forbidden: GPIO26-32/35-37/45-48 untouched, IO19/20
 * left to USB-Serial/JTAG.
 */
#pragma once
#include "vq_types.h"

typedef struct {
    int   reset_reason;
    bool  vdd_spi_3v3;          /* IO45 read-back low → 3.3 V flash      */
    bool  psram_octal;          /* 8 MB OPI detected                     */
    uint32_t flash_bytes;       /* expect 16 MB                          */
    uint32_t psram_bytes;       /* expect 8 MB                           */
    bool  strap_jtag_fused;     /* eFuse STRAP_JTAG_SEL — must be false  */
    char  boot_report[256];     /* rendered for console                  */
} vq_boot_info_t;

vq_err_t vq_board_init(vq_boot_info_t *info);   /* early: pins+buses     */
const vq_boot_info_t *vq_board_info(void);
