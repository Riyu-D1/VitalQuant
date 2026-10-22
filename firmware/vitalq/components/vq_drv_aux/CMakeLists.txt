# vq_drv_aux — always-on auxiliary I2C devices on the fix_v22 bus:
#   vq_rv3028.c    RV-3028-C7 RTC @0x52 — wall-clock anchor (0 channels)
#   vq_max17048.c  MAX17048G+ fuel gauge @0x36 — batt_v/batt_soc @0.2 Hz
# Contract headers come from `vq`; bus access is mutexed inside `vq_i2c`.
# `driver` provides the GAUGE_ALRT (IO6) interrupt; `esp_timer` provides
# the monotonic acquisition stamps required by the vq_types.h contract.
idf_component_register(SRCS "vq_drv_aux.c" "vq_rv3028.c" "vq_max17048.c"
                       INCLUDE_DIRS "include"
                       REQUIRES vq vq_i2c driver esp_timer)
