# vq_console — USB-Serial/JTAG REPL (esp_console) implementing vq_console.h.
#
# REQUIRES notes:
#  - 'console' is the IDF component that ships esp_console.h (there is no
#    component literally named 'esp_console').
#  - 'esp_timer' supplies esp_timer_get_time() used for event stamps and the
#    LED slot timing check.
#  - 'esp_driver_gpio' supplies driver/gpio.h pulled in by vq/board_pins.h and
#    used to mirror LED slots on the status LED (IO17).
idf_component_register(
    SRCS "vq_console.c"
    INCLUDE_DIRS "."
    REQUIRES vq vq_selftest vq_net vq_cloud vq_decision vq_power vq_led
             vq_logstore vq_i2c vq_pipeline vq_edge_model vq_board
             console nvs_flash esp_hw_support esp_timer esp_driver_gpio)
