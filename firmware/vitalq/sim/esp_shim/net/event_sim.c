/* event_sim.c — sim implementation of the ESP-IDF event loop.
 *
 * Semantics mirrored from the target:
 *  - One "default" loop; handlers run serialised on a dedicated thread
 *    (the sys_evt task equivalent) — never on the caller's context.
 *  - esp_event_post() deep-copies the payload into the queue; the copy is
 *    freed after dispatch.
 *  - Matching: exact base pointer (or string-equal, for robustness across
 *    TUs), or ESP_EVENT_ANY_BASE; id equal or ESP_EVENT_ANY_ID.
 *  - Posting from inside a handler is legal (no lock held during dispatch);
 *    registering/unregistering from a handler is legal too — dispatch uses
 *    a snapshot.
 *
 * Thread-safety: a single mutex guards the handler table + event queue +
 * loop state; a condvar wakes the dispatcher.
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_event.h"
#include "esp_err.h"
#include "vboard.h"

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

typedef struct handler_node {
    esp_event_base_t              base;      /* NULL = any base          */
    int32_t                       id;        /* ESP_EVENT_ANY_ID = any   */
    esp_event_handler_t           fn;
    void                         *arg;
    esp_event_loop_handle_t       loop;      /* NULL = default loop      */
    struct handler_node          *next;
} handler_node_t;

typedef struct event_node {
    esp_event_base_t        base;
    int32_t                 id;
    void                   *data;           /* heap copy, freed post-dispatch */
    size_t                  data_len;
    esp_event_loop_handle_t loop;           /* NULL = default loop      */
    struct event_node      *next;
} event_node_t;

static pthread_mutex_t  s_mtx      = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   s_cond     = PTHREAD_COND_INITIALIZER;
static pthread_t        s_thread;
static bool             s_thread_run;    /* dispatcher keeps running    */
static bool             s_loop_created;  /* default loop exists         */
static handler_node_t  *s_handlers;      /* registered handlers          */
static event_node_t    *s_q_head, *s_q_tail;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static bool base_match(esp_event_base_t h_base, esp_event_base_t ev_base)
{
    if (h_base == ESP_EVENT_ANY_BASE) return true;
    if (h_base == ev_base)            return true;   /* same object (usual) */
    if (h_base != NULL && ev_base != NULL && strcmp(h_base, ev_base) == 0) {
        return true;                                 /* cross-TU safety     */
    }
    return false;
}

/* Caller holds s_mtx. Returns a malloc'd snapshot array of matching
 * handlers so dispatch can run without holding the lock (handlers are
 * free to re-enter register/unregister/post). */
static handler_node_t **snapshot_handlers(esp_event_base_t base, int32_t id,
                                          esp_event_loop_handle_t loop,
                                          int *out_n)
{
    int n = 0;
    for (handler_node_t *h = s_handlers; h != NULL; h = h->next) {
        if (h->loop == loop && base_match(h->base, base) &&
            (h->id == ESP_EVENT_ANY_ID || h->id == id)) {
            n++;
        }
    }
    if (n == 0) { *out_n = 0; return NULL; }

    handler_node_t **arr = malloc((size_t)n * sizeof(*arr));
    if (arr == NULL) { *out_n = 0; return NULL; }
    int i = 0;
    for (handler_node_t *h = s_handlers; h != NULL && i < n; h = h->next) {
        if (h->loop == loop && base_match(h->base, base) &&
            (h->id == ESP_EVENT_ANY_ID || h->id == id)) {
            arr[i++] = h;
        }
    }
    *out_n = n;
    return arr;
}

/* ------------------------------------------------------------------ */
/* Dispatch thread ("sys_evt task")                                   */
/* ------------------------------------------------------------------ */

static void *event_thread_main(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&s_mtx);
    for (;;) {
        while (s_q_head == NULL && s_thread_run) {
            pthread_cond_wait(&s_cond, &s_mtx);
        }
        if (!s_thread_run && s_q_head == NULL) break;

        event_node_t *ev = s_q_head;
        if (ev == NULL) continue;
        s_q_head = ev->next;
        if (s_q_head == NULL) s_q_tail = NULL;

        int n = 0;
        handler_node_t **hs = snapshot_handlers(ev->base, ev->id, ev->loop, &n);
        pthread_mutex_unlock(&s_mtx);

        for (int i = 0; i < n; i++) {
            /* A handler unregistered after the snapshot still fires —
             * matches IDF, where events in-flight complete delivery. */
            hs[i]->fn(hs[i]->arg, ev->base, ev->id, ev->data);
        }
        free(hs);
        free(ev->data);
        free(ev);

        pthread_mutex_lock(&s_mtx);
    }
    pthread_mutex_unlock(&s_mtx);
    return NULL;
}

/* Caller does NOT hold the lock. Starts the loop if needed. */
static esp_err_t ensure_loop_locked(void)
{
    /* s_mtx must be held by caller */
    if (s_thread_run) return ESP_OK;
    s_thread_run = true;
    if (pthread_create(&s_thread, NULL, event_thread_main, NULL) != 0) {
        s_thread_run = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ensure_loop(void)
{
    pthread_mutex_lock(&s_mtx);
    esp_err_t r = ensure_loop_locked();
    pthread_mutex_unlock(&s_mtx);
    return r;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

esp_err_t esp_event_loop_create(const esp_event_loop_args_t *args,
                                esp_event_loop_handle_t *event_loop)
{
    (void)args;
    if (event_loop == NULL) return ESP_ERR_INVALID_ARG;
    /* Sim models a single backing loop; "custom" handles share it. */
    esp_err_t r = ensure_loop();
    if (r != ESP_OK) return r;
    *event_loop = (esp_event_loop_handle_t)0x51E7uL; /* non-NULL token */
    return ESP_OK;
}

esp_err_t esp_event_loop_delete(esp_event_loop_handle_t event_loop)
{
    (void)event_loop;
    return esp_event_loop_delete_default();
}

esp_err_t esp_event_loop_create_default(void)
{
    pthread_mutex_lock(&s_mtx);
    if (s_loop_created) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    s_loop_created = true;
    esp_err_t r = ensure_loop_locked();
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_err_t esp_event_loop_delete_default(void)
{
    pthread_mutex_lock(&s_mtx);
    if (!s_loop_created) {
        pthread_mutex_unlock(&s_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    s_loop_created = false;
    s_thread_run   = false;
    pthread_cond_signal(&s_cond);
    pthread_mutex_unlock(&s_mtx);

    pthread_join(s_thread, NULL);

    pthread_mutex_lock(&s_mtx);
    /* Drop queued-but-undelivered events and the handler table (IDF
     * deletes everything with the loop). */
    event_node_t *ev = s_q_head;
    while (ev != NULL) {
        event_node_t *nx = ev->next;
        free(ev->data);
        free(ev);
        ev = nx;
    }
    s_q_head = s_q_tail = NULL;
    handler_node_t *h = s_handlers;
    while (h != NULL) {
        handler_node_t *nx = h->next;
        free(h);
        h = nx;
    }
    s_handlers = NULL;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_event_loop_run(esp_event_loop_handle_t event_loop,
                             TickType_t ticks_to_run)
{
    (void)event_loop; (void)ticks_to_run;
    /* Events are dispatched by the dedicated thread; "running" the loop
     * just yields to let it drain. */
    vboard_clock_sleep_us(1000);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Registration                                                       */
/* ------------------------------------------------------------------ */

esp_err_t esp_event_handler_instance_register_with(
        esp_event_loop_handle_t event_loop,
        esp_event_base_t event_base,
        int32_t event_id,
        esp_event_handler_t event_handler,
        void *event_handler_arg,
        esp_event_handler_instance_t *instance)
{
    if (event_handler == NULL) return ESP_ERR_INVALID_ARG;

    handler_node_t *h = calloc(1, sizeof(*h));
    if (h == NULL) return ESP_ERR_NO_MEM;
    h->base = event_base;
    h->id   = event_id;
    h->fn   = event_handler;
    h->arg  = event_handler_arg;
    h->loop = event_loop;

    pthread_mutex_lock(&s_mtx);
    h->next    = s_handlers;
    s_handlers = h;
    pthread_mutex_unlock(&s_mtx);

    if (instance != NULL) *instance = h;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(
        esp_event_base_t event_base,
        int32_t event_id,
        esp_event_handler_t event_handler,
        void *event_handler_arg,
        esp_event_handler_instance_t *instance)
{
    return esp_event_handler_instance_register_with(
            NULL, event_base, event_id, event_handler,
            event_handler_arg, instance);
}

esp_err_t esp_event_handler_register_with(esp_event_loop_handle_t event_loop,
                                          esp_event_base_t event_base,
                                          int32_t event_id,
                                          esp_event_handler_t event_handler,
                                          void *event_handler_arg)
{
    return esp_event_handler_instance_register_with(
            event_loop, event_base, event_id, event_handler,
            event_handler_arg, NULL);
}

esp_err_t esp_event_handler_register(esp_event_base_t event_base,
                                     int32_t event_id,
                                     esp_event_handler_t event_handler,
                                     void *event_handler_arg)
{
    return esp_event_handler_instance_register_with(
            NULL, event_base, event_id, event_handler,
            event_handler_arg, NULL);
}

/* ------------------------------------------------------------------ */
/* Unregistration                                                     */
/* ------------------------------------------------------------------ */

esp_err_t esp_event_handler_instance_unregister_with(
        esp_event_loop_handle_t event_loop,
        esp_event_base_t event_base,
        int32_t event_id,
        esp_event_handler_instance_t instance)
{
    if (instance == NULL) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_mtx);
    handler_node_t **pp = &s_handlers;
    esp_err_t r = ESP_ERR_NOT_FOUND;
    while (*pp != NULL) {
        handler_node_t *h = *pp;
        if (h == (handler_node_t *)instance &&
            h->loop == event_loop &&
            (event_base == ESP_EVENT_ANY_BASE || h->base == event_base) &&
            (event_id == ESP_EVENT_ANY_ID || h->id == event_id)) {
            *pp = h->next;
            free(h);
            r = ESP_OK;
            continue;   /* ANY matching instances can all go */
        }
        pp = &h->next;
    }
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t event_base,
                                                int32_t event_id,
                                                esp_event_handler_instance_t instance)
{
    return esp_event_handler_instance_unregister_with(
            NULL, event_base, event_id, instance);
}

esp_err_t esp_event_handler_unregister_with(esp_event_loop_handle_t event_loop,
                                            esp_event_base_t event_base,
                                            int32_t event_id,
                                            esp_event_handler_t event_handler)
{
    if (event_handler == NULL) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_mtx);
    handler_node_t **pp = &s_handlers;
    esp_err_t r = ESP_ERR_NOT_FOUND;
    while (*pp != NULL) {
        handler_node_t *h = *pp;
        if (h->loop == event_loop && h->fn == event_handler &&
            (event_base == ESP_EVENT_ANY_BASE || h->base == event_base) &&
            (event_id == ESP_EVENT_ANY_ID || h->id == event_id)) {
            *pp = h->next;
            free(h);
            r = ESP_OK;
            continue;
        }
        pp = &h->next;
    }
    pthread_mutex_unlock(&s_mtx);
    return r;
}

esp_err_t esp_event_handler_unregister(esp_event_base_t event_base,
                                       int32_t event_id,
                                       esp_event_handler_t event_handler)
{
    return esp_event_handler_unregister_with(
            NULL, event_base, event_id, event_handler);
}

/* ------------------------------------------------------------------ */
/* Posting                                                            */
/* ------------------------------------------------------------------ */

esp_err_t esp_event_post_to(esp_event_loop_handle_t event_loop,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            const void *event_data,
                            size_t event_data_size,
                            TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;   /* unbounded queue — never blocks in sim */
    if (event_base == ESP_EVENT_ANY_BASE) return ESP_ERR_INVALID_ARG;
    if (event_data == NULL && event_data_size != 0) return ESP_ERR_INVALID_ARG;

    event_node_t *ev = calloc(1, sizeof(*ev));
    if (ev == NULL) return ESP_ERR_NO_MEM;
    ev->base     = event_base;
    ev->id       = event_id;
    ev->data_len = event_data_size;
    ev->loop     = event_loop;
    if (event_data_size != 0) {
        ev->data = malloc(event_data_size);
        if (ev->data == NULL) { free(ev); return ESP_ERR_NO_MEM; }
        memcpy(ev->data, event_data, event_data_size);
    }

    esp_err_t r = ensure_loop();          /* auto-start so posts aren't lost */
    if (r != ESP_OK) { free(ev->data); free(ev); return r; }

    pthread_mutex_lock(&s_mtx);
    if (s_q_tail != NULL) s_q_tail->next = ev;
    else                  s_q_head       = ev;
    s_q_tail = ev;
    pthread_cond_signal(&s_cond);
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

esp_err_t esp_event_post(esp_event_base_t event_base,
                         int32_t event_id,
                         const void *event_data,
                         size_t event_data_size,
                         TickType_t ticks_to_wait)
{
    return esp_event_post_to(NULL, event_base, event_id,
                             event_data, event_data_size, ticks_to_wait);
}

esp_err_t esp_event_isr_post_to(esp_event_loop_handle_t event_loop,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                const void *event_data,
                                size_t event_data_size,
                                TickType_t ticks_to_wait)
{
    return esp_event_post_to(event_loop, event_base, event_id,
                             event_data, event_data_size, ticks_to_wait);
}

esp_err_t esp_event_isr_post(esp_event_base_t event_base,
                             int32_t event_id,
                             const void *event_data,
                             size_t event_data_size,
                             TickType_t ticks_to_wait)
{
    return esp_event_post_to(NULL, event_base, event_id,
                             event_data, event_data_size, ticks_to_wait);
}

esp_err_t esp_event_dump_handler_instances(esp_event_loop_handle_t event_loop,
                                           void *file)
{
    FILE *f = (file != NULL) ? (FILE *)file : stdout;
    pthread_mutex_lock(&s_mtx);
    for (handler_node_t *h = s_handlers; h != NULL; h = h->next) {
        fprintf(f, "VB-EVT handler base=%s id=%ld loop=%p\n",
                h->base ? h->base : "<any>", (long)h->id,
                (void *)h->loop);
    }
    pthread_mutex_unlock(&s_mtx);
    (void)event_loop;
    return ESP_OK;
}
