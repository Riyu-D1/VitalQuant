/* vq_drv_island.c — registration + bring-up selftest for the 1.8 V
 * island pair (AS7341 0x39, MLX90632 0x3A behind the PCA9306 — same
 * logical bus, everything goes through i2c_bus.h mutexed helpers).
 */
#include <stdio.h>
#include "esp_log.h"
#include "vq_drv_island.h"
#include "vq_island_priv.h"
#include "vq_sensor.h"
#include "i2c_bus.h"
#include "board_pins.h"

static const char *TAG = "vq_island";

esp_err_t vq_island_register(void)
{
    as7341_early_init();                   /* static mutexes, no bus I/O */
    mlx90632_early_init();
    esp_err_t e1 = vq_sensor_register(vq_island_as7341_sensor());
    esp_err_t e2 = vq_sensor_register(vq_island_mlx90632_sensor());
    if (e1 != ESP_OK) return e1;
    if (e2 != ESP_OK) return e2;
    ESP_LOGI(TAG, "registered as7341 + mlx90632 (1V8 island)");
    return ESP_OK;
}

/* Selftest (selftest.h §9 "optical"/"i2c_scan" evidence):
 *   1. bus probe both addresses
 *   2. AS7341: ID/AUXID/REVID + one dark frame + one LED-illuminated frame
 *      → CLEAR delta (proves the LDR-driven ~92 mA LED lights the scene)
 *   3. MLX90632: EEPROM product code + EE_VERSION (DSP5 id check) + one
 *      ambient/object capture in a plausible range
 * Report is always filled; return code reflects the first failure.      */
esp_err_t vq_island_selftest(char *report, size_t len)
{
    if (!report || len == 0) return ESP_ERR_INVALID_ARG;
    (void)vq_i2c_init();                         /* idempotent per i2c_bus.h */
    size_t n = 0;
    esp_err_t first_err = ESP_OK;
#define FAIL(e) do { if (first_err == ESP_OK) first_err = (e); } while (0)
/* bounded append — snprintf's would-be length can push n past len */
#define APPEND(...) do { \
        if (n >= len) n = len - 1; \
        n += snprintf(report + n, len - n, __VA_ARGS__); \
    } while (0)

    /* ---- AS7341 ----------------------------------------------------- */
    APPEND("as7341: ");
    esp_err_t e = vq_i2c_probe(VQ_ADDR_AS7341);
    if (e != ESP_OK) {
        APPEND("ABSENT (no ACK)\n");
        FAIL(ESP_ERR_NOT_FOUND);
    } else {
        uint8_t id = 0, aux = 0, rev = 0;
        e = as7341_read_ids(&id, &aux, &rev);
        if (e != ESP_OK) {
            APPEND("id read fail %s\n", esp_err_to_name(e));
            FAIL(e);
        } else if ((id & 0xFC) != 0x24) {
            APPEND("BAD ID 0x%02x (exp 0x24)\n", id);
            FAIL(ESP_ERR_INVALID_RESPONSE);
        } else {
            uint16_t dark[12] = {0}, lit[12] = {0};
            int64_t  t[4];
            e = as7341_capture_frames(dark, lit, t);
            uint32_t dsum = 0, lsum = 0;
            for (int i = 0; i < 12; i++) { dsum += dark[i]; lsum += lit[i]; }
            if (e != ESP_OK) {
                APPEND("id=0x%02x aux=0x%02x rev=0x%02x | "
                       "capture fail %s (dark clr=%u)\n",
                       id, aux, rev, esp_err_to_name(e), dark[4]);
                FAIL(e);
            } else {
                int32_t dclr = (int32_t)lit[4] - (int32_t)dark[4];
                /* evidence = LED-on CLEAR rises above LED-off CLEAR      */
                bool lit_ok = (int32_t)lsum > (int32_t)dsum && dclr > 0;
                APPEND("id=0x%02x aux=0x%02x rev=0x%02x | "
                       "clr %u->%u (d%+ld) sum %lu->%lu %s\n",
                       id, aux, rev, dark[4], lit[4], (long)dclr,
                       (unsigned long)dsum, (unsigned long)lsum,
                       lit_ok ? "LED-OK" : "LED-NODELTA");
                if (!lit_ok) FAIL(ESP_ERR_INVALID_RESPONSE);
            }
        }
    }

    /* ---- MLX90632 ---------------------------------------------------- */
    APPEND("mlx90632: ");
    e = vq_i2c_probe(VQ_ADDR_MLX90632);
    if (e != ESP_OK) {
        APPEND("ABSENT (no ACK)\n");
        FAIL(ESP_ERR_NOT_FOUND);
    } else {
        uint16_t prod = 0, ver = 0;
        e = mlx90632_read_ids(&prod, &ver);
        if (e != ESP_OK) {
            APPEND("eeprom read fail %s\n", esp_err_to_name(e));
            FAIL(e);
        } else {
            float tobj = 0, tamb = 0;
            esp_err_t e2 = mlx90632_measure(&tobj, &tamb);
            bool dsp5 = (ver & 0x00FF) == 0x05;
            bool sane = (e2 == ESP_OK) &&
                        tamb > -40.0f && tamb < 125.0f &&
                        tobj > -40.0f && tobj < 300.0f;
            APPEND("prod=0x%04x ee=0x%04x %s | Ta=%.2f To=%.2f %s\n",
                   prod, ver, dsp5 ? "DSP5" : "BAD-VER",
                   (double)tamb, (double)tobj,
                   e2 == ESP_OK ? (sane ? "OK" : "IMPLAUSIBLE")
                                : esp_err_to_name(e2));
            if (!dsp5) FAIL(ESP_ERR_INVALID_RESPONSE);
            else if (e2 != ESP_OK) FAIL(e2);
            else if (!sane) FAIL(ESP_ERR_INVALID_RESPONSE);
        }
    }
#undef FAIL
    APPEND("island: %s\n", first_err == ESP_OK ? "PASS" : "FAIL");
    return first_err;
}
