/* bio2_feed.h — scenario feed/stimulus API for the bio-AFE pair
 * (sim/vboard/devices/ad5940.c + sim/vboard/devices/max86141.c).
 *
 * Two kinds of entry points live here:
 *
 *   vbb2_*  — board-stimulus feeds a scenario drives and the models
 *             consume.  Defaults are "hardware plausible" so nothing
 *             needs to be set for the drivers' own self-tests to pass:
 *               RCAL            10000 ohm   (R97 etc., J1 B1/C1)
 *               EDA site |Z|    15000 ohm   (J6 kelvin, incl. nothing —
 *                                            the 51k series-R is added
 *                                            inside the model loop)
 *               BioZ site |Z|     250 ohm   (J7 4-wire)
 *               sweat SINC2     2400 counts (engineering electrode
 *                                            current, RESEARCH-ONLY)
 *               MAX86141 LED    200000 19-bit counts (per-LED scale)
 *               MAX86141 amb     50000 19-bit counts
 *             Setters exist for each feed so fault/characterisation
 *             scenarios can move the numbers.
 *
 *   vbad5940_* — cross-model coupling getters.  AD5940 GPIO2 (ball E1)
 *             is the NIR730 gate (Q4 → D12 730 nm LED cathode): not an
 *             ESP pin, so the only observability is through these
 *             getters.  The AS7341 island model may weak-link them
 *             (`extern __attribute__((weak))` + NULL check) to lift its
 *             NIR channel while the gate is asserted — the optical
 *             selftest's dark/lit comparison then sees real coupling.
 *
 * Registration: vboard_init() weak-calls vbdev_ad5940_register() /
 * vbdev_max86141_register(); declared here for completeness only.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- AD5940 feeds ---------------------------------------------------- */
float    vbb2_rcal_ohm(void);              /* RCAL network, ohm          */
void     vbb2_set_rcal_ohm(float ohm);
float    vbb2_eda_ohm(void);               /* J6 EDA site |Z|, ohm       */
void     vbb2_set_eda_ohm(float ohm);
float    vbb2_bioz_ohm(void);              /* J7 BioZ site |Z|, ohm      */
void     vbb2_set_bioz_ohm(float ohm);
int32_t  vbb2_sweat_counts(void);          /* J11 SINC2 code (18-bit)    */
void     vbb2_set_sweat_counts(int32_t counts);

/* ---- MAX86141 feeds -------------------------------------------------- */
uint32_t vbb2_ppg_sat(void);               /* LED-phase PD counts (19b)  */
void     vbb2_ppg_sat_set(uint32_t counts);
void     vbb2_ppg_set(uint32_t counts);    /* alias of ppg_sat_set       */
uint32_t vbb2_ppg_amb(void);               /* direct-ambient counts      */
void     vbb2_ppg_amb_set(uint32_t counts);

/* ---- AD5940 → NIR730 gate coupling (weak defs, ad5940.c) ------------- */
/* Both return the same logical level (1 = gate on / LED energised);
 * vbad5940_get_gpio2 is the raw-pin spelling for consumers that mirror
 * the ball name.  Defined WEAK so an island model may provide its own
 * stand-in when ad5940.c is not linked.                              */
int      vbad5940_nir_gate(void);
int      vbad5940_get_gpio2(void);

/* ---- vboard_init registration hooks (weak-called by core) ------------ */
void     vbdev_ad5940_register(void);
void     vbdev_max86141_register(void);

#ifdef __cplusplus
}
#endif
