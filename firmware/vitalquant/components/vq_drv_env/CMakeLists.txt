# vq_drv_env — environmental I2C sensor group (TMP117 x3, BME280, SHT45).
# Contract headers come from `vq`; bus access is mutexed inside `vq_i2c`.
# `driver` provides GPIO (TMP117 ALERT input on IO5); `esp_timer` provides
# the monotonic acquisition stamps required by the vq_types.h contract.
idf_component_register(SRCS "vq_drv_env.c" "vq_tmp117.c" "vq_bme280.c" "vq_sht45.c"
                       INCLUDE_DIRS "include"
                       REQUIRES vq vq_i2c driver esp_timer)
