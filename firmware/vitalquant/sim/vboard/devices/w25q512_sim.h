/* w25q512_sim.h — scenario/test-facing interface for the W25Q512JV
 * virtual device model (sim/vboard/devices/w25q512.c).
 *
 * The firmware sees a normal vboard_dev_t on SPI CS=IO16; everything
 * below is BACKDOOR state for scenarios: fault injection, image
 * seeding/inspection, and operation counters.  None of it travels over
 * the virtual bus — it manipulates the model/file directly.
 *
 * Persistence: the array lives in sim/build/extflash.bin (64 MiB
 * sparse file, lazily created).  Bytes are stored INVERTED
 * (stored = flash ^ 0xFF) so never-touched holes read back as erased
 * 0xFF — a fresh file is a factory-fresh chip, and disk usage tracks
 * actually-programmed data.  The file survives across scenario runs in
 * the same build dir (logstore remount tests depend on it); delete the
 * file for a clean chip.
 */
#ifndef W25Q512_SIM_H
#define W25Q512_SIM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "vboard.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- geometry / identity (mirrors components/vq_logstore/w25q512.h) -- */
#define VBFQ_TOTAL_BYTES   (64u * 1024u * 1024u)   /* 0x0400_0000       */
#define VBFQ_PAGE_SIZE     256u
#define VBFQ_SECTOR_4K     4096u
#define VBFQ_BLOCK_32K     (32u * 1024u)
#define VBFQ_BLOCK_64K     (64u * 1024u)
#define VBFQ_TOTAL_SECTORS (VBFQ_TOTAL_BYTES / VBFQ_SECTOR_4K) /* 16384 */

#define VBFQ_CS_GPIO       16                       /* CS_FLASH = IO16   */

#define VBFQ_JEDEC_MFR     0xEFu    /* Winbond                          */
#define VBFQ_JEDEC_TYPE    0x40u    /* W25Q512JV-IQ: single die         */
#define VBFQ_JEDEC_CAP     0x20u    /* 512 Mbit                         */
#define VBFQ_JEDEC_IM_TYPE 0x70u    /* stacked-die -IM (driver rejects) */
#define VBFQ_DEVICE_ID     0x19u    /* legacy ID byte for ABh/90h       */

/* ---- registration -----------------------------------------------------
 * vboard_init() should call vbfq_w25q512_register() while wiring the
 * default device population; it also hooks the CS pin (IO16) and the
 * 3V3 rail.  Idempotent.  vbfq_w25q512_device() exposes the raw
 * vboard_dev_t for manual wiring instead.                               */
vboard_dev_t *vbfq_w25q512_device(void);
void          vbfq_w25q512_register(void);

/* ---- image backing ----------------------------------------------------
 * Default image path: <sim>/build/extflash.bin resolved relative to the
 * model source (independent of the scenario's cwd); env VBFQ_IMAGE
 * overrides.  vbfq_set_image_path() wins over both but must be called
 * before the first SPI access / hook that touches the file.          */
void        vbfq_set_image_path(const char *path);
const char *vbfq_image_path(void);          /* resolved path or ""      */
void        vbfq_flush(void);               /* fsync the image          */

/* Backdoor array access — decoded FLASH bytes (0xFF = erased), bypassing
 * the SPI model and NOR rules entirely.  raw_poke can set 0->1 bits that
 * a page program never could; use it to seed torn/journal states.
 * Returns 0 on success, -1 if the image cannot be opened / range bad. */
int         vbfq_raw_peek(uint32_t addr, uint8_t *out, size_t len);
int         vbfq_raw_poke(uint32_t addr, const uint8_t *data, size_t len);

/* ---- fault hooks -------------------------------------------------------
 * Fill the 4 KB sector CONTAINING addr with a deterministic non-0xFF
 * pattern (a real torn-program / corruption vector: CRCs fail, the
 * record chain scanner sees garbage).  Bypasses NOR rules.  Returns 0/-1. */
int   vbfq_corrupt_sector(uint32_t addr);

/* /WP + /HOLD pin model — both are tied to +3V3 on fix_v22 (driver
 * comment), so scenarios drive them here.  Bitmask: */
#define VBFQ_WP       0x1   /* /WP asserted low                          */
#define VBFQ_HOLD     0x2   /* /HOLD asserted low — freezes the bus FSM  */
#define VBFQ_WP_HARD  0x4   /* treat /WP as protecting the whole array:
                            * blocks program/erase regardless of SR BP
                            * bits (models SRP0+BP configured parts);
                            * without it /WP only blocks WRSR unless
                            * SR1.BP/SEC/TB or SR2.CMP are nonzero      */
int   vbfq_get_wp_hold(void);
void  vbfq_set_wp_hold(int mask);

/* Swap the JEDEC ID the model reports (e.g. EF 70 20 to exercise the
 * driver's stacked-die rejection path).  Reset restores EF 40 20.   */
void  vbfq_set_jedec(uint8_t mfr, uint8_t type, uint8_t cap);
void  vbfq_get_jedec(uint8_t out3[3]);
/* 8-byte unique ID returned by 4Bh (deterministic default).           */
void  vbfq_set_uid(const uint8_t uid8[8]);
void  vbfq_get_uid(uint8_t uid8[8]);

/* ---- operation counters (assert fault vectors did/didn't fire) ------- */
typedef struct {
    uint64_t spi_bytes;         /* MOSI bytes clocked                    */
    uint64_t txns;              /* CS windows / xfer calls completed     */
    uint64_t pp_commits;        /* page programs applied                 */
    uint64_t erase_commits;     /* sector/block/chip erases applied      */
    uint64_t chip_erases;
    uint64_t wrsr_commits;
    uint64_t wr_ignored_no_wel; /* program/erase/WRSR dropped: no WEL    */
    uint64_t wr_ignored_wp;     /* dropped: /WP protection               */
    uint64_t ignored_busy;      /* opcodes ignored while WIP             */
    uint64_t ignored_pd;        /* opcodes ignored in power-down         */
    uint64_t ignored_rail;      /* bytes ignored with 3V3 rail off       */
    uint64_t pd_entries, pd_wakes;
    uint64_t enter4b, exit4b;
    uint64_t sw_resets;
} vbfq_stats_t;
void  vbfq_stats(vbfq_stats_t *out);
void  vbfq_stats_reset(void);

/* Read-only peek at volatile chip state (WEL/ADS/PD/busy) for asserts.  */
typedef struct {
    bool wel, ads4b, powered_down, busy, reset_armed;
    uint8_t sr1, sr2, sr3;
} vbfq_state_t;
void  vbfq_get_state(vbfq_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* W25Q512_SIM_H */
