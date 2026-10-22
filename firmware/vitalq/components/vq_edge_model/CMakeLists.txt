# vq_edge_model — on-device INT8 inference runtime.
# Implements components/vq/include/vq_model.h (shared header, do not edit):
# VQMB blob loader (model-partition mmap + compiled-in stub fallback),
# the static activation arena in octal PSRAM, and the graph executor over
# the fixed vq_op_t op set. Kernels live in vq_kernels.c — plain C99, no
# esp-dsp dependency (see vq_kernels.c header for the upgrade note).
idf_component_register(SRCS "vq_model.c" "vq_kernels.c"
                       REQUIRES vq esp_timer esp_partition)
