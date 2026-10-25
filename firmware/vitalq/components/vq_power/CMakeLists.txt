# vq_power — power_mgr.h implementation: battery/charge/sleep governor.
# The cell is ASSUMED UNPROTECTED — this component is the firmware cutoff.
#
# Dependency notes:
#   * "esp_sleep" functionality (esp_sleep.h, EXT0/EXT1 wake, gpio hold) is
#     provided by the IDF component esp_hw_support — there is no component
#     literally named esp_sleep in IDF 5.4.2.
#   * vq_drv_aux provides the MAX17048 gauge API (vq_gauge_*). If the
#     component/header is not landed yet, vq_power.c falls back to local
#     extern prototypes so this file still compiles standalone.
#   * vq_pipeline (sample windows for board temp) and vq_logstore (event
#     persistence before deep sleep) are soft-fail: absent/stale data -> NAN
#     / skipped append, never fatal.
idf_component_register(
    SRCS "src/vq_power.c"
    REQUIRES vq vq_expander vq_led vq_i2c vq_drv_aux vq_pipeline vq_logstore
             driver esp_timer esp_hw_support esp_adc
             freertos esp_system log)
