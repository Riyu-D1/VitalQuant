# vq_drv_imu — LSM6DSV80X 6-axis IMU driver (U14, I2C0 @0x6A, INT1→IO4).
# fix_v22: 52.0 Hz via the HAODR engine (OP_MODE=HAODR + HAODR_SEL=3 —
# the plain ODR table has no 52 Hz), accel ±8 g low-g + gyro ±500 dps,
# device FIFO batching (WTM=52 tagged words ≈ 26 frames) with INT1 =
# FIFO-TH(+OVR). Contract headers come from `vq`; the mutexed bus is in
# `vq_i2c`; `driver` provides the INT1 GPIO+ISR; `esp_timer` is the
# monotonic acquisition anchor required by the vq_types.h FIFO contract.
idf_component_register(SRCS "vq_drv_imu.c"
                       INCLUDE_DIRS "include"
                       REQUIRES vq vq_i2c driver esp_timer)
