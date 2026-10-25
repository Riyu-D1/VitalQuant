/* soc/spi_periph.h — host-sim SPI peripheral signal map (ESP32-S3).
 *
 * Provides spi_periph_signal[SOC_SPI_PERIPH_NUM] with the same shape
 * and values as the real esp32s3 table — spi_bus.c reads
 *   .spiq_iomux_pin / .spiq_in / .func
 * to restore the native FSPI-Q (IO13) iomux path after the MISO float
 * check.  Only GPSPI2 carries real pins in the sim table (SPI0/1 is the
 * internal-flash MSPI block, SPI3 row is populated for completeness).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "soc/soc.h"
#include "soc/gpio_sig_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/* soc_caps equivalents (guarded — a sibling soc_caps.h may define them). */
#ifndef SOC_SPI_PERIPH_NUM
#define SOC_SPI_PERIPH_NUM          3    /* SPI0/1 (mspi), GPSPI2, GPSPI3 */
#endif
#ifndef SOC_SPI_MAX_CS_NUM
#define SOC_SPI_MAX_CS_NUM          6
#endif
#ifndef SOC_SPI_SUPPORT_OCT
#define SOC_SPI_SUPPORT_OCT         1
#endif

/* spi_pins.h values for ESP32-S3 (non-octal pin set used by fix_v22). */
#define SPI2_FUNC_NUM               4
#define SPI2_IOMUX_PIN_NUM_HD       9
#define SPI2_IOMUX_PIN_NUM_CS       10
#define SPI2_IOMUX_PIN_NUM_MOSI     11
#define SPI2_IOMUX_PIN_NUM_CLK      12
#define SPI2_IOMUX_PIN_NUM_MISO     13
#define SPI2_IOMUX_PIN_NUM_WP       14

#define SPI2_FUNC_NUM_OCT           2
#define SPI2_IOMUX_PIN_NUM_HD_OCT   33
#define SPI2_IOMUX_PIN_NUM_CS_OCT   34
#define SPI2_IOMUX_PIN_NUM_MOSI_OCT 35
#define SPI2_IOMUX_PIN_NUM_CLK_OCT  36
#define SPI2_IOMUX_PIN_NUM_MISO_OCT 37
#define SPI2_IOMUX_PIN_NUM_WP_OCT   38
#define SPI2_IOMUX_PIN_NUM_IO4_OCT  10
#define SPI2_IOMUX_PIN_NUM_IO5_OCT  11
#define SPI2_IOMUX_PIN_NUM_IO6_OCT  12
#define SPI2_IOMUX_PIN_NUM_IO7_OCT  13

/* SPI3 has no iomux pins on ESP32-S3 (matrix-only). */
#define SPI3_FUNC_NUM               3

/* Interrupt source numbers (soc/interrupts.h on target). */
#ifndef ETS_SPI2_INTR_SOURCE
#define ETS_SPI2_INTR_SOURCE        19
#define ETS_SPI3_INTR_SOURCE        20
#endif

/* Opaque register block handle (spi_dev_t on target). */
typedef struct spi_dev spi_dev_t;

/* Same layout as the real spi_signal_conn_t. */
typedef struct {
    const uint8_t spiclk_out;       /* GPIO mux output signals */
    const uint8_t spiclk_in;
    const uint8_t spid_out;
    const uint8_t spiq_out;
    const uint8_t spiwp_out;
    const uint8_t spihd_out;
    const uint8_t spid4_out;
    const uint8_t spid5_out;
    const uint8_t spid6_out;
    const uint8_t spid7_out;
    const uint8_t spid_in;          /* GPIO mux input signals */
    const uint8_t spiq_in;
    const uint8_t spiwp_in;
    const uint8_t spihd_in;
    const uint8_t spid4_in;
    const uint8_t spid5_in;
    const uint8_t spid6_in;
    const uint8_t spid7_in;
    const uint8_t spics_out[SOC_SPI_MAX_CS_NUM]; /* /CS GPIO output mux signals */
    const uint8_t spics_in;
    const uint8_t spidqs_out;
    const uint8_t spicd_out;
    const uint8_t spiclk_iomux_pin; /* IO pins of IO_MUX muxed signals */
    const uint8_t spid_iomux_pin;
    const uint8_t spiq_iomux_pin;
    const uint8_t spiwp_iomux_pin;
    const uint8_t spihd_iomux_pin;
    const uint8_t spics0_iomux_pin;
    const uint8_t irq;              /* irq source for interrupt mux */
    const uint8_t irq_dma;          /* dma irq source for interrupt mux */
    const int     func;             /* function number for IOMUX */
    spi_dev_t      *hw;             /* register block (opaque in sim) */
} spi_signal_conn_t;

extern const spi_signal_conn_t spi_periph_signal[SOC_SPI_PERIPH_NUM];

#ifdef __cplusplus
}
#endif
