/* spi_common.h — host-sim shim of ESP-IDF driver/spi_common.h (+ the
 * hal/spi_types.h host-id enum the real header pulls in).
 *
 * Only the pieces the vitalquant firmware consumes are modelled; anything
 * the IDF version carries that firmware never sees is omitted or a
 * documented no-op. The behavioural core lives in
 * sim/esp_shim/spi/spi_sim.c — this file is types + declarations.
 */
#ifndef DRIVER_SPI_COMMON_H
#define DRIVER_SPI_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SPI peripheral host ids — hal/spi_types.h in real IDF. Board uses
 * SPI2 (FSPI); the other ids exist only for API completeness.        */
typedef enum {
    SPI1_HOST = 0,
    SPI2_HOST = 1,
    SPI3_HOST = 2,
    SPI_HOST_MAX,
} spi_host_device_t;

/* DMA channel selector — IDF keeps this an int so "auto" survives.   */
typedef enum {
    SPI_DMA_DISABLED = 0,   /* no DMA — CPU-buffered transfers        */
    SPI_DMA_CH_AUTO  = 3,   /* let the driver pick a channel          */
} spi_dma_chan_t;

/* Clock source selector — soc_periph_spi_clk_src_t in real IDF. The
 * sim runs every request at the exact requested rate; this exists so
 * designated initialisers using .clock_source still compile.         */
typedef int spi_common_clock_source_t;
#define SPI_CLK_SRC_DEFAULT   ((spi_common_clock_source_t)0)

/* IDF derives an achievable clock from the APB divisor chain. On the
 * virtual board the "PLL" grants any request, so this returns hz.    */
int spi_get_actual_clock(int fapb, int hz, int duty_p);

/* IDF computes the timing (extra dummy bits / MISO delay) a given
 * input-delay budget needs at a given clock. Sim models no sampling
 * delay — returns the request unchanged.                             */
esp_err_t spi_get_timing(bool gpio_is_used, int input_delay_ns,
                         int eff_clk, int *dummy_o, int *miso_delay_o);

/* ------------------------------------------------------------ *
 *  Host-sim test hooks (never present on target builds).
 * ------------------------------------------------------------ */
#ifdef SIM_HOST_BUILD
/* Incremented each time a device is attached (or first used) with
 * cfg.clock_speed_hz above the per-CS cap the fix_v22 silicon allows:
 *   IO38 MAX86141 -> 8 MHz   IO10 ADS1292 -> 4 MHz
 *   IO15 AD5940   -> 8 MHz   IO18 AFE4900  -> 8 MHz   IO16 W25Q512 -> 40 MHz */
extern uint64_t g_spi_speed_violations;
uint64_t vboard_spi_speed_violations(void);
#endif /* SIM_HOST_BUILD */

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_SPI_COMMON_H */
