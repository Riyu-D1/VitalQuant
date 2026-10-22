/* Host-sim esp_attr.h — placement attributes are no-ops on the host.
 *
 * On target these move code/data into IRAM/DRAM/RTC/PSRAM sections; in
 * the sim there is a single address space, so every macro expands to
 * nothing (or plain inline semantics for FORCE_INLINE_ATTR).
 */
#ifndef ESP_ATTR_H
#define ESP_ATTR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Code placement — no-ops in sim. */
#define IRAM_ATTR
#define IRAM_DATA_ATTR(x)
#define IRAM_BSS_ATTR(x)
#define DRAM_ATTR
#define DRAM_STR_ATTR(v)
#define DMA_ATTR
#define EXT_RAM_ATTR
#define EXT_RAM_BSS_ATTR
#define RTC_IRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_RODATA_ATTR
#define RTC_SLOW_ATTR
#define RTC_FAST_ATTR
#define SOC_ATTR
#define ESP_APP_IRAM_ATTR

/* Function attributes that still mean something to the host compiler. */
#define FORCE_INLINE_ATTR static inline __attribute__((always_inline))
#define NOINLINE_ATTR     __attribute__((noinline))
#define FORCE_NOINLINE_ATTR NOINLINE_ATTR

/* Compatibility aliases seen in IDF code. */
#define ESP_IRAM_ATTR     IRAM_ATTR
#define ESP_DRAM_ATTR     DRAM_ATTR

#ifdef __cplusplus
}
#endif

#endif /* ESP_ATTR_H */
