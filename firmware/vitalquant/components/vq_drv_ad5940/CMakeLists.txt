idf_component_register(
    SRCS "src/vq_drv_ad5940.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES vq vq_spi vq_expander vq_led driver esp_timer
)
