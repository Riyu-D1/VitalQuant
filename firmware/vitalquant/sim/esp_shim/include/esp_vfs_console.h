/* esp_vfs_console.h — host-sim stub of the IDF console VFS shim.
 *
 * On target this registers the primary/secondary console character
 * device under /dev/console so stdio can be routed.  In the host sim
 * stdin/stdout ARE the console — registration is a no-op returning OK.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_vfs_console_register(void);

#ifdef __cplusplus
}
#endif
