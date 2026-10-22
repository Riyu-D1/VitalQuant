/* board_init.c — vq_board: fix_v22 earliest bring-up (ESP32-S3-WROOM-1-N16R8).
 *
 * Implements components/vq/include/board_init.h — the header is the
 * contract; call signatures do not change here. Board ordering rule:
 *
 *     strap read-back (IO45/IO46/IO3/IO0)
 *     -> CS lines driven high + LED/MIC_CLK safe idle   (this file)
 *     -> buses (vq_i2c, vq_spi) -> expander -> drivers  (elsewhere)
 *
 * The safe-idle outputs are the FIRST GPIO this firmware configures —
 * strap read-backs are plain input reads that mutate nothing.
 *
 * FORBIDDEN GPIO — never configured or driven here:
 *   19/20   native USB D-/D+ (USB-Serial/JTAG console owns them)
 *   26-32   internal flash / PSRAM SPI
 *   35-37   octal PSRAM
 *   45-48   straps / USB; IO45 gets ONE plain gpio_get_level() only —
 *           never an output, never a pull config
 *   IO0/IO46 read-only straps; IO3 (strap + TCA6408 ~INT) input-only.
 *
 * eFuses are READ ONLY in this file — nothing is ever burned.
 * Research prototype: no medical claims in strings or logs.
 */
#include <stdio.h>
#include <string.h>

#include "board_init.h"        /* contract — vq component              */
#include "board_pins.h"        /* pin truth table — vq component       */
#include "vq_board_priv.h"     /* module-private expected sizes        */

#include "driver/gpio.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"   /* ESP_EFUSE_STRAP_JTAG_SEL / VDD_SPI_*   */
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "sdkconfig.h"

static const char *TAG = "vq_board";

/* Latched copy handed out by vq_board_info(). */
static vq_boot_info_t s_boot;
static bool           s_boot_valid;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* esp_reset_reason_t -> short printable token for the report line. */
static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:    return "power-on";
    case ESP_RST_EXT:        return "ext-pin";
    case ESP_RST_SW:         return "sw-reset";
    case ESP_RST_PANIC:      return "panic";
    case ESP_RST_INT_WDT:    return "int-wdt";
    case ESP_RST_TASK_WDT:   return "task-wdt";
    case ESP_RST_WDT:        return "wdt";
    case ESP_RST_DEEPSLEEP:  return "deepsleep";
    case ESP_RST_BROWNOUT:   return "brownout";
    case ESP_RST_SDIO:       return "sdio";
    case ESP_RST_USB:        return "usb";
    case ESP_RST_JTAG:       return "jtag";
    case ESP_RST_EFUSE:      return "efuse";
    case ESP_RST_PWR_GLITCH: return "pwr-glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
    case ESP_RST_UNKNOWN:
    default:                 return "unknown";
    }
}

#define VQ_GPIO_MASK(pin)   (1ULL << (unsigned)(pin))

/* ------------------------------------------------------------------ */
/* Stage 1 — safe-idle outputs: FIRST GPIO the firmware drives         */
/* ------------------------------------------------------------------ */

/* CS lines have external 10k pull-ups and must come up HIGH (de-
 * asserted; only one CS may ever be low on the shared MISO). Status
 * LED is active-high -> idle low. MIC_CLK is a driven clock -> idle
 * low. Output levels are written BEFORE output-enable so the pads
 * power on already at the safe level and never glitch. */
static const struct {
    gpio_num_t pin;
    uint32_t   idle;
} s_safe_idle[] = {
    { VQ_CS_ADS1292_GPIO,  1 },   /* IO10 — ADS1292 ~CS      */
    { VQ_CS_AD5940_GPIO,   1 },   /* IO15 — AD5940 ~CS       */
    { VQ_CS_FLASH_GPIO,    1 },   /* IO16 — W25Q512 ~CS      */
    { VQ_CS_AFE4900_GPIO,  1 },   /* IO18 — AFE4900 ~CS      */
    { VQ_CS_MAX86141_GPIO, 1 },   /* IO38 — MAX86141 ~CS + U26 OE */
    { VQ_STATUS_LED_GPIO,  0 },   /* IO17 — active-high, off  */
    { VQ_MIC_CLK_GPIO,     0 },   /* IO40 — PDM clk idle low  */
};

static void drive_safe_idle(void)
{
    uint64_t mask = 0;
    const size_t n = sizeof(s_safe_idle) / sizeof(s_safe_idle[0]);

    for (size_t i = 0; i < n; i++) {
        /* Preload the output data register while still input — the pad
         * comes up at `idle` the moment output-enable lands below.    */
        (void)gpio_set_level(s_safe_idle[i].pin, s_safe_idle[i].idle);
        mask |= VQ_GPIO_MASK(s_safe_idle[i].pin);
    }

    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "safe-idle output config failed");
    }
}

/* ------------------------------------------------------------------ */
/* Stage 2 — interrupt / spare inputs                                  */
/* ------------------------------------------------------------------ */

static void config_inputs(void)
{
    esp_err_t err;

    /* External pulls keep these idles defined — add NO internal pulls
     * (double-pulls skew open-drain edges and the IO3 strap read).   */
    const uint64_t ext_pull_mask =
        VQ_GPIO_MASK(VQ_ADS1292_DRDY_GPIO)  |  /* IO2  — R99 100k up    */
        VQ_GPIO_MASK(VQ_EXP_INT_GPIO)       |  /* IO3  — strap+~INT     */
        VQ_GPIO_MASK(VQ_TMP117_ALERT_GPIO)  |  /* IO5  — open-drain     */
        VQ_GPIO_MASK(VQ_GAUGE_ALRT_GPIO)    |  /* IO6  — open-drain     */
        VQ_GPIO_MASK(VQ_CHG_PG_GPIO)        |  /* IO7  — BQ25170 ~PG    */
        VQ_GPIO_MASK(VQ_MAX86141_INT_GPIO);    /* IO39 — open-drain     */
    const gpio_config_t in_ext = {
        .pin_bit_mask = ext_pull_mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&in_ext);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ext-pull input config failed (%d)", (int)err);
    }

    /* No external pull on these -> internal pull-up for a defined idle
     * until the owning driver attaches its ISR and sets real edges.   */
    const uint64_t int_pullup_mask =
        VQ_GPIO_MASK(VQ_LSM6_INT1_GPIO)      | /* IO4  */
        VQ_GPIO_MASK(VQ_AD5940_GPIO0_GPIO)   | /* IO14 */
        VQ_GPIO_MASK(VQ_AFE4900_RDY_GPIO);     /* IO21 */
    const gpio_config_t in_up = {
        .pin_bit_mask = int_pullup_mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&in_up);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "pull-up input config failed (%d)", (int)err);
    }

    /* IO42 SPARE_IN — floating as built (R107/108 DNP): internal
     * pull-down keeps a defined low idle.                            */
    const gpio_config_t spare = {
        .pin_bit_mask = VQ_GPIO_MASK(VQ_SPARE_IN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&spare);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spare input config failed (%d)", (int)err);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

vq_err_t vq_board_init(vq_boot_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));

    /* -- 1. Reset reason: latched by the ROM at boot — copy out first. */
    const esp_reset_reason_t rr = esp_reset_reason();
    info->reset_reason = (int)rr;

    /* -- 2. Strap read-back — READ-ONLY, before any GPIO is written.
     *    IO45 low selects VDD_SPI = VDD3P3 (3.3 V) for the 16 MB flash;
     *    it gets a plain input read only — no config, no pulls, never
     *    driven. IO0/IO46 same. IO3 doubles as the expander ~INT and
     *    is configured input-only in stage 4.                        */
    const int lv45 = gpio_get_level(VQ_STRAP_VDD_SPI_GPIO);   /* IO45 */
    const int lv46 = gpio_get_level(GPIO_NUM_46);             /* IO46 */
    const int lv0  = gpio_get_level(VQ_BOOT_STRAP_GPIO);      /* IO0  */
    const int lv3  = gpio_get_level(VQ_STRAP_JTAG_GPIO);      /* IO3  */

    info->vdd_spi_3v3 = (lv45 == 0);
    if (!info->vdd_spi_3v3) {
        ESP_LOGW(TAG, "IO45 reads %d - expected 0 (3.3 V VDD_SPI strap)",
                 lv45);
    }
    if (lv0 == 0) {
        ESP_LOGW(TAG, "IO0 reads 0 - BOOT strap low (download boot?)");
    }

    /* -- 3. First GPIO writes: every CS de-asserted high, LED off,
     *    mic clock low — before any bus touches the SPI lines.       */
    drive_safe_idle();

    /* -- 4. Interrupt + spare inputs.                               */
    config_inputs();

    /* -- 5. Memory probe (read-only detection).                     */
    uint32_t flash_bytes = 0;
    const esp_err_t ferr = esp_flash_get_size(NULL, &flash_bytes);
    info->flash_bytes = (ferr == ESP_OK) ? flash_bytes : 0;
    if (ferr != ESP_OK) {
        ESP_LOGW(TAG, "esp_flash_get_size failed (%d)", (int)ferr);
    } else if (info->flash_bytes != VQ_BOOT_FLASH_BYTES_EXPECT) {
        ESP_LOGW(TAG, "flash size 0x%lx != expected 0x%lx",
                 (unsigned long)info->flash_bytes,
                 (unsigned long)VQ_BOOT_FLASH_BYTES_EXPECT);
    }

    info->psram_bytes = (uint32_t)esp_psram_get_size();
#ifdef CONFIG_SPIRAM_MODE_OCT          /* ESP-IDF idiom — no IS_ENABLED */
    info->psram_octal = true;
#else
    info->psram_octal = false;
#endif
    if (info->psram_bytes != VQ_BOOT_PSRAM_BYTES_EXPECT) {
        ESP_LOGW(TAG, "psram size 0x%lx != expected 0x%lx",
                 (unsigned long)info->psram_bytes,
                 (unsigned long)VQ_BOOT_PSRAM_BYTES_EXPECT);
    }
    if (!info->psram_octal) {
        ESP_LOGW(TAG, "CONFIG_SPIRAM_MODE_OCT not set on an N16R8 build");
    }

    /* -- 6. eFuse strap state — READ ONLY, never burned.
     *    STRAP_JTAG_SEL must stay 0: if burnt, the JTAG source-select
     *    strap falls to IO3, which is wired to the expander ~INT line
     *    and would couple debug selection to that signal.            */
    const bool ef_force = esp_efuse_read_field_bit(ESP_EFUSE_VDD_SPI_FORCE);
    const bool ef_tieh  = esp_efuse_read_field_bit(ESP_EFUSE_VDD_SPI_TIEH);
    const bool ef_xpd   = esp_efuse_read_field_bit(ESP_EFUSE_VDD_SPI_XPD);
    info->strap_jtag_fused = esp_efuse_read_field_bit(ESP_EFUSE_STRAP_JTAG_SEL);

    if (info->strap_jtag_fused) {
        ESP_LOGW(TAG, "STRAP_JTAG_SEL burnt - JTAG source follows IO3 "
                      "(expander ~INT); not expected on fix_v22");
    }
    if (ef_force) {
        /* eFuse overrides the IO45 strap; TIEH=1 -> VDD_SPI on VDD3P3. */
        ESP_LOGW(TAG, "VDD_SPI_FORCE burnt - strap ignored "
                      "(tieh=%d xpd=%d)", (int)ef_tieh, (int)ef_xpd);
        if (!ef_tieh) {
            ESP_LOGW(TAG, "eFuse selects 1.8 V VDD_SPI on a 3.3 V "
                          "flash board");
        }
    }

    /* -- 7. Render the one-line-per-field boot report, then latch.  */
    (void)snprintf(info->boot_report, sizeof(info->boot_report),
        "reset=%s\n"
        "flash=0x%06lx %s\n"
        "psram=%s 0x%06lx %s\n"
        "vdd_spi=%s(io45=%d ef[f%d t%d x%d])\n"
        "jtag_sel=%s io0=%d io3=%d io46=%d",
        reset_reason_str(rr),
        (unsigned long)info->flash_bytes,
        (info->flash_bytes == VQ_BOOT_FLASH_BYTES_EXPECT) ? "ok"
                                                          : "UNEXPECTED",
        info->psram_octal ? "octal" : "quad",
        (unsigned long)info->psram_bytes,
        (info->psram_bytes == VQ_BOOT_PSRAM_BYTES_EXPECT) ? "ok"
                                                          : "UNEXPECTED",
        info->vdd_spi_3v3 ? "3v3" : "1v8?", lv45,
        (int)ef_force, (int)ef_tieh, (int)ef_xpd,
        info->strap_jtag_fused ? "BURNT" : "0",
        lv0, lv3, lv46);

    s_boot       = *info;
    s_boot_valid = true;
    ESP_LOGI(TAG, "fix_v22 straps: reset=%s io45=%d io0=%d io3=%d io46=%d",
             reset_reason_str(rr), lv45, lv0, lv3, lv46);
    return ESP_OK;
}

const vq_boot_info_t *vq_board_info(void)
{
    return s_boot_valid ? &s_boot : NULL;
}
