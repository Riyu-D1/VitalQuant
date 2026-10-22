idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES vq vq_board vq_i2c vq_spi vq_expander
                                vq_clock vq_power vq_led vq_logstore
                                vq_pipeline vq_features vq_decision
                                vq_edge_model vq_net vq_cloud
                                vq_selftest vq_console
                                esp_timer esp_wifi nvs_flash esp_hw_support)
