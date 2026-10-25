/* spi_bus.h — shared SPI2 host with five active-low chip-selects.
 *
 * HARD RULES (hw brief §3.2/§10):
 *  - ONE CS asserted at a time — the bus driver owns a mutex per device
 *    handle; all CS idle HIGH (each has a 10k pull-up, drivers hold high).
 *  - Per-device clocks are CAPPED at board_pins.h values (MAX86141 <=8 MHz,
 *    ADS1292R <=4 MHz recommended, W25Q512 <=40 MHz).
 *  - CS_MAX86141 (IO38) also enables the U26 MISO level shifter — its MISO
 *    only drives while CS is low; that is by design.
 *  - MISO contention self-test: with all CS high, MISO must float.
 */
#pragma once
#include "vq_types.h"
#include "board_pins.h"

typedef enum {
    VQ_SPI_DEV_ADS1292 = 0,
    VQ_SPI_DEV_AFE4900,
    VQ_SPI_DEV_AD5940,
    VQ_SPI_DEV_FLASH,
    VQ_SPI_DEV_MAX86141,
    VQ_SPI_DEV_COUNT
} vq_spi_dev_t;

typedef struct {
    vq_spi_dev_t dev;
    gpio_num_t   cs_gpio;
    uint8_t      mode;        /* 0..3 */
    uint32_t     clock_hz;    /* request; capped at the per-device max */
    uint8_t      cs_pre_us;   /* setup before SCK (ADS1292 decode timing) */
    uint8_t      cs_post_us;  /* hold after SCK                          */
} vq_spi_dev_cfg_t;

/* Install SPI2 host, all five CS outputs driven HIGH. Idempotent. */
vq_err_t vq_spi_init(void);

/* Register a device → returns handle (or NULL). clock_hz is clamped to
 * the device cap in board_pins.h. */
void    *vq_spi_add(const vq_spi_dev_cfg_t *cfg);
void     vq_spi_remove(void *handle);

/* Mutexed transfers — CS asserted only around the transaction. */
vq_err_t vq_spi_xfer(void *handle, const uint8_t *tx, uint8_t *rx, size_t len);
/* Write-then-read with CS held across both (register protocol). */
vq_err_t vq_spi_write_read(void *handle, const uint8_t *tx, size_t txlen,
                           uint8_t *rx, size_t rxlen);
/* Convenience: 8-bit register write / read (tx/rx incl. command byte). */
vq_err_t vq_spi_reg_write(void *handle, const uint8_t *cmd, size_t cmdlen,
                          const uint8_t *data, size_t dlen);
vq_err_t vq_spi_reg_read(void *handle, const uint8_t *cmd, size_t cmdlen,
                         uint8_t *data, size_t dlen);

/* Self-test aid: force ALL CS high, then configure MISO as a plain GPIO
 * input with the given pull, and read it. Restores MISO to SPI function. */
int      vq_spi_miso_float_check(gpio_pull_mode_t pull);

void     vq_spi_stats(uint32_t *xactions, uint32_t *errors);
void     vq_spi_stats_reset(void);
