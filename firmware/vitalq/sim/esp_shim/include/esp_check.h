/* Host-sim esp_check.h — ESP_RETURN_ON_/ESP_GOTO_ON_ guard macros
 * with the real ESP-IDF signatures.
 *
 * ESP_RETURN_ON_* macros "return" the error (or err_code for _FALSE);
 * ESP_GOTO_ON_* macros assign the error to a local `esp_err_t ret`
 * variable and jump to `goto_tag` — callers must declare `ret`, exactly
 * as on target.
 */
#ifndef ESP_CHECK_H
#define ESP_CHECK_H

#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif

#define ESP_RETURN_ON_ERROR(x, log_tag, format, ...) do {              \
        esp_err_t err_rc_ = (x);                                       \
        if (unlikely(err_rc_ != ESP_OK)) {                             \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__,         \
                     __LINE__, ##__VA_ARGS__);                         \
            return err_rc_;                                            \
        }                                                              \
    } while (0)

#define ESP_GOTO_ON_ERROR(x, goto_tag, log_tag, format, ...) do {      \
        esp_err_t err_rc_ = (x);                                       \
        if (unlikely(err_rc_ != ESP_OK)) {                             \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__,         \
                     __LINE__, ##__VA_ARGS__);                         \
            ret = err_rc_;                                             \
            goto goto_tag;                                             \
        }                                                              \
    } while (0)

#define ESP_RETURN_ON_FALSE(a, err_code, log_tag, format, ...) do {    \
        if (unlikely(!(a))) {                                          \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__,         \
                     __LINE__, ##__VA_ARGS__);                         \
            return err_code;                                           \
        }                                                              \
    } while (0)

#define ESP_GOTO_ON_FALSE(a, err_code, goto_tag, log_tag, format, ...) do { \
        if (unlikely(!(a))) {                                          \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__,         \
                     __LINE__, ##__VA_ARGS__);                         \
            ret = err_code;                                            \
            goto goto_tag;                                             \
        }                                                              \
    } while (0)

/* Void-returning variants: log and bail without a value. */
#define ESP_RETURN_VOID_ON_ERROR(x, log_tag, format, ...) do {         \
        esp_err_t err_rc_ = (x);                                       \
        if (unlikely(err_rc_ != ESP_OK)) {                             \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__,         \
                     __LINE__, ##__VA_ARGS__);                         \
            return;                                                    \
        }                                                              \
    } while (0)

#define ESP_GOTO_VOID_ON_ERROR(x, goto_tag, log_tag, format, ...) do { \
        esp_err_t err_rc_ = (x);                                       \
        if (unlikely(err_rc_ != ESP_OK)) {                             \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__,         \
                     __LINE__, ##__VA_ARGS__);                         \
            goto goto_tag;                                             \
        }                                                              \
    } while (0)

#define ESP_RETURN_VOID_ON_FALSE(a, log_tag, format, ...) do {         \
        if (unlikely(!(a))) {                                          \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__,         \
                     __LINE__, ##__VA_ARGS__);                         \
            return;                                                    \
        }                                                              \
    } while (0)

/* ISR-context variants — on target these log via the early/DRAM path
 * that is safe in an ISR.  In the sim everything runs on host threads,
 * so they map to the normal macros. */
#define ESP_RETURN_ON_ERROR_ISR(x, log_tag, format, ...) \
    ESP_RETURN_ON_ERROR(x, log_tag, format, ##__VA_ARGS__)
#define ESP_GOTO_ON_ERROR_ISR(x, goto_tag, log_tag, format, ...) \
    ESP_GOTO_ON_ERROR(x, goto_tag, log_tag, format, ##__VA_ARGS__)
#define ESP_RETURN_ON_FALSE_ISR(a, err_code, log_tag, format, ...) \
    ESP_RETURN_ON_FALSE(a, err_code, log_tag, format, ##__VA_ARGS__)
#define ESP_GOTO_ON_FALSE_ISR(a, err_code, goto_tag, log_tag, format, ...) \
    ESP_GOTO_ON_FALSE(a, err_code, goto_tag, log_tag, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* ESP_CHECK_H */
