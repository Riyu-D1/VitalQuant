# vq_logstore — log_store.h implementation: CRC-protected append-only
# record ring on the external W25Q512JV (64 MB, CS=IO16, <=40 MHz, mode 0).
#
# Layout inside the component:
#   w25q512.c/.h  private chip driver — attach on VQ_SPI_DEV_FLASH, JEDEC
#                 EF 40 20 probe, 4B opcodes for all addressing, busy-poll,
#                 power-down / release for deep-sleep.
#   log_store.c   the ring: A/B journaled superblock (sectors 0+1), data
#                 sectors 2..16382, scratch sector 16383 for selftest.
#
# REQUIRES per spec: vq (contract headers), vq_spi (shared SPI2 bus),
# driver (gpio_num_t via board_pins.h), esp_timer (record t_us stamps).
# freertos/esp_log/heap/esp_rom are common components — no listing needed.
idf_component_register(SRCS "log_store.c" "w25q512.c"
                       REQUIRES vq vq_spi driver esp_timer)
