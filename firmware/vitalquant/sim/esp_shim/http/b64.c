/* b64.c — real mbedTLS base64 implementation for the host sim.
 *
 * Behaviourally identical to mbedtls library/base64.c (RFC 4648):
 *  - encode: canonical A-Z/a-z/0-9/+/'/' alphabet with '=' padding,
 *    NUL-terminated output, BUFFER_TOO_SMALL reports required size.
 *  - decode: spaces, CR and LF skipped; '=' padding honoured; invalid
 *    characters (and non-pad data after '=') → INVALID_CHARACTER;
 *    BUFFER_TOO_SMALL reports the decoded size.
 */
#include "mbedtls/base64.h"

#include <stdint.h>

static const unsigned char base64_enc_map[64] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
    'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
    'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
};

/* -1 invalid / 64 '=' pad marker / 0-63 sextet (same shape as
 * mbedtls's base64_dec_map lookup, as a function).                  */
static int base64_dec_val(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return 64;
    return -1;
}

int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen)
{
    size_t i, n;
    int C1, C2, C3;
    unsigned char *p;

    if (slen == 0) {
        *olen = 0;
        return 0;
    }

    n = slen / 3 + (slen % 3 != 0);

    if (n > (SIZE_MAX - 1) / 4) {
        *olen = SIZE_MAX;
        return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    }

    n *= 4;

    if (dlen < n + 1 || dst == NULL) {
        *olen = n + 1;
        return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    }

    n = (slen / 3) * 3;

    for (i = 0, p = dst; i < n; i += 3) {
        C1 = *src++;
        C2 = *src++;
        C3 = *src++;

        *p++ = base64_enc_map[(C1 >> 2) & 0x3F];
        *p++ = base64_enc_map[(((C1 &  3) << 4) + (C2 >> 4)) & 0x3F];
        *p++ = base64_enc_map[(((C2 & 15) << 2) + (C3 >> 6)) & 0x3F];
        *p++ = base64_enc_map[C3 & 0x3F];
    }

    if (i < slen) {
        C1 = *src++;
        C2 = ((i + 1) < slen) ? *src++ : 0;

        *p++ = base64_enc_map[(C1 >> 2) & 0x3F];
        *p++ = base64_enc_map[(((C1 & 3) << 4) + (C2 >> 4)) & 0x3F];

        if ((i + 1) < slen)
            *p++ = base64_enc_map[((C2 & 15) << 2) & 0x3F];
        else
            *p++ = '=';

        *p++ = '=';
    }

    *olen = (size_t)(p - dst);
    *p = 0;

    return 0;
}

int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen)
{
    size_t i, n;
    uint32_t j, x;
    unsigned char *p;

    /* First pass: check validity and count output bytes — mirrors
     * mbedtls (spaces allowed only as EOL-adjacent runs, CR/LF pairs,
     * at most two '=' pads, nothing valid after padding).           */
    for (i = n = j = 0; i < slen; i++) {
        /* Skip spaces before checking for EOL */
        x = 0;
        while (i < slen && src[i] == ' ') {
            ++i;
            ++x;
        }

        /* Spaces at end of buffer are OK */
        if (i == slen)
            break;

        if ((slen - i) >= 2 &&
            src[i] == '\r' && src[i + 1] == '\n')
            continue;

        if (src[i] == '\n')
            continue;

        /* Space inside a line is an error */
        if (x != 0)
            return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;

        if (src[i] == '=' && ++j > 2)
            return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;

        if (base64_dec_val(src[i]) < 0)
            return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;

        if (base64_dec_val(src[i]) < 64 && j != 0)
            return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;

        n++;
    }

    if (n == 0) {
        *olen = 0;
        return 0;
    }

    /* n counts every symbol including '='; each pad removes a byte */
    n = ((n * 6) + 7) >> 3;
    n -= j;

    if (dst == NULL || dlen < n) {
        *olen = n;
        return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    }

    /* Second pass: stream sextets through an accumulator — one byte
     * out per 8 accumulated bits.  '=' ends the stream (pass 1 already
     * validated that nothing valid follows the padding).            */
    x = j = 0;
    for (p = dst; i > 0; i--, src++) {
        if (*src == '\r' || *src == '\n' || *src == ' ') {
            i++;
            continue;
        }
        if (*src == '=')
            break;

        x = (x << 6) | (uint32_t)base64_dec_val(*src);
        j += 6;
        if (j >= 8) {
            j -= 8;
            *p++ = (unsigned char)(x >> j);
        }
    }

    *olen = (size_t)(p - dst);

    return 0;
}
