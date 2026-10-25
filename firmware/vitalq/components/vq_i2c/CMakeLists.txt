# vq_i2c — I2C0 master implementing the contract in
# components/vq/include/i2c_bus.h, on the NEW esp_driver_i2c master API
# (driver/i2c_master.h). The 100 kHz hard cap and the single-bus-mutex
# rule live in i2c_bus.c — see its header for the 100 kHz budget rule.
idf_component_register(SRCS "i2c_bus.c"
                       INCLUDE_DIRS "include"
                       REQUIRES vq driver esp_timer)
