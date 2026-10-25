/* Host-sim esp_heap_caps.h — capability-based heap allocation.
 *
 * All allocations are plain host malloc() underneath, but each block is
 * charged against a per-pool byte budget so the sim can catch memory
 * overruns the target would hit:
 *
 *   - MALLOC_CAP_SPIRAM      charges the 8 MB octal-PSRAM pool; requests
 *                            beyond the budget FAIL (so arena overflows
 *                            are caught in tests rather than silently
 *                            succeeding on the big host).
 *   - MALLOC_CAP_INTERNAL    charges the internal SRAM pool
 *                            (default 512 KiB; override with env
 *                            VQ_SIM_INTERNAL_HEAP_BYTES).
 *   - Neither flag           prefers internal, spills into SPIRAM when
 *                            internal is exhausted — matching the real
 *                            allocator's pool-preference order for a
 *                            plain MALLOC_CAP_8BIT request.
 *
 * vboard's heap_fail_after fault knob is honoured: after N counted
 * allocations the next one fails.
 */
#ifndef ESP_HEAP_CAPS_H
#define ESP_HEAP_CAPS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_bit_defs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Capability flags — same bit positions as real IDF v5.x. */
#define MALLOC_CAP_EXEC         BIT(0)   /* executable memory                */
#define MALLOC_CAP_32BIT        BIT(1)   /* 32-bit accessible                */
#define MALLOC_CAP_8BIT         BIT(2)   /* 8-bit accessible                 */
#define MALLOC_CAP_DMA          BIT(3)   /* DMA-capable                      */
#define MALLOC_CAP_PID2         BIT(4)
#define MALLOC_CAP_PID3         BIT(5)
#define MALLOC_CAP_PID4         BIT(6)
#define MALLOC_CAP_PID5         BIT(7)
#define MALLOC_CAP_PID6         BIT(8)
#define MALLOC_CAP_PID7         BIT(9)
#define MALLOC_CAP_SPIRAM       BIT(10)  /* external SPIRAM pool             */
#define MALLOC_CAP_INTERNAL     BIT(11)  /* internal SRAM pool               */
#define MALLOC_CAP_DEFAULT      BIT(12)  /* "most common" capabilities       */
#define MALLOC_CAP_IRAM_8BIT    BIT(13)  /* IRAM, 8-bit accessible           */
#define MALLOC_CAP_RETENTION    BIT(14)  /* retained across light sleep      */
#define MALLOC_CAP_RTOS         BIT(15)  /* RTOS-internal memory             */
#define MALLOC_CAP_INVALID      BIT(31)

#define MALLOC_CAP_NUM          16

/* ------------------------------------------------------------------ */
/*  Allocation                                                         */
/* ------------------------------------------------------------------ */
void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps);
void  heap_caps_free(void *ptr);
void *heap_caps_malloc_prefer(size_t size, uint32_t caps, ...) /* variadic caps list */
    __attribute__((sentinel));
void *heap_caps_calloc_prefer(size_t n, size_t size, uint32_t caps, ...)
    __attribute__((sentinel));
void *heap_caps_realloc_prefer(void *ptr, size_t size, uint32_t caps, ...)
    __attribute__((sentinel));

/* ------------------------------------------------------------------ */
/*  Statistics / integrity                                             */
/* ------------------------------------------------------------------ */
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
size_t heap_caps_get_allocated_size(void *ptr);

/* In the sim every pool is a host malloc arena; "integrity" checks
 * validate allocation headers on the fly and always report success. */
bool heap_caps_check_integrity_all(bool print_errors);
bool heap_caps_check_integrity_addr(uintptr_t addr, size_t size, bool print_errors);
bool heap_caps_check_integrity(bool print_errors); /* convenience alias */

/* Register a callback fired when any heap_caps allocation fails. */
typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps,
                                        const char *function_name);
esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback);

#ifdef __cplusplus
}
#endif

#endif /* ESP_HEAP_CAPS_H */
