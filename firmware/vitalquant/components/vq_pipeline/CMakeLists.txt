# vq_pipeline — sensor orchestration: owns the vq_sensor.h registry +
# vq_channel_name() and implements pipeline.h (hub fan-out, PSRAM ring
# windows, log_store batching, health monitor). Driver register functions
# are called directly — the vq_drv_* components are hard dependencies.
idf_component_register(
    SRCS "src/vq_pipeline.c"
         "src/vq_registry.c"
    REQUIRES vq
             vq_i2c vq_clock vq_logstore
             vq_drv_ads1292 vq_drv_afe4900 vq_drv_ad5940 vq_drv_max86141
             vq_drv_env vq_drv_aux vq_drv_island vq_drv_imu
             vq_drv_mic vq_drv_fsr
             esp_timer freertos)
