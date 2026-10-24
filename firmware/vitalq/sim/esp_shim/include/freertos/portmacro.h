/* portmacro.h — compatibility header.
 * Some sources include this name directly; the sim's port surface lives
 * in portable.h. */
#ifndef INC_PORTMACRO_H
#define INC_PORTMACRO_H

#include "freertos/portable.h"
/* Real IDF pulls esp_attr.h in via portmacro — firmware files use
 * IRAM_ATTR et al. without including it themselves. */
#include "esp_attr.h"

#endif /* INC_PORTMACRO_H */
