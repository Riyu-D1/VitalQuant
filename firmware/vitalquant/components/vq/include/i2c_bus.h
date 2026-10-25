/* i2c_bus.h — single I2C0 master, 100 kHz HARD LIMIT.
 * fix_v22: R19/R20 2.2k pull-ups, 14 nodes → tr ~503 ns fails 400 kHz.
 * The 1.8 V island (AS7341 0x39, MLX90632 0x3A) is reached through the
 * PCA9306 and is the SAME logical bus — one mutex serialises everything.
 * Budget: total traffic must fit these rates (see hw brief §7); drivers
 * batch transactions and never poll faster than profile rates.
 */
#pragma once
#include "vq_types.h"
#include "board_pins.h"

/* Init bus + install mutex. Idempotent. */
vq_err_t vq_i2c_init(void);

/* Probe one address (ACK check). Returns ESP_OK if the device ACKs. */
vq_err_t vq_i2c_probe(uint8_t addr7);

/* Scan 0x08..0x77; writes responding addrs into out[] (7-bit), returns count. */
size_t   vq_i2c_scan(uint8_t *out, size_t max);

/* Register-level helpers (mutexed, single transaction where marked). */
vq_err_t vq_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);
vq_err_t vq_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);
vq_err_t vq_i2c_write(uint8_t addr, const uint8_t *data, size_t len);
vq_err_t vq_i2c_read(uint8_t addr, uint8_t *data, size_t len);
/* reg-write + repeated-start read, the common SMBus pattern */
vq_err_t vq_i2c_write_read(uint8_t addr, const uint8_t *wdata, size_t wlen,
                           uint8_t *rdata, size_t rlen);

/* Bus error counters for the 24 h error-rate acceptance metric. */
void     vq_i2c_stats(uint32_t *xactions, uint32_t *errors);
void     vq_i2c_stats_reset(void);
