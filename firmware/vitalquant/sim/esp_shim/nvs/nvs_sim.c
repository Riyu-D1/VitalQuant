/* nvs_sim.c — host-sim NVS backing store.
 *
 * In-memory, per-namespace key/value store implementing the nvs.h /
 * nvs_flash.h surface used by the firmware.  Persistence is an
 * append-log of whole-store snapshots: each nvs_commit() appends one
 * frame, nvs_flash_init() replays the log and keeps the last valid
 * frame (a torn tail frame is ignored — CRC checked).
 *
 * File location: $VSIM_NVS_PATH, else the first of
 *   ./sim/build/nvs.bin, ./firmware/vitalquant/sim/build/nvs.bin,
 *   ./build/nvs.bin
 * so scenarios run from sim/, firmware/vitalquant, or the repo root all
 * land on sim/build/nvs.bin.
 *
 * Frame format (all integers little-endian):
 *   u32 magic = "NVS1" (0x3153564E)
 *   u32 seq           commit sequence number
 *   u32 payload_len
 *   u32 payload_crc   FNV-1a/32 of payload
 *   u8  payload[payload_len]:
 *        u32 n_namespaces
 *        per namespace: u16 name_len, name[], u32 n_entries
 *        per entry:     u8 nvs_type_t, u16 key_len, key[],
 *                       u32 val_len, val[]
 *                       (scalar types serialize as 8 bytes, str
 *                        includes the trailing NUL)
 */
#include "nvs.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <pthread.h>

#define HANDLE_MAGIC   0x4e565348u   /* "NVSH" */
#define FRAME_MAGIC    0x3153564eu   /* "NVS1" little-endian */
#define MAX_FRAME_LEN  (16u * 1024u * 1024u)
#define MAX_VALUE_LEN  8192u         /* str/blob cap, page-ish */
#define SIM_CAPACITY   1024u         /* fake entry budget for stats */

typedef struct kv_entry {
    struct kv_entry *next;
    char     key[NVS_KEY_NAME_MAX_SIZE];
    uint8_t  type;                    /* nvs_type_t */
    uint32_t len;                     /* buf bytes (str incl. NUL) */
    uint64_t u;                       /* scalar payload          */
    uint8_t *buf;                     /* str/blob payload        */
} kv_entry_t;

typedef struct nvs_ns {
    struct nvs_ns *next;
    char      name[NVS_NS_NAME_MAX_SIZE];
    kv_entry_t *head;
} nvs_ns_t;

struct nvs_opaque_handle {
    uint32_t                 magic;
    nvs_open_mode_t          mode;
    nvs_ns_t                *ns;
    struct nvs_opaque_handle *next;
};

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static nvs_ns_t       *s_namespaces;
static nvs_handle_t    s_handles;
static bool            s_inited;
static uint32_t        s_commit_seq;

/* ---------------------------------------------------------------- */
/* path helpers                                                      */

static void mkdirs_for(const char *file)
{
    char tmp[512];
    size_t n = strlen(file);
    if (n >= sizeof(tmp)) return;
    memcpy(tmp, file, n + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);         /* EEXIST is fine */
            *p = '/';
        }
    }
}

static const char *nvs_path(void)
{
    static char path[512];
    const char *env = getenv("VSIM_NVS_PATH");
    if (env && env[0]) return env;
    struct stat st;
    if (stat("sim", &st) == 0)
        snprintf(path, sizeof(path), "sim/build/nvs.bin");
    else if (stat("firmware/vitalquant", &st) == 0)
        snprintf(path, sizeof(path), "firmware/vitalquant/sim/build/nvs.bin");
    else
        snprintf(path, sizeof(path), "build/nvs.bin");
    return path;
}

/* ---------------------------------------------------------------- */
/* store helpers (all called with s_lock held)                       */

static bool name_ok(const char *s)
{
    if (!s) return false;
    size_t n = strlen(s);
    return n >= 1 && n < NVS_KEY_NAME_MAX_SIZE;
}

static nvs_ns_t *find_ns(const char *name)
{
    for (nvs_ns_t *ns = s_namespaces; ns; ns = ns->next)
        if (strcmp(ns->name, name) == 0) return ns;
    return NULL;
}

static kv_entry_t *find_entry(nvs_ns_t *ns, const char *key)
{
    for (kv_entry_t *e = ns->head; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e;
    return NULL;
}

static bool handle_valid(nvs_handle_t h)
{
    for (nvs_handle_t p = s_handles; p; p = p->next)
        if (p == h) return p->magic == HANDLE_MAGIC;
    return false;
}

static void free_ns_entries(nvs_ns_t *ns)
{
    kv_entry_t *e = ns->head;
    while (e) {
        kv_entry_t *nx = e->next;
        free(e->buf);
        free(e);
        e = nx;
    }
    ns->head = NULL;
}

static void store_clear(void)
{
    nvs_ns_t *ns = s_namespaces;
    while (ns) {
        nvs_ns_t *nx = ns->next;
        free_ns_entries(ns);
        free(ns);
        ns = nx;
    }
    s_namespaces = NULL;
}

/* ---------------------------------------------------------------- */
/* little-endian encode/decode                                       */

static void put16(uint8_t **p, uint16_t v)
{
    (*p)[0] = (uint8_t)v; (*p)[1] = (uint8_t)(v >> 8); *p += 2;
}
static void put32(uint8_t **p, uint32_t v)
{
    (*p)[0] = (uint8_t)v;       (*p)[1] = (uint8_t)(v >> 8);
    (*p)[2] = (uint8_t)(v >> 16); (*p)[3] = (uint8_t)(v >> 24);
    *p += 4;
}
static void put64(uint8_t **p, uint64_t v)
{
    for (int i = 0; i < 8; i++) (*p)[i] = (uint8_t)(v >> (8 * i));
    *p += 8;
}
static uint16_t rd16(const uint8_t **p)
{
    uint16_t v = (uint16_t)((*p)[0] | ((uint16_t)(*p)[1] << 8));
    *p += 2; return v;
}
static uint32_t rd32(const uint8_t **p)
{
    uint32_t v = (uint32_t)(*p)[0]        | ((uint32_t)(*p)[1] << 8) |
                 ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
    *p += 4; return v;
}

static uint32_t fnv1a(const uint8_t *d, size_t n)
{
    uint32_t h = 2166136261u;
    while (n--) { h ^= *d++; h *= 16777619u; }
    return h;
}

static bool is_buf_type(uint8_t t) { return t == NVS_TYPE_STR || t == NVS_TYPE_BLOB; }

/* ---------------------------------------------------------------- */
/* snapshot save/load (s_lock held)                                  */

static size_t payload_size(void)
{
    size_t n = 4;                          /* n_namespaces */
    for (nvs_ns_t *ns = s_namespaces; ns; ns = ns->next) {
        n += 2 + strlen(ns->name) + 4;
        for (kv_entry_t *e = ns->head; e; e = e->next)
            n += 1 + 2 + strlen(e->key) + 4 + (is_buf_type(e->type) ? e->len : 8);
    }
    return n;
}

static esp_err_t store_save(void)
{
    size_t plen = payload_size();
    if (plen > MAX_FRAME_LEN) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    uint8_t *payload = malloc(plen);
    if (!payload) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    uint8_t *p = payload;

    uint32_t nns = 0;
    for (nvs_ns_t *ns = s_namespaces; ns; ns = ns->next) nns++;
    put32(&p, nns);
    for (nvs_ns_t *ns = s_namespaces; ns; ns = ns->next) {
        put16(&p, (uint16_t)strlen(ns->name));
        memcpy(p, ns->name, strlen(ns->name)); p += strlen(ns->name);
        uint32_t ne = 0;
        for (kv_entry_t *e = ns->head; e; e = e->next) ne++;
        put32(&p, ne);
        for (kv_entry_t *e = ns->head; e; e = e->next) {
            *p++ = e->type;
            put16(&p, (uint16_t)strlen(e->key));
            memcpy(p, e->key, strlen(e->key)); p += strlen(e->key);
            if (is_buf_type(e->type)) {
                put32(&p, e->len);
                memcpy(p, e->buf, e->len); p += e->len;
            } else {
                put32(&p, 8);
                put64(&p, e->u);
            }
        }
    }

    const char *path = nvs_path();
    mkdirs_for(path);
    FILE *f = fopen(path, "ab");
    if (!f) { free(payload); return ESP_FAIL; }

    uint8_t hdr[16];
    uint8_t *hp = hdr;
    put32(&hp, FRAME_MAGIC);
    put32(&hp, ++s_commit_seq);
    put32(&hp, (uint32_t)plen);
    put32(&hp, fnv1a(payload, plen));

    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) &&
              fwrite(payload, 1, plen, f) == plen &&
              fflush(f) == 0;
    fclose(f);
    free(payload);
    return ok ? ESP_OK : ESP_FAIL;
}

/* Parse one payload into a fresh store and swap it in. */
static bool store_apply(const uint8_t *buf, size_t len)
{
    const uint8_t *p = buf;
    const uint8_t *end = buf + len;
    nvs_ns_t *new_head = NULL, *new_tail = NULL;

    if (len < 4) return false;
    uint32_t nns = rd32(&p);
    if (nns > 4096) return false;

    for (uint32_t i = 0; i < nns; i++) {
        if (p + 2 > end) goto bad;
        uint16_t nlen = rd16(&p);
        if (nlen == 0 || nlen >= NVS_NS_NAME_MAX_SIZE || p + nlen + 4 > end) goto bad;
        nvs_ns_t *ns = calloc(1, sizeof(*ns));
        if (!ns) goto bad;
        memcpy(ns->name, p, nlen); p += nlen;
        uint32_t ne = rd32(&p);
        for (uint32_t j = 0; j < ne; j++) {
            if (p + 3 > end) { free_ns_entries(ns); free(ns); goto bad; }
            uint8_t  type = *p++;
            uint16_t klen = rd16(&p);
            if (klen == 0 || klen >= NVS_KEY_NAME_MAX_SIZE || p + klen + 4 > end) {
                free_ns_entries(ns); free(ns); goto bad;
            }
            kv_entry_t *e = calloc(1, sizeof(*e));
            if (!e) { free_ns_entries(ns); free(ns); goto bad; }
            e->type = type;
            memcpy(e->key, p, klen); p += klen;
            uint32_t vlen = rd32(&p);
            if (is_buf_type(type)) {
                if (vlen > MAX_VALUE_LEN || p + vlen > end) {
                    free(e); free_ns_entries(ns); free(ns); goto bad;
                }
                e->buf = malloc(vlen ? vlen : 1);
                if (!e->buf) { free(e); free_ns_entries(ns); free(ns); goto bad; }
                memcpy(e->buf, p, vlen); p += vlen;
                e->len = vlen;
            } else {
                if (vlen != 8 || p + 8 > end) {
                    free(e); free_ns_entries(ns); free(ns); goto bad;
                }
                uint64_t v = 0;
                for (int b = 0; b < 8; b++) v |= (uint64_t)p[b] << (8 * b);
                p += 8;
                e->u = v;
            }
            e->next = ns->head; ns->head = e;
        }
        if (new_tail) new_tail->next = ns; else new_head = ns;
        new_tail = ns;
    }
    store_clear();
    s_namespaces = new_head;
    return true;

bad:
    while (new_head) {
        nvs_ns_t *nx = new_head->next;
        free_ns_entries(new_head);
        free(new_head);
        new_head = nx;
    }
    return false;
}

static void store_load(void)
{
    FILE *f = fopen(nvs_path(), "rb");
    if (!f) return;                          /* fresh store */

    for (;;) {
        uint8_t hdr[16];
        if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) break;

        const uint8_t *hp = hdr;
        uint32_t magic = rd32(&hp);
        uint32_t seq   = rd32(&hp);
        uint32_t plen  = rd32(&hp);
        uint32_t crc   = rd32(&hp);
        if (magic != FRAME_MAGIC || plen == 0 || plen > MAX_FRAME_LEN) break;

        uint8_t *payload = malloc(plen);
        if (!payload) break;
        bool ok = fread(payload, 1, plen, f) == plen && fnv1a(payload, plen) == crc;
        if (ok) ok = store_apply(payload, plen);
        free(payload);
        if (!ok) break;                      /* torn/corrupt tail */
        s_commit_seq = seq;
    }
    fclose(f);
}

/* ---------------------------------------------------------------- */
/* nvs_flash_* lifecycle                                             */

esp_err_t nvs_flash_init(void)
{
    pthread_mutex_lock(&s_lock);
    if (!s_inited) {
        store_load();
        s_inited = true;
    }
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t nvs_flash_init_partition(const char *partition_label)
{
    (void)partition_label;                   /* single-store sim */
    return nvs_flash_init();
}

esp_err_t nvs_flash_deinit(void)
{
    pthread_mutex_lock(&s_lock);
    while (s_handles) {
        nvs_handle_t nx = s_handles->next;
        s_handles->magic = 0;
        free(s_handles);
        s_handles = nx;
    }
    store_clear();
    s_inited = false;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t nvs_flash_deinit_partition(const char *partition_label)
{
    (void)partition_label;
    return nvs_flash_deinit();
}

esp_err_t nvs_flash_erase(void)
{
    pthread_mutex_lock(&s_lock);
    store_clear();
    s_commit_seq = 0;
    remove(nvs_path());
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t nvs_flash_erase_partition(const char *part_name)
{
    (void)part_name;
    return nvs_flash_erase();
}

/* ---------------------------------------------------------------- */
/* handle management                                                 */

esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle)
{
    if (!name_ok(name) || !out_handle) return ESP_ERR_NVS_INVALID_NAME;
    if (open_mode != NVS_READONLY && open_mode != NVS_READWRITE)
        return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_lock);
    if (!s_inited) {
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_NVS_NOT_INITIALIZED;
    }
    nvs_ns_t *ns = find_ns(name);
    if (!ns) {
        if (open_mode == NVS_READONLY) {
            pthread_mutex_unlock(&s_lock);
            return ESP_ERR_NVS_NOT_FOUND;
        }
        ns = calloc(1, sizeof(*ns));
        if (!ns) { pthread_mutex_unlock(&s_lock); return ESP_ERR_NVS_NOT_ENOUGH_SPACE; }
        strncpy(ns->name, name, sizeof(ns->name) - 1);
        ns->next = s_namespaces;
        s_namespaces = ns;
    }
    nvs_handle_t h = calloc(1, sizeof(*h));
    if (!h) { pthread_mutex_unlock(&s_lock); return ESP_ERR_NVS_NOT_ENOUGH_SPACE; }
    h->magic = HANDLE_MAGIC;
    h->mode  = open_mode;
    h->ns    = ns;
    h->next  = s_handles;
    s_handles = h;
    *out_handle = h;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t nvs_open_from_partition(const char *part_name, const char *name,
                                  nvs_open_mode_t open_mode,
                                  nvs_handle_t *out_handle)
{
    (void)part_name;                         /* single-store sim */
    return nvs_open(name, open_mode, out_handle);
}

void nvs_close(nvs_handle_t handle)
{
    pthread_mutex_lock(&s_lock);
    nvs_handle_t *pp = &s_handles;
    while (*pp) {
        if (*pp == handle) {
            *pp = handle->next;
            handle->magic = 0;
            free(handle);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&s_lock);
}

/* ---------------------------------------------------------------- */
/* get/set                                                           */

static esp_err_t get_scalar(nvs_handle_t h, const char *key, uint8_t type,
                            void *out)
{
    if (!handle_valid(h)) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!name_ok(key) || !out) return ESP_ERR_NVS_INVALID_NAME;
    kv_entry_t *e = find_entry(h->ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->type != type) return ESP_ERR_NVS_TYPE_MISMATCH;
    switch (type) {
    case NVS_TYPE_U8:  *(uint8_t  *)out = (uint8_t)e->u;  break;
    case NVS_TYPE_I8:  *(int8_t   *)out = (int8_t)e->u;   break;
    case NVS_TYPE_U16: *(uint16_t *)out = (uint16_t)e->u; break;
    case NVS_TYPE_I16: *(int16_t  *)out = (int16_t)e->u;  break;
    case NVS_TYPE_U32: *(uint32_t *)out = (uint32_t)e->u; break;
    case NVS_TYPE_I32: *(int32_t  *)out = (int32_t)e->u;  break;
    default:           *(uint64_t *)out = e->u;           break;  /* 64-bit */
    }
    return ESP_OK;
}

static esp_err_t set_scalar(nvs_handle_t h, const char *key, uint8_t type,
                            uint64_t v)
{
    if (!handle_valid(h)) return ESP_ERR_NVS_INVALID_HANDLE;
    if (h->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;
    if (!name_ok(key)) return ESP_ERR_NVS_INVALID_NAME;
    kv_entry_t *e = find_entry(h->ns, key);
    if (!e) {
        e = calloc(1, sizeof(*e));
        if (!e) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
        strncpy(e->key, key, sizeof(e->key) - 1);
        e->next = h->ns->head;
        h->ns->head = e;
    } else {
        free(e->buf); e->buf = NULL; e->len = 0;
    }
    e->type = type;
    e->u = v;
    return ESP_OK;
}

#define NVS_GET_FN(name, ctype, tag)                                      \
    esp_err_t name(nvs_handle_t h, const char *key, ctype *out)           \
    {                                                                     \
        esp_err_t r;                                                      \
        pthread_mutex_lock(&s_lock);                                      \
        r = get_scalar(h, key, tag, out);                                 \
        pthread_mutex_unlock(&s_lock);                                    \
        return r;                                                         \
    }

#define NVS_SET_FN(name, ctype, tag)                                      \
    esp_err_t name(nvs_handle_t h, const char *key, ctype v)              \
    {                                                                     \
        esp_err_t r;                                                      \
        pthread_mutex_lock(&s_lock);                                      \
        r = set_scalar(h, key, tag, (uint64_t)v);                         \
        pthread_mutex_unlock(&s_lock);                                    \
        return r;                                                         \
    }

NVS_GET_FN(nvs_get_i8,  int8_t,   NVS_TYPE_I8)
NVS_GET_FN(nvs_get_i16, int16_t,  NVS_TYPE_I16)
NVS_GET_FN(nvs_get_i32, int32_t,  NVS_TYPE_I32)
NVS_GET_FN(nvs_get_i64, int64_t,  NVS_TYPE_I64)
NVS_GET_FN(nvs_get_u8,  uint8_t,  NVS_TYPE_U8)
NVS_GET_FN(nvs_get_u16, uint16_t, NVS_TYPE_U16)
NVS_GET_FN(nvs_get_u32, uint32_t, NVS_TYPE_U32)
NVS_GET_FN(nvs_get_u64, uint64_t, NVS_TYPE_U64)

NVS_SET_FN(nvs_set_i8,  int8_t,   NVS_TYPE_I8)
NVS_SET_FN(nvs_set_i16, int16_t,  NVS_TYPE_I16)
NVS_SET_FN(nvs_set_i32, int32_t,  NVS_TYPE_I32)
NVS_SET_FN(nvs_set_i64, int64_t,  NVS_TYPE_I64)
NVS_SET_FN(nvs_set_u8,  uint8_t,  NVS_TYPE_U8)
NVS_SET_FN(nvs_set_u16, uint16_t, NVS_TYPE_U16)
NVS_SET_FN(nvs_set_u32, uint32_t, NVS_TYPE_U32)
NVS_SET_FN(nvs_set_u64, uint64_t, NVS_TYPE_U64)

static esp_err_t get_buf(nvs_handle_t h, const char *key, uint8_t type,
                         void *out, size_t *length)
{
    if (!handle_valid(h)) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!name_ok(key) || !length) return ESP_ERR_NVS_INVALID_NAME;
    kv_entry_t *e = find_entry(h->ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->type != type) return ESP_ERR_NVS_TYPE_MISMATCH;
    if (!out) { *length = e->len; return ESP_OK; }
    if (*length < e->len) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, e->buf, e->len);
    *length = e->len;
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out_value,
                      size_t *length)
{
    esp_err_t r;
    pthread_mutex_lock(&s_lock);
    r = get_buf(h, key, NVS_TYPE_STR, out_value, length);
    pthread_mutex_unlock(&s_lock);
    return r;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out_value,
                       size_t *length)
{
    esp_err_t r;
    pthread_mutex_lock(&s_lock);
    r = get_buf(h, key, NVS_TYPE_BLOB, out_value, length);
    pthread_mutex_unlock(&s_lock);
    return r;
}

static esp_err_t set_buf(nvs_handle_t h, const char *key, uint8_t type,
                         const void *value, size_t length)
{
    if (!handle_valid(h)) return ESP_ERR_NVS_INVALID_HANDLE;
    if (h->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;
    if (!name_ok(key)) return ESP_ERR_NVS_INVALID_NAME;
    if (!value && (length > 0 || type == NVS_TYPE_STR))
        return ESP_ERR_INVALID_ARG;
    if (length > MAX_VALUE_LEN) return ESP_ERR_NVS_VALUE_TOO_LONG;

    uint8_t *copy = malloc(length ? length : 1);
    if (!copy) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    if (length) memcpy(copy, value, length);

    kv_entry_t *e = find_entry(h->ns, key);
    if (!e) {
        e = calloc(1, sizeof(*e));
        if (!e) { free(copy); return ESP_ERR_NVS_NOT_ENOUGH_SPACE; }
        strncpy(e->key, key, sizeof(e->key) - 1);
        e->next = h->ns->head;
        h->ns->head = e;
    }
    free(e->buf);
    e->buf  = copy;
    e->len  = (uint32_t)length;
    e->type = type;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{
    esp_err_t r;
    pthread_mutex_lock(&s_lock);
    r = set_buf(h, key, NVS_TYPE_STR, value, value ? strlen(value) + 1 : 0);
    pthread_mutex_unlock(&s_lock);
    return r;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value,
                       size_t length)
{
    esp_err_t r;
    pthread_mutex_lock(&s_lock);
    r = set_buf(h, key, NVS_TYPE_BLOB, value, length);
    pthread_mutex_unlock(&s_lock);
    return r;
}

/* ---------------------------------------------------------------- */
/* erase / commit / stats                                            */

esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    pthread_mutex_lock(&s_lock);
    esp_err_t r = ESP_OK;
    if (!handle_valid(h))            r = ESP_ERR_NVS_INVALID_HANDLE;
    else if (h->mode == NVS_READONLY) r = ESP_ERR_NVS_READ_ONLY;
    else if (!name_ok(key))          r = ESP_ERR_NVS_INVALID_NAME;
    else {
        kv_entry_t **pp = &h->ns->head;
        while (*pp && strcmp((*pp)->key, key) != 0) pp = &(*pp)->next;
        if (*pp) {
            kv_entry_t *dead = *pp;
            *pp = dead->next;
            free(dead->buf);
            free(dead);
        } else {
            r = ESP_ERR_NVS_NOT_FOUND;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return r;
}

esp_err_t nvs_erase_all(nvs_handle_t h)
{
    pthread_mutex_lock(&s_lock);
    esp_err_t r = ESP_OK;
    if (!handle_valid(h))             r = ESP_ERR_NVS_INVALID_HANDLE;
    else if (h->mode == NVS_READONLY) r = ESP_ERR_NVS_READ_ONLY;
    else free_ns_entries(h->ns);
    pthread_mutex_unlock(&s_lock);
    return r;
}

esp_err_t nvs_commit(nvs_handle_t h)
{
    pthread_mutex_lock(&s_lock);
    esp_err_t r;
    if (!handle_valid(h))             r = ESP_ERR_NVS_INVALID_HANDLE;
    else if (h->mode == NVS_READONLY) r = ESP_ERR_NVS_READ_ONLY;
    else                              r = store_save();
    pthread_mutex_unlock(&s_lock);
    return r;
}

esp_err_t nvs_get_stats(const char *part_name, nvs_stats_t *nvs_stats)
{
    (void)part_name;                            /* single-store sim */
    if (!nvs_stats) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_lock);
    if (!s_inited) {
        pthread_mutex_unlock(&s_lock);
        return ESP_ERR_NVS_NOT_INITIALIZED;
    }
    size_t used = 0, nns = 0;
    for (nvs_ns_t *ns = s_namespaces; ns; ns = ns->next) {
        nns++;
        for (kv_entry_t *e = ns->head; e; e = e->next) used++;
    }
    nvs_stats->used_entries      = used;
    nvs_stats->total_entries     = SIM_CAPACITY;
    nvs_stats->free_entries      = used < SIM_CAPACITY ? SIM_CAPACITY - used : 0;
    nvs_stats->available_entries = nvs_stats->free_entries;
    nvs_stats->namespace_count   = nns;
    pthread_mutex_unlock(&s_lock);
    return ESP_OK;
}

esp_err_t nvs_get_used_entry_count(nvs_handle_t h, size_t *used_entries)
{
    if (!used_entries) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_lock);
    esp_err_t r = ESP_OK;
    if (!handle_valid(h)) r = ESP_ERR_NVS_INVALID_HANDLE;
    else {
        size_t n = 0;
        for (kv_entry_t *e = h->ns->head; e; e = e->next) n++;
        *used_entries = n;
    }
    pthread_mutex_unlock(&s_lock);
    return r;
}
