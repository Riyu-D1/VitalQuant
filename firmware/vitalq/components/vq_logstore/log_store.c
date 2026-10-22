/* log_store.c — implements components/vq/include/log_store.h:
 * CRC-protected, append-only record ring on the W25Q512JV (64 MB,
 * CS=IO16). Purpose per contract: survive Wi-Fi loss (offline buffer),
 * hold escalation bundles, keep self-test + event logs.
 *
 * ============================ FLASH MAP ============================
 * 64 MB = 16 384 x 4 KB sectors:
 *   sector 0          superblock journal A (64 slots x 64 B, append-only)
 *   sector 1          superblock journal B (same format; A/B alternate)
 *   sectors 2..16382  log ring — 16 381 data sectors (~64 MB)
 *   sector 16383      scratch — reserved for selftest R/W/E. Its address
 *                     0x3FFF000 is >16 MB, so the selftest exercises the
 *                     4-byte addressing path end-to-end.
 *
 * ========================== RECORD FORMAT ==========================
 * Little-endian, tightly packed (no alignment padding):
 *   u32 id | u32 type | s64 t_us | u32 len | payload[len] | u32 crc32
 * crc32 covers bytes [id .. end of payload] (zlib poly, init/final ~).
 * 0xFF in the id field = unprogrammed NOR tail = end of the sector's
 * record chain. A record never straddles a sector: if it does not fit
 * the sector's remaining bytes, the tail is left erased (padding) and
 * the record starts the next sector. Records DO straddle 256 B pages —
 * program writes are split on page boundaries. Chosen because it makes
 * each sector a self-contained chain that can be validated and dropped
 * (erased) independently — iteration and CRC checks never cross sectors.
 *
 * ========================= RING INVARIANTS =========================
 *  - head_sector/head_off = append cursor; tail_sector = oldest data.
 *  - A sector is always erased BEFORE it becomes head (or before being
 *    appended to at offset 0): appends only ever program bytes that are
 *    0xFF, which is what NOR requires.
 *  - Ring full when a record needs a new sector and the next sector is
 *    tail -> the tail sector is erased and reused: oldest data is lost
 *    first; rotation gives natural wear levelling (~16k sector pool,
 *    each sector erased once per full revolution).
 *  - used_sectors is tracked explicitly: head==tail&&off==0 is EMPTY
 *    when used==0, FULL when used==RING_SECTORS — the counter removes
 *    the ambiguity.
 *
 * ====================== SUPERBLOCK / RECOVERY ======================
 * Scanning all 16k sectors at every boot is too slow, so the ring
 * window is journaled: each commit writes ONE 64-byte slot (no erase)
 * into the current journal sector with magic + seq + CRC:
 *   { seq, head_sector, head_off, tail_sector, next_id,
 *     appends, drops, used_sectors }
 * Commits happen on: every sector advance (head moves -> ~1 commit per
 * 4 KB written), every COMMIT_APPENDS=32 appends, every drop_oldest,
 * and vq_log_sleep. A journal holds 64 commits; when it fills, the OTHER
 * journal sector is erased and becomes current — the live journal is
 * never the one being erased, so a torn journal erase can never destroy
 * the only checkpoint (seq picks the newest valid entry on boot).
 * Journal-sector erase rate: ~1 per 64 commits ~= per 256 KB logged.
 *
 * Boot mount:
 *   1. scan both journal sectors, take the highest-seq valid entry.
 *   2. walk the record chain forward from hinted head (catches up any
 *      appends after the last commit + torn page-programs: a corrupt
 *      tail makes the sector be abandoned — valid prefix stays readable).
 *   3. walk tail forward over erased sectors (catches torn drop_oldest).
 *   4. both journals dead -> full rebuild scan (slow, ~16k sectors, once).
 * The journal's head_off is only a hint — the true append offset is
 * always re-derived by scanning the sector's record chain.
 *
 * Thread safety: one mutex serialises every public entry point.
 * vq_log_iterate holds the mutex while invoking cb — the callback must
 * NOT call any vq_log_* API (self-deadlock) and must not keep the
 * payload pointer (it aliases the shared sector buffer).
 * ESP-IDF 5.4, C99. Non-medical research prototype.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "vq_types.h"
#include "log_store.h"
#include "w25q512.h"

static const char *TAG = "vq_log";

/* ------------------------- flash map ------------------------------ */
#define SECT_SB_A        0u
#define SECT_SB_B        1u
#define SECT_SCRATCH     (W25_TOTAL_SECTORS - 1u)             /* 16383   */
#define SECT_ALIAS_24B   4095u        /* 0xFFF000: where 0x3FFF000 lands
                                       * if 4B addressing were broken   */
#define RING_FIRST       (SECT_SB_B + 1u)                     /* 2       */
#define RING_LAST        (SECT_SCRATCH - 1u)                  /* 16382   */
#define RING_SECTORS     (RING_LAST - RING_FIRST + 1u)        /* 16381   */

/* ------------------------- record --------------------------------- */
#define REC_HDR_BYTES    20u      /* u32 id, u32 type, s64 t_us, u32 len */
#define REC_CRC_BYTES    4u
#define REC_OVERHEAD     (REC_HDR_BYTES + REC_CRC_BYTES)      /* 24      */
#define REC_MAX_PAYLOAD  (W25_SECTOR_SIZE - REC_OVERHEAD)     /* 4072    */
#define BLANK_ID         0xFFFFFFFFu

/* ------------------------- superblock journal --------------------- */
#define VQSB_MAGIC       0x4A4C5156u  /* "VQLJ" little-endian           */
#define VQSB_SLOT        64u          /* slot stride — divides the 256 B
                                       * page, a slot write never splits */
#define VQSB_SLOTS       (W25_SECTOR_SIZE / VQSB_SLOT)        /* 64      */
#define VQSB_DATA_BYTES  40u          /* 10 x u32, crc32 over first 36   */

#define COMMIT_APPENDS   32u          /* journal cadence (appends)       */
#define SCAN_MAX         512u         /* forward-scan bound on mount     */

typedef struct {                    /* on-flash 40 B in a 64 B slot, LE  */
    uint32_t magic;                 /* VQSB_MAGIC                        */
    uint32_t seq;                   /* monotone; highest valid wins      */
    uint32_t head_sector;
    uint32_t head_off;
    uint32_t tail_sector;
    uint32_t next_id;
    uint32_t appends;
    uint32_t drops;
    uint32_t used;                  /* data sectors — packed fast-path   */
    uint32_t crc32;                 /* crc32_buf over bytes [0..36)      */
} vq_sb_t;
typedef char vq_sb_size_check[(sizeof(vq_sb_t) == VQSB_DATA_BYTES) ? 1 : -1];

/* Result of scanning one sector's record chain. */
typedef struct {
    uint32_t first_id, last_id;     /* valid when rec_count > 0          */
    uint32_t rec_count;
    uint32_t valid_end;             /* offset one past last valid record */
    bool     all_ff;                /* whole sector erased               */
    bool     clean_end;             /* bytes [valid_end..4096) all 0xFF  */
} scan_t;

static struct {
    bool        inited;
    bool        present;
    bool        sleeping;
    SemaphoreHandle_t mu;
    uint8_t    *buf;                /* W25_SECTOR_SIZE scratch (PSRAM)   */

    uint32_t    head_sector, head_off;
    uint32_t    tail_sector;
    uint32_t    used;               /* sectors currently holding data    */
    uint32_t    next_id;

    uint32_t    sb_sector;          /* SECT_SB_A or SECT_SB_B            */
    uint32_t    sb_cursor;          /* next free slot byte off, 4096=full */
    uint32_t    sb_seq;

    uint32_t    appends, drops;     /* lifetime stats (journaled)        */
    uint32_t    since_commit;
} s;

/* ------------------------- small helpers --------------------------- */
static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t rd64(const uint8_t *p)
{
    return rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}
static inline void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}
static bool all_ff(const uint8_t *p, size_t n)
{
    while (n--) {
        if (*p++ != 0xFF) {
            return false;
        }
    }
    return true;
}
static inline uint32_t ring_next(uint32_t sec)
{
    return (sec >= RING_LAST) ? RING_FIRST : sec + 1u;
}
static inline uint32_t sect_addr(uint32_t sec)
{
    return sec * W25_SECTOR_SIZE;
}
static void lock(void)   { xSemaphoreTake(s.mu, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s.mu); }

/* ------------------------- CRC32 (zlib poly) ------------------------
 * Table built once at init — avoids depending on ROM CRC entry points
 * and keeps the on-flash format self-describing.                       */
static uint32_t s_crc_tab[256];
static void crc_table_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        s_crc_tab[i] = c;
    }
}
static uint32_t crc32_buf(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFu;
    while (len--) {
        c = s_crc_tab[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* ------------------------- sector chain scanner ---------------------
 * Walk records from offset 0 until the 0xFF tail or the first invalid
 * record (bad len / overrun / CRC). valid_end/clean_end tell the mounter
 * where the append cursor truly is; callers also get first/last ids. */
static void scan_sector(const uint8_t *b, scan_t *o)
{
    memset(o, 0, sizeof(*o));
    o->clean_end = true;
    if (all_ff(b, W25_SECTOR_SIZE)) {
        o->all_ff = true;
        return;
    }
    uint32_t off = 0;
    while (off + REC_OVERHEAD <= W25_SECTOR_SIZE) {
        uint32_t id = rd32(b + off);
        if (id == BLANK_ID) {
            break;                                  /* erased tail      */
        }
        uint32_t len = rd32(b + off + 16);
        uint32_t rl  = REC_OVERHEAD + len;
        if (len > REC_MAX_PAYLOAD || off + rl > W25_SECTOR_SIZE) {
            break;                                  /* corrupt: stop    */
        }
        if (crc32_buf(b + off, REC_HDR_BYTES + len) !=
            rd32(b + off + REC_HDR_BYTES + len)) {
            break;                                  /* corrupt: stop    */
        }
        if (o->rec_count == 0) {
            o->first_id = id;
        }
        o->last_id = id;
        o->rec_count++;
        off += rl;
    }
    o->valid_end = off;
    o->clean_end = all_ff(b + off, W25_SECTOR_SIZE - off);
}

static vq_err_t scan_flash_sector(uint32_t sec, scan_t *o)
{
    vq_err_t e = w25q512_read(sect_addr(sec), s.buf, W25_SECTOR_SIZE);
    if (e == ESP_OK) {
        scan_sector(s.buf, o);
    }
    return e;
}

/* ------------------------- superblock journal ---------------------- */
static void sb_store(uint8_t *p, const vq_sb_t *e)
{
    wr32(p + 0,  e->magic);       wr32(p + 4,  e->seq);
    wr32(p + 8,  e->head_sector); wr32(p + 12, e->head_off);
    wr32(p + 16, e->tail_sector); wr32(p + 20, e->next_id);
    wr32(p + 24, e->appends);     wr32(p + 28, e->drops);
    wr32(p + 32, e->used);        wr32(p + 36, crc32_buf(p, 36));
}
static bool sb_decode(const uint8_t *p, vq_sb_t *e)
{
    if (rd32(p) != VQSB_MAGIC || crc32_buf(p, 36) != rd32(p + 36)) {
        return false;
    }
    e->magic       = rd32(p + 0);  e->seq         = rd32(p + 4);
    e->head_sector = rd32(p + 8);  e->head_off    = rd32(p + 12);
    e->tail_sector = rd32(p + 16); e->next_id     = rd32(p + 20);
    e->appends     = rd32(p + 24); e->drops       = rd32(p + 28);
    e->used        = rd32(p + 32); e->crc32       = rd32(p + 36);
    return true;
}

/* Find the newest valid journal entry across sectors A/B and position
 * the write cursor in the winning sector. */
static bool sb_load(vq_sb_t *best)
{
    bool found = false;
    int  first_blank[2] = { -1, -1 };
    uint32_t win = SECT_SB_A;

    for (uint32_t sec = SECT_SB_A; sec <= SECT_SB_B; sec++) {
        if (w25q512_read(sect_addr(sec), s.buf, W25_SECTOR_SIZE) != ESP_OK) {
            continue;
        }
        for (uint32_t i = 0; i < VQSB_SLOTS; i++) {
            const uint8_t *p = s.buf + i * VQSB_SLOT;
            if (rd32(p) == BLANK_ID) {          /* never-programmed slot */
                if (first_blank[sec] < 0) {
                    first_blank[sec] = (int)i;
                }
                continue;
            }
            vq_sb_t e;
            if (sb_decode(p, &e) && (!found || e.seq > best->seq)) {
                *best = e;
                found = true;
                win   = sec;
            }
        }
    }
    if (found) {
        s.sb_sector = win;
        s.sb_cursor = (first_blank[win] >= 0)
                      ? (uint32_t)first_blank[win] * VQSB_SLOT
                      : W25_SECTOR_SIZE;        /* full -> swap on commit */
        s.sb_seq    = best->seq;
    } else {
        /* No valid entry anywhere (fresh flash or both sectors torn):
         * force the first commit to erase sector A and start there.     */
        s.sb_sector = SECT_SB_B;
        s.sb_cursor = W25_SECTOR_SIZE;
        s.sb_seq    = 0;
    }
    return found;
}

/* Write one journal slot. Swaps to the other sector (erasing it) when
 * the current one is full — the sector being erased is never the one
 * holding the newest valid checkpoint. */
static vq_err_t sb_commit(void)
{
    if (s.sb_cursor >= W25_SECTOR_SIZE) {
        uint32_t other = (s.sb_sector == SECT_SB_A) ? SECT_SB_B : SECT_SB_A;
        vq_err_t e = w25q512_sector_erase(sect_addr(other));
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "journal sector erase failed: %s", esp_err_to_name(e));
            return e;
        }
        s.sb_sector = other;
        s.sb_cursor = 0;
    }
    uint8_t slot[VQSB_SLOT];
    memset(slot, 0xFF, sizeof(slot));
    vq_sb_t e = {
        .magic = VQSB_MAGIC, .seq = s.sb_seq + 1,
        .head_sector = s.head_sector, .head_off = s.head_off,
        .tail_sector = s.tail_sector, .next_id  = s.next_id,
        .appends = s.appends, .drops = s.drops, .used = s.used, .crc32 = 0,
    };
    sb_store(slot, &e);
    vq_err_t r = w25q512_page_program(sect_addr(s.sb_sector) + s.sb_cursor,
                                    slot, VQSB_SLOT);
    if (r == ESP_OK) {
        s.sb_seq++;
        s.sb_cursor += VQSB_SLOT;
        s.since_commit = 0;
    }
    return r;
}

/* ------------------------- ring mechanics --------------------------
 * Select the sector the append cursor moves to, drop-oldest if it holds
 * data, erase it, make it head, commit.
 *
 * victim selection:
 *  - normal case: the sector AFTER head in ring order (free zone or the
 *    oldest data sector when the ring is full -> drop it).
 *  - full ring with head==tail&&off==0 (only reachable after mounting a
 *    completely packed ring): head itself is the oldest data sector —
 *    reuse it in place.
 */
static vq_err_t head_advance(void)
{
    uint32_t victim;
    if (s.head_off == 0 && s.used > 0 && s.head_sector == s.tail_sector) {
        victim = s.head_sector;
    } else {
        victim = ring_next(s.head_sector);
    }
    if (victim == s.tail_sector && s.used > 0) {
        s.tail_sector = ring_next(s.tail_sector);   /* oldest sector drops */
        s.used--;
        s.drops++;
        ESP_LOGD(TAG, "ring full — dropped oldest sector");
    }
    vq_err_t e = w25q512_sector_erase(sect_addr(victim));
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "sector %"PRIu32" erase failed: %s",
                 victim, esp_err_to_name(e));
        return e;
    }
    s.head_sector = victim;
    s.head_off    = 0;
    (void)sb_commit();          /* head move -> checkpoint (warn only)  */
    return ESP_OK;
}

/* Walk the record chain forward from a hinted sector until the real
 * append point: an erased sector, a clean chain tail, or the oldest
 * sector (ring packed). *wrapped reports the last case. */
static vq_err_t find_append_point(uint32_t hint, uint32_t *o_sec,
                                  uint32_t *o_off, uint32_t *max_id,
                                  bool *wrapped)
{
    uint32_t sec = hint;
    *wrapped = false;
    for (uint32_t t = 0; t < SCAN_MAX; t++) {
        scan_t sc;
        vq_err_t e = scan_flash_sector(sec, &sc);
        if (e != ESP_OK) {
            return e;
        }
        if (sc.rec_count && sc.last_id > *max_id) {
            *max_id = sc.last_id;
        }
        if (sc.all_ff) {
            *o_sec = sec; *o_off = 0;
            return ESP_OK;
        }
        if (sc.clean_end) {
            if (sc.valid_end == W25_SECTOR_SIZE) {   /* exactly full     */
                sec = ring_next(sec);
            } else {
                *o_sec = sec; *o_off = sc.valid_end;
                return ESP_OK;
            }
        } else {
            /* Torn program / corrupt tail: cannot append here (NOR can
             * only clear bits). Abandon the sector — its valid prefix
             * stays readable to iterate.                              */
            ESP_LOGW(TAG, "sector %"PRIu32" has corrupt tail — abandoned", sec);
            sec = ring_next(sec);
        }
        if (sec == s.tail_sector) {              /* walked the whole ring */
            *o_sec   = s.tail_sector;
            *o_off   = 0;
            *wrapped = true;
            return ESP_OK;
        }
        if ((t & 15u) == 15u) {
            vTaskDelay(pdMS_TO_TICKS(1));        /* WDT-friendly          */
        }
    }
    /* SCAN_MAX uncommitted full sectors — impossible when commits land
     * (one per advance); treat as packed: append point = oldest sector. */
    ESP_LOGW(TAG, "append scan bound hit — assuming packed ring");
    *o_sec = s.tail_sector; *o_off = 0; *wrapped = true;
    return ESP_OK;
}

/* Both journals unreadable: rebuild the window by scanning every ring
 * sector once. Slow (~16k sectors) but only needed after catastrophic
 * journal loss. Record ids are monotone -> min first_id sector is tail,
 * max last_id sector is head. */
static vq_err_t rebuild_full(void)
{
    ESP_LOGW(TAG, "journal unreadable — full ring rescan (slow)");
    uint32_t min_id = 0xFFFFFFFFu, max_id = 0;
    uint32_t tsec = RING_FIRST, hsec = RING_FIRST, hend = 0;
    bool hclean = true, any = false;

    for (uint32_t sec = RING_FIRST; sec <= RING_LAST; sec++) {
        scan_t sc;
        vq_err_t e = scan_flash_sector(sec, &sc);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "rescan read err sector %"PRIu32, sec);
            continue;                            /* skip on bus error    */
        }
        if (sc.rec_count) {
            any = true;
            if (sc.first_id < min_id) { min_id = sc.first_id; tsec = sec; }
            if (sc.last_id >= max_id) {
                max_id = sc.last_id; hsec = sec;
                hend = sc.valid_end; hclean = sc.clean_end;
            }
        }
        if ((sec & 15u) == 15u) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    if (!any) {                                  /* genuinely empty ring */
        s.head_sector = s.tail_sector = RING_FIRST;
        s.head_off = 0; s.used = 0; s.next_id = 1;
        /* Guarantee the head-is-erased invariant — a fresh chip may not
         * ship all-FF, and a torn state could have left garbage here.   */
        (void)w25q512_sector_erase(sect_addr(s.head_sector));
        return ESP_OK;
    }
    s.tail_sector = tsec;
    if (hclean && hend < W25_SECTOR_SIZE) {
        s.head_sector = hsec;
        s.head_off    = hend;                    /* verified-FF tail     */
    } else {
        /* newest sector full/corrupt -> append point is the next sector.
         * If that IS the tail sector the ring is packed: keep its data —
         * head_advance() will drop-and-reuse it lazily when an append
         * needs the room (never erase data before it must go). Otherwise
         * erase it so the head-is-erased invariant holds (stale content
         * there is older than max_id by definition).                   */
        s.head_sector = ring_next(hsec);
        s.head_off    = 0;
        if (s.head_sector != tsec) {
            (void)w25q512_sector_erase(sect_addr(s.head_sector));
        }
    }
    {
        uint32_t d = (s.head_sector - s.tail_sector + RING_SECTORS)
                     % RING_SECTORS;
        /* head==tail&&off==0 with records present = packed ring, not
         * empty — `any` is guaranteed true on this path.               */
        s.used = (s.head_sector == s.tail_sector && s.head_off == 0)
                 ? RING_SECTORS : d + (s.head_off ? 1u : 0u);
    }
    s.next_id = max_id + 1;
    return ESP_OK;
}

static vq_err_t ring_mount(void)
{
    vq_sb_t sb = {0};
    if (!sb_load(&sb)) {
        vq_err_t e = rebuild_full();
        if (e != ESP_OK) {
            return e;
        }
        return sb_commit();              /* checkpoint rebuilt window     */
    }
    s.head_sector = (sb.head_sector >= RING_FIRST && sb.head_sector <= RING_LAST)
                    ? sb.head_sector : RING_FIRST;
    s.head_off    = (sb.head_off <= W25_SECTOR_SIZE) ? sb.head_off : 0;
    s.tail_sector = (sb.tail_sector >= RING_FIRST && sb.tail_sector <= RING_LAST)
                    ? sb.tail_sector : RING_FIRST;
    s.next_id     = sb.next_id ? sb.next_id : 1;
    s.appends     = sb.appends;
    s.drops       = sb.drops;

    /* 1) true append point, catching up records written after the last
     *    journal commit (commit lag is <= one sector-advance, so this is
     *    normally a single-sector scan). If the ring was packed and a
     *    commit tore, the walk lands back on tail -> wrapped.           */
    uint32_t max_id = 0;
    bool wrapped = false;
    vq_err_t e = find_append_point(s.head_sector, &s.head_sector,
                                   &s.head_off, &max_id, &wrapped);
    if (e != ESP_OK) {
        return e;
    }
    if (max_id + 1 > s.next_id) {
        s.next_id = max_id + 1;                  /* recover torn commits */
    }

    /* 2) tail repair: a drop_oldest may have torn between the sector
     *    erase(s) and the journal commit -> skip erased sectors. Runs
     *    in the wrapped case too (head==tail is FULL there, not empty —
     *    the oldest sector still has to be located).                  */
    uint32_t skipped = 0;
    for (uint32_t t = 0; t < RING_SECTORS; t++) {
        if (!wrapped && s.tail_sector == s.head_sector) {
            break;                               /* normal empty/1-sect  */
        }
        scan_t sc;
        e = scan_flash_sector(s.tail_sector, &sc);
        if (e != ESP_OK) {
            return e;
        }
        if (!sc.all_ff) {
            break;                               /* oldest data found    */
        }
        s.tail_sector = ring_next(s.tail_sector);
        skipped++;
        if ((t & 15u) == 15u) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    /* 3) used count; wrapped means the scan walked tail-to-tail:
     *    every ring sector held data — minus tail sectors found erased. */
    if (wrapped) {
        s.used = RING_SECTORS - skipped;
    } else {
        uint32_t d = (s.head_sector - s.tail_sector + RING_SECTORS)
                     % RING_SECTORS;
        s.used = d + (s.head_off ? 1u : 0u);
    }
    ESP_LOGI(TAG, "mounted: head=%"PRIu32"+%"PRIu32" tail=%"PRIu32
             " used=%"PRIu32"/%"PRIu32" next_id=%"PRIu32,
             s.head_sector, s.head_off, s.tail_sector, s.used,
             (uint32_t)RING_SECTORS, s.next_id);
    return sb_commit();                          /* checkpoint the mount */
}

/* ------------------------- public API ------------------------------ */

vq_err_t vq_log_init(void)
{
    if (s.inited) {
        return s.present ? ESP_OK : ESP_ERR_NOT_FOUND;
    }
    if (s.mu == NULL) {
        s.mu = xSemaphoreCreateMutex();
        if (s.mu == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s.buf == NULL) {
        s.buf = heap_caps_malloc(W25_SECTOR_SIZE,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s.buf == NULL) {                      /* PSRAM-less fallback  */
            s.buf = heap_caps_malloc(W25_SECTOR_SIZE, MALLOC_CAP_8BIT);
        }
        if (s.buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    crc_table_init();

    vq_err_t e = w25q512_init();
    s.present = (e == ESP_OK) && w25q512_present();
    if (s.present) {
        e = ring_mount();
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "ring mount failed: %s", esp_err_to_name(e));
            s.present = false;
        }
    } else {
        ESP_LOGW(TAG, "W25Q512 absent — log store disabled");
    }
    /* Only latch `inited` on success: a transient bus error during probe
     * or mount must not brick the store — the next init call retries.
     * (ring_mount is idempotent: it re-derives all state from flash.)  */
    s.inited = s.present;
    return s.present ? ESP_OK : (e != ESP_OK ? e : ESP_ERR_NOT_FOUND);
}

bool vq_log_flash_present(void)
{
    return s.inited && s.present;
}

vq_err_t vq_log_append(uint32_t type, const void *payload, uint32_t len)
{
    if (!s.inited)   return ESP_ERR_INVALID_STATE;
    if (!s.present)  return ESP_ERR_NOT_FOUND;
    if (len > REC_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (len && payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    lock();
    if (s.sleeping) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t rl = REC_OVERHEAD + len;

    /* Need a new sector when the record would not fit the sector's
     * remaining bytes (tail of sector stays erased = padding), or when
     * the ring is packed and head still points at the oldest sector.    */
    if (W25_SECTOR_SIZE - s.head_off < rl ||
        (s.head_off == 0 && s.used > 0 && s.head_sector == s.tail_sector)) {
        vq_err_t e = head_advance();
        if (e != ESP_OK) {
            unlock();
            return e;
        }
    }

    /* Build the record in the shared buffer. */
    uint8_t *b = s.buf;
    wr32(b + 0, s.next_id);
    wr32(b + 4, type);
    wr64(b + 8, (uint64_t)esp_timer_get_time());
    wr32(b + 16, len);
    if (len) {
        memcpy(b + REC_HDR_BYTES, payload, len);
    }
    wr32(b + REC_HDR_BYTES + len, crc32_buf(b, REC_HDR_BYTES + len));

    /* Program in <=256 B chunks that never cross a page boundary. */
    uint32_t addr = sect_addr(s.head_sector) + s.head_off;
    uint32_t done = 0;
    vq_err_t e = ESP_OK;
    while (done < rl) {
        uint32_t page_off = (addr + done) & (W25_PAGE_SIZE - 1u);
        uint32_t chunk = W25_PAGE_SIZE - page_off;
        if (chunk > rl - done) {
            chunk = rl - done;
        }
        e = w25q512_page_program(addr + done, b + done, chunk);
        if (e != ESP_OK) {
            /* Partial program -> the bytes past `done` are non-FF: mark
             * the sector consumed so the next append starts a fresh
             * sector; the valid prefix stays readable. The sector must
             * also count in `used` — it is no longer erased space.     */
            ESP_LOGE(TAG, "program failed @%"PRIu32": %s",
                     addr + done, esp_err_to_name(e));
            if (s.head_off == 0) {
                s.used++;
            }
            s.head_off = W25_SECTOR_SIZE;
            unlock();
            return e;
        }
        done += chunk;
    }
    if (s.head_off == 0) {
        s.used++;                                /* head gained data     */
    }
    s.head_off += rl;
    s.next_id++;
    s.appends++;
    if (++s.since_commit >= COMMIT_APPENDS) {
        (void)sb_commit();
    }
    unlock();
    return ESP_OK;
}

vq_err_t vq_log_iterate(uint32_t type,
                        bool (*cb)(const vq_log_hdr_t *h,
                                   const uint8_t *payload, void *ctx),
                        void *ctx)
{
    if (!s.inited)  return ESP_ERR_INVALID_STATE;
    if (!s.present) return ESP_ERR_NOT_FOUND;
    if (cb == NULL) return ESP_ERR_INVALID_ARG;

    lock();
    if (s.sleeping) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (s.used == 0) {
        unlock();
        return ESP_OK;
    }
    uint32_t corrupt = 0;
    uint32_t sec = s.tail_sector;
    /* Walk exactly `used` data sectors from tail -> oldest->newest. Using
     * `used` (not a head==tail stop test) also covers the packed ring,
     * where head==tail and every sector holds data.                    */
    for (uint32_t i = 0; i < s.used; i++) {
        vq_err_t e = w25q512_read(sect_addr(sec), s.buf, W25_SECTOR_SIZE);
        if (e != ESP_OK) {
            unlock();
            return e;                            /* bus error is real    */
        }
        uint32_t off = 0;
        while (off + REC_OVERHEAD <= W25_SECTOR_SIZE) {
            uint32_t id = rd32(s.buf + off);
            if (id == BLANK_ID) {
                break;
            }
            uint32_t len = rd32(s.buf + off + 16);
            uint32_t rl  = REC_OVERHEAD + len;
            if (len > REC_MAX_PAYLOAD || off + rl > W25_SECTOR_SIZE ||
                crc32_buf(s.buf + off, REC_HDR_BYTES + len) !=
                    rd32(s.buf + off + REC_HDR_BYTES + len)) {
                corrupt++;
                break;                           /* unrecoverable inside
                                                  * this sector — skip it */
            }
            if (type == 0 || rd32(s.buf + off + 4) == type) {
                vq_log_hdr_t h = {
                    .id   = id,
                    .type = rd32(s.buf + off + 4),
                    .t_us = (int64_t)rd64(s.buf + off + 8),
                    .len  = len,
                };
                if (!cb(&h, s.buf + off + REC_HDR_BYTES, ctx)) {
                    unlock();
                    return ESP_OK;               /* caller asked to stop */
                }
            }
            off += rl;
        }
        sec = ring_next(sec);
        if ((i & 15u) == 15u) {
            vTaskDelay(pdMS_TO_TICKS(1));        /* long drains stay nice */
        }
    }
    unlock();
    if (corrupt) {
        ESP_LOGW(TAG, "iterate: %"PRIu32" corrupt record(s) skipped", corrupt);
    }
    return ESP_OK;
}

/* Drop the oldest n RECORDS — sector-quantized: a tail sector is erased
 * only when every valid record in it lies within the oldest-n span (the
 * erase unit is a whole sector; a partially-covered sector is kept and
 * its records survive to be re-read — the upload drain relies on never
 * losing an un-uploaded record). Erased/record-free sectors are reclaimed
 * without consuming the budget. To erase EVERYTHING pass n >= total
 * record capacity (e.g. UINT32_MAX). */
vq_err_t vq_log_drop_oldest(uint32_t n)
{
    if (!s.inited)  return ESP_ERR_INVALID_STATE;
    if (!s.present) return ESP_ERR_NOT_FOUND;
    if (n == 0)     return ESP_OK;

    lock();
    if (s.sleeping) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    vq_err_t e = ESP_OK;
    uint32_t budget = n;
    while (budget && s.used > 0) {
        scan_t sc;
        e = scan_flash_sector(s.tail_sector, &sc);
        if (e != ESP_OK) {
            break;
        }
        if (!sc.all_ff && sc.rec_count > budget) {
            break;   /* sector straddles the drop span — keep it        */
        }
        uint32_t victim = s.tail_sector;
        e = w25q512_sector_erase(sect_addr(victim));
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "drop erase sector %"PRIu32" failed: %s",
                     victim, esp_err_to_name(e));
            break;
        }
        budget -= sc.rec_count;                  /* 0 for dead sectors    */
        s.tail_sector = ring_next(victim);
        s.used--;
        s.drops++;
        if (victim == s.head_sector) {
            /* Dropped the live head: point head at the just-erased
             * sector — it is the only sector guaranteed erased.        */
            s.head_sector = victim;
            s.head_off    = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(1));            /* up to ~400 ms each    */
    }
    if (s.used == 0) {                           /* normalise empty ring  */
        s.tail_sector = s.head_sector;
    }
    (void)sb_commit();                           /* destructive -> commit  */
    unlock();
    return e;
}

void vq_log_space(uint32_t *used, uint32_t *total)
{
    uint32_t u = 0;
    if (s.inited) {
        lock();
        /* Upper-bound byte estimate: pad bytes inside full sectors count
         * as used — they are not reclaimable without an erase.          */
        u = s.used * W25_SECTOR_SIZE -
            (s.head_off ? (W25_SECTOR_SIZE - s.head_off) : 0u);
        unlock();
    }
    if (used)  *used  = u;
    if (total) *total = RING_SECTORS * W25_SECTOR_SIZE;
}

vq_err_t vq_log_sleep(void)
{
    if (!s.inited)  return ESP_ERR_INVALID_STATE;
    if (!s.present) return ESP_ERR_NOT_FOUND;
    lock();
    vq_err_t e = sb_commit();                    /* flush the window      */
    vq_err_t e2 = w25q512_power_down();
    if (e == ESP_OK) {
        e = e2;
    }
    if (e == ESP_OK) {
        s.sleeping = true;
    }
    unlock();
    return e;
}

vq_err_t vq_log_wake(void)
{
    if (!s.inited)  return ESP_ERR_INVALID_STATE;
    if (!s.present) return ESP_ERR_NOT_FOUND;
    lock();
    vq_err_t e = w25q512_release_power_down();
    if (e == ESP_OK) {
        e = w25q512_enter_4b();                  /* ADS latch is lost in
                                                  * PD — restore it      */
    }
    if (e == ESP_OK) {
        s.sleeping = false;
    }
    unlock();
    return e;
}

/* Self-test: JEDEC, 4B-addressing end-to-end via the scratch sector
 * (0x3FFF000 needs >24-bit address), and a full sector R/W/E verify.
 *
 * The scratch sector's 24-bit truncation alias is sector 4095 — inside
 * the ring. If addressing were somehow broken, scratch ops would land on
 * 4095 and corrupt log data, so we snapshot 4095 first and compare
 * afterwards; a mismatch fails the test AND triggers a restore attempt
 * through the same (truncated) address path, which lands back on 4095. */
vq_err_t vq_log_selftest(char *report, size_t len)
{
    if (report == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t pos = 0;
    int fails = 0;
#define SELF_RPT(...)                                                        \
    do {                                                                     \
        if (pos < len) {                                                     \
            int k_ = snprintf(report + pos, len - pos, __VA_ARGS__);         \
            if (k_ > 0) {                                                    \
                pos += (size_t)k_;                                           \
                if (pos > len) { pos = len; }                                \
            }                                                                \
        }                                                                    \
    } while (0)

    SELF_RPT("w25q512 logstore selftest\n");
    if (!s.inited) {
        SELF_RPT("  init: NOT RUN\n");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s.present) {
        SELF_RPT("  jedec: ABSENT — flash not detected\n");
        return ESP_ERR_NOT_FOUND;
    }
    if (s.sleeping) {
        SELF_RPT("  state: SLEEPING — call vq_log_wake() first\n");
        return ESP_ERR_INVALID_STATE;
    }

    lock();

    /* 1) JEDEC */
    uint8_t id[3] = {0};
    vq_err_t e = w25q512_jedec(id);
    bool jedec_ok = (e == ESP_OK) &&
                    id[0] == W25_JEDEC_MFR && id[1] == W25_JEDEC_TYPE &&
                    id[2] == W25_JEDEC_CAP;
    SELF_RPT("  jedec: %02X %02X %02X %s\n", id[0], id[1], id[2],
             jedec_ok ? "ok" : "MISMATCH");
    if (!jedec_ok) {
        fails++;
    }
    uint8_t sr3 = w25q512_status3();
    SELF_RPT("  sr3.ads(4B): %u\n", (unsigned)(sr3 & 1u));

    /* 2) alias-guard snapshot — only trusted if the read succeeded; a
     * failed read would make snap garbage and trigger a bogus restore. */
    uint8_t *snap = heap_caps_malloc(W25_SECTOR_SIZE,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (snap == NULL) {
        snap = heap_caps_malloc(W25_SECTOR_SIZE, MALLOC_CAP_8BIT);
    }
    bool snap_ok = (snap != NULL) &&
        (w25q512_read(SECT_ALIAS_24B * W25_SECTOR_SIZE, snap,
                      W25_SECTOR_SIZE) == ESP_OK);
    if (!snap_ok) {
        free(snap);
        snap = NULL;
        SELF_RPT("  alias-guard: skipped (no buffer / read failed)\n");
    }

    /* 3) scratch sector R/W/E at 0x3FFF000 (>16 MB -> needs 4B addr) */
    const uint32_t saddr = SECT_SCRATCH * W25_SECTOR_SIZE;
    bool ok = true;
    e = w25q512_sector_erase(saddr);
    if (e == ESP_OK &&
        w25q512_read(saddr, s.buf, W25_SECTOR_SIZE) == ESP_OK &&
        all_ff(s.buf, W25_SECTOR_SIZE)) {
        SELF_RPT("  scratch: erase->FF ok\n");
    } else {
        SELF_RPT("  scratch: erase/FF FAIL (%s)\n", esp_err_to_name(e));
        ok = false; fails++;
    }
    if (ok) {
        for (uint32_t i = 0; i < W25_SECTOR_SIZE; i++) {
            s.buf[i] = (uint8_t)(i ^ (i >> 8) ^ 0x5Au);   /* PRBS-ish    */
        }
        for (uint32_t off = 0; off < W25_SECTOR_SIZE && ok;
             off += W25_PAGE_SIZE) {
            if (w25q512_page_program(saddr + off, s.buf + off,
                                     W25_PAGE_SIZE) != ESP_OK) {
                ok = false;
            }
        }
        /* Verify in 256 B chunks against the deterministic pattern kept
         * in s.buf — no second 4 KB buffer needed.                     */
        uint8_t chk[W25_PAGE_SIZE];
        bool vok = true;
        for (uint32_t off = 0; off < W25_SECTOR_SIZE && vok;
             off += W25_PAGE_SIZE) {
            if (w25q512_read(saddr + off, chk, W25_PAGE_SIZE) != ESP_OK ||
                memcmp(chk, s.buf + off, W25_PAGE_SIZE) != 0) {
                vok = false;
            }
        }
        SELF_RPT("  scratch: program+verify %s\n",
                 (ok && vok) ? "ok" : "FAIL");
        if (!ok || !vok) { fails++; ok = false; }
    }
    if (ok) {                                    /* leave scratch clean  */
        e = w25q512_sector_erase(saddr);
        bool erased_ok = (e == ESP_OK) &&
            (w25q512_read(saddr, s.buf, W25_SECTOR_SIZE) == ESP_OK) &&
            all_ff(s.buf, W25_SECTOR_SIZE);
        SELF_RPT("  scratch: re-erase->FF %s\n", erased_ok ? "ok" : "FAIL");
        if (!erased_ok) { fails++; }
    }

    /* 4) alias-guard verdict + restore */
    if (snap != NULL) {
        uint8_t *now = s.buf;
        if (w25q512_read(SECT_ALIAS_24B * W25_SECTOR_SIZE, now,
                         W25_SECTOR_SIZE) == ESP_OK &&
            memcmp(now, snap, W25_SECTOR_SIZE) == 0) {
            SELF_RPT("  4B-addressing: ok (alias sector untouched)\n");
        } else {
            fails++;
            SELF_RPT("  4B-addressing: FAIL — 0x3FFF000 aliased onto "
                     "0xFFF000; attempting restore\n");
            if (w25q512_sector_erase(saddr) == ESP_OK) {
                bool rok = true;
                for (uint32_t off = 0; off < W25_SECTOR_SIZE && rok;
                     off += W25_PAGE_SIZE) {
                    rok = (w25q512_page_program(saddr + off, snap + off,
                                                W25_PAGE_SIZE) == ESP_OK);
                }
                SELF_RPT("  alias restore: %s\n", rok ? "ok" : "FAILED");
                if (!rok) { fails++; }
            } else {
                SELF_RPT("  alias restore: erase FAILED\n");
                fails++;
            }
        }
        free(snap);
        snap = NULL;
    }

    /* 5) ring state */
    SELF_RPT("  ring: used=%"PRIu32"/%"PRIu32" head=%"PRIu32"+%"PRIu32
             " tail=%"PRIu32" next_id=%"PRIu32" sbseq=%"PRIu32"\n",
             s.used, (uint32_t)RING_SECTORS, s.head_sector, s.head_off,
             s.tail_sector, s.next_id, s.sb_seq);
    SELF_RPT("  result: %s\n", fails ? "FAIL" : "PASS");

    unlock();
    return fails ? ESP_FAIL : ESP_OK;
#undef SELF_RPT
}
