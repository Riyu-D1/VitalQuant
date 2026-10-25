/* Host-sim esp_random.h — pseudo-random numbers for the firmware.
 *
 * On target, esp_random() mixes true hardware entropy once Wi-Fi/BT is
 * up.  In the sim it is a host PRNG (arc4random where available,
 * rand_r fallback) — good enough for backoff jitter and test nonces,
 * NOT a faithful entropy model.
 */
#ifndef ESP_RANDOM_H
#define ESP_RANDOM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return one random 32-bit word. */
uint32_t esp_random(void);

/** Fill a buffer with random bytes. */
void esp_fill_random(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ESP_RANDOM_H */
