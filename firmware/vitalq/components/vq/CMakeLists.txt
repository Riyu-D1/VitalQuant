# vq — interface-only component: every contract header for firmware v2.
# Implementation components REQUIRES vq and must not modify these headers;
# module-private constants go in each component's own private headers.
idf_component_register(INCLUDE_DIRS "include")
