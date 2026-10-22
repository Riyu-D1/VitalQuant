/* esp_flash_sim.c — host-sim backing for esp_flash.h.
 *
 * The 16 MB QIO flash is a raw file, offset == flash address, erased
 * state 0xFF.  Created lazily on first access and padded to 16 MB;
 * the partition-table agent owns what lives inside the image.
 *
 * Location: $VSIM_FLASH_PATH, else the first of
 *   ./sim/build/flash_image.bin,
 *   ./firmware/vitalq/sim/build/flash_image.bin, ./build/flash_image.bin
 */
#include "esp_flash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <pthread.h>

#define SIM_JEDEC_ID 0x1840EFu   /* Winbond W25Q128-class, 16 MB */

static esp_flash_t s_chip = {
    .size      = ESP_FLASH_SIM_SIZE_BYTES,
    .device_id = SIM_JEDEC_ID,
    .host_ctx  = NULL,
};
esp_flash_t *esp_flash_default_chip = &s_chip;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *s_img;

static esp_flash_t *dev(esp_flash_t *chip)
{
    return chip ? chip : esp_flash_default_chip;
}

static void mkdirs_for(const char *file)
{
    char tmp[512];
    size_t n = strlen(file);
    if (n >= sizeof(tmp)) return;
    memcpy(tmp, file, n + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

static const char *flash_path(void)
{
    static char path[512];
    const char *env = getenv("VSIM_FLASH_PATH");
    if (env && env[0]) return env;
    struct stat st;
    if (stat("sim", &st) == 0)
        snprintf(path, sizeof(path), "sim/build/flash_image.bin");
    else if (stat("firmware/vitalq", &st) == 0)
        snprintf(path, sizeof(path), "firmware/vitalq/sim/build/flash_image.bin");
    else
        snprintf(path, sizeof(path), "build/flash_image.bin");
    return path;
}

/* Open (or create + 0xFF-pad to 16 MB) the image.  s_lock held. */
static esp_err_t ensure_img(void)
{
    if (s_img) return ESP_OK;

    const char *path = flash_path();
    mkdirs_for(path);
    s_img = fopen(path, "r+b");
    if (!s_img) s_img = fopen(path, "w+b");
    if (!s_img) return ESP_FAIL;

    if (fseek(s_img, 0, SEEK_END) != 0) { fclose(s_img); s_img = NULL; return ESP_FAIL; }
    long sz = ftell(s_img);
    if (sz < 0) { fclose(s_img); s_img = NULL; return ESP_FAIL; }

    if ((uint32_t)sz < ESP_FLASH_SIM_SIZE_BYTES) {
        uint8_t ff[SPI_FLASH_SEC_SIZE];
        memset(ff, 0xff, sizeof(ff));
        if (fseek(s_img, (long)sz, SEEK_SET) != 0) { fclose(s_img); s_img = NULL; return ESP_FAIL; }
        uint32_t todo = ESP_FLASH_SIM_SIZE_BYTES - (uint32_t)sz;
        while (todo) {
            uint32_t n = todo < sizeof(ff) ? todo : (uint32_t)sizeof(ff);
            if (fwrite(ff, 1, n, s_img) != n) { fclose(s_img); s_img = NULL; return ESP_FAIL; }
            todo -= n;
        }
        fflush(s_img);
    }
    return ESP_OK;
}

static bool range_ok(uint32_t address, uint32_t length)
{
    return address <= ESP_FLASH_SIM_SIZE_BYTES &&
           length <= ESP_FLASH_SIM_SIZE_BYTES - address;
}

esp_err_t esp_flash_init(esp_flash_t *chip)
{
    pthread_mutex_lock(&s_lock);
    esp_err_t r = ensure_img();
    pthread_mutex_unlock(&s_lock);
    if (r == ESP_OK && chip) {
        chip->size      = ESP_FLASH_SIM_SIZE_BYTES;
        chip->device_id = SIM_JEDEC_ID;
    }
    return r;
}

esp_err_t esp_flash_get_size(esp_flash_t *chip, uint32_t *out_size)
{
    if (!out_size) return ESP_ERR_INVALID_ARG;
    *out_size = dev(chip)->size;
    return ESP_OK;
}

esp_err_t esp_flash_read_id(esp_flash_t *chip, uint32_t *out_id)
{
    if (!out_id) return ESP_ERR_INVALID_ARG;
    *out_id = dev(chip)->device_id;
    return ESP_OK;
}

uint32_t esp_flash_get_id(esp_flash_t *chip)
{
    return dev(chip)->device_id;
}

esp_err_t esp_flash_read(esp_flash_t *chip, void *buffer,
                         uint32_t address, uint32_t length)
{
    (void)dev(chip);
    if (!buffer && length) return ESP_ERR_INVALID_ARG;
    if (!range_ok(address, length)) return ESP_ERR_INVALID_SIZE;
    if (!length) return ESP_OK;

    pthread_mutex_lock(&s_lock);
    esp_err_t r = ensure_img();
    if (r == ESP_OK) {
        if (fseek(s_img, (long)address, SEEK_SET) != 0 ||
            fread(buffer, 1, length, s_img) != length)
            r = ESP_FAIL;
    }
    pthread_mutex_unlock(&s_lock);
    return r;
}

esp_err_t esp_flash_write(esp_flash_t *chip, const void *buffer,
                          uint32_t address, uint32_t length)
{
    (void)dev(chip);
    if (!buffer && length) return ESP_ERR_INVALID_ARG;
    if (!range_ok(address, length)) return ESP_ERR_INVALID_SIZE;
    if (!length) return ESP_OK;

    pthread_mutex_lock(&s_lock);
    esp_err_t r = ensure_img();
    if (r == ESP_OK) {
        if (fseek(s_img, (long)address, SEEK_SET) != 0 ||
            fwrite(buffer, 1, length, s_img) != length ||
            fflush(s_img) != 0)
            r = ESP_FAIL;
    }
    pthread_mutex_unlock(&s_lock);
    return r;
}

esp_err_t esp_flash_erase_region(esp_flash_t *chip,
                                 uint32_t start, uint32_t len)
{
    (void)dev(chip);
    if (len == 0 || (start % SPI_FLASH_SEC_SIZE) != 0 ||
        (len % SPI_FLASH_SEC_SIZE) != 0)
        return ESP_ERR_INVALID_ARG;
    if (!range_ok(start, len)) return ESP_ERR_INVALID_SIZE;

    pthread_mutex_lock(&s_lock);
    esp_err_t r = ensure_img();
    if (r == ESP_OK) {
        uint8_t ff[SPI_FLASH_SEC_SIZE];
        memset(ff, 0xff, sizeof(ff));
        if (fseek(s_img, (long)start, SEEK_SET) != 0) {
            r = ESP_FAIL;
        } else {
            uint32_t todo = len;
            while (r == ESP_OK && todo) {
                uint32_t n = todo < sizeof(ff) ? todo : (uint32_t)sizeof(ff);
                if (fwrite(ff, 1, n, s_img) != n) r = ESP_FAIL;
                todo -= n;
            }
            if (fflush(s_img) != 0) r = ESP_FAIL;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return r;
}

esp_err_t esp_flash_erase_chip(esp_flash_t *chip)
{
    return esp_flash_erase_region(chip, 0, ESP_FLASH_SIM_SIZE_BYTES);
}
