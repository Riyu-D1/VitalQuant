# vq_drv_fsr — FSR402 contact sensor (IO1/ADC1_CH0, +3V3-FSR / R8-10k-GND
# divider -> ORDINAL contact level, NO SI units) + status LED on IO17.
# 20 Hz sampling via the driver's own esp_timer, per vq_sensor.h contract.
idf_component_register(SRCS "vq_drv_fsr.c" "vq_status_led.c"
                       INCLUDE_DIRS "include"
                       REQUIRES vq driver esp_timer esp_adc)
