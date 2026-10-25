/* Host-sim heap_caps — plain host malloc underneath, but every block
 * is charged against a per-pool byte budget so the sim catches memory
 * overruns the target would hit (see esp_heap_caps.h for the policy).
 *
 * Allocation metadata (pool/size) lives in a SIDE MAP keyed by pointer —
 * NOT a prepended header.  On real IDF, free() and heap_caps_free() hit
 * the same unified heap, so firmware legally mixes malloc/free/
 * heap_caps_* in either direction.  A header made plain free() abort
 * ("free(): invalid pointer") — a crash class the target never has.
 * With the side map, a plain free() on a heap_caps block simply works;
 * the stale map slot is harmless (bounded drift, reclaimed on reuse).  */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <malloc.h>

#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "vboard.h"

#define TAG "heap"

/* ------------------------------------------------------------------ */
/*  Pools                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    POOL_INTERNAL = 0,
    POOL_SPIRAM   = 1,
    POOL__N
} pool_t;

#ifndef CONFIG_SPIRAM_SIZE
#define CONFIG_SPIRAM_SIZE (8u * 1024u * 1024u)
#endif
#define SPIRAM_BYTES ((size_t)CONFIG_SPIRAM_SIZE)

/* Internal SRAM on ESP32-S3 is 512 KiB physical; the realistic free
 * heap after stacks/BSS is smaller, but we keep the full physical size
 * as the budget so the sim stays lenient while still bounding runaway
 * internal allocations.  Override with VQ_SIM_INTERNAL_HEAP_BYTES. */
#define INTERNAL_BYTES_DEFAULT ((size_t)512u * 1024u)

static pthread_mutex_t s_mtx = PTHREAD_MUTEX_INITIALIZER;
static size_t s_budget[POOL__N];
static size_t s_used[POOL__N];
static size_t s_min_free[POOL__N];
static bool   s_budget_init;
static esp_alloc_failed_hook_t s_fail_hook;

/* ------------------------------------------------------------------ */
/*  Side map: ptr -> {pool, caps, size}.  Open addressing, linear       */
/*  probe, fixed capacity.  Untracked pointers are foreign mallocs —    */
/*  heap_caps_* treats them leniently (realloc copies usable bytes).    */
/* ------------------------------------------------------------------ */
#define MAP_CAP 8192

typedef struct {
    void    *ptr;       /* NULL = empty slot                            */
    size_t   size;
    uint32_t caps;
    uint8_t  pool;
} mapent_t;

static mapent_t s_map[MAP_CAP];

static size_t map_hash(const void *p)
{
    return ((uintptr_t)p >> 4) % MAP_CAP;
}

static mapent_t *map_lookup(void *p)
{
    size_t i = map_hash(p);
    for (size_t n = 0; n < MAP_CAP; n++) {
        mapent_t *e = &s_map[(i + n) % MAP_CAP];
        if (!e->ptr)      return NULL;
        if (e->ptr == p)  return e;
    }
    return NULL;
}

static void map_insert(void *p, size_t size, uint32_t caps, pool_t pool)
{
    size_t i = map_hash(p);
    for (size_t n = 0; n < MAP_CAP; n++) {
        mapent_t *e = &s_map[(i + n) % MAP_CAP];
        if (!e->ptr || e->ptr == p) {
            e->ptr = p; e->size = size; e->caps = caps; e->pool = (uint8_t)pool;
            return;
        }
    }
    /* table full — block stays allocated but untracked (warn once) */
    static bool warned;
    if (!warned) {
        warned = true;
        fprintf(stderr, "[%s] side-map full — %zu bytes untracked\n",
                TAG, size);
    }
}

static bool map_remove(void *p, mapent_t *out)
{
    size_t i = map_hash(p);
    for (size_t n = 0; n < MAP_CAP; n++) {
        mapent_t *e = &s_map[(i + n) % MAP_CAP];
        if (!e->ptr) return false;
        if (e->ptr == p) {
            if (out) *out = *e;
            e->ptr = NULL;
            /* linear-probe chains can break on hole — reinsert tail    */
            for (size_t j = 1; j < MAP_CAP; j++) {
                mapent_t *t = &s_map[(i + n + j) % MAP_CAP];
                if (!t->ptr) return true;
                mapent_t tmp = *t;
                t->ptr = NULL;
                map_insert(tmp.ptr, tmp.size, tmp.caps, (pool_t)tmp.pool);
            }
            return true;
        }
    }
    return false;
}

static void budgets_init(void)
{
    if (s_budget_init) return;
    s_budget[POOL_SPIRAM] = SPIRAM_BYTES;
    s_budget[POOL_INTERNAL] = INTERNAL_BYTES_DEFAULT;
    const char *env = getenv("VQ_SIM_INTERNAL_HEAP_BYTES");
    if (env && *env) {
        const size_t v = (size_t)strtoull(env, NULL, 0);
        if (v > 0) s_budget[POOL_INTERNAL] = v;
    }
    s_min_free[POOL_SPIRAM]   = s_budget[POOL_SPIRAM];
    s_min_free[POOL_INTERNAL] = s_budget[POOL_INTERNAL];
    s_budget_init = true;
}

/* Pool selection: strict when a pool is named, else prefer internal and
 * spill into SPIRAM (mirrors the real allocator's preference order for
 * an unconstrained MALLOC_CAP_8BIT request). */
static bool pool_allows(uint32_t caps, pool_t p)
{
    if (caps & MALLOC_CAP_SPIRAM)   return p == POOL_SPIRAM;
    if (caps & MALLOC_CAP_INTERNAL) return p == POOL_INTERNAL;
    return true; /* 8BIT/32BIT/etc. — any pool satisfies */
}

static pool_t pool_pick(uint32_t caps)
{
    if (caps & MALLOC_CAP_SPIRAM)   return POOL_SPIRAM;
    if (caps & MALLOC_CAP_INTERNAL) return POOL_INTERNAL;
    return POOL_INTERNAL; /* preferred; caller retries POOL_SPIRAM on failure */
}

/* ------------------------------------------------------------------ */
/*  Core alloc                                                         */
/* ------------------------------------------------------------------ */

static void *alloc_from_pool(size_t size, uint32_t caps, pool_t p)
{
    void *raw = NULL;
    pthread_mutex_lock(&s_mtx);
    if (s_used[p] + size <= s_budget[p]) {
        raw = malloc(size);
        if (raw) {
            s_used[p] += size;
            if (s_budget[p] - s_used[p] < s_min_free[p])
                s_min_free[p] = s_budget[p] - s_used[p];
            map_insert(raw, size, caps, p);
        }
    }
    pthread_mutex_unlock(&s_mtx);
    return raw;
}

static void alloc_failed(size_t size, uint32_t caps, const char *fn)
{
    ESP_LOGD(TAG, "%s: alloc of %u bytes failed (caps 0x%08lx)",
             fn, (unsigned)size, (unsigned long)caps);
    if (s_fail_hook) s_fail_hook(size, caps, fn);
}

static void *alloc_impl(size_t size, uint32_t caps, const char *fn)
{
    if (size == 0) size = 1;

    pthread_mutex_lock(&s_mtx);
    budgets_init();
    /* vboard fault knob: N counted allocations, then the next fails. */
    if (g_vb_faults.heap_fail_after > 0 &&
            --g_vb_faults.heap_fail_after == 0) {
        pthread_mutex_unlock(&s_mtx);
        alloc_failed(size, caps, fn);
        return NULL;
    }
    pthread_mutex_unlock(&s_mtx);

    const pool_t first = pool_pick(caps);
    void *p = alloc_from_pool(size, caps, first);
    if (!p && !((caps & (MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL)))) {
        /* Unconstrained request — spill into the other pool. */
        p = alloc_from_pool(size, caps,
                            first == POOL_INTERNAL ? POOL_SPIRAM : POOL_INTERNAL);
    }
    if (!p) alloc_failed(size, caps, fn);
    return p;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void *heap_caps_malloc(size_t size, uint32_t caps)
{
    return alloc_impl(size, caps, __FUNCTION__);
}

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    if (n && size > SIZE_MAX / n) return NULL; /* overflow guard */
    void *p = alloc_impl(n * size, caps, __FUNCTION__);
    if (p) memset(p, 0, n * size);
    return p;
}

void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps)
{
    if (!ptr) return heap_caps_malloc(size, caps);
    if (size == 0) { heap_caps_free(ptr); return NULL; }

    pthread_mutex_lock(&s_mtx);
    mapent_t ent;
    const bool tracked = map_lookup(ptr) && (map_remove(ptr, &ent), true);
    pthread_mutex_unlock(&s_mtx);

    if (!tracked) {
        /* foreign block (plain malloc): new alloc + usable-size copy   */
        void *np = alloc_impl(size, caps, __FUNCTION__);
        if (!np) return NULL;
        const size_t u = malloc_usable_size(ptr);
        memcpy(np, ptr, u < size ? u : size);
        free(ptr);
        return np;
    }

    const pool_t old_pool = (pool_t)ent.pool;
    const size_t old_size = ent.size;

    /* Same-pool grow/shrink: charge the delta against the budget. */
    if (pool_allows(caps, old_pool)) {
        bool ok = false;
        pthread_mutex_lock(&s_mtx);
        if (size <= old_size ||
                s_used[old_pool] + (size - old_size) <= s_budget[old_pool]) {
            ok = true;
        }
        pthread_mutex_unlock(&s_mtx);
        if (ok) {
            void *raw = realloc(ptr, size);
            if (raw) {
                pthread_mutex_lock(&s_mtx);
                s_used[old_pool] = s_used[old_pool] - old_size + size;
                if (s_budget[old_pool] - s_used[old_pool] < s_min_free[old_pool])
                    s_min_free[old_pool] = s_budget[old_pool] - s_used[old_pool];
                map_insert(raw, size, caps, old_pool);
                pthread_mutex_unlock(&s_mtx);
                return raw;
            }
            /* realloc failed: block still live — put the entry back. */
            pthread_mutex_lock(&s_mtx);
            map_insert(ptr, old_size, ent.caps, old_pool);
            pthread_mutex_unlock(&s_mtx);
        } else {
            pthread_mutex_lock(&s_mtx);
            map_insert(ptr, old_size, ent.caps, old_pool);
            pthread_mutex_unlock(&s_mtx);
        }
        alloc_failed(size, caps, __FUNCTION__);
        return NULL;
    }

    /* Pool migration: allocate under the new caps, copy, free old. */
    void *np = alloc_impl(size, caps, __FUNCTION__);
    if (!np) {
        pthread_mutex_lock(&s_mtx);
        map_insert(ptr, old_size, ent.caps, old_pool);
        pthread_mutex_unlock(&s_mtx);
        return NULL;
    }
    memcpy(np, ptr, old_size < size ? old_size : size);
    pthread_mutex_lock(&s_mtx);
    s_used[old_pool] -= old_size;
    pthread_mutex_unlock(&s_mtx);
    free(ptr);
    return np;
}

void heap_caps_free(void *ptr)
{
    if (!ptr) return;
    pthread_mutex_lock(&s_mtx);
    mapent_t ent;
    if (map_remove(ptr, &ent))
        s_used[ent.pool] -= ent.size;
    /* untracked = foreign malloc — nothing to uncharge either way      */
    pthread_mutex_unlock(&s_mtx);
    free(ptr);
}

void *heap_caps_malloc_prefer(size_t size, uint32_t caps, ...)
{
    va_list ap;
    va_start(ap, caps);
    void *p = alloc_impl(size, caps, __FUNCTION__);
    while (!p) {
        uint32_t c = va_arg(ap, uint32_t);
        if (c == 0) break;
        p = alloc_impl(size, c, __FUNCTION__);
    }
    va_end(ap);
    return p;
}

void *heap_caps_calloc_prefer(size_t n, size_t size, uint32_t caps, ...)
{
    va_list ap;
    va_start(ap, caps);
    void *p = NULL;
    uint32_t c = caps;
    do {
        p = heap_caps_calloc(n, size, c);
        if (p) break;
        c = va_arg(ap, uint32_t);
    } while (c != 0);
    va_end(ap);
    return p;
}

void *heap_caps_realloc_prefer(void *ptr, size_t size, uint32_t caps, ...)
{
    va_list ap;
    va_start(ap, caps);
    void *p = NULL;
    uint32_t c = caps;
    do {
        p = heap_caps_realloc(ptr, size, c);
        if (p) break;
        c = va_arg(ap, uint32_t);
    } while (c != 0);
    va_end(ap);
    return p;
}

/* ------------------------------------------------------------------ */
/*  Statistics / integrity                                             */
/* ------------------------------------------------------------------ */

size_t heap_caps_get_free_size(uint32_t caps)
{
    pthread_mutex_lock(&s_mtx);
    budgets_init();
    size_t v;
    if (caps & MALLOC_CAP_SPIRAM)         v = s_budget[POOL_SPIRAM] - s_used[POOL_SPIRAM];
    else if (caps & MALLOC_CAP_INTERNAL)  v = s_budget[POOL_INTERNAL] - s_used[POOL_INTERNAL];
    else v = (s_budget[POOL_SPIRAM] - s_used[POOL_SPIRAM]) +
             (s_budget[POOL_INTERNAL] - s_used[POOL_INTERNAL]);
    pthread_mutex_unlock(&s_mtx);
    return v;
}

size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    /* Pools are contiguous host arenas — report the whole remainder. */
    return heap_caps_get_free_size(caps);
}

size_t heap_caps_get_total_size(uint32_t caps)
{
    pthread_mutex_lock(&s_mtx);
    budgets_init();
    size_t v;
    if (caps & MALLOC_CAP_SPIRAM)         v = s_budget[POOL_SPIRAM];
    else if (caps & MALLOC_CAP_INTERNAL)  v = s_budget[POOL_INTERNAL];
    else v = s_budget[POOL_SPIRAM] + s_budget[POOL_INTERNAL];
    pthread_mutex_unlock(&s_mtx);
    return v;
}

size_t heap_caps_get_minimum_free_size(uint32_t caps)
{
    pthread_mutex_lock(&s_mtx);
    budgets_init();
    size_t v;
    if (caps & MALLOC_CAP_SPIRAM)         v = s_min_free[POOL_SPIRAM];
    else if (caps & MALLOC_CAP_INTERNAL)  v = s_min_free[POOL_INTERNAL];
    else v = s_min_free[POOL_SPIRAM] + s_min_free[POOL_INTERNAL];
    pthread_mutex_unlock(&s_mtx);
    return v;
}

size_t heap_caps_get_allocated_size(void *ptr)
{
    if (!ptr) return 0;
    pthread_mutex_lock(&s_mtx);
    mapent_t *e = map_lookup(ptr);
    const size_t v = e ? e->size : 0;
    pthread_mutex_unlock(&s_mtx);
    return v;
}

bool heap_caps_check_integrity_all(bool print_errors)
{
    /* Host arenas can't be walked like IDF heaps; the side map is
     * updated on every alloc/free/realloc instead. */
    (void)print_errors;
    return true;
}

bool heap_caps_check_integrity_addr(uintptr_t addr, size_t size, bool print_errors)
{
    (void)addr; (void)size; (void)print_errors;
    return true;
}

bool heap_caps_check_integrity(bool print_errors)
{
    return heap_caps_check_integrity_all(print_errors);
}

esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_mtx);
    s_fail_hook = callback;
    pthread_mutex_unlock(&s_mtx);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  esp_psram                                                          */
/* ------------------------------------------------------------------ */

size_t esp_psram_get_size(void)
{
    pthread_mutex_lock(&s_mtx);
    budgets_init();
    const size_t v = s_budget[POOL_SPIRAM];
    pthread_mutex_unlock(&s_mtx);
    return v;
}

bool esp_psram_is_initialized(void)
{
    return true;
}
