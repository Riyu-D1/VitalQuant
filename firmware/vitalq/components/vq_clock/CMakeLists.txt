# vq_clock — clock_service.h implementation.
# Monotonic esp_timer µs ↔ wall anchor: SNTP (esp_netif_sntp, pool.ntp.org,
# 3600 s re-sync) and/or the RV-3028-C7 RTC @0x52 (hourly re-anchor via a
# periodic esp_timer; INT not wired → flags polled, not interrupted).
#
# NOTE: spec listed `REQUIRES vq vq_i2c esp_timer lwip`, but esp_netif_sntp.h
# (esp_netif_sntp_init/ESP_NETIF_SNTP_DEFAULT_CONFIG) lives in the esp_netif
# component — lwip alone only ships the raw esp_sntp.h API. esp_netif is added
# here so the mandated esp_netif_sntp_* API resolves at compile time.
idf_component_register(SRCS "vq_clock.c"
                       REQUIRES vq vq_i2c esp_timer esp_netif lwip)
