/* sntp_sim.c — sim SNTP service.
 *
 * After esp_netif_sntp_start() a detached worker waits ~10 ms on the
 * vboard clock, then delivers a sync: sntp_sync_time() records the
 * status and invokes the registered notification callback with the
 * "server" time (host wall clock — the same wall epoch a real NTP
 * answer carries). The clock service's sntp_time_sync_cb anchors from
 * that tv, so no privileged host settimeofday() is needed (or wanted).
 *
 * Subsequent syncs fire every sntp_get_sync_interval() — under scenario
 * time-acceleration the firmware's hourly re-arm still exercises.
 */
#define _DEFAULT_SOURCE    /* strdup + gettimeofday under -std=c11 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "esp_err.h"
#include "vboard.h"
#include "sim_net_priv.h"

#ifndef ESP_ERR_NOT_FINISHED
#define ESP_ERR_NOT_FINISHED 0x10C
#endif

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

#define SIM_SNTP_FIRST_SYNC_US   (10 * 1000)     /* ~10 ms to first answer */
#define SIM_SNTP_MIN_INTERVAL_MS 15000u          /* RFC 4330 floor (lwip)  */
#define SIM_SNTP_MAX_SERVERS     8

typedef struct {
    bool                    inited;
    bool                    running;
    unsigned long           gen;           /* bump cancels sync worker  */

    esp_sntp_time_cb_t      sync_cb;       /* per esp_netif_sntp config */
    sntp_sync_time_cb_t     notify_cb;     /* sntp_set_time_sync_…_cb   */
    sntp_sync_mode_t        sync_mode;
    sntp_sync_status_t      status;
    esp_sntp_operatingmode_t opmode;
    uint32_t                interval_ms;
    char                   *servers[SIM_SNTP_MAX_SERVERS];
    int                     nservers;
    pthread_cond_t          cond;          /* sync_wait waiters         */
} sim_sntp_t;

static pthread_mutex_t  s_mtx  = PTHREAD_MUTEX_INITIALIZER;
static sim_sntp_t       s_sntp = {
    .cond = PTHREAD_COND_INITIALIZER,
};

/* ------------------------------------------------------------------ */
/* Sync delivery                                                      */
/* ------------------------------------------------------------------ */

/* The "server answer": host wall clock — real Unix epoch, exactly what
 * NTP would give. (No host settimeofday: the callback's tv is the sync.) */
static void sim_sntp_now(struct timeval *tv)
{
    gettimeofday(tv, NULL);
}

void sntp_sync_time(struct timeval *tv)
{
    pthread_mutex_lock(&s_mtx);
    s_sntp.status = SNTP_SYNC_STATUS_COMPLETED;
    esp_sntp_time_cb_t cb      = s_sntp.sync_cb;
    sntp_sync_time_cb_t notify = s_sntp.notify_cb;
    pthread_cond_broadcast(&s_sntp.cond);
    pthread_mutex_unlock(&s_mtx);

    /* Same order as lwip sntp: time applied, then notifications fire. */
    if (notify != NULL) notify(tv);
    if (cb != NULL && cb != (esp_sntp_time_cb_t)notify) cb(tv);
}

static void *sntp_worker(void *arg)
{
    const unsigned long gen = (unsigned long)arg;

    /* First answer ~10 ms after start. */
    vboard_clock_sleep_us(SIM_SNTP_FIRST_SYNC_US);

    for (;;) {
        pthread_mutex_lock(&s_mtx);
        bool live = s_sntp.running && s_sntp.inited && s_sntp.gen == gen;
        uint32_t interval = s_sntp.interval_ms;
        pthread_mutex_unlock(&s_mtx);
        if (!live) return NULL;

        struct timeval tv;
        sim_sntp_now(&tv);
        sntp_sync_time(&tv);

        /* Re-sync every interval (poll mode) — then loop. */
        vboard_clock_sleep_us((int64_t)interval * 1000LL);
    }
}

static void sntp_schedule(void)
{
    /* Caller holds s_mtx. */
    pthread_t t;
    if (pthread_create(&t, NULL, sntp_worker, (void *)s_sntp.gen) == 0) {
        pthread_detach(t);
    }
}

/* ------------------------------------------------------------------ */
/* esp_netif_sntp layer                                               */
/* ------------------------------------------------------------------ */

esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    if (!sim_netif_inited()) return ESP_ERR_INVALID_STATE;  /* tcpip_exec dep */

    pthread_mutex_lock(&s_mtx);
    if (s_sntp.inited) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    s_sntp.inited   = true;
    s_sntp.sync_cb  = config->sync_cb;
    s_sntp.sync_mode = config->smooth_sync ? SNTP_SYNC_MODE_SMOOTH
                                           : SNTP_SYNC_MODE_IMMED;
    s_sntp.nservers = 0;
    for (size_t i = 0;
         i < config->num_of_servers && i < SIM_SNTP_MAX_SERVERS; i++) {
        if (config->servers[i] != NULL) {
            s_sntp.servers[s_sntp.nservers++] = strdup(config->servers[i]);
        }
    }
    if (s_sntp.interval_ms == 0) s_sntp.interval_ms = 3600000u;
    bool autostart = config->start;
    pthread_mutex_unlock(&s_mtx);

    if (autostart) return esp_netif_sntp_start();
    return ESP_OK;
}

esp_err_t esp_netif_sntp_start(void)
{
    pthread_mutex_lock(&s_mtx);
    if (!s_sntp.inited) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    bool restart = s_sntp.running;
    s_sntp.running = true;
    s_sntp.gen++;                        /* retire any old worker       */
    sntp_schedule();
    pthread_mutex_unlock(&s_mtx);
    (void)restart;
    return ESP_OK;
}

void esp_netif_sntp_deinit(void)
{
    pthread_mutex_lock(&s_mtx);
    s_sntp.inited  = false;
    s_sntp.running = false;
    s_sntp.gen++;
    s_sntp.sync_cb = NULL;
    for (int i = 0; i < s_sntp.nservers; i++) {
        free(s_sntp.servers[i]);
        s_sntp.servers[i] = NULL;
    }
    s_sntp.nservers = 0;
    s_sntp.status   = SNTP_SYNC_STATUS_RESET;
    pthread_cond_broadcast(&s_sntp.cond);
    pthread_mutex_unlock(&s_mtx);
}

esp_err_t esp_netif_sntp_sync_wait(TickType_t tout)
{
    /* Poll the status on the vboard clock; treat a tick as 1 ms. */
    uint32_t waited = 0;
    const uint32_t budget = (uint32_t)(tout > 0 ? tout : 1);
    for (;;) {
        pthread_mutex_lock(&s_mtx);
        sntp_sync_status_t st = s_sntp.status;
        pthread_mutex_unlock(&s_mtx);
        if (st == SNTP_SYNC_STATUS_COMPLETED) return ESP_OK;
        if (st == SNTP_SYNC_STATUS_IN_PROGRESS) return ESP_ERR_NOT_FINISHED;
        if (waited >= budget) return ESP_ERR_TIMEOUT;
        vboard_clock_sleep_us(1000);
        waited += 1;
    }
}

esp_err_t esp_netif_sntp_reachability(unsigned int index,
                                    unsigned int *reachability)
{
    if (reachability == NULL || index >= SIM_SNTP_MAX_SERVERS) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_mtx);
    if (!s_sntp.inited) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    /* All 8 probes answered while running; 0 otherwise. */
    *reachability = (s_sntp.running && index < (unsigned)s_sntp.nservers)
                        ? 0xFFu : 0u;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* esp_sntp.h surface                                                 */
/* ------------------------------------------------------------------ */

void sntp_set_sync_mode(sntp_sync_mode_t sync_mode)
{
    pthread_mutex_lock(&s_mtx);
    s_sntp.sync_mode = sync_mode;
    pthread_mutex_unlock(&s_mtx);
}

sntp_sync_mode_t sntp_get_sync_mode(void)
{
    pthread_mutex_lock(&s_mtx);
    sntp_sync_mode_t m = s_sntp.sync_mode;
    pthread_mutex_unlock(&s_mtx);
    return m;
}

sntp_sync_status_t sntp_get_sync_status(void)
{
    pthread_mutex_lock(&s_mtx);
    sntp_sync_status_t st = s_sntp.status;
    /* lwip semantics: read-and-clear after a completed sync. */
    if (st == SNTP_SYNC_STATUS_COMPLETED) {
        s_sntp.status = SNTP_SYNC_STATUS_RESET;
    }
    pthread_mutex_unlock(&s_mtx);
    return st;
}

void sntp_set_sync_status(sntp_sync_status_t sync_status)
{
    pthread_mutex_lock(&s_mtx);
    s_sntp.status = sync_status;
    pthread_mutex_unlock(&s_mtx);
}

void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t callback)
{
    pthread_mutex_lock(&s_mtx);
    s_sntp.notify_cb = callback;
    pthread_mutex_unlock(&s_mtx);
}

void sntp_set_sync_interval(uint32_t interval_ms)
{
    pthread_mutex_lock(&s_mtx);
    if (interval_ms < SIM_SNTP_MIN_INTERVAL_MS) {
        interval_ms = SIM_SNTP_MIN_INTERVAL_MS;    /* same clamp as lwip */
    }
    s_sntp.interval_ms = interval_ms;
    pthread_mutex_unlock(&s_mtx);
}

uint32_t sntp_get_sync_interval(void)
{
    pthread_mutex_lock(&s_mtx);
    uint32_t i = s_sntp.interval_ms;
    pthread_mutex_unlock(&s_mtx);
    return i;
}

bool sntp_restart(void)
{
    pthread_mutex_lock(&s_mtx);
    if (!s_sntp.inited) {
        pthread_mutex_unlock(&s_mtx);
        return false;
    }
    s_sntp.running = true;
    s_sntp.gen++;
    sntp_schedule();
    pthread_mutex_unlock(&s_mtx);
    return true;
}

void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t operating_mode)
{
    pthread_mutex_lock(&s_mtx);
    s_sntp.opmode = operating_mode;
    pthread_mutex_unlock(&s_mtx);
}

esp_sntp_operatingmode_t esp_sntp_getoperatingmode(void)
{
    pthread_mutex_lock(&s_mtx);
    esp_sntp_operatingmode_t m = s_sntp.opmode;
    pthread_mutex_unlock(&s_mtx);
    return m;
}

void esp_sntp_init(void)
{
    /* Legacy direct API: init with defaults + start. */
    pthread_mutex_lock(&s_mtx);
    if (!s_sntp.inited) s_sntp.inited = true;
    s_sntp.running = true;
    s_sntp.gen++;
    sntp_schedule();
    pthread_mutex_unlock(&s_mtx);
}

void esp_sntp_stop(void)
{
    pthread_mutex_lock(&s_mtx);
    s_sntp.running = false;
    s_sntp.gen++;
    pthread_mutex_unlock(&s_mtx);
}

void esp_sntp_setserver(uint8_t idx, const esp_ip_addr_t *addr)
{
    (void)idx; (void)addr;    /* sim only models server names */
}

void esp_sntp_setservername(uint8_t idx, const char *server)
{
    pthread_mutex_lock(&s_mtx);
    if (idx < SIM_SNTP_MAX_SERVERS) {
        free(s_sntp.servers[idx]);
        s_sntp.servers[idx] = server ? strdup(server) : NULL;
        if (idx >= s_sntp.nservers) s_sntp.nservers = idx + 1;
    }
    pthread_mutex_unlock(&s_mtx);
}

const char *esp_sntp_getservername(uint8_t idx)
{
    pthread_mutex_lock(&s_mtx);
    const char *s = (idx < SIM_SNTP_MAX_SERVERS) ? s_sntp.servers[idx] : NULL;
    pthread_mutex_unlock(&s_mtx);
    return s;
}

const esp_ip_addr_t *esp_sntp_getserver(uint8_t idx)
{
    (void)idx;
    return NULL;              /* names only in the sim */
}

bool esp_sntp_enabled(void)
{
    pthread_mutex_lock(&s_mtx);
    bool r = s_sntp.inited && s_sntp.running;
    pthread_mutex_unlock(&s_mtx);
    return r;
}

uint8_t esp_sntp_getreachability(uint8_t idx)
{
    pthread_mutex_lock(&s_mtx);
    uint8_t r = (s_sntp.running && idx < (uint8_t)s_sntp.nservers) ? 0xFF : 0;
    pthread_mutex_unlock(&s_mtx);
    return r;
}
