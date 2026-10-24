# vq_cloud — HTTPS uplink: batch ingest, escalation advisories, heartbeat.
# Implements components/vq/include/cloud_client.h. All network I/O runs on
# the dedicated "vq_net" task so callers never block on Wi-Fi/TLS.
idf_component_register(
    SRCS "cloud_client.c"
    INCLUDE_DIRS "."
    # vq_pipeline for vq_channel_name, vq_features for vq_feature_name.
    # NOTE: vq_decision is deliberately NOT listed — it already REQUIRES
    # vq_cloud, so naming it here would create a component cycle. Its
    # contract (decision.h) arrives via the `vq` interface component, and
    # vq_decision_on_cloud()/vq_decision_last() resolve at link time
    # because all component archives are linked inside --start-group.
    REQUIRES vq vq_net vq_power vq_clock vq_logstore vq_pipeline
             vq_features
             esp_http_client esp_timer nvs_flash mbedtls json)
