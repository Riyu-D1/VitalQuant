/* Host-sim log core — timestamped, level-filtered writers.
 *
 * Output format (no ANSI colours, parseable by scenario scripts):
 *
 *     [I] (   12.345678) tag: message
 *
 * The timestamp is seconds.microseconds of the virtual vboard clock, so
 * log ordering matches simulated time — not wall time — even at >1x
 * time scales.
 *
 * E/W go to stderr; I/D/V to stdout.  A mutex keeps whole lines atomic
 * when firmware tasks and shim threads log concurrently.
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "esp_log.h"
#include "vboard.h"

/* ------------------------------------------------------------------ */
/*  Per-tag runtime level override                                     */
/* ------------------------------------------------------------------ */

#define MAX_TAG_LEVELS 32

typedef struct {
    char            tag[24];
    esp_log_level_t level;
} tag_level_t;

static pthread_mutex_t s_log_lock = PTHREAD_MUTEX_INITIALIZER;
static esp_log_level_t s_default_level = ESP_LOG_INFO;
static tag_level_t     s_tag_levels[MAX_TAG_LEVELS];
static int             s_n_tag_levels;

static const char LEVEL_CHARS[] = { 'N', 'E', 'W', 'I', 'D', 'V', '?' };

static esp_log_level_t effective_level_locked(const char *tag)
{
    for (int i = 0; i < s_n_tag_levels; i++) {
        if (strncmp(s_tag_levels[i].tag, tag, sizeof(s_tag_levels[i].tag)) == 0)
            return s_tag_levels[i].level;
    }
    return s_default_level;
}

void esp_log_level_set(const char *tag, esp_log_level_t level)
{
    if (!tag) return;
    pthread_mutex_lock(&s_log_lock);
    if (strcmp(tag, "*") == 0) {
        s_default_level = level;
    } else {
        for (int i = 0; i < s_n_tag_levels; i++) {
            if (strncmp(s_tag_levels[i].tag, tag, sizeof(s_tag_levels[i].tag)) == 0) {
                s_tag_levels[i].level = level;
                pthread_mutex_unlock(&s_log_lock);
                return;
            }
        }
        if (s_n_tag_levels < MAX_TAG_LEVELS) {
            snprintf(s_tag_levels[s_n_tag_levels].tag,
                     sizeof(s_tag_levels[0].tag), "%s", tag);
            s_tag_levels[s_n_tag_levels].level = level;
            s_n_tag_levels++;
        }
    }
    pthread_mutex_unlock(&s_log_lock);
}

esp_log_level_t esp_log_level_get(const char *tag)
{
    esp_log_level_t l;
    pthread_mutex_lock(&s_log_lock);
    l = tag ? effective_level_locked(tag) : s_default_level;
    pthread_mutex_unlock(&s_log_lock);
    return l;
}

void esp_log_level_set_default(esp_log_level_t level)
{
    pthread_mutex_lock(&s_log_lock);
    s_default_level = level;
    pthread_mutex_unlock(&s_log_lock);
}

esp_log_level_t esp_log_level_get_default(void)
{
    return s_default_level;
}

/* ------------------------------------------------------------------ */
/*  Timestamps                                                         */
/* ------------------------------------------------------------------ */

uint32_t esp_log_timestamp(void)
{
    return (uint32_t)(vboard_now_us() / 1000);
}

uint32_t esp_log_early_timestamp(void)
{
    return esp_log_timestamp();
}

/* ------------------------------------------------------------------ */
/*  Writers                                                            */
/* ------------------------------------------------------------------ */

void esp_log_writev(esp_log_level_t level, const char *tag,
                    const char *format, va_list args)
{
    if (!tag || !format) return;
    if (level <= ESP_LOG_NONE || level >= ESP_LOG_MAX) return;

    pthread_mutex_lock(&s_log_lock);
    if (level > effective_level_locked(tag)) {
        pthread_mutex_unlock(&s_log_lock);
        return;
    }

    const int64_t now_us = vboard_now_us();
    const long long secs = (long long)(now_us / 1000000);
    const long long usec = (long long)(now_us % 1000000);
    FILE *out = (level <= ESP_LOG_WARN) ? stderr : stdout;

    fprintf(out, "[%c] (%5lld.%06lld) %s: ",
            LEVEL_CHARS[level], secs, usec, tag);
    vfprintf(out, format, args);
    fputc('\n', out);
    fflush(out);
    pthread_mutex_unlock(&s_log_lock);
}

void esp_log_write(esp_log_level_t level, const char *tag,
                   const char *format, ...)
{
    va_list args;
    va_start(args, format);
    esp_log_writev(level, tag, format, args);
    va_end(args);
}
