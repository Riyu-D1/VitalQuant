/* esp_sntp.h — host-sim shim of the lwIP SNTP API.
 *
 * Same surface as the target header; u8_t → uint8_t and ip_addr_t →
 * esp_ip_addr_t so we don't pull lwIP into the host build.
 * Implemented by sim/esp_shim/net/sntp_sim.c.
 */
#ifndef __ESP_SNTP_H__
#define __ESP_SNTP_H__

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Aliases for esp_sntp prefixed API (inherently thread safe) */
#define esp_sntp_sync_time sntp_sync_time
#define esp_sntp_set_sync_mode sntp_set_sync_mode
#define esp_sntp_get_sync_mode sntp_get_sync_mode
#define esp_sntp_get_sync_status sntp_get_sync_status
#define esp_sntp_set_sync_status sntp_set_sync_status
#define esp_sntp_set_time_sync_notification_cb sntp_set_time_sync_notification_cb
#define esp_sntp_set_sync_interval sntp_set_sync_interval
#define esp_sntp_get_sync_interval sntp_get_sync_interval
#define esp_sntp_restart sntp_restart

#ifndef SNTP_OPMODE_POLL
#define SNTP_OPMODE_POLL ESP_SNTP_OPMODE_POLL
#endif /* SNTP_OPMODE_POLL */

/// SNTP time update mode
typedef enum {
    SNTP_SYNC_MODE_IMMED,   /*!< Update system time immediately when receiving a response from the SNTP server. */
    SNTP_SYNC_MODE_SMOOTH,  /*!< Smooth time updating. */
} sntp_sync_mode_t;

/// SNTP sync status
typedef enum {
    SNTP_SYNC_STATUS_RESET,         // Reset status.
    SNTP_SYNC_STATUS_COMPLETED,     // Time is synchronized.
    SNTP_SYNC_STATUS_IN_PROGRESS,   // Smooth time sync in progress.
} sntp_sync_status_t;

/// SNTP operating modes per lwip SNTP module
typedef enum {
    ESP_SNTP_OPMODE_POLL,
    ESP_SNTP_OPMODE_LISTENONLY,
} esp_sntp_operatingmode_t;

/**
 * @brief SNTP callback function for notifying about time sync event
 *
 * @param tv Time received from SNTP server.
 */
typedef void (*sntp_sync_time_cb_t) (struct timeval *tv);

/**
 * @brief Updates the system time (sim: records + notifies cb).
 */
void sntp_sync_time(struct timeval *tv);

void sntp_set_sync_mode(sntp_sync_mode_t sync_mode);
sntp_sync_mode_t sntp_get_sync_mode(void);

sntp_sync_status_t sntp_get_sync_status(void);
void sntp_set_sync_status(sntp_sync_status_t sync_status);

void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t callback);

void sntp_set_sync_interval(uint32_t interval_ms);
uint32_t sntp_get_sync_interval(void);

/**
 * @brief Restart SNTP — reschedules the next sync.
 */
bool sntp_restart(void);

void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t operating_mode);

/**
 * @brief Init and start SNTP service
 */
void esp_sntp_init(void);

/**
 * @brief Stops SNTP service
 */
void esp_sntp_stop(void);

void esp_sntp_setserver(uint8_t idx, const esp_ip_addr_t *addr);
void esp_sntp_setservername(uint8_t idx, const char *server);
const char *esp_sntp_getservername(uint8_t idx);
const esp_ip_addr_t *esp_sntp_getserver(uint8_t idx);

bool esp_sntp_enabled(void);

uint8_t esp_sntp_getreachability(uint8_t idx);

esp_sntp_operatingmode_t esp_sntp_getoperatingmode(void);

/* Translating inlines mirroring the target header (deprecated legacy API) */
static inline __attribute__((deprecated("use esp_sntp_setoperatingmode() instead")))
void sntp_setoperatingmode(uint8_t operating_mode)
{
    esp_sntp_setoperatingmode((esp_sntp_operatingmode_t)operating_mode);
}

static inline __attribute__((deprecated("use esp_sntp_servermode_dhcp() instead")))
void sntp_servermode_dhcp(int set_servers_from_dhcp)
{
    (void)set_servers_from_dhcp;
}

static inline __attribute__((deprecated("use esp_sntp_setservername() instead")))
void sntp_setservername(uint8_t idx, const char *server)
{
    esp_sntp_setservername(idx, server);
}

static inline __attribute__((deprecated("use esp_sntp_init() instead")))
void sntp_init(void)
{
    esp_sntp_init();
}

static inline __attribute__((deprecated("use esp_sntp_getservername() instead")))
const char *sntp_getservername(uint8_t idx)
{
    return esp_sntp_getservername(idx);
}

static inline __attribute__((deprecated("use esp_sntp_getserver() instead")))
const esp_ip_addr_t *sntp_getserver(uint8_t idx)
{
    return esp_sntp_getserver(idx);
}

static inline __attribute__((deprecated("use esp_sntp_getreachability() instead")))
uint8_t sntp_getreachability(uint8_t idx)
{
    return esp_sntp_getreachability(idx);
}

static inline __attribute__((deprecated("use esp_sntp_getoperatingmode() instead")))
esp_sntp_operatingmode_t sntp_getoperatingmode(void)
{
    return esp_sntp_getoperatingmode();
}

#ifdef __cplusplus
}
#endif

#endif // __ESP_SNTP_H__
