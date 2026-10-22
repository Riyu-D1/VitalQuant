# vq_expander — TCA6408 8-bit I2C GPIO expander driver.
# Implements components/vq/include/expander.h (shared header, do not edit).
# Owns all power-domain switches (AFE/ADS resets, TX5_EN, IR_GATE, CHG_DIS)
# plus CHG_STAT/VBUS_DET input tracking and the EXP_INT (IO3) edge path.
idf_component_register(SRCS "vq_expander.c"
                       REQUIRES vq vq_i2c driver esp_timer)
