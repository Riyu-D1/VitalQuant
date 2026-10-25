/* Host-sim esp_system + esp_err + esp_cpu + esp_random + esp_app_desc
 * implementation.
 *
 * esp_restart() prints a marker line and exits with code 42 — scenario
 * harnesses treat 42 as "firmware asked for a clean reboot", distinct
 * from crashes and from deep-sleep exits handled by the sleep shim.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "esp_system.h"
#include "esp_err.h"
#include "esp_cpu.h"
#include "esp_random.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_bit_defs.h"
#include "esp_heap_caps.h"
#include "vboard.h"

#define SIM_RESET_EXIT_CODE 42

/* ------------------------------------------------------------------ */
/*  Reset / restart                                                    */
/* ------------------------------------------------------------------ */

static esp_reset_reason_t s_reset_reason = ESP_RST_UNKNOWN;

/* The sleep shim exposes a sticky "woke from deep sleep" flag for us
 * (see esp_sleep.h: vsim_sleep_woke_from_deep).  Referenced weakly so
 * this file still links if the sleep shim is absent. */
__attribute__((weak)) int vsim_sleep_woke_from_deep(void);

void esp_restart(void)
{
    fflush(NULL);
    fprintf(stderr, "[SIM] esp_restart() -> clean reboot requested "
            "(exit %d)\n", SIM_RESET_EXIT_CODE);
    fflush(NULL);
    _exit(SIM_RESET_EXIT_CODE);
}

esp_reset_reason_t esp_reset_reason(void)
{
    if (s_reset_reason != ESP_RST_UNKNOWN)
        return s_reset_reason;
    if (vsim_sleep_woke_from_deep && vsim_sleep_woke_from_deep())
        return ESP_RST_DEEPSLEEP;
    return ESP_RST_POWERON;
}

void esp_shim_set_reset_reason(esp_reset_reason_t reason)
{
    s_reset_reason = reason;
}

void esp_system_abort(const char *details)
{
    fprintf(stderr, "[SIM] esp_system_abort: %s\n",
            details ? details : "(no details)");
    abort();
}

/* ------------------------------------------------------------------ */
/*  Heap stats (delegated to the heap_caps pool accounting)            */
/* ------------------------------------------------------------------ */

uint32_t esp_get_free_heap_size(void)
{
    /* "all heaps" — internal + SPIRAM pools. */
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

uint32_t esp_get_free_internal_heap_size(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

uint32_t esp_get_minimum_free_heap_size(void)
{
    return (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
}

/* ------------------------------------------------------------------ */
/*  Chip info                                                          */
/* ------------------------------------------------------------------ */

void esp_chip_info(esp_chip_info_t *out_chip_info)
{
    if (!out_chip_info) return;
    memset(out_chip_info, 0, sizeof(*out_chip_info));
    out_chip_info->model    = CHIP_ESP32S3;
    out_chip_info->features = CHIP_FEATURE_WIFI_BGN | CHIP_FEATURE_BLE;
    out_chip_info->cores    = 2;
    out_chip_info->revision = 2;          /* rev v0.2 -> prints "0.2" */
    out_chip_info->revision_full = 0x0002;
}

const char *esp_get_idf_version(void)
{
    return "v5.4.2";
}

/* ------------------------------------------------------------------ */
/*  CPU                                                                */
/* ------------------------------------------------------------------ */

uint32_t esp_cpu_get_cycle_count(void)
{
    /* 240 MHz core clock — wraps every ~17.9 s like a real CCOUNT. */
    return (uint32_t)(vboard_now_us() * 240u);
}

bool esp_cpu_in_ocd_debug_mode(void)
{
    return false;
}

/* ------------------------------------------------------------------ */
/*  Random                                                             */
/* ------------------------------------------------------------------ */

#if defined(__GLIBC__) && (__GLIBC__ > 2 || \
    (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 36))
#define HAVE_ARC4RANDOM 1
#else
#define HAVE_ARC4RANDOM 0
#endif

#if !HAVE_ARC4RANDOM
static unsigned int *rand_seed(void)
{
    static _Thread_local unsigned int seed;
    if (!seed) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        seed = (unsigned int)(ts.tv_nsec ^ ts.tv_sec ^
                              ((uintptr_t)&seed >> 4) ^ (unsigned)getpid());
        if (!seed) seed = 0x9E3779B9u;
    }
    return &seed;
}
#endif

uint32_t esp_random(void)
{
#if HAVE_ARC4RANDOM
    return arc4random();
#else
    return (uint32_t)rand_r(rand_seed());
#endif
}

void esp_fill_random(void *buf, size_t len)
{
    if (!buf) return;
#if HAVE_ARC4RANDOM
    arc4random_buf(buf, len);
#else
    uint8_t *p = buf;
    while (len >= sizeof(uint32_t)) {
        uint32_t r = esp_random();
        memcpy(p, &r, sizeof r);
        p += sizeof r;
        len -= sizeof r;
    }
    if (len) {
        uint32_t r = esp_random();
        memcpy(p, &r, len);
    }
#endif
}

/* ------------------------------------------------------------------ */
/*  esp_err_to_name                                                    */
/* ------------------------------------------------------------------ */

const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK:                    return "ESP_OK";
    case ESP_FAIL:                  return "ESP_FAIL";
    case ESP_ERR_NO_MEM:            return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:       return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:     return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE:      return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND:         return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED:     return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT:           return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_RESPONSE:  return "ESP_ERR_INVALID_RESPONSE";
    case ESP_ERR_INVALID_CRC:       return "ESP_ERR_INVALID_CRC";
    case ESP_ERR_INVALID_VERSION:   return "ESP_ERR_INVALID_VERSION";
    case ESP_ERR_INVALID_MAC:       return "ESP_ERR_INVALID_MAC";
    case ESP_ERR_NOT_FINISHED:      return "ESP_ERR_NOT_FINISHED";
    case ESP_ERR_NOT_ALLOWED:       return "ESP_ERR_NOT_ALLOWED";
    case ESP_ERR_TIMEOUT_FROM_ISR:  return "ESP_ERR_TIMEOUT_FROM_ISR";
    case ESP_ERR_NVS_NOT_INITIALIZED:   return "ESP_ERR_NVS_NOT_INITIALIZED";
    case ESP_ERR_NVS_NOT_FOUND:         return "ESP_ERR_NVS_NOT_FOUND";
    case ESP_ERR_NVS_TYPE_MISMATCH:     return "ESP_ERR_NVS_TYPE_MISMATCH";
    case ESP_ERR_NVS_READ_ONLY:         return "ESP_ERR_NVS_READ_ONLY";
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:  return "ESP_ERR_NVS_NOT_ENOUGH_SPACE";
    case ESP_ERR_NVS_INVALID_NAME:      return "ESP_ERR_NVS_INVALID_NAME";
    case ESP_ERR_NVS_INVALID_HANDLE:    return "ESP_ERR_NVS_INVALID_HANDLE";
    case ESP_ERR_NVS_REMOVE_FAILED:     return "ESP_ERR_NVS_REMOVE_FAILED";
    case ESP_ERR_NVS_KEY_TOO_LONG:      return "ESP_ERR_NVS_KEY_TOO_LONG";
    case ESP_ERR_NVS_PAGE_FULL:         return "ESP_ERR_NVS_PAGE_FULL";
    case ESP_ERR_NVS_INVALID_STATE:     return "ESP_ERR_NVS_INVALID_STATE";
    case ESP_ERR_NVS_INVALID_LENGTH:    return "ESP_ERR_NVS_INVALID_LENGTH";
    case ESP_ERR_NVS_NO_FREE_PAGES:     return "ESP_ERR_NVS_NO_FREE_PAGES";
    case ESP_ERR_NVS_VALUE_TOO_LONG:    return "ESP_ERR_NVS_VALUE_TOO_LONG";
    case ESP_ERR_NVS_PART_NOT_FOUND:    return "ESP_ERR_NVS_PART_NOT_FOUND";
    case ESP_ERR_NVS_NEW_VERSION_FOUND: return "ESP_ERR_NVS_NEW_VERSION_FOUND";
    case ESP_ERR_NVS_XTS_ENCR_FAILED:   return "ESP_ERR_NVS_XTS_ENCR_FAILED";
    case ESP_ERR_NVS_XTS_DECR_FAILED:   return "ESP_ERR_NVS_XTS_DECR_FAILED";
    case ESP_ERR_NVS_XTS_ENCR_NOT_SUPPORTED:
        return "ESP_ERR_NVS_XTS_ENCR_NOT_SUPPORTED";
    default:                        return "UNKNOWN_ERR";
    }
}

/* ------------------------------------------------------------------ */
/*  ESP_ERROR_CHECK support                                            */
/* ------------------------------------------------------------------ */

void esp_error_check_failed(esp_err_t rc, const char *file, int line,
                            const char *function, const char *expression)
{
    fprintf(stderr,
            "[E] (%lld.%06lld) esp_system: ESP_ERROR_CHECK failed: "
            "esp_err_t 0x%x (%s) at %s:%d (%s), expr: %s\n",
            (long long)(vboard_now_us() / 1000000),
            (long long)(vboard_now_us() % 1000000),
            (int)rc, esp_err_to_name(rc), file, line, function, expression);
    fflush(NULL);
    abort();
}

void esp_error_check_failed_without_abort(esp_err_t rc, const char *file,
                                          int line, const char *function,
                                          const char *expression)
{
    fprintf(stderr,
            "[E] (%lld.%06lld) esp_system: ESP_ERROR_CHECK_WITHOUT_ABORT "
            "failed: esp_err_t 0x%x (%s) at %s:%d (%s), expr: %s\n",
            (long long)(vboard_now_us() / 1000000),
            (long long)(vboard_now_us() % 1000000),
            (int)rc, esp_err_to_name(rc), file, line, function, expression);
}

/* Real-IDF spellings. */
void _esp_error_check_failed(esp_err_t rc, const char *file, int line,
                             const char *function, const char *expression)
{
    esp_error_check_failed(rc, file, line, function, expression);
}

void _esp_error_check_failed_without_abort(esp_err_t rc, const char *file,
                                           int line, const char *function,
                                           const char *expression)
{
    esp_error_check_failed_without_abort(rc, file, line, function, expression);
}

/* ------------------------------------------------------------------ */
/*  Application descriptor                                             */
/* ------------------------------------------------------------------ */

static const esp_app_desc_t s_app_desc = {
    .magic_word    = ESP_APP_DESC_MAGIC_WORD,
    .secure_version = 0,
    .reserv1       = {0, 0},
    .version       = "2.0.0-sim",
    .project_name  = "vitalquant",
    .time          = __TIME__,
    .date          = __DATE__,
    .idf_ver       = "v5.4.2",
    /* Sim ELF fingerprint — a fixed nonzero pattern so tests can tell
     * "present" from "uninitialized" without pretending to be a real
     * image hash. */
    .app_elf_sha256 = {
        0x51, 0x1A, 0x51, 0x1A, 0x51, 0x1A, 0x51, 0x1A,
        0x51, 0x1A, 0x51, 0x1A, 0x51, 0x1A, 0x51, 0x1A,
        0x51, 0x1A, 0x51, 0x1A, 0x51, 0x1A, 0x51, 0x1A,
        0x51, 0x1A, 0x51, 0x1A, 0x51, 0x1A, 0x51, 0x1A
    },
    .reserv2       = {0},
};

const esp_app_desc_t *esp_app_get_description(void)
{
    return &s_app_desc;
}

int esp_app_get_elf_sha256(char *dst, size_t size)
{
    if (!dst || size == 0) return -1;
    size_t n = sizeof(s_app_desc.app_elf_sha256);
    if (n > size) n = size;
    memcpy(dst, s_app_desc.app_elf_sha256, n);
    return (int)n;
}
