# vq_decision — the few-minutes local-model -> cloud-escalation loop.
# Implements the frozen contract in vq/include/decision.h; private
# constants + wire formats live in src/vq_decision_priv.h, optional
# console/selftest hooks in include/vq_decision_ext.h.
idf_component_register(
    SRCS "src/vq_decision.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES vq vq_edge_model vq_features vq_pipeline vq_cloud vq_power
             vq_logstore esp_timer
)
