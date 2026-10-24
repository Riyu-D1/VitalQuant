# vq_drv_max86141 — MAX86141ENP+ satellite PPG AFE (J10 tail, SFH7050A).
# Legacy net labels CS_MAX86178 / MAX86178_INT refer to this part.
idf_component_register(SRCS "src/vq_drv_max86141.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES vq vq_spi vq_led driver esp_timer)
