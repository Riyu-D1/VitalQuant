/* esp_intr_alloc.h — host-sim shim for the ESP-IDF interrupt allocator.
 *
 * Table-registry model: esp_intr_alloc() records {source, flags,
 * handler, arg} in a fixed-size table and returns an opaque handle;
 * esp_intr_free() releases the slot.  No real vectoring happens on the
 * host — GPIO ISRs are delivered through the gpio shim / vboard IRQ
 * path; this API exists so drivers can request IRAM-flagged ISRs and
 * share/free sources exactly as on target.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* esp_err_t comes from the shim's esp_err.h when present; fall back to
 * the same typedef/values vboard.h uses so this header is standalone. */
#if defined(__has_include)
#  if __has_include("esp_err.h")
#    include "esp_err.h"
#    define ESP_ERR_T_DECLARED
#  endif
#endif
#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#endif
#ifndef ESP_OK
#define ESP_OK                   0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL                 (-1)
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM           0x101
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG      0x102
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND        0x105
#endif

/* Interrupt handler types — taken from the shim's esp_intr_types.h when
 * a sibling shim provides it; identical fallback otherwise.           */
#if defined(__has_include)
#  if __has_include("esp_intr_types.h")
#    include "esp_intr_types.h"
#  endif
#endif
#if !defined(VSIM_INTR_TYPES_DECLARED) && !defined(ESP_INTR_TYPES_H)
#define VSIM_INTR_TYPES_DECLARED
typedef void (*intr_handler_t)(void *arg);
typedef struct intr_handle_data_t *intr_handle_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Interrupt allocation flags (level bits match (1<<level)). */
#define ESP_INTR_FLAG_LEVEL1        (1<<1)
#define ESP_INTR_FLAG_LEVEL2        (1<<2)
#define ESP_INTR_FLAG_LEVEL3        (1<<3)
#define ESP_INTR_FLAG_LEVEL4        (1<<4)
#define ESP_INTR_FLAG_LEVEL5        (1<<5)
#define ESP_INTR_FLAG_LEVEL6        (1<<6)
#define ESP_INTR_FLAG_NMI           (1<<7)
#define ESP_INTR_FLAG_SHARED        (1<<8)
#define ESP_INTR_FLAG_EDGE          (1<<9)
#define ESP_INTR_FLAG_IRAM          (1<<10)
#define ESP_INTR_FLAG_INTRDISABLED  (1<<11)

#define ESP_INTR_FLAG_LOWMED    (ESP_INTR_FLAG_LEVEL1|ESP_INTR_FLAG_LEVEL2|ESP_INTR_FLAG_LEVEL3)
#define ESP_INTR_FLAG_HIGH      (ESP_INTR_FLAG_LEVEL4|ESP_INTR_FLAG_LEVEL5|ESP_INTR_FLAG_LEVEL6|ESP_INTR_FLAG_NMI)

#define ESP_INTR_FLAG_LEVELMASK (ESP_INTR_FLAG_LEVEL1|ESP_INTR_FLAG_LEVEL2|ESP_INTR_FLAG_LEVEL3| \
                                 ESP_INTR_FLAG_LEVEL4|ESP_INTR_FLAG_LEVEL5|ESP_INTR_FLAG_LEVEL6| \
                                 ESP_INTR_FLAG_NMI)

/* Pseudo-sources for non-muxed internal interrupts. */
#define ETS_INTERNAL_TIMER0_INTR_SOURCE     -1
#define ETS_INTERNAL_TIMER1_INTR_SOURCE     -2
#define ETS_INTERNAL_TIMER2_INTR_SOURCE     -3
#define ETS_INTERNAL_SW0_INTR_SOURCE        -4
#define ETS_INTERNAL_SW1_INTR_SOURCE        -5
#define ETS_INTERNAL_PROFILING_INTR_SOURCE  -6
#define ETS_INTERNAL_UNUSED_INTR_SOURCE    -99

#define ETS_INTERNAL_INTR_SOURCE_OFF        (-ETS_INTERNAL_PROFILING_INTR_SOURCE)

/* Enable/disable by interrupt number (compat macros). */
#define ESP_INTR_ENABLE(inum)  esp_intr_enable_source(inum)
#define ESP_INTR_DISABLE(inum) esp_intr_disable_source(inum)

esp_err_t esp_intr_mark_shared(int intno, int cpu, bool is_in_iram);
esp_err_t esp_intr_reserve(int intno, int cpu);

/* Allocate an interrupt matching `flags` for `source`; on success
 * *ret_handle (if non-NULL) receives an opaque handle.  In sim this
 * registers the handler in the shim's interrupt table and always
 * succeeds (subject to table space).                                */
esp_err_t esp_intr_alloc(int source, int flags, intr_handler_t handler,
                         void *arg, intr_handle_t *ret_handle);
esp_err_t esp_intr_alloc_intrstatus(int source, int flags,
                                    uint32_t intrstatusreg,
                                    uint32_t intrstatusmask,
                                    intr_handler_t handler, void *arg,
                                    intr_handle_t *ret_handle);
esp_err_t esp_intr_free(intr_handle_t handle);

int       esp_intr_get_cpu(intr_handle_t handle);
int       esp_intr_get_intno(intr_handle_t handle);
esp_err_t esp_intr_disable(intr_handle_t handle);
esp_err_t esp_intr_enable(intr_handle_t handle);
esp_err_t esp_intr_set_in_iram(intr_handle_t handle, bool is_in_iram);

void      esp_intr_enable_source(int inum);
void      esp_intr_disable_source(int inum);

/* ---- sim introspection (NOT part of IDF) -------------------------- */
/* Invoke every handler registered for `source` (used by vboard IRQ
 * delivery if a scenario wants to run ISRs directly).                 */
int       esp_shim_intr_dispatch(int source);
/* Number of registered handlers.                                     */
int       esp_shim_intr_count(void);

#ifdef __cplusplus
}
#endif
