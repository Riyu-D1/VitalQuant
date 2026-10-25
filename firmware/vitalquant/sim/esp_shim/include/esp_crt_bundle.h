/* esp_crt_bundle.h — host-sim stub of the ESP-IDF x509 certificate
 * bundle API.
 *
 * TLS is off-path in the sim (see esp_http_client.h), so the bundle
 * attach is a successful no-op: firmware still passes
 * cfg.crt_bundle_attach = esp_crt_bundle_attach exactly as on target.
 */
#ifndef ESP_CRT_BUNDLE_H
#define ESP_CRT_BUNDLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#endif
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL (-1)
#endif

/* No-op: "attaching" the bundle always succeeds — there is no TLS
 * session to configure in the host sim. */
static inline esp_err_t esp_crt_bundle_attach(void *conf)
{ (void)conf; return ESP_OK; }

static inline void esp_crt_bundle_detach(void *conf)
{ (void)conf; }

static inline esp_err_t esp_crt_bundle_set(const void *x509_bundle,
                                           size_t bundle_size)
{ (void)x509_bundle; (void)bundle_size; return ESP_OK; }

#ifdef __cplusplus
}
#endif
#endif /* ESP_CRT_BUNDLE_H */
