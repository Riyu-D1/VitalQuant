/* driver/i2c_master.h — HOST-SIM shim of the ESP-IDF >= 5.2 "new" I2C
 * master API (esp_driver_i2c).  Signatures, config layouts and error
 * codes mirror IDF 5.4 so the real firmware (components/vq_i2c) builds
 * unmodified; the wire side is fulfilled by the vboard device models
 * (sim/vboard/vboard.h) in i2c_sim.c, not by GPIO/SCL hardware.
 *
 * Modelled: 7/10-bit addressing at the API level, synchronous blocking
 *           transactions, per-device SCL speed, ACK/NACK semantics via
 *           the injected vboard faults.
 * NOT silicon: no SDA/SCL line state, no clock stretching, no —
 *           trans_queue_depth and async callbacks are accepted but
 *           every transaction still completes before the call returns.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* esp_err_t comes from the core esp_err shim when it exists; fall back
 * to the pattern vboard.h already uses so this header is self-contained
 * while the shim tree is being built out.                             */
#if defined(__has_include)
#  if __has_include("esp_err.h")
#    include "esp_err.h"
#  endif
#endif
#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- port / address-length / clock-source types ---------------------
 * (hal/i2c_types.h + driver/i2c_types.h equivalents, folded in.)      */
typedef int i2c_port_num_t;

typedef enum {
    I2C_NUM_0 = 0,              /*!< I2C port 0 — the only bus fix_v22 uses */
    I2C_NUM_1,                  /*!< I2C port 1 (exists on ESP32-S3)        */
    I2C_NUM_MAX,
} i2c_port_t;

typedef enum {
    I2C_ADDR_BIT_LEN_7 = 0,     /*!< 7-bit slave addressing  */
    I2C_ADDR_BIT_LEN_10 = 1,    /*!< 10-bit slave addressing */
} i2c_addr_bit_len_t;

typedef enum {
    I2C_CLK_SRC_XTAL = 0,       /*!< XTAL clock source              */
    I2C_CLK_SRC_RC_FAST,        /*!< RC_FAST clock source           */
    I2C_CLK_SRC_DEFAULT = I2C_CLK_SRC_XTAL,
} i2c_clock_source_t;

/* ---- opaque handles ------------------------------------------------- */
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;
typedef struct i2c_master_dev_t *i2c_master_dev_handle_t;

/* ---- transaction status / event / callback types -------------------- */
typedef enum {
    I2C_STATUS_READ,            /*!< read status for current master command  */
    I2C_STATUS_WRITE,           /*!< write status for current master command */
    I2C_STATUS_START,           /*!< start status for current master command */
    I2C_STATUS_STOP,            /*!< stop status for current master command  */
    I2C_STATUS_IDLE,            /*!< idle status for current master command  */
    I2C_STATUS_ACK_ERROR,       /*!< ack error status for current command    */
    I2C_STATUS_DONE,            /*!< I2C command done                        */
    I2C_STATUS_TIMEOUT,         /*!< I2C bus status error, operation timeout */
} i2c_master_status_t;

typedef enum {
    I2C_EVENT_ALIVE,            /*!< i2c bus in alive status   */
    I2C_EVENT_DONE,             /*!< i2c bus transaction done  */
    I2C_EVENT_NACK,             /*!< i2c bus nack              */
    I2C_EVENT_TIMEOUT,          /*!< i2c bus timeout           */
} i2c_master_event_t;

typedef struct {
    i2c_master_event_t event;   /*!< the hardware event reported */
} i2c_master_event_data_t;

typedef bool (*i2c_master_callback_t)(i2c_master_dev_handle_t i2c_dev,
                                      const i2c_master_event_data_t *evt_data,
                                      void *arg);

typedef struct {
    i2c_master_callback_t on_trans_done;  /*!< transaction-finish callback */
} i2c_master_event_callbacks_t;

/* ---- bus / device configuration (layout matches IDF 5.4) ------------ */
typedef struct {
    i2c_port_num_t i2c_port;            /*!< I2C port number, -1 = auto     */
    int sda_io_num;                     /*!< SDA GPIO (gpio_num_t in IDF)   */
    int scl_io_num;                     /*!< SCL GPIO (gpio_num_t in IDF)   */
    i2c_clock_source_t clk_source;      /*!< bus clock source               */
    uint8_t glitch_ignore_cnt;          /*!< spike filter, module clocks    */
    int intr_priority;                  /*!< 0 = driver default             */
    size_t trans_queue_depth;           /*!< async depth; sim is sync only  */
    struct {
        uint32_t enable_internal_pullup : 1;  /*!< R19/R20 are external   */
        uint32_t allow_pd : 1;                /*!< power-domain in sleep  */
    } flags;
} i2c_master_bus_config_t;

#define I2C_DEVICE_ADDRESS_NOT_USED    (0xffff) /*!< skip address phase   */

typedef struct {
    i2c_addr_bit_len_t dev_addr_length; /*!< I2C_ADDR_BIT_LEN_*             */
    uint16_t device_address;            /*!< raw 7/10-bit address (no R/W)  */
    uint32_t scl_speed_hz;              /*!< SCL frequency for this device  */
    uint32_t scl_wait_us;               /*!< stretch timeout, 0 = default   */
    struct {
        uint32_t disable_ack_check : 1; /*!< 1 = NACKs don't abort        */
    } flags;
} i2c_device_config_t;

/* ---- low-level job API (i2c_master_execute_defined_operations) ------ */
typedef enum {
    I2C_MASTER_CMD_START,       /*!< Start or Restart condition */
    I2C_MASTER_CMD_WRITE,       /*!< Write operation            */
    I2C_MASTER_CMD_READ,        /*!< Read operation             */
    I2C_MASTER_CMD_STOP,        /*!< Stop condition             */
} i2c_master_command_t;

typedef enum {
    I2C_ACK_VAL = 0,            /*!< ACK after a read byte   */
    I2C_NACK_VAL = 1,           /*!< NACK after a read byte  */
} __attribute__((packed)) i2c_ack_value_t;

typedef struct {
    i2c_master_command_t command;
    union {
        struct {
            bool ack_check;         /*!< enable ACK check for this write */
            uint8_t *data;          /*!< bytes to write                  */
            size_t total_bytes;     /*!< byte count                      */
        } write;
        struct {
            i2c_ack_value_t ack_value;  /*!< ACK/NACK sent after read    */
            uint8_t *data;              /*!< buffer for read bytes       */
            size_t total_bytes;         /*!< byte count                  */
        } read;
    };
} i2c_operation_job_t;

typedef struct {
    uint8_t *write_buffer;      /*!< buffer to write   */
    size_t buffer_size;         /*!< bytes in buffer   */
} i2c_master_transmit_multi_buffer_info_t;

/* ---- API ------------------------------------------------------------ */

/* Allocate an I2C master bus.
 *  ESP_OK / ESP_ERR_INVALID_ARG / ESP_ERR_NO_MEM / ESP_ERR_INVALID_STATE
 *  (port already bound — real IDF returns ESP_ERR_NOT_FOUND when no bus
 *  is free, but a busy port is the only reachable conflict here).     */
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                             i2c_master_bus_handle_t *ret_bus_handle);

/* Delete a master bus and every device still attached to it. */
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle);

/* Attach a device to the bus. NOTE (sim): scl_speed_hz > 100000 trips
 * the fix_v22 hard cap — logged as VB-I2C-SPEED and counted in
 * g_i2c_speed_violations; the device is still created (board limit is
 * physical, not an API error).                                       */
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                    const i2c_device_config_t *dev_config,
                                    i2c_master_dev_handle_t *ret_handle);

/* Remove one device (alias: i2c_master_del_device). */
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle);
esp_err_t i2c_master_del_device(i2c_master_dev_handle_t handle);

/* Remove every device attached to the bus. */
esp_err_t i2c_master_bus_rm_all_device(i2c_master_bus_handle_t bus_handle);

/* Blocking write: START, addr+W, write_buffer[0..n), STOP.
 * Per the board's device models, write_buffer[0] is the register byte.
 * Missing/silenced device -> ESP_ERR_TIMEOUT (address-phase NACK);
 * injected faults -> ESP_FAIL; wire overrun -> ESP_ERR_TIMEOUT.       */
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev,
                              const uint8_t *write_buffer,
                              size_t write_size, int xfer_timeout_ms);

/* Blocking read: START, addr+R, read_size bytes, STOP. */
esp_err_t i2c_master_receive(i2c_master_dev_handle_t i2c_dev,
                             uint8_t *read_buffer, size_t read_size,
                             int xfer_timeout_ms);

/* SMBus reg-read pattern: write phase then repeated-START read phase.
 * A write-phase failure short-circuits — the read never happens.      */
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev,
                                      const uint8_t *write_buffer,
                                      size_t write_size,
                                      uint8_t *read_buffer,
                                      size_t read_size,
                                      int xfer_timeout_ms);

/* Multi-buffer transmit; buffers are coalesced into one wire frame. */
esp_err_t i2c_master_multi_buffer_transmit(
    i2c_master_dev_handle_t i2c_dev,
    i2c_master_transmit_multi_buffer_info_t *buffer_info_array,
    size_t array_size, int xfer_timeout_ms);

/* Address probe: START, addr+W, wait for ACK, STOP.
 * ESP_OK iff the address ACKs.  Device absent/silenced ->
 * ESP_ERR_TIMEOUT in this sim (NACK semantics — see i2c_sim.c; real
 * IDF reports ESP_ERR_NOT_FOUND, firmware treats any != ESP_OK as
 * absent so behaviour is preserved).                                 */
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle,
                           uint16_t address, int xfer_timeout_ms);

/* Execute a job list (START/WRITE/READ/STOP) as one transaction. */
esp_err_t i2c_master_execute_defined_operations(
    i2c_master_dev_handle_t i2c_dev, i2c_operation_job_t *i2c_operation,
    size_t operation_list_num, int xfer_timeout_ms);

/* Register on_trans_done; sim stays synchronous — the callback fires
 * on the calling thread before the transfer function returns.        */
esp_err_t i2c_master_register_event_callbacks(
    i2c_master_dev_handle_t i2c_dev,
    const i2c_master_event_callbacks_t *cbs, void *user_data);

/* Soft bus reset — no-op on the modelled bus (returns ESP_OK). */
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus_handle);

/* All sim transactions are synchronous: nothing is ever pending. */
esp_err_t i2c_master_bus_wait_all_done(i2c_master_bus_handle_t bus_handle,
                                       int timeout_ms);

/* Fetch the handle bound to a port by i2c_new_master_bus. */
esp_err_t i2c_master_get_bus_handle(i2c_port_num_t port_num,
                                    i2c_master_bus_handle_t *ret_handle);

/* ---- host-sim observability (built only under -DSIM_HOST_BUILD) ----- */
#ifdef SIM_HOST_BUILD
/* Count of i2c_master_bus_add_device calls whose scl_speed_hz exceeded
 * the fix_v22 100 kHz hard cap (each one also prints "VB-I2C-SPEED"). */
extern uint64_t g_i2c_speed_violations;
uint64_t vboard_i2c_speed_violations(void);
#endif /* SIM_HOST_BUILD */

#ifdef __cplusplus
}
#endif
