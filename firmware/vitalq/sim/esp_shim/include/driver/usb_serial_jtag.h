/* driver/usb_serial_jtag.h — host-sim stub of the ESP32-S3 USJ driver.
 *
 * On target this drives the native USB CDC/JTAG block the console lives
 * on; in the sim "the USJ" is process stdin/stdout, always attached.
 * Everything here is a no-op that reports a healthy, connected link.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t tx_buffer_size;
    uint32_t rx_buffer_size;
} usb_serial_jtag_driver_config_t;

#define USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() \
    {                                           \
        .tx_buffer_size = 256,                  \
        .rx_buffer_size = 256,                  \
    }

esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *cfg);
esp_err_t usb_serial_jtag_driver_uninstall(void);

/* Host sim: the "USB host" is always connected (stdio is the cable). */
bool usb_serial_jtag_is_connected(void);

/* Byte I/O — mapped onto stdin/stdout. */
int usb_serial_jtag_read_bytes(void *buf, uint32_t length, uint32_t ticks_to_wait);
int usb_serial_jtag_write_bytes(const void *src, uint32_t length);

/* VFS glue stubs (names mirror esp_vfs_usb_serial_jtag / the older
 * esp_vfs_dev_usb_serial_jtag API — all no-ops in the sim).          */
esp_err_t usb_serial_jtag_vfs_register(void);
esp_err_t usb_serial_jtag_vfs_use_driver(void);
esp_err_t usb_serial_jtag_vfs_use_nonblocking(void);
#define esp_vfs_dev_usb_serial_jtag_register    usb_serial_jtag_vfs_register
#define esp_vfs_dev_usb_serial_jtag_use_driver  usb_serial_jtag_vfs_use_driver
#define esp_vfs_dev_usb_serial_jtag_use_nonblocking \
    usb_serial_jtag_vfs_use_nonblocking

#ifdef __cplusplus
}
#endif
