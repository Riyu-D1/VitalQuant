# vq_drv_ads1292 — ADS1292R 2-ch AFE driver (ecg.ii + resp.waveform @ 500 sps)
# fix_v22: SPI mode 1, CS=IO10, ~DRDY=IO2, PWDN via TCA6408 P1, START=GND,
# internal 512 kHz oscillator. See vq_drv_ads1292.c for the clocking strategy.
idf_component_register(SRCS "vq_drv_ads1292.c"
                       INCLUDE_DIRS "include"
                       REQUIRES vq vq_spi vq_expander driver esp_timer)
