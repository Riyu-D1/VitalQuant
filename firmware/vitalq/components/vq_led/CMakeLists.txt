# vq_led — hard-interlock LED slot scheduler (implements include/led_sched.h).
# Public contract lives in the interface-only 'vq' component; this module
# exports no headers of its own — vq_led_priv.h stays private (quoted
# include resolves in the source dir without INCLUDE_DIRS).
idf_component_register(SRCS "vq_led.c"
                       REQUIRES vq vq_expander vq_power esp_timer driver)
