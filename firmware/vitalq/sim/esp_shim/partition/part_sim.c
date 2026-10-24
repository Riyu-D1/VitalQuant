/* part_sim.c — esp_partition shim over a host file, 16 MiB NOR image.
 *
 * Backing store: sim/build/flash_image.bin (see part_sim_flash_path for
 * resolution order).  The file is created lazily on first access and
 * ftruncate()d to exactly PART_SIM_FLASH_BYTES — a sparse file, so the
 * image costs ~0 blocks until something is written.
 *
 * NOR semantics modelled on top of the file:
 *   - never-written regions are fs holes: reads synthesise 0xFF via
 *     SEEK_DATA/SEEK_HOLE when the fs supports it (ext4/xfs/btrfs/tmpfs
 *     on Linux do); without support holes read back as 0x00.
 *   - esp_partition_erase_range materialises real 0xFF bytes (dd of the
 *     image then shows NOR-erased state, not zeros).
 *   - esp_partition_write is a plain pwrite — unlike silicon it can set
 *     bits, not just clear them (documented in the header).
 *   - esp_partition_mmap is a genuine mmap(2), PROT_READ|MAP_SHARED, so
 *     vq_model's 4 MiB weights window is zero-copy.  out_ptr gets the
 *     page-alignment bias; (base,len) live in a small handle pool for
 *     munmap.
 *
 * All public entry points serialise on s_mu: the tick thread and the
 * app thread may touch the image concurrently.
 */
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#include "esp_partition.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ------------------ in-shim partition table -------------------------
 * Exact mirror of firmware/vitalq/partitions.csv — keep in sync.      */
static const esp_partition_t s_table[] = {
    { ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS,
      0x009000u,   0x6000u, "nvs",      false },
    { ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA,
      0x00f000u,   0x2000u, "otadata",  false },
    { ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PHY,
      0x011000u,   0x1000u, "phy_init", false },
    { ESP_PARTITION_TYPE_APP,  ESP_PARTITION_SUBTYPE_APP_OTA_0,
      0x020000u, 0x400000u, "ota_0",    false },
    { ESP_PARTITION_TYPE_APP,  ESP_PARTITION_SUBTYPE_APP_OTA_1,
      0x420000u, 0x400000u, "ota_1",    false },
    { ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_VQMODEL,
      0x820000u, 0x400000u, "model",    false },
    { ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
      0xc20000u, 0x3d0000u, "storage",  false },
    { ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
      0xff0000u,  0x10000u, "coredump", false },
};
#define PART_SIM_NPARTS (sizeof(s_table) / sizeof(s_table[0]))

/* ---------------------- shared state ------------------------------- */
static int              s_fd      = -1;   /* lazily opened image        */
static int              s_hole_ok = -1;   /* -1 unprobed, 0/1 result    */
static pthread_mutex_t  s_mu = PTHREAD_MUTEX_INITIALIZER;

#define PART_SIM_MMAP_MAX 16
static struct {
    void  *base;                            /* page-aligned map base    */
    size_t len;                             /* mapping length           */
} s_mmaps[PART_SIM_MMAP_MAX];

/* ---------------------- path resolution ---------------------------- */
static void resolve_path(char *buf, size_t len)
{
    const char *env = getenv("VSIM_FLASH_IMAGE");
    if (env != NULL && env[0] != '\0') {
        snprintf(buf, len, "%s", env);
        return;
    }
    env = getenv("SIM_HOST_BUILD");
    if (env != NULL && env[0] != '\0') {
        snprintf(buf, len, "%s/flash_image.bin", env);
        return;
    }
    /* __FILE__ is absolute in the sim build (Makefile uses abspath):
     * .../sim/esp_shim/partition/part_sim.c -> .../sim/build/flash_image.bin */
    const char *anchor = "/esp_shim/partition/part_sim.c";
    const char *hit = strstr(__FILE__, anchor);
    if (hit != NULL && __FILE__[0] == '/') {
        snprintf(buf, len, "%.*s/build/flash_image.bin",
                 (int)(hit - __FILE__), __FILE__);
        return;
    }
    snprintf(buf, len, "sim/build/flash_image.bin");
}

int part_sim_flash_path(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return -1;
    }
    char tmp[PATH_MAX];
    resolve_path(tmp, sizeof(tmp));
    const size_t need = strlen(tmp) + 1u;
    if (need > len) {
        buf[0] = '\0';
        return -1;
    }
    memcpy(buf, tmp, need);
    return (int)(need - 1u);
}

/* mkdir -p for the image's parent directory (sim/build may not exist
 * on a fresh tree). Errors ignored — a real failure surfaces at open(). */
static void mkdir_parents(const char *path)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash == NULL) {
        return;
    }
    *slash = '\0';
    for (char *p = dir + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            (void)mkdir(dir, 0755);
            *p = '/';
        }
    }
    (void)mkdir(dir, 0755);
}

/* Caller holds s_mu.  Returns the fd or -1 (errno preserved by caller). */
static int flash_open_locked(void)
{
    if (s_fd >= 0) {
        return s_fd;
    }
    char path[PATH_MAX];
    resolve_path(path, sizeof(path));
    mkdir_parents(path);

    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 ||
        (st.st_size != (off_t)PART_SIM_FLASH_BYTES &&
         ftruncate(fd, (off_t)PART_SIM_FLASH_BYTES) != 0)) {
        close(fd);
        return -1;
    }
    /* Fresh image: seed a plausible ESP image magic at ota_0 so the
     * rails_boot "app image magic" check sees what silicon would after
     * a real flash (the host binary itself is ELF, not an ESP image). */
    if (st.st_size == 0) {
        const uint8_t magic = 0xE9;
        (void)pwrite(fd, &magic, 1, (off_t)0x20000u);   /* ota_0 base  */
    }
    /* Probe SEEK_DATA/SEEK_HOLE support once: EINVAL = whence unknown
     * to the fs, ENXIO = supported but nothing found. */
    errno = 0;
    const int ok_data = (lseek(fd, 0, SEEK_DATA) >= 0 || errno == ENXIO);
    errno = 0;
    const int ok_hole = (lseek(fd, 0, SEEK_HOLE) >= 0 || errno == ENXIO);
    s_hole_ok = (ok_data && ok_hole) ? 1 : 0;

    s_fd = fd;
    return s_fd;
}

/* ---------------------- low-level image io --------------------------
 * All of these run with s_mu held and a valid s_fd. */

/* Logical read: holes (never-written, NOR-erased) come back as 0xFF
 * when the fs reports them, real bytes elsewhere. */
static esp_err_t flash_read_locked(uint32_t abs_off, void *dst, size_t len)
{
    uint8_t *p = (uint8_t *)dst;
    size_t   done = 0;
    const off_t end = (off_t)abs_off + (off_t)len;

    while (done < len) {
        const off_t cur = (off_t)abs_off + (off_t)done;
        if (s_hole_ok == 1) {
            errno = 0;
            const off_t d = lseek(s_fd, cur, SEEK_DATA);
            if (d < 0) {
                if (errno == ENXIO) {           /* hole to EOF          */
                    memset(p + done, 0xFF, len - done);
                    return ESP_OK;
                }
                return ESP_FAIL;
            }
            if (d > cur) {                      /* hole [cur, d)        */
                size_t hz = (size_t)(d - cur);
                if (hz > len - done) {
                    hz = len - done;
                }
                memset(p + done, 0xFF, hz);
                done += hz;
                continue;
            }
            /* d == cur: inside a data extent — find its end. */
            off_t h = lseek(s_fd, cur, SEEK_HOLE);
            if (h < 0 || h > end) {
                h = end;
            }
            const size_t chunk = (size_t)(h - cur);
            const ssize_t r = pread(s_fd, p + done, chunk, cur);
            if (r < 0) {
                return ESP_FAIL;
            }
            if (r == 0) {                       /* raced truncate       */
                memset(p + done, 0xFF, chunk);
                done += chunk;
            } else {
                done += (size_t)r;
            }
        } else {
            const ssize_t r = pread(s_fd, p + done, len - done, cur);
            if (r <= 0) {
                return ESP_FAIL;
            }
            done += (size_t)r;
        }
    }
    return ESP_OK;
}

static esp_err_t flash_write_locked(uint32_t abs_off, const void *src,
                                    size_t len)
{
    const uint8_t *p = (const uint8_t *)src;
    size_t done = 0;
    while (done < len) {
        const ssize_t w = pwrite(s_fd, p + done, len - done,
                                 (off_t)abs_off + (off_t)done);
        if (w <= 0) {
            return ESP_FAIL;
        }
        done += (size_t)w;
    }
    return ESP_OK;
}

/* Materialise erased NOR: real 0xFF bytes in the image. */
static esp_err_t flash_fill_ff_locked(uint32_t abs_off, size_t len)
{
    uint8_t ff[16384];
    memset(ff, 0xFF, sizeof(ff));
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > sizeof(ff)) {
            chunk = sizeof(ff);
        }
        if (flash_write_locked(abs_off + (uint32_t)done, ff, chunk)
            != ESP_OK) {
            return ESP_FAIL;
        }
        done += chunk;
    }
    return ESP_OK;
}

/* ---------------------- bounds ------------------------------------- */
static esp_err_t check_region(const esp_partition_t *p, uint64_t off,
                              uint64_t size)
{
    if (p == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (off + size > (uint64_t)p->size) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((uint64_t)p->address + off + size >
        (uint64_t)PART_SIM_FLASH_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/* ==================== enumeration ================================== */

struct esp_partition_iterator_opaque_ {
    uint8_t idx[PART_SIM_NPARTS];   /* s_table indexes of the matches   */
    size_t  n;
    size_t  pos;
};

static bool part_matches(const esp_partition_t *p,
                         esp_partition_type_t type,
                         esp_partition_subtype_t subtype,
                         const char *label)
{
    if ((uint32_t)type != 0xffu && p->type != type) {
        return false;
    }
    if (subtype != ESP_PARTITION_SUBTYPE_ANY && p->subtype != subtype) {
        return false;
    }
    if (label != NULL &&
        strncmp(p->label, label, sizeof(p->label)) != 0) {
        return false;
    }
    return true;
}

esp_partition_iterator_t esp_partition_find(esp_partition_type_t type,
                                            esp_partition_subtype_t subtype,
                                            const char *label)
{
    esp_partition_iterator_t it =
        calloc(1, sizeof(struct esp_partition_iterator_opaque_));
    if (it == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < PART_SIM_NPARTS; i++) {
        if (part_matches(&s_table[i], type, subtype, label)) {
            it->idx[it->n++] = (uint8_t)i;
        }
    }
    return it;
}

const esp_partition_t *esp_partition_get(esp_partition_iterator_t it)
{
    if (it == NULL || it->pos >= it->n) {
        return NULL;
    }
    return &s_table[it->idx[it->pos]];
}

esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it)
{
    if (it == NULL) {
        return NULL;
    }
    it->pos++;
    if (it->pos >= it->n) {
        free(it);                   /* consumed — no more partitions   */
        return NULL;
    }
    return it;
}

void esp_partition_iterator_release(esp_partition_iterator_t it)
{
    free(it);
}

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label)
{
    esp_partition_iterator_t it = esp_partition_find(type, subtype, label);
    if (it == NULL) {
        return NULL;
    }
    const esp_partition_t *p = esp_partition_get(it);
    esp_partition_iterator_release(it);
    return p;                       /* points into the static table    */
}

bool esp_partition_verify(const esp_partition_t *partition)
{
    if (partition == NULL) {
        return false;
    }
    for (size_t i = 0; i < PART_SIM_NPARTS; i++) {
        const esp_partition_t *t = &s_table[i];
        if (t->type == partition->type &&
            t->subtype == partition->subtype &&
            t->address == partition->address &&
            t->size == partition->size &&
            strncmp(t->label, partition->label, sizeof(t->label)) == 0) {
            return true;
        }
    }
    return false;
}

/* ==================== read / write / erase ========================= */

esp_err_t esp_partition_read(const esp_partition_t *partition,
                             size_t src_offset, void *dst, size_t size)
{
    if (dst == NULL && size > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t e = check_region(partition, src_offset, size);
    if (e != ESP_OK) {
        return e;
    }
    if (size == 0) {
        return ESP_OK;
    }
    esp_err_t r = ESP_FAIL;
    pthread_mutex_lock(&s_mu);
    if (flash_open_locked() >= 0) {
        r = flash_read_locked(partition->address + (uint32_t)src_offset,
                              dst, size);
    }
    pthread_mutex_unlock(&s_mu);
    return r;
}

esp_err_t esp_partition_read_raw(const esp_partition_t *partition,
                                 size_t src_offset, void *dst, size_t size)
{
    return esp_partition_read(partition, src_offset, dst, size);
}

esp_err_t esp_partition_write(const esp_partition_t *partition,
                              size_t dst_offset, const void *src,
                              size_t size)
{
    if (src == NULL && size > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t e = check_region(partition, dst_offset, size);
    if (e != ESP_OK) {
        return e;
    }
    if (size == 0) {
        return ESP_OK;
    }
    esp_err_t r = ESP_FAIL;
    pthread_mutex_lock(&s_mu);
    if (flash_open_locked() >= 0) {
        r = flash_write_locked(partition->address + (uint32_t)dst_offset,
                               src, size);
    }
    pthread_mutex_unlock(&s_mu);
    return r;
}

esp_err_t esp_partition_write_raw(const esp_partition_t *partition,
                                  size_t dst_offset, const void *src,
                                  size_t size)
{
    return esp_partition_write(partition, dst_offset, src, size);
}

esp_err_t esp_partition_erase_range(const esp_partition_t *partition,
                                    size_t offset, size_t size)
{
    if (partition == NULL || (offset % PART_SIM_ERASE_BLOCK) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (size == 0 || (size % PART_SIM_ERASE_BLOCK) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t e = check_region(partition, offset, size);
    if (e != ESP_OK) {
        return e;
    }
    esp_err_t r = ESP_FAIL;
    pthread_mutex_lock(&s_mu);
    if (flash_open_locked() >= 0) {
        r = flash_fill_ff_locked(partition->address + (uint32_t)offset,
                                 size);
    }
    pthread_mutex_unlock(&s_mu);
    return r;
}

/* ==================== mmap ========================================= */

esp_err_t esp_partition_mmap(const esp_partition_t *partition,
                             uint32_t offset, uint32_t size,
                             esp_partition_mmap_memory_t memory,
                             const void **out_ptr,
                             esp_partition_mmap_handle_t *out_handle)
{
    (void)memory;                   /* DATA/INST both -> PROT_READ here */
    if (out_ptr == NULL || out_handle == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (check_region(partition, offset, size) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) {
        page = 4096;
    }
    const uint64_t abs     = (uint64_t)partition->address + offset;
    const off_t    map_off = (off_t)(abs & ~((uint64_t)page - 1u));
    const size_t   delta   = (size_t)(abs - (uint64_t)map_off);
    const size_t   map_len = (size_t)size + delta;

    esp_err_t r = ESP_FAIL;
    pthread_mutex_lock(&s_mu);
    if (flash_open_locked() < 0) {
        goto out;
    }
    {
        int slot = -1;
        for (int i = 0; i < PART_SIM_MMAP_MAX; i++) {
            if (s_mmaps[i].base == NULL) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            r = ESP_ERR_NO_MEM;
            goto out;
        }
        void *base = mmap(NULL, map_len, PROT_READ, MAP_SHARED,
                          s_fd, map_off);
        if (base == MAP_FAILED) {
            r = ESP_FAIL;
            goto out;
        }
        s_mmaps[slot].base = base;
        s_mmaps[slot].len  = map_len;
        *out_ptr           = (const uint8_t *)base + delta;
        *out_handle        = (esp_partition_mmap_handle_t)(slot + 1);
        r = ESP_OK;
    }
out:
    pthread_mutex_unlock(&s_mu);
    return r;
}

void esp_partition_munmap(esp_partition_mmap_handle_t handle)
{
    if (handle == 0 || handle > PART_SIM_MMAP_MAX) {
        return;
    }
    pthread_mutex_lock(&s_mu);
    const int slot = (int)handle - 1;
    if (s_mmaps[slot].base != NULL) {
        (void)munmap(s_mmaps[slot].base, s_mmaps[slot].len);
        s_mmaps[slot].base = NULL;
        s_mmaps[slot].len  = 0;
    }
    pthread_mutex_unlock(&s_mu);
}

/* ==================== SHA-256 (FIPS 180-4) ========================= */

typedef struct {
    uint32_t h[8];
    uint64_t tot;                   /* total message bytes             */
    uint8_t  blk[64];
    size_t   n;                     /* bytes pending in blk            */
} ps_sha_t;

static const uint32_t s_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static inline uint32_t rotr32(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void sha_block(ps_sha_t *s, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^
                            (w[i - 15] >> 3);
        const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^
                            (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + s_k[i] + w[i];
        const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(ps_sha_t *s)
{
    static const uint32_t h0[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    memcpy(s->h, h0, sizeof(h0));
    s->tot = 0;
    s->n   = 0;
}

static void sha_update(ps_sha_t *s, const uint8_t *p, size_t len)
{
    s->tot += len;
    while (len > 0) {
        const size_t room = 64u - s->n;
        const size_t take = (len < room) ? len : room;
        memcpy(s->blk + s->n, p, take);
        s->n += take;
        p    += take;
        len  -= take;
        if (s->n == 64u) {
            sha_block(s, s->blk);
            s->n = 0;
        }
    }
}

static void sha_final(ps_sha_t *s, uint8_t out[32])
{
    const uint64_t bits = s->tot * 8u;
    uint8_t pad = 0x80;
    sha_update(s, &pad, 1);
    uint8_t z = 0;
    while (s->n != 56u) {
        sha_update(s, &z, 1);
    }
    uint8_t lenbe[8];
    for (int i = 0; i < 8; i++) {
        lenbe[i] = (uint8_t)(bits >> (56 - i * 8));
    }
    sha_update(s, lenbe, 8);
    for (int i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

esp_err_t esp_partition_get_sha256(const esp_partition_t *partition,
                                   uint8_t *sha_256)
{
    if (sha_256 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t e = check_region(partition, 0, 0);
    if (e != ESP_OK) {
        return e;
    }
    esp_err_t r = ESP_FAIL;
    pthread_mutex_lock(&s_mu);
    if (flash_open_locked() >= 0) {
        ps_sha_t s;
        sha_init(&s);
        uint8_t  buf[32768];
        uint32_t off = 0;
        r = ESP_OK;
        while (off < partition->size) {
            size_t chunk = partition->size - off;
            if (chunk > sizeof(buf)) {
                chunk = sizeof(buf);
            }
            r = flash_read_locked(partition->address + off, buf, chunk);
            if (r != ESP_OK) {
                break;
            }
            sha_update(&s, buf, chunk);
            off += (uint32_t)chunk;
        }
        if (r == ESP_OK) {
            sha_final(&s, sha_256);
        }
    }
    pthread_mutex_unlock(&s_mu);
    return r;
}

/* ==================== host-sim helpers ============================= */

esp_err_t part_sim_inject(uint32_t offset, const void *data, size_t len)
{
    if (data == NULL && len > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((uint64_t)offset + len > (uint64_t)PART_SIM_FLASH_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    esp_err_t r = ESP_FAIL;
    pthread_mutex_lock(&s_mu);
    if (flash_open_locked() >= 0) {
        r = flash_write_locked(offset, data, len);
    }
    pthread_mutex_unlock(&s_mu);
    return r;
}

esp_err_t part_sim_erase_all(void)
{
    esp_err_t r = ESP_FAIL;
    pthread_mutex_lock(&s_mu);
    if (flash_open_locked() < 0) {
        goto out;
    }
    if (s_hole_ok == 1) {
        /* shrink + regrow: back to an all-hole (logically all-0xFF)
         * sparse image. */
        if (ftruncate(s_fd, 0) != 0 ||
            ftruncate(s_fd, (off_t)PART_SIM_FLASH_BYTES) != 0) {
            goto out;
        }
        r = ESP_OK;
    } else {
        r = flash_fill_ff_locked(0, PART_SIM_FLASH_BYTES);
    }
out:
    pthread_mutex_unlock(&s_mu);
    return r;
}

void part_sim_reset(void)
{
    pthread_mutex_lock(&s_mu);
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
    }
    s_hole_ok = -1;
    /* live mmap entries stay: the mappings hold their own reference to
     * the file pages and munmap needs the recorded base/len. */
    pthread_mutex_unlock(&s_mu);
}
