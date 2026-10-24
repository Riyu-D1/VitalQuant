/* spi_sim.c — ESP-IDF SPI master shim on top of the virtual board.
 *
 * WHAT THE FIRMWARE THINKS IT HAS
 *   A GPSPI master on SPI2 with per-device handles. This firmware
 *   (components/vq_spi/spi_bus.c) attaches every device with
 *   spics_io_num = -1 and drives chip-selects by hand through the GPIO
 *   layer — the IDF driver never owns a CS pin. The GPIO shim lands
 *   those edges on the board pin state, so by the time a transaction
 *   reaches us the CS level is already visible via vboard_gpio_get().
 *
 * DISPATCH MODEL
 *   Each transmit scans the five board CS pins {IO10, IO15, IO16,
 *   IO18, IO38} and routes to the device model registered for the ONE
 *   line that reads low (vboard_find_spi). The wire image handed to the
 *   model is a single flat MSB-first stream:
 *       [ command_bits of .cmd ][ address_bits of .addr ]
 *       [ dummy_bits of 1s — don't-care phase ][ `length` bits of data ]
 *   and the model fills the same-length rx image; the last `length`
 *   bits of it (the data phase) are what get copied out, honouring
 *   rxlength. Devices that answer only to their own framing see exactly
 *   what the real bus would have put on MOSI.
 *
 *   If the handle was added with spics_io_num >= 0 the IDF driver
 *   itself asserts that pin, so we route on it directly — the scan then
 *   only watches for extra manually-driven lines fighting it.
 *
 * SAFETY CHECKS (why this file exists)
 *   - EXACTLY-ONE-CS: with a shared MISO (and CS38 gating the U26 level
 *     shifter) the bus dies if zero lines (manual-CS handles only — the
 *     transfer floats to 0xFF) or two or more lines read low during a
 *     transfer. Every violation bumps g_vb_stats.spi_cs_violations and
 *     prints a VB-SPI-CS line. We still return ESP_OK and keep going —
 *     aborting would hide the evidence; the counter is the evidence.
 *   - CLOCK CAP: the first time a handle routes to a known CS, its
 *     cfg.clock_speed_hz is compared to the per-pin cap (IO38 8 MHz,
 *     IO10 4 MHz, IO15/18 8 MHz, IO16 40 MHz). Over-cap prints
 *     VB-SPI-SPEED and bumps g_spi_speed_violations (see spi_common.h).
 *     Done once per handle: it is a config error, not a per-byte one.
 *
 * FAULTS honoured from g_vb_faults: spi_bitflip_rate (one random bit in
 * the rx image), miso_fight (a phantom second driver ORs 0x55 in),
 * spi_stuck_cs (a device that answers even while its CS is idle —
 * OR'd in and counted as a MISO fight), spi_corrupt_id[] (forced first
 * rx byte, indexed by position in the CS table).
 *
 * STATS: every transfer (even a floating one) bumps spi_txns; every
 * wire byte bumps miso_bytes.
 *
 * Not modelled: DMA (spi_dma_chan_t is recorded, not used), real clock
 * stretching, LSB-first bit order, DIO/QIO/OCT multi-line phases, ISR
 * latency. C11, -Wall -Wextra clean.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "vboard.h"

/* ------------------------------------------------------------------ */
/*  Board truth — the five CS lines the shared-MISO rule applies to,   */
/*  and each one's clock ceiling (mirrors board_pins.h hard caps).     */
/* ------------------------------------------------------------------ */
typedef struct {
    int      cs_gpio;
    uint32_t cap_hz;
    const char *name;
} cs_map_t;

static const cs_map_t s_cs_map[] = {
    { .cs_gpio = 10, .cap_hz =  4u * 1000 * 1000, .name = "ADS1292"  },
    { .cs_gpio = 15, .cap_hz =  8u * 1000 * 1000, .name = "AD5940"   },
    { .cs_gpio = 16, .cap_hz = 40u * 1000 * 1000, .name = "W25Q512"  },
    { .cs_gpio = 18, .cap_hz =  8u * 1000 * 1000, .name = "AFE4900"  },
    { .cs_gpio = 38, .cap_hz =  8u * 1000 * 1000, .name = "MAX86141" },
};
#define CS_MAP_N    (sizeof(s_cs_map) / sizeof(s_cs_map[0]))

#define SIM_MAX_DEVS    8u      /* board has 5 SPI devices + headroom  */

uint64_t g_spi_speed_violations;

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */
struct spi_device_t {
    spi_host_device_t              host;
    spi_device_interface_config_t  cfg;
    bool                           in_use;
    uint32_t                       speed_checked;  /* bitmask of CS map
                                                      indices already
                                                      cap-checked — a
                                                      manual-CS handle's
                                                      pin isn't known
                                                      until routed     */
    spi_transaction_t             *done_trans;     /* last queued txn  */
};

static struct {
    bool             claimed;
    spi_bus_config_t cfg;
    spi_dma_chan_t   dma_chan;
} s_bus[SPI_HOST_MAX];

static struct spi_device_t s_devs[SIM_MAX_DEVS];

/* The real driver serialises on a per-device semaphore; the firmware
 * already owns a bus-wide mutex, and this one additionally keeps the
 * CS scan + model dispatch atomic if a second task ever sneaks in.    */
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */
static const cs_map_t *cs_lookup(int cs_gpio)
{
    for (size_t i = 0; i < CS_MAP_N; i++) {
        if (s_cs_map[i].cs_gpio == cs_gpio) {
            return &s_cs_map[i];
        }
    }
    return NULL;
}

static struct spi_device_t *dev_from_handle(spi_device_handle_t h)
{
    if (h == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < SIM_MAX_DEVS; i++) {
        if (&s_devs[i] == h) {
            return s_devs[i].in_use ? &s_devs[i] : NULL;
        }
    }
    return NULL;
}

/* Count + report CS lines currently low among the watched set.
 * Returns the count; when `low_out` is non-NULL it gets the last low
 * pin (meaningful only when count == 1).                             */
static int cs_low_count(int *low_out)
{
    int n = 0;
    int last = -1;
    for (size_t i = 0; i < CS_MAP_N; i++) {
        if (vboard_gpio_get(s_cs_map[i].cs_gpio) == 0) {
            n++;
            last = s_cs_map[i].cs_gpio;
        }
    }
    if (low_out != NULL) {
        *low_out = (n == 1) ? last : -1;
    }
    return n;
}

static void cs_violation(struct spi_device_t *d, int low_n)
{
    g_vb_stats.spi_cs_violations++;
    fprintf(stderr, "VB-SPI-CS: %d CS line(s) low during transfer "
            "(device cfg cs=%d)\n", low_n, d->cfg.spics_io_num);
}

/* Over-clock check: fires at add_device when the driver owns the CS,
 * else the first time a transfer routes to each CS — the earliest
 * moment a manual-CS pairing is actually known. Once per (handle, CS)
 * pair so a misroute to a tighter-capped pin still gets checked.       */
static void speed_check(struct spi_device_t *d, int cs_gpio)
{
    const cs_map_t *m = cs_lookup(cs_gpio);
    if (m == NULL) {
        return;                     /* unknown pin — nothing to cap   */
    }
    const uint32_t bit = 1u << (unsigned)(m - s_cs_map);
    if (d->speed_checked & bit) {
        return;
    }
    d->speed_checked |= bit;
    if (d->cfg.clock_speed_hz > (int)m->cap_hz) {
        g_spi_speed_violations++;
        fprintf(stderr, "VB-SPI-SPEED: dev requests %d Hz on CS%d "
                "(%s cap %u Hz)\n",
                d->cfg.clock_speed_hz, cs_gpio, m->name,
                (unsigned)m->cap_hz);
    }
}

/* MSB-first bit writer into a pre-sized buffer.                       */
static void put_bits(uint8_t *buf, size_t *bitpos, uint64_t val, unsigned nbits)
{
    for (unsigned i = 0; i < nbits; i++) {
        uint8_t mask = (uint8_t)(0x80u >> (*bitpos & 7u));
        if ((val >> (nbits - 1u - i)) & 1u) {
            buf[*bitpos >> 3] |= mask;
        } else {
            buf[*bitpos >> 3] &= (uint8_t)~mask;
        }
        (*bitpos)++;
    }
}

static void put_bytes(uint8_t *buf, size_t *bitpos,
                      const uint8_t *src, size_t nbits)
{
    if ((*bitpos & 7u) == 0u && (nbits & 7u) == 0u) {
        memcpy(&buf[*bitpos >> 3], src, nbits >> 3);
        *bitpos += nbits;
        return;
    }
    for (size_t i = 0; i < nbits; i++) {
        put_bits(buf, bitpos, (uint64_t)((src[i >> 3] >> (7u - (i & 7u))) & 1u), 1u);
    }
}

static void get_bytes(const uint8_t *buf, size_t *bitpos,
                      uint8_t *dst, size_t nbits)
{
    memset(dst, 0, (nbits + 7u) >> 3);
    for (size_t i = 0; i < nbits; i++) {
        if ((buf[(*bitpos + i) >> 3] >> (7u - ((*bitpos + i) & 7u))) & 1u) {
            dst[i >> 3] |= (uint8_t)(0x80u >> (i & 7u));
        }
    }
    *bitpos += nbits;
}

/* ------------------------------------------------------------------ */
/*  Bus API                                                            */
/* ------------------------------------------------------------------ */
esp_err_t spi_bus_initialize(spi_host_device_t host_id,
                             const spi_bus_config_t *bus_config,
                             spi_dma_chan_t dma_chan)
{
    if (host_id < 0 || host_id >= SPI_HOST_MAX || bus_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_lock);
    if (s_bus[host_id].claimed) {
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_bus[host_id].cfg      = *bus_config;
    s_bus[host_id].dma_chan = dma_chan;
    s_bus[host_id].claimed  = true;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t spi_bus_free(spi_host_device_t host_id)
{
    if (host_id < 0 || host_id >= SPI_HOST_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_lock);
    if (!s_bus[host_id].claimed) {
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < SIM_MAX_DEVS; i++) {
        if (s_devs[i].in_use && s_devs[i].host == host_id) {
            pthread_mutex_unlock(&s_lock);   /* devices still attached */
            return ESP_ERR_INVALID_STATE;
        }
    }
    s_bus[host_id].claimed = false;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t spi_bus_add_device(spi_host_device_t host_id,
                             const spi_device_interface_config_t *dev_config,
                             spi_device_handle_t *handle)
{
    if (host_id < 0 || host_id >= SPI_HOST_MAX ||
        dev_config == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dev_config->queue_size <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_lock);
    if (!s_bus[host_id].claimed) {
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    struct spi_device_t *slot = NULL;
    for (size_t i = 0; i < SIM_MAX_DEVS; i++) {
        if (!s_devs[i].in_use) {
            slot = &s_devs[i];
            break;
        }
    }
    if (slot == NULL) {
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    memset(slot, 0, sizeof(*slot));
    slot->host   = host_id;
    slot->cfg    = *dev_config;
    slot->in_use = true;
    *handle      = slot;
    /* Driver-managed CS: the pin is known now — check the cap today. */
    if (dev_config->spics_io_num >= 0) {
        speed_check(slot, dev_config->spics_io_num);
    }
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t spi_bus_remove_device(spi_device_handle_t handle)
{
    pthread_mutex_lock(&s_lock);
    struct spi_device_t *d = dev_from_handle(handle);
    if (d == NULL) {
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    d->in_use = false;
    d->done_trans = NULL;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  The transfer engine — both transmit entry points land here.        */
/*  Caller holds NO lock; we take s_lock for the whole dispatch so the  */
/*  CS scan and the model call are one atomic "bus transaction".        */
/* ------------------------------------------------------------------ */
static esp_err_t do_transmit(struct spi_device_t *d, spi_transaction_t *t)
{
    /* ---- phase widths: dev defaults, VARIABLE_* overrides ---------- */
    unsigned cmd_bits   = d->cfg.command_bits;
    unsigned addr_bits  = d->cfg.address_bits;
    unsigned dummy_bits = d->cfg.dummy_bits;
    if (t->flags & (SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
                    SPI_TRANS_VARIABLE_DUMMY)) {
        spi_transaction_ext_t *e = (spi_transaction_ext_t *)t;
        if (t->flags & SPI_TRANS_VARIABLE_CMD)   cmd_bits   = e->command_bits;
        if (t->flags & SPI_TRANS_VARIABLE_ADDR)  addr_bits  = e->address_bits;
        if (t->flags & SPI_TRANS_VARIABLE_DUMMY) dummy_bits = e->dummy_bits;
    }
    if (d->cfg.flags & SPI_DEVICE_NO_DUMMY) {
        dummy_bits = 0;
    }

    const size_t rx_bits = (t->rxlength != 0) ? t->rxlength : t->length;
    if (rx_bits > t->length) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((t->flags & SPI_TRANS_USE_TXDATA) && t->length > 32u) {
        return ESP_ERR_INVALID_ARG;          /* doesn't fit tx_data[4] */
    }
    if ((t->flags & SPI_TRANS_USE_RXDATA) && rx_bits > 32u) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t header_bits  = (size_t)cmd_bits + addr_bits + dummy_bits;
    const size_t total_bits   = header_bits + t->length;
    const size_t total_bytes  = (total_bits + 7u) >> 3;
    const size_t rx_out_bytes = (rx_bits + 7u) >> 3;

    pthread_mutex_lock(&s_lock);
    if (!d->in_use) {               /* removed between lookup and lock */
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_INVALID_ARG;
    }

    /* ---- CS arbitration: who is actually selected ----------------- */
    int low_pin = -1;
    const int low_n = cs_low_count(&low_pin);
    int sel_cs;
    if (d->cfg.spics_io_num >= 0) {
        /* Driver owns this CS — it asserts the pin itself, so zero low
         * lines is normal. Any manually-asserted line besides ours is
         * still a shared-MISO fight.                                */
        sel_cs = d->cfg.spics_io_num;
        const bool extra = (low_n > 1) || (low_n == 1 && low_pin != sel_cs);
        if (extra) {
            cs_violation(d, low_n);
        }
    } else {
        /* Firmware drives CS by hand: exactly one line must be low.  */
        sel_cs = low_pin;
        if (low_n != 1) {
            cs_violation(d, low_n);
        }
    }
    if (sel_cs >= 0) {
        speed_check(d, sel_cs);
    }

    /* ---- build the wire image -------------------------------------- */
    uint8_t *tx_img = NULL;
    uint8_t *rx_img = NULL;
    if (total_bytes > 0) {
        tx_img = malloc(total_bytes);
        rx_img = malloc(total_bytes);
        if (tx_img == NULL || rx_img == NULL) {
            free(tx_img);
            free(rx_img);
            pthread_mutex_unlock(&s_lock);
            return ESP_ERR_NO_MEM;
        }
        memset(tx_img, 0xFF, total_bytes);   /* idle-high MOSI + holes */
        memset(rx_img, 0xFF, total_bytes);   /* MISO floats high       */
        size_t pos = 0;
        put_bits(tx_img, &pos, t->cmd, cmd_bits);
        put_bits(tx_img, &pos, t->addr, addr_bits);
        pos += dummy_bits;                   /* dummy = don't care     */
        if (t->length > 0) {
            const uint8_t *src =
                (t->flags & SPI_TRANS_USE_TXDATA) ? t->tx_data
                                                  : (const uint8_t *)t->tx_buffer;
            if (src != NULL) {
                put_bytes(tx_img, &pos, src, t->length);
            } else {
                pos += t->length;            /* read leg: MOSI stays FF */
            }
        }
    }

    if (d->cfg.pre_cb != NULL) {
        d->cfg.pre_cb(t);
    }

    /* ---- dispatch: selected device + any stuck-CS eavesdropper ------ */
    vboard_dev_t *sel = (sel_cs >= 0) ? vboard_find_spi(sel_cs) : NULL;
    vboard_dev_t *stuck = NULL;
    if (g_vb_faults.spi_stuck_cs != 0 &&
        g_vb_faults.spi_stuck_cs != sel_cs) {
        stuck = vboard_find_spi(g_vb_faults.spi_stuck_cs);
    }

    esp_err_t err = ESP_OK;
    int drivers = 0;
    if (total_bytes > 0 && (sel != NULL || stuck != NULL)) {
        uint8_t *tmp = malloc(total_bytes);
        if (tmp == NULL) {
            err = ESP_ERR_NO_MEM;
        } else {
            vboard_dev_t *responders[2] = { sel, stuck };
            for (size_t i = 0; i < 2; i++) {
                vboard_dev_t *dev = responders[i];
                if (dev == NULL || dev->spi_xfer == NULL) {
                    continue;
                }
                esp_err_t e = dev->spi_xfer(dev, tx_img, tmp, total_bytes);
                if (e != ESP_OK && err == ESP_OK) {
                    err = e;
                }
                for (size_t b = 0; b < total_bytes; b++) {
                    /* MISO floats high; any driver pulls a bit low, so
                     * contended bytes are the AND of all responders.  */
                    rx_img[b] &= tmp[b];
                }
                drivers++;
            }
            free(tmp);
        }
    }
    if (sel_cs >= 0 && sel == NULL && total_bytes > 0) {
        fprintf(stderr, "VB-SPI-NODEV: no model on CS%d — MISO floats\n",
                sel_cs);
    }

    /* miso_fight: a phantom second driver ORs 0x55 onto the wire.      */
    bool fought = (drivers > 1);
    if (g_vb_faults.miso_fight && total_bytes > 0) {
        for (size_t b = 0; b < total_bytes; b++) {
            rx_img[b] |= 0x55u;
        }
        fought = true;
    }
    if (fought) {
        g_vb_stats.miso_fights++;
    }

    /* ---- deliver rx phase to the firmware's buffer ------------------ */
    uint8_t *rx_dst = NULL;
    if (rx_bits > 0) {
        rx_dst = (t->flags & SPI_TRANS_USE_RXDATA) ? t->rx_data
                                                 : (uint8_t *)t->rx_buffer;
    }
    if (rx_dst != NULL && total_bytes > 0) {
        if ((header_bits & 7u) == 0u && (rx_bits & 7u) == 0u) {
            memcpy(rx_dst, &rx_img[header_bits >> 3], rx_out_bytes);
        } else {
            size_t pos = header_bits;
            get_bytes(rx_img, &pos, rx_dst, rx_bits);
        }

        /* Faults that land on what the firmware reads.                */
        const cs_map_t *m = (sel_cs >= 0) ? cs_lookup(sel_cs) : NULL;
        const int cidx = (m != NULL) ? (int)(m - s_cs_map) : -1;
        if (cidx >= 0 && cidx < (int)sizeof(g_vb_faults.spi_corrupt_id) &&
            g_vb_faults.spi_corrupt_id[cidx] != 0 && rx_out_bytes > 0) {
            rx_dst[0] = g_vb_faults.spi_corrupt_id[cidx];
        }
        if (g_vb_faults.spi_bitflip_rate > 0.0f &&
            rx_bits > 0 &&
            (float)rand() / (float)RAND_MAX < g_vb_faults.spi_bitflip_rate) {
            const size_t bit = (size_t)rand() % rx_bits;
            rx_dst[bit >> 3] ^= (uint8_t)(0x80u >> (bit & 7u));
        }
    }

    if (d->cfg.post_cb != NULL) {
        d->cfg.post_cb(t);
    }

    g_vb_stats.spi_txns++;
    g_vb_stats.miso_bytes += total_bytes;

    free(tx_img);
    free(rx_img);
    pthread_mutex_unlock(&s_lock);
    return err;
}

/* ------------------------------------------------------------------ */
/*  Transaction entry points                                           */
/* ------------------------------------------------------------------ */
esp_err_t spi_device_transmit(spi_device_handle_t handle,
                              spi_transaction_t *trans_desc)
{
    if (trans_desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_lock);
    struct spi_device_t *d = dev_from_handle(handle);
    pthread_mutex_unlock(&s_lock);
    if (d == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return do_transmit(d, trans_desc);
}

/* Polling is identical in the sim — the model answers synchronously.  */
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,
                                      spi_transaction_t *trans_desc)
{
    return spi_device_transmit(handle, trans_desc);
}

esp_err_t spi_device_queue_trans(spi_device_handle_t handle,
                                 spi_transaction_t *trans_desc,
                                 TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (trans_desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_lock);
    struct spi_device_t *d = dev_from_handle(handle);
    pthread_mutex_unlock(&s_lock);
    if (d == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = do_transmit(d, trans_desc);
    if (err == ESP_OK) {
        pthread_mutex_lock(&s_lock);
        d->done_trans = trans_desc;
        pthread_mutex_unlock(&s_lock);
    }
    return err;
}

esp_err_t spi_device_get_trans_result(spi_device_handle_t handle,
                                      spi_transaction_t **trans_desc,
                                      TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (trans_desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_lock);
    struct spi_device_t *d = dev_from_handle(handle);
    if (d == NULL || d->done_trans == NULL) {
        pthread_mutex_unlock(&s_lock);
        return (d == NULL) ? ESP_ERR_INVALID_ARG : ESP_ERR_TIMEOUT;
    }
    *trans_desc = d->done_trans;
    d->done_trans = NULL;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t spi_device_polling_start(spi_device_handle_t handle,
                                   spi_transaction_t *trans_desc,
                                   TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    return spi_device_transmit(handle, trans_desc);
}

esp_err_t spi_device_polling_end(spi_device_handle_t handle,
                                 TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    pthread_mutex_lock(&s_lock);
    struct spi_device_t *d = dev_from_handle(handle);
    pthread_mutex_unlock(&s_lock);
    return (d == NULL) ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t spi_device_acquire_bus(spi_device_handle_t device, TickType_t wait)
{
    (void)wait;
    pthread_mutex_lock(&s_lock);
    struct spi_device_t *d = dev_from_handle(device);
    pthread_mutex_unlock(&s_lock);
    /* Bus exclusion already lives in the firmware's own mutex.         */
    return (d == NULL) ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t spi_device_release_bus(spi_device_handle_t device)
{
    pthread_mutex_lock(&s_lock);
    struct spi_device_t *d = dev_from_handle(device);
    pthread_mutex_unlock(&s_lock);
    return (d == NULL) ? ESP_ERR_INVALID_ARG : ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Misc API                                                           */
/* ------------------------------------------------------------------ */
esp_err_t spi_bus_get_max_transaction_len(spi_host_device_t host_id,
                                          size_t *max_transfer_sz)
{
    if (host_id < 0 || host_id >= SPI_HOST_MAX || max_transfer_sz == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_lock);
    if (!s_bus[host_id].claimed) {
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    *max_transfer_sz = (size_t)s_bus[host_id].cfg.max_transfer_sz;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t spi_device_get_actual_freq(spi_device_handle_t handle, int *freq_khz)
{
    if (freq_khz == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_lock);
    struct spi_device_t *d = dev_from_handle(handle);
    pthread_mutex_unlock(&s_lock);
    if (d == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *freq_khz = d->cfg.clock_speed_hz / 1000;
    return ESP_OK;
}

int spi_get_actual_clock(int fapb, int hz, int duty_p)
{
    (void)fapb;
    (void)duty_p;
    return hz;               /* the virtual PLL grants every request   */
}

esp_err_t spi_get_timing(bool gpio_is_used, int input_delay_ns,
                         int eff_clk, int *dummy_o, int *miso_delay_o)
{
    (void)gpio_is_used;
    (void)eff_clk;
    if (dummy_o != NULL) {
        *dummy_o = 0;
    }
    if (miso_delay_o != NULL) {
        *miso_delay_o = input_delay_ns;
    }
    return ESP_OK;
}

uint64_t vboard_spi_speed_violations(void)
{
    return g_spi_speed_violations;
}
