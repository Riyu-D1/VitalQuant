/* main.c — firmware v2 entry. Orchestration lives here; the heavy work
 * is in components. Boot: board_init → buses → expander → drivers probe
 * → services → pipeline → decision loop → net. Nothing optional is fatal.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_init.h"
#include "i2c_bus.h"
#include "spi_bus.h"
#include "expander.h"
#include "clock_service.h"
#include "power_mgr.h"
#include "led_sched.h"
#include "log_store.h"
#include "pipeline.h"
#include "decision.h"
#include "wifi_mgr.h"
#include "cloud_client.h"
#include "vq_console.h"
#include "selftest.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "vitalq";

void app_main(void)
{
    vq_boot_info_t boot = {0};
    ESP_ERROR_CHECK(nvs_flash_init());

    /* 1. Board + buses — asserts only legal pins, CS high, I2C @100k. */
    vq_board_init(&boot);
    ESP_LOGI(TAG, "%s", boot.boot_report);
    vq_i2c_init();
    vq_spi_init();
    vq_exp_init();

    /* 2. Core services */
    vq_clock_init();
    vq_power_init();
    vq_led_init();
    vq_log_init();

    /* 3. Sensing */
    vq_pipeline_init();
    vq_pipeline_start();

    /* 4. On-device model + decision loop */
    vq_decision_cfg_t dcfg = {
        .period_s = 180, .threshold = 0.65f,
        .motion_gate = true, .require_ppg = true,
        .esc_window_s = 60,
        .esc_ch = {VQ_CH_PPG_IR, VQ_CH_PPG_RED, VQ_CH_ECG_II,
                   VQ_CH_RESP, VQ_CH_EDA, VQ_CH_ACC_X,
                   VQ_CH_TEMP_SKIN, VQ_CH_PPG_SAT},
    };
    vq_decision_init(&dcfg);
    vq_decision_start();

    /* 5. Net last — everything works offline without it */
    vq_wifi_init();
    vq_cloud_init();
    if (vq_wifi_have_creds()) {
        vq_wifi_connect();   /* async reconnects after this */
    }
    vq_wifi_status_httpd_start();

    /* 6. Console last — recovery path always present */
    vq_console_start();
    ESP_LOGI(TAG, "vitalq fw2 up — research prototype, no medical claims");
}
