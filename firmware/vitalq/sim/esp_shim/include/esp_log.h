/* Host-sim esp_log.h — leveled logging to stdout/stderr with a virtual
 * (vboard) timestamp.  No ANSI colours so scenario output stays
 * machine-parseable.
 *
 * Compile-time filtering honours LOG_LOCAL_LEVEL (default: the
 * sdkconfig CONFIG_LOG_MAXIMUM_LEVEL_* knob, else ESP_LOG_INFO), the
 * same way real IDF gates ESP_LOG* calls at compile time.  A per-tag
 * runtime override is applied inside esp_log_writev().
 */
#ifndef ESP_LOG_H
#define ESP_LOG_H

#include <stdint.h>
#include <stdarg.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_LOG_NONE    = 0,   /* no log output                          */
    ESP_LOG_ERROR   = 1,   /* critical errors                        */
    ESP_LOG_WARN    = 2,   /* error conditions less severe           */
    ESP_LOG_INFO    = 3,   /* informational (default)                */
    ESP_LOG_DEBUG   = 4,   /* extra debugging detail                 */
    ESP_LOG_VERBOSE = 5,   /* bigger chunks of debugging info        */
    ESP_LOG_MAX     = 6
} esp_log_level_t;

/* Compile-time level: pick up the sdkconfig maximum-level knob like
 * real IDF does; fall back to INFO. */
#ifndef LOG_LOCAL_LEVEL
#  if defined(CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE) && CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE
#    define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#  elif defined(CONFIG_LOG_MAXIMUM_LEVEL_DEBUG) && CONFIG_LOG_MAXIMUM_LEVEL_DEBUG
#    define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#  elif defined(CONFIG_LOG_MAXIMUM_LEVEL_INFO) && CONFIG_LOG_MAXIMUM_LEVEL_INFO
#    define LOG_LOCAL_LEVEL ESP_LOG_INFO
#  elif defined(CONFIG_LOG_MAXIMUM_LEVEL_WARN) && CONFIG_LOG_MAXIMUM_LEVEL_WARN
#    define LOG_LOCAL_LEVEL ESP_LOG_WARN
#  elif defined(CONFIG_LOG_MAXIMUM_LEVEL_ERROR) && CONFIG_LOG_MAXIMUM_LEVEL_ERROR
#    define LOG_LOCAL_LEVEL ESP_LOG_ERROR
#  elif defined(CONFIG_LOG_MAXIMUM_LEVEL_NONE) && CONFIG_LOG_MAXIMUM_LEVEL_NONE
#    define LOG_LOCAL_LEVEL ESP_LOG_NONE
#  else
#    define LOG_LOCAL_LEVEL ESP_LOG_INFO
#  endif
#endif

/* ------------------------------------------------------------------ */
/*  Runtime level control (per-tag; "*" sets the default)              */
/* ------------------------------------------------------------------ */
void            esp_log_level_set(const char *tag, esp_log_level_t level);
esp_log_level_t esp_log_level_get(const char *tag);
void            esp_log_level_set_default(esp_log_level_t level);
esp_log_level_t esp_log_level_get_default(void);

/* ------------------------------------------------------------------ */
/*  Emitters (also callable directly — real IDF exposes them too)      */
/* ------------------------------------------------------------------ */
void esp_log_write(esp_log_level_t level, const char *tag,
                   const char *format, ...) __attribute__((format(printf, 3, 4)));
void esp_log_writev(esp_log_level_t level, const char *tag,
                    const char *format, va_list args);

/** Log timestamp in ms — derived from the virtual vboard clock. */
uint32_t esp_log_timestamp(void);
uint32_t esp_log_early_timestamp(void);

/* ------------------------------------------------------------------ */
/*  ESP_LOG* macros — real IDF signatures.                             */
/* ------------------------------------------------------------------ */
#define ESP_LOG_LEVEL(level, tag, format, ...) do {                    \
        if (LOG_LOCAL_LEVEL >= (level)) {                              \
            esp_log_write((level), (tag), format, ##__VA_ARGS__);      \
        }                                                              \
    } while (0)

#define ESP_LOG_LEVEL_LOCAL(level, tag, format, ...) \
    ESP_LOG_LEVEL(level, tag, format, ##__VA_ARGS__)

#define ESP_LOGE(tag, format, ...) \
    ESP_LOG_LEVEL(ESP_LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) \
    ESP_LOG_LEVEL(ESP_LOG_WARN,    tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) \
    ESP_LOG_LEVEL(ESP_LOG_INFO,    tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) \
    ESP_LOG_LEVEL(ESP_LOG_DEBUG,   tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) \
    ESP_LOG_LEVEL(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

/* Early-boot logging — in the sim the log backend is always available,
 * so these map onto the normal writers. */
#define ESP_EARLY_LOGE(tag, format, ...) \
    esp_log_write(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGW(tag, format, ...) \
    esp_log_write(ESP_LOG_WARN,  tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGI(tag, format, ...) \
    esp_log_write(ESP_LOG_INFO,  tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGD(tag, format, ...) \
    esp_log_write(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGV(tag, format, ...) \
    esp_log_write(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* ESP_LOG_H */
