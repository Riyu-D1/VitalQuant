/* log_store.h — W25Q512 (64 MB, CS=IO16) append-only sample/event store.
 *
 * 512 Mbit → 4-BYTE addressing required (4B opcodes or enter-4B mode).
 * /WP and /HOLD are tied to +3V3. Deep-sleep prep must issue power-down
 * (B9h); wake uses release-power-down (ABh) + ID resync.
 *
 * Records are CRC-protected, atomic-per-sector, wear-levelled across a
 * ring of 4 KB sectors. Purpose: survive Wi-Fi loss (offline_buffer),
 * hold escalation bundles, and keep self-test + event logs.
 */
#pragma once
#include "vq_types.h"

typedef struct {
    uint32_t id;          /* monotonically increasing record id        */
    uint32_t type;        /* VQ_LOG_*                                  */
    int64_t  t_us;        /* esp_timer stamp                           */
    uint32_t len;         /* payload bytes                             */
    /* payload follows, CRC32(header+payload) trailer                    */
} vq_log_hdr_t;

#define VQ_LOG_SAMPLES    1
#define VQ_LOG_EVENT      2
#define VQ_LOG_ESCALATION 3
#define VQ_LOG_BATCH_OUT  4   /* queued for upload                      */

/* Largest payload vq_log_append() accepts: one 4096 B flash sector minus
 * the 20 B record header + 4 B CRC trailer. Callers batching samples
 * MUST stay under this — oversized appends are rejected with
 * ESP_ERR_INVALID_SIZE and the data is dropped.                        */
#define VQ_LOG_MAX_PAYLOAD  (4096u - 24u)

vq_err_t vq_log_init(void);            /* probe JEDEC EF 40 20, map ring */
bool     vq_log_flash_present(void);
vq_err_t vq_log_append(uint32_t type, const void *payload, uint32_t len);
/* Iterate oldest→newest; cb returns false to stop. For upload drain.   */
vq_err_t vq_log_iterate(uint32_t type,
                        bool (*cb)(const vq_log_hdr_t *h, const uint8_t *payload, void *ctx),
                        void *ctx);
vq_err_t vq_log_drop_oldest(uint32_t n);   /* frees ring space           */
void     vq_log_space(uint32_t *used, uint32_t *total);
vq_err_t vq_log_sleep(void);              /* W25Q512 power-down          */
vq_err_t vq_log_wake(void);
/* Self-test: JEDEC id, 4B addressing check, sector R/W/E verify.        */
vq_err_t vq_log_selftest(char *report, size_t len);
