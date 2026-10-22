/* w25q512.c — Winbond W25Q512JV (512 Mbit / 64 MB) SPI flash driver.
 *
 * Device: VQ_SPI_DEV_FLASH on the shared SPI2 host — CS=IO16, mode 0,
 * capped at VQ_SPI_FLASH_HZ (40 MHz) by spi_bus.h. /WP and /HOLD are
 * tied to +3V3 in hardware, so no GPIO handling is needed here.
 *
 * ==================== 4-BYTE ADDRESSING POLICY ====================
 * >16 MB needs >24-bit addresses. Two mechanisms exist:
 *   (a) Enter-4B (B7h) flips the ADS latch so 03h/02h/20h take 4 B addr;
 *   (b) dedicated 4B opcodes — 13h read, 12h page-program, 21h 4KB
 *       sector-erase (also 5Ch/DC for larger blocks) — that ALWAYS take
 *       a 4-byte address regardless of the ADS latch.
 *
 * This driver uses (b): the ADS latch resets to 3B on every power-on /
 * reset and after power-down, and a latch-dependent driver is one stale
 * bit away from silently aliasing 0x3FFF000 onto 0xFFF000. Opcodes are
 * mode-independent — no state to lose. B7h is STILL issued at init and
 * after each power-down release (and SR3.ADS verified) so the chip
 * reports 4B mode and any out-of-band 3B-opcode access stays correct.
 * ================================================================
 *
 * Timing (datasheet): tPP 0.7 ms typ / 3 ms max, tSE ~50 ms typ /
 * 400 ms max, tDP ~3 us, tRES1 ~30 us — all waited via RDSR1.BUSY
 * (05h bit0) with margins, or fixed µs delays for the power ops.
 */
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"            /* esp_rom_delay_us */

#include "spi_bus.h"
#include "board_pins.h"
#include "w25q512.h"

static const char *TAG = "w25q512";

/* ---- opcodes ---------------------------------------------------------- */
#define CMD_JEDEC        0x9F   /* JEDEC ID — works in 3B or 4B mode   */
#define CMD_WREN         0x06   /* write enable (sets SR1.WEL)         */
#define CMD_RDSR1        0x05   /* read status register 1              */
#define CMD_RDSR3        0x15   /* read status register 3              */
#define CMD_READ_4B      0x13   /* read, always 4-byte address         */
#define CMD_PP_4B        0x12   /* page program, always 4-byte address */
#define CMD_SE_4B        0x21   /* 4 KB sector erase, always 4B addr   */
#define CMD_ENTER_4B     0xB7   /* enter 4-byte address mode           */
#define CMD_PWRDN        0xB9   /* deep power-down                     */
#define CMD_REL_PD       0xAB   /* release power-down / device ID      */

/* ---- status bits ------------------------------------------------------ */
#define SR1_BUSY         0x01u
#define SR1_WEL          0x02u
#define SR3_ADS          0x01u  /* 0 = 3B mode, 1 = 4B mode            */

/* ---- timing ----------------------------------------------------------- */
#define T_DP_US          10u    /* power-down entry; datasheet max ~3 us  */
#define T_RES1_US        50u    /* release-PD to standby; max ~30 us      */
#define PP_TIMEOUT_MS    20u    /* tPP typ 0.7 ms / max 3 ms + margin     */
#define SE_TIMEOUT_MS    900u   /* tSE typ ~50 ms / max 400 ms + margin   */

static void *s_dev;                   /* vq_spi handle (spi_device_handle_t) */
static bool  s_present;

static inline void put_addr4(uint8_t *p, uint32_t a)
{
    p[0] = (uint8_t)(a >> 24);
    p[1] = (uint8_t)(a >> 16);
    p[2] = (uint8_t)(a >> 8);
    p[3] = (uint8_t)a;
}

/* Single-byte command, no data phase (WREN, B7, B9, AB...). */
static vq_err_t cmd0(uint8_t op)
{
    return vq_spi_xfer(s_dev, &op, NULL, 1);
}

static vq_err_t write_enable(void)
{
    vq_err_t e = cmd0(CMD_WREN);
    if (e != ESP_OK) {
        return e;
    }
    /* WEL must latch; if CS/SPI is dead the SR read would say no and the
     * erase/program would silently do nothing — catch it here instead. */
    if ((w25q512_status1() & SR1_WEL) == 0) {
        ESP_LOGW(TAG, "WREN did not latch WEL");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ API */

vq_err_t w25q512_jedec(uint8_t out3[3])
{
    if (s_dev == NULL || out3 == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t cmd = CMD_JEDEC;
    return vq_spi_write_read(s_dev, &cmd, 1, out3, 3);
}

uint8_t w25q512_status1(void)
{
    uint8_t cmd = CMD_RDSR1, v = 0xFF;
    if (s_dev) {
        (void)vq_spi_write_read(s_dev, &cmd, 1, &v, 1);
    }
    return v;
}

uint8_t w25q512_status3(void)
{
    uint8_t cmd = CMD_RDSR3, v = 0xFF;
    if (s_dev) {
        (void)vq_spi_write_read(s_dev, &cmd, 1, &v, 1);
    }
    return v;
}

vq_err_t w25q512_wait_idle(uint32_t timeout_ms)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        if ((w25q512_status1() & SR1_BUSY) == 0) {
            return ESP_OK;
        }
        if (esp_timer_get_time() >= deadline) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));   /* yield — erases run ~50-400 ms */
    }
}

vq_err_t w25q512_read(uint32_t addr, uint8_t *buf, size_t len)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len == 0) {
        return ESP_OK;
    }
    if (buf == NULL || addr + len > W25_TOTAL_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t hdr[5] = { CMD_READ_4B };
    put_addr4(hdr + 1, addr);
    return vq_spi_write_read(s_dev, hdr, sizeof(hdr), buf, len);
}

vq_err_t w25q512_page_program(uint32_t addr, const uint8_t *buf, size_t len)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (buf == NULL || len == 0 || len > W25_PAGE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    /* A page program may never cross a 256 B boundary. */
    if ((addr & (W25_PAGE_SIZE - 1u)) + len > W25_PAGE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    vq_err_t e = write_enable();
    if (e != ESP_OK) {
        return e;
    }
    /* [opcode | 4B addr | data] under one CS. Build one contiguous TX
     * buffer so only vq_spi_xfer() semantics are needed.             */
    uint8_t tx[5 + W25_PAGE_SIZE];
    tx[0] = CMD_PP_4B;
    put_addr4(tx + 1, addr);
    memcpy(tx + 5, buf, len);
    e = vq_spi_xfer(s_dev, tx, NULL, 5 + len);
    if (e != ESP_OK) {
        return e;
    }
    return w25q512_wait_idle(PP_TIMEOUT_MS);
}

vq_err_t w25q512_sector_erase(uint32_t addr)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (addr & (W25_SECTOR_SIZE - 1u)) {
        return ESP_ERR_INVALID_ARG;
    }
    vq_err_t e = write_enable();
    if (e != ESP_OK) {
        return e;
    }
    uint8_t tx[5] = { CMD_SE_4B };
    put_addr4(tx + 1, addr);
    e = vq_spi_xfer(s_dev, tx, NULL, sizeof(tx));
    if (e != ESP_OK) {
        return e;
    }
    return w25q512_wait_idle(SE_TIMEOUT_MS);
}

vq_err_t w25q512_enter_4b(void)
{
    vq_err_t e = cmd0(CMD_ENTER_4B);
    if (e != ESP_OK) {
        return e;
    }
    /* Confirm SR3.ADS latched — informational; 4B opcodes don't need it. */
    if ((w25q512_status3() & SR3_ADS) == 0) {
        ESP_LOGW(TAG, "B7h issued but SR3.ADS still 0");
        return ESP_FAIL;
    }
    return ESP_OK;
}

vq_err_t w25q512_power_down(void)
{
    vq_err_t e = cmd0(CMD_PWRDN);
    esp_rom_delay_us(T_DP_US);          /* tDP: ~3 us until standby      */
    return e;
}

vq_err_t w25q512_release_power_down(void)
{
    vq_err_t e = cmd0(CMD_REL_PD);
    esp_rom_delay_us(T_RES1_US);        /* tRES1: ~30 us until standby   */
    return e;
}

vq_err_t w25q512_init(void)
{
    if (s_dev != NULL && s_present) {
        return ESP_OK;                                   /* idempotent */
    }
    if (s_dev == NULL) {
        const vq_spi_dev_cfg_t cfg = {
            .dev        = VQ_SPI_DEV_FLASH,
            .cs_gpio    = VQ_CS_FLASH_GPIO,      /* IO16 */
            .mode       = 0,
            .clock_hz   = VQ_SPI_FLASH_HZ,       /* 40 MHz (bus clamps)  */
            .cs_pre_us  = 0,
            .cs_post_us = 0,
        };
        s_dev = vq_spi_add(&cfg);
        if (s_dev == NULL) {
            ESP_LOGE(TAG, "vq_spi_add(FLASH) failed");
            return ESP_FAIL;
        }
    }

    /* Release-PD unconditionally: cheap no-op when already awake, and it
     * revives the chip after deep-sleep where the PD latch persisted. */
    (void)w25q512_release_power_down();

    uint8_t id[3] = {0};
    vq_err_t e = w25q512_jedec(id);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "JEDEC read failed: %s", esp_err_to_name(e));
        return e;
    }
    ESP_LOGI(TAG, "JEDEC %02X %02X %02X", id[0], id[1], id[2]);
    if (!(id[0] == W25_JEDEC_MFR && id[1] == W25_JEDEC_TYPE &&
          id[2] == W25_JEDEC_CAP)) {
        if (id[0] == W25_JEDEC_MFR && id[1] == 0x70 && id[2] == W25_JEDEC_CAP) {
            ESP_LOGE(TAG, "EF7020 = stacked-die W25Q512JV-IM — unsupported");
        } else {
            ESP_LOGE(TAG, "unexpected JEDEC — expected EF 40 20");
        }
        return ESP_ERR_NOT_FOUND;
    }

    e = w25q512_enter_4b();             /* belt: 4B opcodes don't rely on it */
    if (e != ESP_OK) {
        return e;
    }
    s_present = true;
    ESP_LOGI(TAG, "W25Q512JV present, 4B mode, %"PRIu32" sectors",
             (uint32_t)W25_TOTAL_SECTORS);
    return ESP_OK;
}

bool w25q512_present(void)
{
    return s_present;
}
