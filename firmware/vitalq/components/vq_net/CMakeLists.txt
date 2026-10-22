# vq_net — wifi_mgr.h implementation: STA Wi-Fi (power-gated bursts),
# LAN status httpd (/status, /healthz) + mDNS "vitalq" announcer.
#
# NOTE: `mdns` is an IDF Component Registry component (espressif/mdns) in
# IDF 5.x — no longer in the IDF tree. It is declared in idf_component.yml;
# the component manager fetches it on the first `idf.py reconfigure`
# (needs registry/network access once, then cached under managed_components).
idf_component_register(SRCS "wifi_mgr.c"
                       REQUIRES vq vq_power esp_wifi esp_netif esp_http_server
                                mdns nvs_flash esp_timer)
