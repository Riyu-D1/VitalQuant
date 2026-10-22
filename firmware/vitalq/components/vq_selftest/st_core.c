/* st_core.c — bring-up tests that exercise the board fabric:
 *   rails_boot, i2c_scan, expander_walk, spi_ids, miso_contention, irq_latency
 */
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "esp_attr.h"
#include "driver/gpio.h"

#if defined(__has_include)
#  if __has_include("esp_partition.h")
#    define ST_HAVE_PARTITION 1
#    include "esp_partition.h"
#  endif
#  if __has_include("esp_ota_ops.h")
#    define ST_HAVE_OTA 1
#    include "esp_ota_ops.h"
#  endif
#endif

#include "st_priv.h"
#include "board_init.h"
#include "board_pins.h"
#include "i2c_bus.h"
#include "spi_bus.h"
#include "expander.h"
#include "pipeline.h"

/* ================================ rails_boot ========================== */

static const char *st_rr_str(int r)
{
    switch ((esp_reset_reason_t)r) {
    case ESP_RST_POWERON:    return "poweron";
    case ESP_RST_SW:         return "sw";
    case ESP_RST_PANIC:      return "panic";
    case ESP_RST_INT_WDT:    return "int_wdt";
    case ESP_RST_TASK_WDT:   return "task_wdt";
    case ESP_RST_WDT:        return "wdt";
    case ESP_RST_DEEPSLEEP:  return "deepsleep";
    case ESP_RST_BROWNOUT:   return "brownout";
    case ESP_RST_SDIO:       return "sdio";
    case ESP_RST_USB:        return "usb";
    case ESP_RST_JTAG:       return "jtag";
    case ESP_RST_EFUSE:      return "efuse";
    case ESP_RST_PWR_GLITCH: return "pwr_glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
    default:                 return "?";
    }
}

/* PSRAM march: 0xAA/0x55 byte sweeps + index + walking-bit word passes.
 * Cache caveat: PSRAM is cache-mapped; volatile accesses keep the smoke
 * honest but this is not a substitute for a cache-off march. */
static uint32_t st_psram_march(uint8_t *m, size_t bytes)
{
    volatile uint8_t  *p8  = m;
    volatile uint32_t *p32 = (volatile uint32_t *)m;
    const size_t w = bytes / 4;
    uint32_t errs = 0;

    memset(m, 0xAA, bytes);
    for (size_t i = 0; i < bytes; i++) if (p8[i] != 0xAA) errs++;
    memset(m, 0x55, bytes);
    for (size_t i = 0; i < bytes; i++) if (p8[i] != 0x55) errs++;

    for (size_t i = 0; i < w; i++) p32[i] = (uint32_t)i;
    for (size_t i = 0; i < w; i++) if (p32[i] != (uint32_t)i) errs++;

    static const uint8_t bits[] = { 0, 8, 16, 24, 31 };
    for (size_t b = 0; b < sizeof(bits); b++) {
        uint32_t msk = 1u << bits[b];
        for (size_t i = 0; i < w; i++) p32[i] = (uint32_t)i ^ msk;
        for (size_t i = 0; i < w; i++)
            if (p32[i] != ((uint32_t)i ^ msk)) errs++;
    }
    return errs;
}

vq_err_t st_rails_boot(char *r, size_t n)
{
    char det[896];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_verdict_t v = ST_V_PASS;
    const char *fail = "";

    const vq_boot_info_t *bi = vq_board_info();
    if (!bi) {
        st_printf(&d, "vq_board_info() NULL");
        st_done(r, n, "rails_boot", ST_V_FAIL, &d);
        return ST_RC_FAIL;
    }
    st_printf(&d, "reset=%s(%d) vdd_spi=%s flash=%luB psram=%luB(%s)",
              st_rr_str(bi->reset_reason), bi->reset_reason,
              bi->vdd_spi_3v3 ? "3V3" : "1V8!",
              (unsigned long)bi->flash_bytes,
              (unsigned long)bi->psram_bytes,
              bi->psram_octal ? "octal" : "quad?");

    if (!bi->vdd_spi_3v3)            { v = ST_V_FAIL; fail = "vdd_spi strap != 3V3"; }
    if (bi->flash_bytes != 16u << 20) { v = ST_V_FAIL; fail = "flash != 16MB"; }
    if (bi->psram_bytes != 8u << 20 || !bi->psram_octal)
                                     { v = ST_V_FAIL; fail = "psram != 8MB octal"; }
    if (bi->strap_jtag_fused)        { v = ST_V_FAIL; fail = "STRAP_JTAG_SEL fused"; }

    /* PSRAM march over >= 1 MB */
    const size_t march_len = 1u << 20;
    uint8_t *m = heap_caps_malloc(march_len, MALLOC_CAP_SPIRAM);
    if (!m) {
        v = ST_V_FAIL; fail = "no SPIRAM heap";
        st_printf(&d, "\n  march: SKIP (malloc fail)");
    } else {
        uint32_t errs = st_psram_march(m, march_len);
        heap_caps_free(m);
        st_printf(&d, "\n  march 1MB AA/55/idx/walk: %s errs=%lu",
                  errs ? "FAIL" : "ok", (unsigned long)errs);
        if (errs) { v = ST_V_FAIL; fail = "psram march errors"; }
    }

    /* app partition CRC + image magic */
#ifdef ST_HAVE_PARTITION
    {
        const esp_partition_t *app = NULL;
#ifdef ST_HAVE_OTA
        app = esp_ota_get_running_partition();
#endif
        if (!app) app = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
        if (!app) {
            v = ST_V_FAIL; fail = "no app partition";
            st_printf(&d, "\n  app partition: not found");
        } else {
            uint32_t crc = 0;
            uint8_t  buf[4096];
            esp_partition_read(app, 0, buf, 1);
            uint8_t magic = buf[0];
            for (size_t off = 0; off < app->size; off += sizeof(buf)) {
                size_t cl = app->size - off < sizeof(buf) ? app->size - off
                                                        : sizeof(buf);
                if (esp_partition_read(app, off, buf, cl) != ESP_OK) {
                    v = ST_V_FAIL; fail = "partition read err";
                    break;
                }
                crc = esp_rom_crc32_le(crc, buf, cl);
            }
            st_printf(&d, "\n  app %s@0x%lx %luKB crc32=0x%08lx magic=0x%02x%s",
                      app->label, (unsigned long)app->address,
                      (unsigned long)(app->size >> 10), (unsigned long)crc,
                      magic, magic == 0xE9 ? "" : " BAD-IMAGE");
            if (magic != 0xE9) { v = ST_V_FAIL; fail = "app image magic"; }
        }
    }
#else
    st_printf(&d, "\n  app crc: SKIP (esp_partition.h not on include path)");
#endif

    if (fail[0]) st_printf(&d, "\n  -> %s", fail);
    st_done(r, n, "rails_boot", v, &d);
    return st_rc(v);
}

/* ================================ i2c_scan ============================ */

/* read + expect-match a 1-byte ID register */
static bool st_id8(uint8_t a, uint8_t reg, uint8_t expect,
                   st_buf_t *d, const char *label)
{
    uint8_t v = 0;
    vq_err_t e = vq_i2c_read_reg(a, reg, &v, 1);
    st_printf(d, "\n  %s@0x%02X r%02X=0x%02X exp0x%02X %s",
              label, a, reg, v, expect,
              e != ESP_OK ? "ERR" : v == expect ? "ok" : "MISMATCH");
    return e == ESP_OK && v == expect;
}

static bool st_id16(uint8_t a, uint8_t reg, uint16_t expect,
                    st_buf_t *d, const char *label)
{
    uint16_t v = 0;
    vq_err_t e = st_rd16be(a, reg, &v);
    st_printf(d, "\n  %s@0x%02X r%02X=0x%04X exp0x%04X %s",
              label, a, reg, v, expect,
              e != ESP_OK ? "ERR" : v == expect ? "ok" : "MISMATCH");
    return e == ESP_OK && v == expect;
}

vq_err_t st_i2c_scan(char *r, size_t n)
{
    char det[960];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_verdict_t v = ST_V_PASS;

    static const uint8_t req[] =
        { 0x20, 0x36, 0x39, 0x3A, 0x44, 0x48, 0x49, 0x52, 0x6A, 0x76 };
    uint8_t addrs[24];
    size_t cnt = vq_i2c_scan(addrs, sizeof(addrs));
    uint8_t bm[16] = {0};
    for (size_t i = 0; i < cnt; i++) bm[addrs[i] >> 3] |= 1u << (addrs[i] & 7);
#define ST_SEEN(a) (bm[(a) >> 3] & (1u << ((a) & 7)))

    st_printf(&d, "%u nodes:", (unsigned)cnt);
    for (size_t i = 0; i < cnt; i++) st_printf(&d, " %02X", addrs[i]);

    uint8_t missing[11]; size_t nmis = 0;
    for (size_t i = 0; i < sizeof(req); i++)
        if (!ST_SEEN(req[i])) missing[nmis++] = req[i];
    if (nmis) {
        v = ST_V_FAIL;
        st_printf(&d, "\n  MISSING:");
        for (size_t i = 0; i < nmis; i++) st_printf(&d, " 0x%02X", missing[i]);
    }
    st_printf(&d, "\n  0x4A tmp117.tail: %s",
              ST_SEEN(0x4A) ? "present (optional)" : "absent — optional");
    /* unexpected responders */
    for (size_t i = 0; i < cnt; i++) {
        uint8_t a = addrs[i];
        bool known = a == 0x4A;
        for (size_t j = 0; j < sizeof(req); j++) known |= a == req[j];
        if (!known) st_printf(&d, "\n  UNEXPECTED 0x%02X on bus", a);
    }

    /* per-device ID reads — only where the node ACKed */
    if (ST_SEEN(VQ_ADDR_TCA6408)) {
        uint8_t in = 0, out = 0;
        vq_i2c_read_reg(VQ_ADDR_TCA6408, 0x00, &in, 1);
        vq_i2c_read_reg(VQ_ADDR_TCA6408, 0x01, &out, 1);
        st_printf(&d, "\n  tca6408 in=0x%02X out=0x%02X", in, out);
    }
    if (ST_SEEN(VQ_ADDR_MAX17048)) {
        uint16_t ver = 0;
        vq_err_t e = st_rd16be(VQ_ADDR_MAX17048, 0x08, &ver);
        st_printf(&d, "\n  max17048 ver=0x%04X%s", ver,
                  e || !ver ? " ERR" : " ok");
        if (e || !ver) v = ST_V_FAIL;
    }
    if (ST_SEEN(VQ_ADDR_AS7341)) {
        uint8_t aux = 0, rev = 0, id = 0;
        vq_i2c_read_reg(VQ_ADDR_AS7341, 0x90, &aux, 1);
        vq_i2c_read_reg(VQ_ADDR_AS7341, 0x91, &rev, 1);
        vq_i2c_read_reg(VQ_ADDR_AS7341, 0x92, &id, 1);
        st_printf(&d, "\n  as7341 id=0x%02X auxid=0x%02X rev=0x%02X exp0x24 %s",
                  id, aux, rev, id == 0x24 ? "ok" : "MISMATCH");
        if (id != 0x24) v = ST_V_FAIL;
    }
    if (ST_SEEN(VQ_ADDR_MLX90632)) {
        uint8_t w[2] = { 0x24, 0x05 }, b[6] = {0};
        vq_err_t e = vq_i2c_write_read(VQ_ADDR_MLX90632, w, 2, b, 6);
        st_printf(&d, "\n  mlx90632 id=%02X%02X%02X%02X%02X%02X%s",
                  b[0], b[1], b[2], b[3], b[4], b[5],
                  e == ESP_OK ? " ok" : " ERR");
        if (e) v = ST_V_FAIL;
    }
    if (ST_SEEN(VQ_ADDR_SHT45)) {
        uint8_t cmd = 0x89, b[6] = {0};
        vq_err_t e = vq_i2c_write(VQ_ADDR_SHT45, &cmd, 1);
        if (e == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            e = vq_i2c_read(VQ_ADDR_SHT45, b, 6);
        }
        uint32_t ser = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
                       ((uint32_t)b[3] << 8) | b[4];
        st_printf(&d, "\n  sht45 sn=0x%08lX%s", (unsigned long)ser,
                  e == ESP_OK && ser && ser != 0xFFFFFFFF ? " ok" : " ERR");
        if (e || !ser || ser == 0xFFFFFFFF) v = ST_V_FAIL;
    }
    if (ST_SEEN(VQ_ADDR_TMP117_SKIN) &&
        !st_id16(VQ_ADDR_TMP117_SKIN, 0x0F, 0x0117, &d, "tmp117.skin")) v = ST_V_FAIL;
    if (ST_SEEN(VQ_ADDR_TMP117_DOME) &&
        !st_id16(VQ_ADDR_TMP117_DOME, 0x0F, 0x0117, &d, "tmp117.dome")) v = ST_V_FAIL;
    if (ST_SEEN(0x4A))
        st_id16(0x4A, 0x0F, 0x0117, &d, "tmp117.tail(opt)");
    if (ST_SEEN(VQ_ADDR_RV3028)) {
        uint8_t s0 = 0, s1 = 0;
        vq_err_t e0 = vq_i2c_read_reg(VQ_ADDR_RV3028, 0x00, &s0, 1);
        vTaskDelay(pdMS_TO_TICKS(1100));
        vq_err_t e1 = vq_i2c_read_reg(VQ_ADDR_RV3028, 0x00, &s1, 1);
        bool tick = e0 == ESP_OK && e1 == ESP_OK && s0 != s1;
        st_printf(&d, "\n  rv3028 sec 0x%02X->0x%02X %s",
                  s0, s1, tick ? "ok (ticking)" : "no-move? (no backup cell)");
    }
    if (ST_SEEN(VQ_ADDR_LSM6DSV) &&
        !st_id8(VQ_ADDR_LSM6DSV, 0x0F, 0x73, &d, "lsm6dsv80x")) v = ST_V_FAIL;
    if (ST_SEEN(VQ_ADDR_BME280) &&
        !st_id8(VQ_ADDR_BME280, 0xD0, 0x60, &d, "bme280")) v = ST_V_FAIL;

    st_done(r, n, "i2c_scan", v, &d);
    return st_rc(v);
#undef ST_SEEN
}

/* ================================ expander_walk ======================= */

vq_err_t st_expander_walk(char *r, size_t n)
{
    char det[640];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_verdict_t v = ST_V_PASS;

    uint8_t bad = vq_exp_selftest_walk();
    uint8_t in = 0;
    vq_exp_read_inputs(&in);
    bool vbus = vq_exp_vbus_present();
    bool chg  = vq_exp_charging();
    gpio_set_direction(VQ_CHG_PG_GPIO, GPIO_MODE_INPUT);
    int pg = gpio_get_level(VQ_CHG_PG_GPIO);

    st_printf(&d, "walk_bad=0x%02X in=0x%02X", bad, in);
    if (bad) {
        v = ST_V_FAIL;
        st_printf(&d, " failed:");
        for (int i = 0; i < 8; i++)
            if (bad & (1u << i)) st_printf(&d, " P%d", i);
    }
    st_printf(&d, "\n  P5 chg_stat=%d (%s)", (in >> 5) & 1,
              chg ? "charging" : "idle");
    st_printf(&d, "\n  P6 vbus_det=%d vbus=%s", (in >> 6) & 1,
              vbus ? "PRESENT" : "absent");
    st_printf(&d, "\n  chg_pg(IO7)=%d %s", pg,
              pg == 0 ? "(power good)" : "(not good/float)");
    if (vbus && pg) {
        st_printf(&d, "\n  note: VBUS_DET high but ~PG high — check divider/Qx");
        v = ST_V_FAIL;   /* contradictory evidence: flag for the bench */
    }
    st_done(r, n, "expander_walk", v, &d);
    return st_rc(v);
}

/* ================================ spi_ids ============================= */

typedef vq_err_t (*st_spi_read_fn)(void *h, uint32_t *id);

static void *st_spi_add(vq_spi_dev_t dev, uint8_t mode, uint32_t hz,
                        uint8_t pre, uint8_t post)
{
    static const gpio_num_t cs[VQ_SPI_DEV_COUNT] = {
        VQ_CS_ADS1292_GPIO, VQ_CS_AFE4900_GPIO, VQ_CS_AD5940_GPIO,
        VQ_CS_FLASH_GPIO,   VQ_CS_MAX86141_GPIO,
    };
    vq_spi_dev_cfg_t c = {
        .dev = dev, .cs_gpio = cs[dev], .mode = mode,
        .clock_hz = hz, .cs_pre_us = pre, .cs_post_us = post,
    };
    return vq_spi_add(&c);
}

static vq_err_t st_rd_w25q(void *h, uint32_t *id)
{
    uint8_t cmd = 0x9F, rx[3] = {0};
    vq_err_t e = vq_spi_write_read(h, &cmd, 1, rx, 3);
    *id = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];
    return e;
}

static vq_err_t st_rd_ads1292(void *h, uint32_t *id)
{
    uint8_t sdatac = 0x11, junk[4] = {0};
    vq_spi_xfer(h, &sdatac, junk, 1);              /* exit RDATAC */
    uint8_t cmd[2] = { 0x20, 0x00 }, rx = 0;       /* RREG ID, 1 reg */
    vq_err_t e = vq_spi_write_read(h, cmd, 2, &rx, 1);
    *id = rx;
    return e;
}

static vq_err_t st_rd_ad5940(void *h, uint32_t *id)
{
    /* After power/sleep prep parks the chip, the AD5940 hibernates:
     * the first SPI traffic starts its wake timer but reads stay 0x00
     * until it lapses, so retry the ID read once after a wake delay.   */
    for (int attempt = 0; attempt < 2; attempt++) {
        uint8_t sa[3] = { 0x20, 0x04, 0x00 }, junk[4] = {0};  /* SETADDR 0x0400 */
        vq_spi_xfer(h, sa, junk, 3);
        /* READREG frame layout (same as the driver's reg_rd): the byte
         * right after the 0x6D opcode is a turnaround/dummy — register
         * data starts at frame offset 2.                            */
        uint8_t rr = 0x6D, rx[3] = {0};                     /* READREG */
        vq_err_t e = vq_spi_write_read(h, &rr, 1, rx, 3);
        *id = ((uint32_t)rx[1] << 8) | rx[2];
        if (e != ESP_OK || (*id != 0 && *id != 0xFFFF))
            return e;
        vTaskDelay(pdMS_TO_TICKS(20));           /* hibernate wake      */
    }
    return ESP_OK;
}

static vq_err_t st_rd_max86141(void *h, uint32_t *id)
{
    uint8_t tx[3] = { 0xFF, 0xFF, 0x00 }, rx[3] = {0};  /* addr, READ_EN, dummy */
    vq_err_t e = vq_spi_xfer(h, tx, rx, 3);
    *id = rx[2];
    return e;
}

vq_err_t st_spi_ids(char *r, size_t n)
{
    char det[1024];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_verdict_t v = ST_V_PASS;
    bool any = false;

    /* release the reset/pwdn clamps the expander boots with */
    bool s0 = vq_exp_get_out(VQ_EXP_AFE_RESETZ);
    bool s1 = vq_exp_get_out(VQ_EXP_ADS_PWDN);
    bool s2 = vq_exp_get_out(VQ_EXP_AD5940_RST);
    vq_exp_set(VQ_EXP_AFE_RESETZ, true);
    vq_exp_set(VQ_EXP_ADS_PWDN,  true);
    vq_exp_set(VQ_EXP_AD5940_RST, true);
    vTaskDelay(pdMS_TO_TICKS(200));   /* ADS1292 POR + osc settle */

    static const struct {
        const char *name; vq_spi_dev_t dev; uint8_t mode;
        uint8_t pre, post; uint32_t op_hz; st_spi_read_fn fn;
        uint32_t expect, alt;
    } t[] = {
        { "w25q512",  VQ_SPI_DEV_FLASH,    0, 1, 1, VQ_SPI_FLASH_HZ,
          st_rd_w25q,    0xEF4020, 0 },
        { "ads1292r", VQ_SPI_DEV_ADS1292,  1, 20, 20, VQ_SPI_ADS1292_HZ,
          st_rd_ads1292, 0x73,     0 },
        { "ad5940",   VQ_SPI_DEV_AD5940,   0, 1, 1, VQ_SPI_AD5940_HZ,
          st_rd_ad5940,  0x4144,   0x0552 },
        { "max86141", VQ_SPI_DEV_MAX86141, 0, 1, 1, VQ_SPI_MAX86141_HZ,
          st_rd_max86141, 0x25,    0 },
    };

    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        uint32_t id1 = 0, id2 = 0;
        vq_err_t e1 = ESP_FAIL, e2 = ESP_FAIL;
        void *h = st_spi_add(t[i].dev, t[i].mode, VQ_SPI_PROBE_HZ,
                             t[i].pre, t[i].post);
        if (h) { e1 = t[i].fn(h, &id1); vq_spi_remove(h); }
        h = st_spi_add(t[i].dev, t[i].mode, t[i].op_hz, t[i].pre, t[i].post);
        if (h) { e2 = t[i].fn(h, &id2); vq_spi_remove(h); }

        if (e1 != ESP_OK && e2 != ESP_OK) {
            v = ST_V_FAIL;
            st_printf(&d, "\n  %s: no reply @1M/%luM (e=%d/%d)", t[i].name,
                      (unsigned long)(t[i].op_hz / 1000000), e1, e2);
            continue;
        }
        any = true;
        bool match = id1 == id2 &&
                     (id1 == t[i].expect || (t[i].alt && id1 == t[i].alt));
        st_printf(&d, "\n  %s id=0x%06lX @1M / 0x%06lX @%luM %s",
                  t[i].name, (unsigned long)id1, (unsigned long)id2,
                  (unsigned long)(t[i].op_hz / 1000000),
                  match ? "ok" : "MISMATCH");
        if (!match) v = ST_V_FAIL;
    }

    /* AFE4900: no public ID reg on file — driver hook or skip */
    if (vq_afe4900_diag_read) {
        uint32_t val = 0;
        vq_err_t e = vq_afe4900_diag_read(0x00, &val);  /* diag/id reg per drv */
        st_printf(&d, "\n  afe4900 diag[0x00]=0x%06lX via drv %s",
                  (unsigned long)val, e == ESP_OK ? "ok" : "ERR");
        if (e == ESP_OK) any = true; else v = ST_V_FAIL;
    } else {
        st_printf(&d, "\n  afe4900: SKIP — no vq_afe4900_diag_read hook");
    }
    /* MAX86141 driver hook cross-check when linked */
    if (vq_max86141_read_partid) {
        uint8_t pid = 0;
        vq_err_t e = vq_max86141_read_partid(&pid);
        st_printf(&d, "\n  max86141 partid=0x%02X via drv %s",
                  pid, e == ESP_OK ? "ok" : "ERR");
    }
    if (vq_ads1292_read_id) {
        uint8_t id = 0;
        vq_err_t e = vq_ads1292_read_id(&id);
        st_printf(&d, "\n  ads1292r id=0x%02X via drv %s",
                  id, e == ESP_OK ? "ok" : "ERR");
    }

    vq_exp_set(VQ_EXP_AFE_RESETZ, s0);
    vq_exp_set(VQ_EXP_ADS_PWDN,  s1);
    vq_exp_set(VQ_EXP_AD5940_RST, s2);

    if (!any && v == ST_V_PASS) v = ST_V_SKIP;
    st_done(r, n, "spi_ids", v, &d);
    return st_rc(v);
}

/* ============================ miso_contention ========================= */

vq_err_t st_miso_contention(char *r, size_t n)
{
    char det[384];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_verdict_t v = ST_V_PASS;

    /* all CS idle-high is the bus driver's steady state — read MISO with
     * each pull; float = the line tracks the pull (hi!=lo). */
    int hi = vq_spi_miso_float_check(GPIO_PULLUP_ONLY);
    int lo = vq_spi_miso_float_check(GPIO_PULLDOWN_ONLY);
    st_printf(&d, "MISO pullup=%d pulldown=%d", hi, lo);
    if (hi == lo) {
        v = ST_V_FAIL;
        st_printf(&d, " — DRIVEN to %d with all CS high", hi);
    } else {
        st_printf(&d, " — floats ok");
    }
    st_done(r, n, "miso_contention", v, &d);
    return st_rc(v);
}

/* ============================== irq_latency =========================== */

typedef struct {
    QueueHandle_t     q;
    volatile uint32_t isr_n;
} st_isr_ctx_t;

static void IRAM_ATTR st_isr(void *arg)
{
    st_isr_ctx_t *c = arg;
    int64_t t = esp_timer_get_time();
    c->isr_n++;
    xQueueSendFromISR(c->q, &t, NULL);
}

/* returns edges seen; fills detail text */
static uint32_t st_probe_line(gpio_num_t g, gpio_int_type_t type,
                              uint32_t window_ms, st_buf_t *d,
                              const char *label)
{
    st_isr_ctx_t ctx = { .q = xQueueCreate(32, sizeof(int64_t)), .isr_n = 0 };
    gpio_set_direction(g, GPIO_MODE_INPUT);
    gpio_set_intr_type(g, type);
    vq_err_t add = gpio_isr_handler_add(g, st_isr, &ctx);
    if (add != ESP_OK) {
        /* line already owned by a driver ISR → slow-poll fallback */
        st_printf(d, "\n  %s: ISR slot busy (drv owns) — polled:", label);
        int prev = gpio_get_level(g);
        int64_t t0 = esp_timer_get_time(), last = 0, first = 0;
        uint32_t edges = 0, it = 0;
        while (esp_timer_get_time() - t0 < (int64_t)window_ms * 1000) {
            int cur = gpio_get_level(g);
            bool edge = (type == GPIO_INTR_POSEDGE && !prev && cur) ||
                        (type == GPIO_INTR_NEGEDGE && prev && !cur) ||
                        (type == GPIO_INTR_ANYEDGE && prev != cur);
            if (edge) {
                if (!first) first = esp_timer_get_time();
                last = esp_timer_get_time();
                edges++;
            }
            prev = cur;
            esp_rom_delay_us(25);
            if (!(++it & 0x3FF)) vTaskDelay(1);  /* yield ~every 10 ms */
        }
        st_printf(d, " edges=%lu", (unsigned long)edges);
        if (edges > 1)
            st_printf(d, " int=%luus",
                      (unsigned long)((last - first) / (edges - 1)));
        vQueueDelete(ctx.q);
        return edges;
    }

    int64_t t0 = esp_timer_get_time(), prev_t = 0;
    uint32_t edges = 0;
    int64_t drain_sum = 0, drain_max = 0, int_sum = 0;
    uint32_t ints = 0;
    while (esp_timer_get_time() - t0 < (int64_t)window_ms * 1000) {
        int64_t te;
        if (xQueueReceive(ctx.q, &te, pdMS_TO_TICKS(20)) == pdTRUE) {
            int64_t now = esp_timer_get_time();
            int64_t lat = now - te;
            drain_sum += lat;
            if (lat > drain_max) drain_max = lat;
            if (prev_t) { int_sum += te - prev_t; ints++; }
            prev_t = te;
            edges++;
        }
    }
    gpio_isr_handler_remove(g);
    gpio_set_intr_type(g, GPIO_INTR_DISABLE);
    vQueueDelete(ctx.q);

    st_printf(d, "\n  %s: edges=%lu", label, (unsigned long)edges);
    if (ints)
        st_printf(d, " int=%luus", (unsigned long)(int_sum / ints));
    if (edges)
        st_printf(d, " isr->drain avg=%luus max=%luus",
                  (unsigned long)(drain_sum / edges),
                  (unsigned long)drain_max);
    return edges;
}

vq_err_t st_irq_latency(char *r, size_t n)
{
    char det[896];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    uint32_t total_edges = 0;

    gpio_install_isr_service(0);   /* benign if already installed */

    /* driver-side edge stats where the pipeline is already stamping them */
    static const struct { vq_channel_id_t ch; const char *n; } stt[] = {
        { VQ_CH_ECG_II,  "ads.drdy"  },
        { VQ_CH_PPG_IR,  "afe.rdy"   },
        { VQ_CH_PPG_SAT, "max.int"   },
    };
    for (size_t i = 0; i < sizeof(stt) / sizeof(stt[0]); i++) {
        vq_edge_stats_t s = {0};
        vq_pipeline_edge_stats(stt[i].ch, &s);
        if (s.n)
            st_printf(&d, "\n  %s drv: n=%lu mean=%.0fus jit=%.1fus",
                      stt[i].n, (unsigned long)s.n, (double)s.mean_us,
                      (double)s.jitter_us);
    }

    total_edges += st_probe_line(VQ_ADS1292_DRDY_GPIO, GPIO_INTR_NEGEDGE,
                                 300, &d, "ads.drdy io2");
    total_edges += st_probe_line(VQ_AFE4900_RDY_GPIO, GPIO_INTR_POSEDGE,
                                 300, &d, "afe.rdy io21");
    total_edges += st_probe_line(VQ_MAX86141_INT_GPIO, GPIO_INTR_NEGEDGE,
                                 300, &d, "max.int io39");
    uint32_t imu_edges = st_probe_line(VQ_LSM6_INT1_GPIO, GPIO_INTR_POSEDGE,
                                       300, &d, "imu.int1 io4");

    /* IMU: if no INT1 edges, try enabling DRDY_XL->INT1 at the current ODR
     * (restore after). Only pokes when the device stays silent. */
    if (!imu_edges && vq_i2c_probe(VQ_ADDR_LSM6DSV) == ESP_OK) {
        uint8_t c1 = 0, i1 = 0;
        vq_i2c_read_reg(VQ_ADDR_LSM6DSV, 0x10, &c1, 1);
        vq_i2c_read_reg(VQ_ADDR_LSM6DSV, 0x0D, &i1, 1);
        uint8_t ni1 = i1 | 0x01;                     /* INT1_DRDY_XL */
        uint8_t nc1 = (c1 & 0x0F) ? c1 : (c1 & 0xF0) | 0x03; /* ~15Hz */
        vq_i2c_write_reg(VQ_ADDR_LSM6DSV, 0x0D, &ni1, 1);
        vq_i2c_write_reg(VQ_ADDR_LSM6DSV, 0x10, &nc1, 1);
        vTaskDelay(pdMS_TO_TICKS(20));
        imu_edges = st_probe_line(VQ_LSM6_INT1_GPIO, GPIO_INTR_POSEDGE,
                                  500, &d, "imu.int1(st-en)");
        vq_i2c_write_reg(VQ_ADDR_LSM6DSV, 0x0D, &i1, 1);
        vq_i2c_write_reg(VQ_ADDR_LSM6DSV, 0x10, &c1, 1);
    }
    total_edges += imu_edges;

    st_printf(&d, "\n  static: gauge_alrt io6=%d ad5940.io14=%d "
              "exp_int io3=%d tmp_alrt io5=%d chg_pg io7=%d (no trigger — "
              "gauge/RTC alert forcing is bench-risky)",
              gpio_get_level(VQ_GAUGE_ALRT_GPIO),
              gpio_get_level(VQ_AD5940_GPIO0_GPIO),
              gpio_get_level(VQ_EXP_INT_GPIO),
              gpio_get_level(VQ_TMP117_ALERT_GPIO),
              gpio_get_level(VQ_CHG_PG_GPIO));

    st_verdict_t v = total_edges ? ST_V_PASS : ST_V_SKIP;
    if (v == ST_V_SKIP)
        st_printf(&d, "\n  no edges on any INT line — devices not streaming?");
    st_done(r, n, "irq_latency", v, &d);
    return st_rc(v);
}
