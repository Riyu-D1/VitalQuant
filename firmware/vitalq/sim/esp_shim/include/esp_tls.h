/* esp_tls.h — host-sim shim: opaque types only.
 *
 * TLS is off-path in the sim (esp_http_client downgrades https:// to
 * plain TCP), so nothing is defined here beyond the opaque types the
 * ESP-IDF API surface names.  No functions are provided.
 */
#ifndef ESP_TLS_H
#define ESP_TLS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_tls              esp_tls_t;
typedef struct esp_tls_cfg          esp_tls_cfg_t;
typedef struct esp_tls_cfg_server   esp_tls_cfg_server_t;
typedef struct esp_tls_error_handle esp_tls_error_handle_t;
typedef struct esp_tls_errors       esp_tls_errors_t;
typedef struct psk_key_hint         psk_key_hint_t;

/* Error categories (kept as plain codes — no error object exists). */
#define ESP_TLS_ERR_TYPE_UNKNOWN        0
#define ESP_TLS_ERR_TYPE_SYSTEM         1
#define ESP_TLS_ERR_TYPE_MBEDTLS        2
#define ESP_TLS_ERR_TYPE_WOLFSSL        3
#define ESP_TLS_ERR_TYPE_CERT_VERIFY    4

typedef enum {
    ESP_TLS_INIT = 0,
    ESP_TLS_CLIENT_CONNECTING,
    ESP_TLS_HANDSHAKE,
    ESP_TLS_FAIL,
    ESP_TLS_DONE,
} esp_tls_conn_state_t;

#ifdef __cplusplus
}
#endif
#endif /* ESP_TLS_H */
