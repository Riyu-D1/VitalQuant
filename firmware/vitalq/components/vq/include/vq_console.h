/* vq_console.h — USB-Serial/JTAG REPL (native USB on GPIO19/20).
 * Commands: selftest, status, sensors, scan (i2c), wifi, cloud, model,
 * score (decision once), led, power, sleep, logtail, fsr, mic, reboot.
 * NEVER burns eFuses, NEVER reclaims GPIO19/20, recovery path stays.
 */
#pragma once
#include "vq_types.h"

vq_err_t vq_console_start(void);        /* esp_console REPL on USJ      */
void     vq_console_log_event(vq_event_id_t id, const char *fmt, ...);
