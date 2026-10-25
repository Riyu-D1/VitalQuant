# vq_drv_mic — IM69D130 PDM mic driver (U25, fix_v22).
# REQUIRES vq (sensor registry contract), esp_timer (µs stamps),
# esp_driver_i2s (driver/i2s_pdm.h — new I2S API only),
# esp_driver_gpio (IO40 park-low in sleep; IDF 5.4's GPIO driver
# component — the deprecated "driver" umbrella is not used).
# heap/esp_rom/soc/freertos/log come via the common component requires.
idf_component_register(
    SRCS "vq_drv_mic.c"
    INCLUDE_DIRS "include"
    REQUIRES vq esp_timer esp_driver_i2s esp_driver_gpio
)
