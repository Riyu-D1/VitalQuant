/* mbedtls/base64.h — mbedTLS base64 API for the host sim.
 *
 * Implemented for real in sim/esp_shim/http/b64.c (RFC 4648, mbedtls
 * semantics: '=' padding, whitespace-tolerant decode, the mbedtls
 * error-code contract).
 */
#ifndef MBEDTLS_BASE64_H
#define MBEDTLS_BASE64_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL   (-0x002A)
#define MBEDTLS_ERR_BASE64_INVALID_CHARACTER  (-0x002C)

/*
 * Encode a buffer into base64 and write the NUL-terminated result.
 * Returns 0 on success (*olen = encoded length, excluding the NUL),
 * MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL if dlen is insufficient
 * (*olen = required size including the NUL).
 */
int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen);

/*
 * Decode a base64 buffer.  Spaces/CR/LF are skipped; '=' padding is
 * honoured.  Returns 0 on success (*olen = decoded length),
 * MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL if dlen is insufficient
 * (*olen = required size), or MBEDTLS_ERR_BASE64_INVALID_CHARACTER
 * on malformed input.
 */
int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen);

#ifdef __cplusplus
}
#endif
#endif /* MBEDTLS_BASE64_H */
