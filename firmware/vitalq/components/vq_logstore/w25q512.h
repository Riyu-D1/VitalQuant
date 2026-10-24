/* w25q512.h — PRIVATE interface between log_store.c and the W25Q512JV
 * low-level driver. Component-internal: the public contract is
 * components/vq/include/log_store.h only, never include this from
 * another component. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "vq_types.h"

/* ---- geometry ---------------------------------------------------------
 * W25Q512JV: 512 Mbit = 64 MiB = 0x0400_0000 bytes
 *   16 384 sectors of 4 KB (erase unit), 256 B pages (program unit). */
#define W25_TOTAL_BYTES     (64u * 1024u * 1024u)
#define W25_SECTOR_SIZE     4096u
#define W25_PAGE_SIZE       256u
#define W25_TOTAL_SECTORS   (W25_TOTAL_BYTES / W25_SECTOR_SIZE)   /* 16384 */

/* JEDEC 9Fh expectation: Winbond EF, memory type 40, capacity 20 (512 Mbit).
 * NOTE: the stacked-die variant (W25Q512JV-IM) reports EF 70 20 and needs
 * Die-Select C2h handling — not supported here; init rejects it loudly. */
#define W25_JEDEC_MFR       0xEFu
#define W25_JEDEC_TYPE      0x40u
#define W25_JEDEC_CAP       0x20u

/* Attach on VQ_SPI_DEV_FLASH, release any power-down (covers deep-sleep
 * wake where the chip kept its PD latch), verify JEDEC EF 40 20, enter
 * 4-byte mode. Idempotent; w25q512_present() reports the probe result. */
vq_err_t w25q512_init(void);
bool     w25q512_present(void);

vq_err_t w25q512_jedec(uint8_t out3[3]);        /* raw 9Fh response        */
uint8_t  w25q512_status1(void);                 /* SR1: bit0 BUSY, bit1 WEL */
uint8_t  w25q512_status3(void);                 /* SR3: bit0 ADS (4B mode)  */

/* Poll SR1.BUSY until clear or timeout. */
vq_err_t w25q512_wait_idle(uint32_t timeout_ms);

/* All address-bearing commands use the dedicated 4-BYTE opcodes, so they
 * are correct in BOTH the 3B and 4B address modes (see w25q512.c header). */
vq_err_t w25q512_read(uint32_t addr, uint8_t *buf, size_t len);

/* Single Page-Program: len 1..256, must not cross a 256 B page boundary
 * (returns ESP_ERR_INVALID_ARG otherwise). Waits for BUSY to clear.   */
vq_err_t w25q512_page_program(uint32_t addr, const uint8_t *buf, size_t len);

/* 4 KB sector erase (addr must be sector-aligned); waits for BUSY clear. */
vq_err_t w25q512_sector_erase(uint32_t addr);

/* Enter-4B (B7h) + verify SR3.ADS. Issued at init and after PD release. */
vq_err_t w25q512_enter_4b(void);

/* B9h power-down + tDP; ABh release + tRES1. */
vq_err_t w25q512_power_down(void);
vq_err_t w25q512_release_power_down(void);
