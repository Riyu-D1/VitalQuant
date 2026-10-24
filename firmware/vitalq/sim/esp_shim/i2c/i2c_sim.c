/* i2c_sim.c — vboard-backed I2C master for the host sim (C11).
 *
 * Implements driver/i2c_master.h (ESP-IDF >= 5.2 API) against the
 * virtual board: every transaction is gated by the injected vboard
 * faults, accounted in g_vb_stats, billed virtual wire-time on the
 * vboard clock, then dispatched to the device model registered under
 * that 7-bit address via vboard_find_i2c().
 *
 *   transmit           -> model->i2c_write(d, buf, len)   (buf[0] = reg)
 *   receive            -> model->i2c_read (d, buf, len)
 *   transmit_receive   -> i2c_write then i2c_read (repeated-START pair)
 *   probe              -> presence check only (address-phase ACK/NACK)
 *
 * Error-code semantics vs real silicon (documented contract):
 *   - Device absent or silenced   : ESP_ERR_TIMEOUT.  On real hardware an
 *     address-phase NACK during transmit/receive fails the transaction;
 *     i2c_master_probe is the one place IDF distinguishes
 *     ESP_ERR_NOT_FOUND — this sim reports ESP_ERR_TIMEOUT uniformly so
 *     "did not ACK" is one testable class.  The firmware only checks
 *     != ESP_OK, so scan/probe behaviour is preserved.
 *   - Random-NACK / wedged bus    : ESP_FAIL (generic transfer abort).
 *   - Wire time > xfer_timeout_ms : ESP_ERR_TIMEOUT (capped wait).
 *   - flags.disable_ack_check     : NACKs are ignored on the wire — a
 *     missing device does NOT fail a write (same as real silicon).
 *   - Missing device NEVER crashes; absent -> clean error return.
 */
#define _DEFAULT_SOURCE 1        /* rand_r() under strict -std=c11       */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "driver/i2c_master.h"
#include "vboard.h"

/* Real esp_err values.  If the core esp_err shim header exists it was
 * already pulled in by i2c_master.h and these guards all skip; they
 * only fire while the shim tree is still being built out.            */
#ifndef ESP_OK
#define ESP_OK                 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL               (-1)
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM         0x101
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG    0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE  0x103
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND      0x105
#endif
#ifndef ESP_ERR_TIMEOUT
#define ESP_ERR_TIMEOUT        0x107
#endif

/* fix_v22 hard cap: 2.2k pull-ups, tr ~503 ns — see components/vq_i2c. */
#define VB_I2C_CAP_HZ        100000u
#define VB_I2C_ADDR_MAX_7BIT 0x7Fu
#define VB_I2C_ADDR_MAX_10BIT 0x3FFu

#define BUS_MAGIC 0x49434255u   /* 'ICBU' */
#define DEV_MAGIC 0x49434456u   /* 'ICDV' */

struct i2c_master_dev_t {
    uint32_t                 magic;
    bool                     alive;   /* tombstoned by rm_device        */
    i2c_master_bus_handle_t  bus;
    uint16_t                 addr;
    uint32_t                 scl_speed_hz;
    uint8_t                  addr_len;  /* i2c_addr_bit_len_t           */
    uint8_t                  no_ack_check;
    i2c_master_callback_t    on_trans_done;
    void                    *cb_ctx;
    struct i2c_master_dev_t *next;
};

struct i2c_master_bus_t {
    uint32_t                 magic;
    bool                     alive;
    i2c_port_num_t           port;
    int                      sda_io_num, scl_io_num;
    i2c_clock_source_t       clk_source;
    uint8_t                  glitch_ignore_cnt;
    i2c_master_dev_handle_t  devs;   /* includes tombstones until del  */
};

/* Port -> bus registry (for i2c_master_get_bus_handle) + device-list
 * integrity.  The firmware serialises all transfers on its own mutex;
 * this lock only guards handle-table mutations vs the tick threads.  */
static pthread_mutex_t            s_reg_lock = PTHREAD_MUTEX_INITIALIZER;
static i2c_master_bus_handle_t    s_buses[I2C_NUM_MAX];

#ifdef SIM_HOST_BUILD
uint64_t g_i2c_speed_violations;

uint64_t vboard_i2c_speed_violations(void)
{
    return __atomic_load_n(&g_i2c_speed_violations, __ATOMIC_RELAXED);
}
#endif /* SIM_HOST_BUILD */

/* ---- deterministic fault-injection RNG ---------------------------------
 * rand_r() on a per-thread seed: reproducible within a scenario, immune
 * to races on the shared bus.  VSIM_I2C_SEED (env) overrides the base.  */
static unsigned int *seedp(void)
{
    static _Thread_local unsigned int t_seed;
    if (t_seed == 0) {
        const char *e = getenv("VSIM_I2C_SEED");
        unsigned int base = (e != NULL) ? (unsigned int)strtoul(e, NULL, 0)
                                        : 0x1CE5EEDu;
        t_seed = base ^ (unsigned int)(uintptr_t)pthread_self() ^ 0x9E3779B9u;
        if (t_seed == 0) {
            t_seed = 1u;
        }
    }
    return &t_seed;
}

static float nack_roll(void)
{
    return (float)rand_r(seedp()) / (float)RAND_MAX;
}

/* ---- virtual wire time -------------------------------------------------
 * One byte on the wire = 9 bit-times (8 data + ACK).  Each frame adds
 * one address byte and ~4 bit-times of START/STOP/turnaround overhead,
 * billed at the device's SCL speed so fast-forward stays proportional.  */
static int64_t wire_us(uint32_t speed_hz, size_t payload_bytes, int n_frames)
{
    uint64_t bits;
    if (speed_hz == 0 || speed_hz > VB_I2C_CAP_HZ) {
        speed_hz = VB_I2C_CAP_HZ;   /* wire physically caps at 100 kHz  */
    }
    bits = ((uint64_t)payload_bytes + (uint64_t)n_frames) * 9u
           + 4u * (uint64_t)n_frames;
    return (int64_t)(bits * 1000000ull / (uint64_t)speed_hz);
}

/* Sleep scaled virtual time.  Returns false when the full latency did
 * not fit inside xfer_timeout_ms (-1 = wait forever).                  */
static bool clock_wait(int64_t virt_us, int xfer_timeout_ms)
{
    if (xfer_timeout_ms >= 0 &&
        virt_us > (int64_t)xfer_timeout_ms * 1000) {
        vboard_clock_sleep_us((int64_t)xfer_timeout_ms * 1000);
        return false;
    }
    vboard_clock_sleep_us(virt_us);
    return true;
}

static void count_txn(void)
{
    __atomic_fetch_add(&g_vb_stats.i2c_txns, 1u, __ATOMIC_RELAXED);
}

static void count_nack(void)
{
    __atomic_fetch_add(&g_vb_stats.i2c_nacks, 1u, __ATOMIC_RELAXED);
}

/* Fault gate shared by every wire transaction (probe included — a probe
 * is a wire transaction too).  Counts the transaction, applies the
 * injected fault set, returns ESP_OK when the address phase may run.   */
static esp_err_t wire_gate(uint8_t addr7, bool ack_check, int xfer_timeout_ms)
{
    int64_t addr_phase_us = wire_us(VB_I2C_CAP_HZ, 0, 1);
    float rate;

    count_txn();

    /* Bus wedged: nothing clocks out — burn the caller's whole timeout
     * (bounded; with -1 "forever" the address phase still returns).    */
    if (g_vb_faults.i2c_sda_stuck_low) {
        if (xfer_timeout_ms >= 0) {
            vboard_clock_sleep_us((int64_t)xfer_timeout_ms * 1000);
        } else {
            vboard_clock_sleep_us(addr_phase_us);
        }
        return ESP_FAIL;
    }
    if (!ack_check) {
        return ESP_OK;              /* NACKs are invisible on the wire  */
    }
    if (g_vb_faults.i2c_silence_addr != 0 &&
        addr7 == g_vb_faults.i2c_silence_addr) {
        count_nack();
        vboard_clock_sleep_us(addr_phase_us);
        return ESP_ERR_TIMEOUT;     /* this address NACKs forever       */
    }
    rate = g_vb_faults.i2c_nack_rate;
    if (rate > 0.0f && nack_roll() < rate) {
        count_nack();
        vboard_clock_sleep_us(addr_phase_us);
        return ESP_FAIL;            /* random mid-frame NACK storm      */
    }
    return ESP_OK;
}

/* Handle validation.  Distinguishes "not our handle" (INVALID_ARG) from
 * a tombstoned device or deleted bus (INVALID_STATE) so stale-handle
 * firmware bugs surface as clean errors, never crashes.               */
static esp_err_t dev_check(i2c_master_dev_handle_t dev)
{
    if (dev == NULL || dev->magic != DEV_MAGIC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!dev->alive || dev->bus == NULL ||
        dev->bus->magic != BUS_MAGIC || !dev->bus->alive) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t bus_check(i2c_master_bus_handle_t bus)
{
    if (bus == NULL || bus->magic != BUS_MAGIC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bus->alive) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void fire_cb(i2c_master_dev_handle_t dev, i2c_master_event_t ev)
{
    if (dev->on_trans_done != NULL) {
        const i2c_master_event_data_t d = { .event = ev };
        (void)dev->on_trans_done(dev, &d, dev->cb_ctx);
    }
}

/* One wire transaction against the device model at dev->addr.
 * wlen/rlen 0 means "phase absent"; both 0 = address-only probe frame.
 * The fault gate has already run; this does presence + dispatch.      */
static esp_err_t dispatch(i2c_master_dev_handle_t dev,
                          const uint8_t *wbuf, size_t wlen,
                          uint8_t *rbuf, size_t rlen,
                          int xfer_timeout_ms)
{
    vboard_dev_t *d = vboard_find_i2c((uint8_t)dev->addr);
    int n_frames = (wlen > 0 ? 1 : 0) + (rlen > 0 ? 1 : 0);
    int64_t t;
    esp_err_t err;

    if (d == NULL) {
        /* Nobody ACKs this address.  With ACK check off the write goes
         * into the void and the master still reports success (real
         * silicon behaves exactly this way); a read returns 0xFFs — the
         * bus pull-ups hold SDA high while nobody drives it.           */
        if (dev->no_ack_check) {
            if (rbuf != NULL && rlen > 0) {
                memset(rbuf, 0xFF, rlen);
            }
            (void)clock_wait(wire_us(dev->scl_speed_hz,
                                     wlen + rlen, n_frames ? n_frames : 1),
                             xfer_timeout_ms);
            return ESP_OK;
        }
        count_nack();
        (void)clock_wait(wire_us(VB_I2C_CAP_HZ, 0, 1), xfer_timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    if (n_frames == 0) {
        n_frames = 1;               /* address-only frame               */
    }
    t = wire_us(dev->scl_speed_hz, wlen + rlen, n_frames);
    if (!clock_wait(t, xfer_timeout_ms)) {
        return ESP_ERR_TIMEOUT;     /* master aborted mid-frame         */
    }

    if (wlen > 0) {
        if (d->i2c_write == NULL) {
            return ESP_FAIL;        /* model bug: device can't be written */
        }
        err = d->i2c_write(d, wbuf, wlen);   /* wbuf[0] = register      */
        if (err != ESP_OK) {
            return err;             /* model-level NACK/error flows up  */
        }
    }
    if (rlen > 0) {
        if (d->i2c_read == NULL) {
            return ESP_FAIL;        /* model bug: device can't be read  */
        }
        return d->i2c_read(d, rbuf, rlen);
    }
    return ESP_OK;
}

/* ---- bus lifecycle ---------------------------------------------------- */

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                             i2c_master_bus_handle_t *ret_bus_handle)
{
    i2c_master_bus_handle_t bus;

    if (bus_config == NULL || ret_bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bus_config->i2c_port < 0 ||
        bus_config->i2c_port >= I2C_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    bus = calloc(1, sizeof(*bus));
    if (bus == NULL) {
        return ESP_ERR_NO_MEM;
    }
    bus->magic             = BUS_MAGIC;
    bus->alive             = true;
    bus->port              = bus_config->i2c_port;
    bus->sda_io_num        = bus_config->sda_io_num;
    bus->scl_io_num        = bus_config->scl_io_num;
    bus->clk_source        = bus_config->clk_source;
    bus->glitch_ignore_cnt = bus_config->glitch_ignore_cnt;

    pthread_mutex_lock(&s_reg_lock);
    if (s_buses[bus->port] != NULL) {
        pthread_mutex_unlock(&s_reg_lock);
        bus->magic = 0;
        free(bus);
        return ESP_ERR_INVALID_STATE;   /* port already has a live bus */
    }
    s_buses[bus->port] = bus;
    pthread_mutex_unlock(&s_reg_lock);

    *ret_bus_handle = bus;
    return ESP_OK;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle)
{
    i2c_master_dev_handle_t d, next;

    if (bus_handle == NULL || bus_handle->magic != BUS_MAGIC) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_reg_lock);
    for (d = bus_handle->devs; d != NULL; d = next) {
        next = d->next;
        d->magic = 0;
        free(d);
    }
    if (bus_handle->port >= 0 && bus_handle->port < I2C_NUM_MAX &&
        s_buses[bus_handle->port] == bus_handle) {
        s_buses[bus_handle->port] = NULL;
    }
    pthread_mutex_unlock(&s_reg_lock);
    bus_handle->alive = false;
    bus_handle->magic = 0;
    free(bus_handle);
    return ESP_OK;
}

/* ---- device lifecycle -------------------------------------------------- */

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                    const i2c_device_config_t *dev_config,
                                    i2c_master_dev_handle_t *ret_handle)
{
    i2c_master_dev_handle_t dev;
    esp_err_t err = bus_check(bus_handle);
    uint16_t max_addr;

    if (err != ESP_OK) {
        return err;
    }
    if (dev_config == NULL || ret_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dev_config->dev_addr_length != I2C_ADDR_BIT_LEN_7 &&
        dev_config->dev_addr_length != I2C_ADDR_BIT_LEN_10) {
        return ESP_ERR_INVALID_ARG;
    }
    max_addr = (dev_config->dev_addr_length == I2C_ADDR_BIT_LEN_7)
               ? VB_I2C_ADDR_MAX_7BIT : VB_I2C_ADDR_MAX_10BIT;
    if (dev_config->device_address != I2C_DEVICE_ADDRESS_NOT_USED &&
        dev_config->device_address > max_addr) {
        return ESP_ERR_INVALID_ARG;
    }

    /* fix_v22 100 kHz hard cap: warn + count, but the device is still
     * created — exceeding the budget is a schematic violation, not an
     * API error (the physical pull-ups are what fails on real HW).    */
    if (dev_config->scl_speed_hz > VB_I2C_CAP_HZ) {
        fprintf(stderr,
                "VB-I2C-SPEED: dev 0x%02x scl_speed_hz=%lu exceeds the "
                "fix_v22 100 kHz hard cap\n",
                (unsigned int)dev_config->device_address,
                (unsigned long)dev_config->scl_speed_hz);
#ifdef SIM_HOST_BUILD
        __atomic_fetch_add(&g_i2c_speed_violations, 1u, __ATOMIC_RELAXED);
#endif
    }

    dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }
    dev->magic        = DEV_MAGIC;
    dev->alive        = true;
    dev->bus          = bus_handle;
    dev->addr         = dev_config->device_address;
    dev->scl_speed_hz = dev_config->scl_speed_hz;
    dev->addr_len     = (uint8_t)dev_config->dev_addr_length;
    dev->no_ack_check = dev_config->flags.disable_ack_check ? 1u : 0u;

    pthread_mutex_lock(&s_reg_lock);
    dev->next       = bus_handle->devs;
    bus_handle->devs = dev;
    pthread_mutex_unlock(&s_reg_lock);

    *ret_handle = dev;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle)
{
    if (handle == NULL || handle->magic != DEV_MAGIC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!handle->alive) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Tombstone, don't free: a stale firmware handle then fails
     * dev_check() cleanly instead of dereferencing freed memory.
     * The node is reclaimed by i2c_del_master_bus.                    */
    handle->alive = false;
    return ESP_OK;
}

esp_err_t i2c_master_del_device(i2c_master_dev_handle_t handle)
{
    return i2c_master_bus_rm_device(handle);
}

esp_err_t i2c_master_bus_rm_all_device(i2c_master_bus_handle_t bus_handle)
{
    i2c_master_dev_handle_t d;
    esp_err_t err = bus_check(bus_handle);

    if (err != ESP_OK) {
        return err;
    }
    pthread_mutex_lock(&s_reg_lock);
    for (d = bus_handle->devs; d != NULL; d = d->next) {
        d->alive = false;
    }
    pthread_mutex_unlock(&s_reg_lock);
    return ESP_OK;
}

/* ---- transactions ------------------------------------------------------- */

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev,
                              const uint8_t *write_buffer,
                              size_t write_size, int xfer_timeout_ms)
{
    esp_err_t err = dev_check(i2c_dev);

    if (err != ESP_OK) {
        return err;
    }
    if (write_buffer == NULL && write_size > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    err = wire_gate((uint8_t)i2c_dev->addr,
                    !i2c_dev->no_ack_check, xfer_timeout_ms);
    if (err == ESP_OK) {
        err = dispatch(i2c_dev, write_buffer, write_size,
                       NULL, 0, xfer_timeout_ms);
    }
    fire_cb(i2c_dev, (err == ESP_OK) ? I2C_EVENT_DONE : I2C_EVENT_NACK);
    return err;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t i2c_dev,
                             uint8_t *read_buffer, size_t read_size,
                             int xfer_timeout_ms)
{
    esp_err_t err = dev_check(i2c_dev);

    if (err != ESP_OK) {
        return err;
    }
    if (read_buffer == NULL || read_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    err = wire_gate((uint8_t)i2c_dev->addr,
                    !i2c_dev->no_ack_check, xfer_timeout_ms);
    if (err == ESP_OK) {
        err = dispatch(i2c_dev, NULL, 0,
                       read_buffer, read_size, xfer_timeout_ms);
    }
    fire_cb(i2c_dev, (err == ESP_OK) ? I2C_EVENT_DONE : I2C_EVENT_NACK);
    return err;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev,
                                      const uint8_t *write_buffer,
                                      size_t write_size,
                                      uint8_t *read_buffer,
                                      size_t read_size,
                                      int xfer_timeout_ms)
{
    esp_err_t err = dev_check(i2c_dev);

    if (err != ESP_OK) {
        return err;
    }
    if ((write_buffer == NULL && write_size > 0) ||
        read_buffer == NULL || read_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* One START..STOP transaction (repeated START between phases): the
     * fault gate and the txn counter run once.                        */
    err = wire_gate((uint8_t)i2c_dev->addr,
                    !i2c_dev->no_ack_check, xfer_timeout_ms);
    if (err == ESP_OK) {
        err = dispatch(i2c_dev, write_buffer, write_size,
                       read_buffer, read_size, xfer_timeout_ms);
    }
    fire_cb(i2c_dev, (err == ESP_OK) ? I2C_EVENT_DONE : I2C_EVENT_NACK);
    return err;
}

esp_err_t i2c_master_multi_buffer_transmit(
    i2c_master_dev_handle_t i2c_dev,
    i2c_master_transmit_multi_buffer_info_t *buffer_info_array,
    size_t array_size, int xfer_timeout_ms)
{
    esp_err_t err = dev_check(i2c_dev);
    size_t total = 0, i;
    uint8_t *stage, *p;

    if (err != ESP_OK) {
        return err;
    }
    if (buffer_info_array == NULL || array_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (i = 0; i < array_size; i++) {
        if (buffer_info_array[i].write_buffer == NULL &&
            buffer_info_array[i].buffer_size > 0) {
            return ESP_ERR_INVALID_ARG;
        }
        total += buffer_info_array[i].buffer_size;
    }
    stage = (total > 0) ? malloc(total) : NULL;
    if (total > 0 && stage == NULL) {
        return ESP_ERR_NO_MEM;
    }
    /* Coalesce into ONE wire frame so the device model sees a single
     * contiguous [reg|payload] transaction like real hardware.        */
    for (p = stage, i = 0; i < array_size; i++) {
        if (buffer_info_array[i].buffer_size > 0) {
            memcpy(p, buffer_info_array[i].write_buffer,
                   buffer_info_array[i].buffer_size);
            p += buffer_info_array[i].buffer_size;
        }
    }
    err = wire_gate((uint8_t)i2c_dev->addr,
                    !i2c_dev->no_ack_check, xfer_timeout_ms);
    if (err == ESP_OK) {
        err = dispatch(i2c_dev, stage, total, NULL, 0, xfer_timeout_ms);
    }
    free(stage);
    fire_cb(i2c_dev, (err == ESP_OK) ? I2C_EVENT_DONE : I2C_EVENT_NACK);
    return err;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle,
                           uint16_t address, int xfer_timeout_ms)
{
    esp_err_t err = bus_check(bus_handle);
    vboard_dev_t *d;

    if (err != ESP_OK) {
        return err;
    }
    if (address > VB_I2C_ADDR_MAX_7BIT) {
        return ESP_ERR_INVALID_ARG;
    }
    err = wire_gate((uint8_t)address, true /* probes always ACK-check */,
                    xfer_timeout_ms);
    if (err != ESP_OK) {
        return err;
    }
    d = vboard_find_i2c((uint8_t)address);
    if (d == NULL) {
        count_nack();
        (void)clock_wait(wire_us(VB_I2C_CAP_HZ, 0, 1), xfer_timeout_ms);
        return ESP_ERR_TIMEOUT;     /* address-phase NACK — see header  */
    }
    /* A real probe transfers no data — the device decides ACK purely on
     * reachability.  A registered but rail-dead model must NACK too, so
     * run the bare write leg (len=0) that every model treats as the
     * address-phase check.                                             */
    if (d->i2c_write != NULL &&
            d->i2c_write(d, NULL, 0) == VB_ESP_ERR_TIMEOUT) {
        count_nack();
        (void)clock_wait(wire_us(VB_I2C_CAP_HZ, 0, 1), xfer_timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    (void)clock_wait(wire_us(VB_I2C_CAP_HZ, 0, 1), xfer_timeout_ms);
    return ESP_OK;
}

esp_err_t i2c_master_execute_defined_operations(
    i2c_master_dev_handle_t i2c_dev, i2c_operation_job_t *i2c_operation,
    size_t operation_list_num, int xfer_timeout_ms)
{
    esp_err_t err = dev_check(i2c_dev);
    vboard_dev_t *d;
    size_t i;

    if (err != ESP_OK) {
        return err;
    }
    if (i2c_operation == NULL || operation_list_num == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    err = wire_gate((uint8_t)i2c_dev->addr,
                    !i2c_dev->no_ack_check, xfer_timeout_ms);
    if (err != ESP_OK) {
        return err;
    }
    d = vboard_find_i2c((uint8_t)i2c_dev->addr);
    if (d == NULL) {
        if (i2c_dev->no_ack_check) {
            return ESP_OK;
        }
        count_nack();
        return ESP_ERR_TIMEOUT;
    }
    for (i = 0; i < operation_list_num; i++) {
        const i2c_operation_job_t *job = &i2c_operation[i];

        switch (job->command) {
        case I2C_MASTER_CMD_WRITE:
            if (job->write.total_bytes > 0) {
                if (job->write.data == NULL || d->i2c_write == NULL) {
                    return job->write.data == NULL ? ESP_ERR_INVALID_ARG
                                                  : ESP_FAIL;
                }
                err = d->i2c_write(d, job->write.data,
                                   job->write.total_bytes);
            }
            break;
        case I2C_MASTER_CMD_READ:
            if (job->read.total_bytes > 0) {
                if (job->read.data == NULL || d->i2c_read == NULL) {
                    return job->read.data == NULL ? ESP_ERR_INVALID_ARG
                                                  : ESP_FAIL;
                }
                err = d->i2c_read(d, job->read.data,
                                  job->read.total_bytes);
            }
            break;
        case I2C_MASTER_CMD_START:
        case I2C_MASTER_CMD_STOP:
        default:
            err = ESP_OK;           /* framing is implicit in the sim   */
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t i2c_master_register_event_callbacks(
    i2c_master_dev_handle_t i2c_dev,
    const i2c_master_event_callbacks_t *cbs, void *user_data)
{
    esp_err_t err = dev_check(i2c_dev);

    if (err != ESP_OK) {
        return err;
    }
    if (cbs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_dev->on_trans_done = cbs->on_trans_done;
    i2c_dev->cb_ctx        = user_data;
    return ESP_OK;
}

esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus_handle)
{
    return bus_check(bus_handle);   /* modelled bus needs no reset       */
}

esp_err_t i2c_master_bus_wait_all_done(i2c_master_bus_handle_t bus_handle,
                                       int timeout_ms)
{
    (void)timeout_ms;               /* synchronous: nothing is pending  */
    return bus_check(bus_handle);
}

esp_err_t i2c_master_get_bus_handle(i2c_port_num_t port_num,
                                    i2c_master_bus_handle_t *ret_handle)
{
    i2c_master_bus_handle_t bus;

    if (ret_handle == NULL || port_num < 0 || port_num >= I2C_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_reg_lock);
    bus = s_buses[port_num];
    pthread_mutex_unlock(&s_reg_lock);
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;   /* port never got a bus          */
    }
    *ret_handle = bus;
    return ESP_OK;
}
