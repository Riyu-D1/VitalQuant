/* cloud_client.h — HTTPS uplink to the VitalQ platform + cloud model.
 *
 * Two flows:
 *  1. Routine batch upload  → POST {base}/v1/ingest/batch   (existing
 *     API shape: BatchIngest JSON, batch_id idempotency; honours the
 *     server's returned clock_correction).
 *  2. Escalation            → POST {base}/v1/edge/escalation
 *     (local model anomaly/uncertain → cloud model re-scores; response
 *     is advisory only — no clinical content in or out).
 *  3. Heartbeat             → POST {base}/v1/device/heartbeat (monitoring).
 *
 * Auth: device key in NVS (`cloud set <url> <key>` via console). TLS on
 * by default; plain HTTP only if the URL scheme is http (lab use).
 * Offline → bundles persist in log_store and drain FIFO on reconnect.
 */
#pragma once
#include "vq_types.h"
#include "decision.h"

vq_err_t vq_cloud_init(void);
vq_err_t vq_cloud_config(const char *base_url, const char *device_key);
bool     vq_cloud_configured(void);

/* Batch upload: drains VQ_LOG_BATCH_OUT records → BatchIngest JSON.
 * batch_id = hash(device,first_t,last_t) for idempotency.             */
vq_err_t vq_cloud_flush(size_t max_records);

/* Escalation: posts the bundle; on 2xx parses {prob, model_id, advice}
 * and calls vq_decision_on_cloud(). Non-blocking: queues to net task.  */
vq_err_t vq_cloud_escalate(const vq_decision_t *dec,
                           const vq_feature_vec_t *features,
                           const uint8_t *windows_blob, size_t blob_len);

/* Heartbeat with power/health summary (monitoring without streaming). */
vq_err_t vq_cloud_heartbeat(void);

/* Stats for self-test/acceptance: attempts, ok, fail, bytes, rtt.     */
void     vq_cloud_stats(uint64_t *ok, uint64_t *fail, uint32_t *rtt_ms);
