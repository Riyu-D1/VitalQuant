/* vq_drv_fsr_priv.h — component-private glue between vq_drv_fsr.c and
 * vq_status_led.c. NOT installed, NOT API: public surface is only
 * include/vq_drv_fsr.h (per the vq component rule — module-private
 * constants/declarations never touch components/vq/include). */
#pragma once

/* Sleep-path hook called by the FSR sensor's sleep() op (the only
 * component function the power manager's sleep walk reaches): stops
 * any blink and leaves IO17 driven LOW. Actual gpio_hold_en for
 * deep-sleep retention is power_mgr's job (§4.4 checklist). */
void vq_status_led_park(void);
