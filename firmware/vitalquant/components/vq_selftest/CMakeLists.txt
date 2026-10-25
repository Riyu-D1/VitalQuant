# vq_selftest — hw brief §9 bring-up suite (selftest.h impl).
# Driver hooks are resolved weakly at link time; absent hooks degrade to
# SKIP notes, so the suite links/runs before all vq_drv_* components land.
idf_component_register(SRCS "selftest.c" "st_core.c" "st_sys.c"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES vq vq_board vq_i2c vq_spi vq_expander
                                vq_clock vq_logstore vq_power vq_led
                                vq_edge_model vq_pipeline vq_decision
                                vq_net vq_drv_aux
                                esp_timer driver esp_hw_support)
