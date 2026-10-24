# vq_drv_afe4900 — AFE4900YZR (U6) optical PPG AFE driver, fix_v22 board.
# Contract headers live in components/vq/include — do NOT modify them;
# all AFE4900-private constants stay in src/afe4900_regs.h.
idf_component_register(SRCS "src/vq_drv_afe4900.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES vq vq_spi vq_expander vq_led driver esp_timer)
