# vq_drv_island — 1.8 V island drivers (AS7341 spectral, MLX90632 IR temp)
# behind the PCA9306; same logical I2C bus via vq_i2c helpers.
idf_component_register(SRCS "src/vq_drv_island.c" "src/as7341.c" "src/mlx90632.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES vq vq_i2c vq_led driver esp_timer)
