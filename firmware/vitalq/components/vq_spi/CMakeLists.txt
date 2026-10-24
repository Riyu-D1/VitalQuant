# vq_spi — SPI2 (FSPI) master implementing the contract in
# components/vq/include/spi_bus.h on driver/spi_master.h.
# CS is driven MANUALLY (spics_io_num = -1): one global mutex plus the
# s_cs_owner token enforce the shared-MISO "one CS low at a time" rule.
idf_component_register(SRCS "spi_bus.c"
                       REQUIRES vq driver esp_timer)
