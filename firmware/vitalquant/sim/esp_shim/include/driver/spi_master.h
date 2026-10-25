/* spi_master.h — host-sim shim of ESP-IDF driver/spi_master.h.
 *
 * Same type/flag vocabulary as IDF 5.4 so the real firmware compiles
 * unchanged. The "hardware" is the virtual board: spi_device_transmit /
 * spi_device_polling_transmit serialise the transaction into one flat
 * byte stream (cmd | addr | dummy | data) and hand it to the vboard
 * device model selected by *which CS line the firmware drove low*
 * (this firmware attaches everything with spics_io_num = -1 and owns
 * CS itself — see sim/esp_shim/spi/spi_sim.c).
 */
#ifndef DRIVER_SPI_MASTER_H
#define DRIVER_SPI_MASTER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"          /* TickType_t — as real IDF does */
#include "driver/spi_common.h"          /* spi_host_device_t, spi_dma_chan_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque device handle — backed by a sim_dev_t in spi_sim.c.         */
typedef struct spi_device_t *spi_device_handle_t;

/* ---------------- device flags (spi_device_interface_config_t.flags) -- */
#define SPI_DEVICE_TXBIT_LSBFIRST   (1u << 0)   /* TX LSB-first        */
#define SPI_DEVICE_RXBIT_LSBFIRST   (1u << 1)   /* RX LSB-first        */
#define SPI_DEVICE_BIT_LSBFIRST     (SPI_DEVICE_TXBIT_LSBFIRST | SPI_DEVICE_RXBIT_LSBFIRST)
#define SPI_DEVICE_3WIRE            (1u << 2)   /* half-duplex 3-wire  */
#define SPI_DEVICE_POSITIVE_CS      (1u << 3)   /* CS active-high      */
#define SPI_DEVICE_HALFDUPLEX       (1u << 4)   /* no full-duplex data */
#define SPI_DEVICE_CLK_AS_CS        (1u << 5)   /* clock idles as CS   */
#define SPI_DEVICE_NO_DUMMY         (1u << 6)   /* skip dummy phase    */
#define SPI_DEVICE_DDRCLK           (1u << 7)   /* DDR clock           */
#define SPI_DEVICE_NO_RETURN_RESULT (1u << 8)   /* async: drop result  */

/* ---------------- transaction flags (spi_transaction_t.flags) ------- */
#define SPI_TRANS_MODE_DIO          (1u << 0)   /* cmd/addr dual-line  */
#define SPI_TRANS_MODE_QIO          (1u << 1)   /* cmd/addr quad-line  */
#define SPI_TRANS_USE_RXDATA        (1u << 2)   /* rx into .rx_data[4] */
#define SPI_TRANS_USE_TXDATA        (1u << 3)   /* tx from .tx_data[4] */
#define SPI_TRANS_MODE_DIOQIO_ADDR  (1u << 4)   /* addr on data lines  */
#define SPI_TRANS_VARIABLE_CMD      (1u << 5)   /* ext_t cmd bits      */
#define SPI_TRANS_VARIABLE_ADDR     (1u << 6)   /* ext_t addr bits     */
#define SPI_TRANS_VARIABLE_DUMMY    (1u << 7)   /* ext_t dummy bits    */
#define SPI_TRANS_CS_KEEP_ACTIVE    (1u << 8)   /* don't raise CS      */
#define SPI_TRANS_MULTILINE_CMD     (1u << 9)   /* cmd on data lines   */
#define SPI_TRANS_MULTILINE_ADDR    (1u << 10)  /* addr on data lines  */
#define SPI_TRANS_MODE_OCT          (1u << 11)  /* octal mode          */
#define SPI_TRANS_SW_GPIO_CS        (1u << 16)  /* CS via GPIO matrix  */
#define SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL (1u << 18)

/* ---------------- bus config --------------------------------------- */
typedef struct {
    int      mosi_io_num;
    int      miso_io_num;
    int      sclk_io_num;
    int      quadwp_io_num;
    int      quadhd_io_num;
    int      data4_io_num;
    int      data5_io_num;
    int      data6_io_num;
    int      data7_io_num;
    int      max_transfer_sz;        /* bytes, per transaction         */
    uint32_t flags;
    int      intr_flags;
    int      isr_cpu_id;             /* esp_intr_cpu_affinity_t in IDF */
} spi_bus_config_t;

/* ---------------- device config ------------------------------------ */
struct spi_transaction_t;
typedef void (*transaction_cb_t)(struct spi_transaction_t *trans);

typedef struct {
    uint8_t  command_bits;           /* 0..16 — cmd phase length       */
    uint8_t  address_bits;           /* 0..64 — addr phase length      */
    uint8_t  dummy_bits;             /* 0..8? IDF: dummy phase length  */
    uint8_t  mode;                   /* SPI mode 0..3                  */
    uint16_t duty_cycle_pos;         /* 128 = 50%                      */
    uint16_t cs_ena_pretrans;        /* SCK cycles CS early            */
    uint8_t  cs_ena_posttrans;       /* SCK cycles CS held             */
    int      clock_speed_hz;         /* request — sim runs it exactly  */
    int      input_delay_ns;         /* MISO sample delay              */
    int      spics_io_num;           /* -1 = firmware drives CS itself */
    uint32_t flags;                  /* SPI_DEVICE_*                   */
    int      queue_size;             /* queued-transaction depth       */
    transaction_cb_t pre_cb;         /* before the transfer            */
    transaction_cb_t post_cb;        /* after it completes             */
    spi_common_clock_source_t clock_source; /* IDF 5.x field           */
} spi_device_interface_config_t;

/* ---------------- transaction -------------------------------------- */
/* length/rxlength are in BITS. rxlength == 0 means "same as length".
 * With SPI_TRANS_USE_TXDATA/USE_RXDATA the inline 4-byte arrays are the
 * buffers (payload must fit: length/rxlength <= 32 bits).             */
struct spi_transaction_t {
    uint32_t flags;                  /* SPI_TRANS_*                    */
    uint16_t cmd;                    /* command phase value            */
    uint64_t addr;                   /* address phase value            */
    size_t   length;                 /* data phase, bits (tx = rx len) */
    size_t   rxlength;               /* bits actually stored to rx dst */
    void    *user;                   /* opaque to the driver           */
    union {
        const void *tx_buffer;
        uint8_t     tx_data[4];      /* USE_TXDATA fast path           */
    };
    union {
        void   *rx_buffer;
        uint8_t rx_data[4];          /* USE_RXDATA fast path           */
    };
};
typedef struct spi_transaction_t spi_transaction_t;

/* VARIABLE_CMD/ADDR/DUMMY transactions are cast to this — the first
 * member must stay exactly spi_transaction_t.                        */
typedef struct {
    struct spi_transaction_t base;
    uint8_t command_bits;
    uint8_t address_bits;
    uint8_t dummy_bits;
    uint8_t cs_flags;
} spi_transaction_ext_t;

/* ---------------- API ---------------------------------------------- */
esp_err_t spi_bus_initialize(spi_host_device_t host_id,
                             const spi_bus_config_t *bus_config,
                             spi_dma_chan_t dma_chan);
esp_err_t spi_bus_free(spi_host_device_t host_id);
esp_err_t spi_bus_add_device(spi_host_device_t host_id,
                             const spi_device_interface_config_t *dev_config,
                             spi_device_handle_t *handle);
esp_err_t spi_bus_remove_device(spi_device_handle_t handle);

/* Queued API — in the sim every "queued" transaction runs to
 * completion immediately; get_trans_result then hands it back.        */
esp_err_t spi_device_queue_trans(spi_device_handle_t handle,
                                 spi_transaction_t *trans_desc,
                                 TickType_t ticks_to_wait);
esp_err_t spi_device_get_trans_result(spi_device_handle_t handle,
                                      spi_transaction_t **trans_desc,
                                      TickType_t ticks_to_wait);
esp_err_t spi_device_transmit(spi_device_handle_t handle,
                              spi_transaction_t *trans_desc);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,
                                      spi_transaction_t *trans_desc);
esp_err_t spi_device_polling_start(spi_device_handle_t handle,
                                   spi_transaction_t *trans_desc,
                                   TickType_t ticks_to_wait);
esp_err_t spi_device_polling_end(spi_device_handle_t handle,
                                 TickType_t ticks_to_wait);
esp_err_t spi_device_acquire_bus(spi_device_handle_t device,
                                 TickType_t wait);
esp_err_t spi_device_release_bus(spi_device_handle_t device);

esp_err_t spi_bus_get_max_transaction_len(spi_host_device_t host_id,
                                          size_t *max_transfer_sz);
esp_err_t spi_device_get_actual_freq(spi_device_handle_t handle,
                                     int *freq_khz);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_SPI_MASTER_H */
