# vq_features — windowed engineering-proxy feature extractor.
# Implements vq/include/features.h: pulls channel windows out of the
# pipeline rings into one static PSRAM scratch buffer and computes the
# VQ_F_* vector. No heap per call; every output is a rate / variability /
# perfusion / quality INDEX — never a clinical measurement.
idf_component_register(SRCS "features.c"
                       REQUIRES vq vq_pipeline esp_timer)
