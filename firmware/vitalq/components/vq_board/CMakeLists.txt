# vq_board — fix_v22 earliest bring-up, implementing board_init.h from
# the vq (interface-only) component. Strap read-back -> CS high / safe
# idle -> [buses and expander handled later by vq_i2c/vq_spi/vq_expander].
#
# The spec-pinned REQUIRES line is kept verbatim; PRIV_REQUIRES is added
# because on ESP-IDF 5.4 the headers this file's own sources include are
# NOT in the public dependency closure of `vq esp_timer esp_hw_support
# soc`: esp_hw_support keeps efuse/spi_flash PRIVATE, and driver/gpio.h
# (pulled in transitively by board_pins.h) lives in esp_driver_gpio.
idf_component_register(SRCS "board_init.c"
                       INCLUDE_DIRS "include"
                       REQUIRES vq esp_timer esp_hw_support soc
                       PRIV_REQUIRES esp_driver_gpio spi_flash esp_psram efuse)
