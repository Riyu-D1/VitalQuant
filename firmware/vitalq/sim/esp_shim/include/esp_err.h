/* Host-sim esp_err.h — ESP-IDF-compatible error codes for the native
 * (non-IDF) build.  Self-contained: no real IDF headers.
 *
 * esp_err_t is declared behind ESP_ERR_T_DECLARED so this header can be
 * included in either order with sim/vboard/vboard.h (which carries the
 * same guarded forward declaration for device models).
 */
#ifndef ESP_ERR_H
#define ESP_ERR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#endif

/* vboard.h only defines its VB_* aliases when it is the header that
 * declares esp_err_t.  Define them here too (idempotent) so device
 * models work regardless of include order. */
#ifndef VB_ESP_OK
#define VB_ESP_OK            0
#define VB_ESP_ERR_TIMEOUT  0x107
#define VB_ESP_FAIL         (-1)
#endif

#define ESP_OK    0       /* esp_err_t value indicating success (no error) */
#define ESP_FAIL (-1)     /* generic esp_err_t code indicating failure     */
/* Some firmware/IDF code spells the generic failure ESP_ERR_FAIL. */
#ifndef ESP_ERR_FAIL
#define ESP_ERR_FAIL ESP_FAIL
#endif

#define ESP_ERR_NO_MEM           0x101   /* out of memory                        */
#define ESP_ERR_INVALID_ARG      0x102   /* invalid argument                     */
#define ESP_ERR_INVALID_STATE    0x103   /* invalid state                        */
#define ESP_ERR_INVALID_SIZE     0x104   /* invalid size                         */
#define ESP_ERR_NOT_FOUND        0x105   /* requested resource not found         */
#define ESP_ERR_NOT_SUPPORTED    0x106   /* operation or feature not supported   */
#define ESP_ERR_TIMEOUT          0x107   /* operation timed out                  */
#define ESP_ERR_INVALID_RESPONSE 0x108   /* received response was invalid        */
#define ESP_ERR_INVALID_CRC      0x109   /* CRC or checksum was invalid          */
#define ESP_ERR_INVALID_VERSION  0x10A   /* version was invalid                  */
#define ESP_ERR_INVALID_MAC      0x10B   /* MAC address was invalid              */
#define ESP_ERR_NOT_FINISHED     0x10C   /* operation not finished (IDF v5.x)    */
#define ESP_ERR_NOT_ALLOWED      0x10D   /* operation not allowed (IDF v5.x)     */
/* Not a stock IDF code — sim extension so ISR-context timeout paths that
 * distinguish themselves on target still compile and stay distinguishable
 * in logs/test asserts. */
#define ESP_ERR_TIMEOUT_FROM_ISR 0x10E   /* timeout reported from ISR context  */

/* NVS error codes occupy the 0x1100 range in real IDF. */
#define ESP_ERR_NVS_BASE                0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED     (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND           (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH       (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY           (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE    (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_NAME        (ESP_ERR_NVS_BASE + 0x06)
#define ESP_ERR_NVS_INVALID_HANDLE      (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_REMOVE_FAILED       (ESP_ERR_NVS_BASE + 0x08)
#define ESP_ERR_NVS_KEY_TOO_LONG        (ESP_ERR_NVS_BASE + 0x09)
#define ESP_ERR_NVS_PAGE_FULL           (ESP_ERR_NVS_BASE + 0x0A)
#define ESP_ERR_NVS_INVALID_STATE       (ESP_ERR_NVS_BASE + 0x0B)
#define ESP_ERR_NVS_INVALID_LENGTH      (ESP_ERR_NVS_BASE + 0x0C)
#define ESP_ERR_NVS_NO_FREE_PAGES       (ESP_ERR_NVS_BASE + 0x0D)
#define ESP_ERR_NVS_VALUE_TOO_LONG      (ESP_ERR_NVS_BASE + 0x0E)
#define ESP_ERR_NVS_PART_NOT_FOUND      (ESP_ERR_NVS_BASE + 0x0F)
#define ESP_ERR_NVS_NEW_VERSION_FOUND   (ESP_ERR_NVS_BASE + 0x10)
#define ESP_ERR_NVS_XTS_ENCR_FAILED     (ESP_ERR_NVS_BASE + 0x11)
#define ESP_ERR_NVS_XTS_DECR_FAILED     (ESP_ERR_NVS_BASE + 0x12)
#define ESP_ERR_NVS_XTS_ENCR_NOT_SUPPORTED (ESP_ERR_NVS_BASE + 0x13)

/* ULP error base (kept for completeness; only the base is used by code
 * that ranges error values). */
#define ESP_ERR_ULP_BASE        0x1200
#define ESP_ERR_ULP_SIZE_TOO_BIG        (ESP_ERR_ULP_BASE + 1)
#define ESP_ERR_ULP_INVALID_LOAD_ADDR   (ESP_ERR_ULP_BASE + 2)
#define ESP_ERR_ULP_DUPLICATE_LABEL     (ESP_ERR_ULP_BASE + 3)
#define ESP_ERR_ULP_UNDEFINED_LABEL     (ESP_ERR_ULP_BASE + 4)
#define ESP_ERR_ULP_TOO_MANY_LABELS     (ESP_ERR_ULP_BASE + 5)

/**
 * Return a string representation of an esp_err_t ("ESP_OK",
 * "ESP_ERR_NO_MEM", ...).  Unknown values return "UNKNOWN_ERR".
 */
const char *esp_err_to_name(esp_err_t code);

/**
 * ESP_ERROR_CHECK / ESP_ERROR_CHECK_WITHOUT_ABORT — abort-or-print
 * convenience macros matching real IDF behaviour.
 */
void esp_error_check_failed(esp_err_t rc, const char *file, int line,
                            const char *function, const char *expression)
    __attribute__((noreturn));

void esp_error_check_failed_without_abort(esp_err_t rc, const char *file,
                                          int line, const char *function,
                                          const char *expression);

/* Real-IDF spellings (leading underscore) — same functions. */
void _esp_error_check_failed(esp_err_t rc, const char *file, int line,
                             const char *function, const char *expression)
    __attribute__((noreturn));

void _esp_error_check_failed_without_abort(esp_err_t rc, const char *file,
                                           int line, const char *function,
                                           const char *expression);

#define ESP_ERROR_CHECK(x) do {                                         \
        esp_err_t err_rc_ = (x);                                        \
        if (err_rc_ != ESP_OK) {                                        \
            esp_error_check_failed(err_rc_, __FILE__, __LINE__,         \
                                   __FUNCTION__, #x);                   \
        }                                                               \
    } while (0)

#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) do {                           \
        esp_err_t err_rc_ = (x);                                        \
        if (err_rc_ != ESP_OK) {                                        \
            esp_error_check_failed_without_abort(err_rc_, __FILE__,     \
                                   __LINE__, __FUNCTION__, #x);         \
        }                                                               \
    } while (0)

/* Older spellings kept for compatibility. */
#define ESP_ERROR_CHECK_WITHOUT_PRINT(x) ESP_ERROR_CHECK_WITHOUT_ABORT(x)

#ifdef __cplusplus
}
#endif

#endif /* ESP_ERR_H */
