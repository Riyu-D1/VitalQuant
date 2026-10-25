/* bioz_feed.h — shared synthetic physiology feed for the vboard
 * bio-signal device models.
 *
 * One virtual "subject" drives every front-end so channels stay
 * physiologically consistent across chips:
 *
 *   vbbio_ecg()   lead-II surface ECG, µV            -> ads1292 ch1
 *   vbbio_resp()  respiration demod output, µV-equiv -> ads1292 ch2
 *   vbbio_ppg()   optical pulse waveform, 0..~1 norm -> afe4900 PD slots
 *
 * The heart-rate source is shared: the PPG pulse on the AFE4900 and the
 * R-R interval on the ADS1292 land on the same beat, which is what the
 * real hardware would produce.
 *
 * Scenario hooks: heart/resp rates, channel amplitudes, electrode
 * lead-off.  vbbio_reset() restores the defaults below.
 *
 * IMPLEMENTATION lives in ads1292.c (single definition point; afe4900.c
 * and scenario tests link against it).
 */
#ifndef BIOZ_FEED_H
#define BIOZ_FEED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default subject profile. */
#define VBBIO_DEF_HEART_BPM     72.0f
#define VBBIO_DEF_RESP_BRPM     15.0f
#define VBBIO_DEF_ECG_AMP_UV    1000.0f   /* ~1 mV lead-II R peak       */
#define VBBIO_DEF_RESP_AMP_UV   300.0f    /* demod output, µV-equiv     */
#define VBBIO_DEF_PPG_SCALE     1.0f

float vbbio_ecg (int64_t t_us);   /* lead-II ECG, µV                    */
float vbbio_resp(int64_t t_us);   /* respiration demod, µV-equiv        */
float vbbio_ppg (int64_t t_us);   /* PPG pulse, ~0..1 (scaled)          */

/* Scenario setters — call before/while streaming. */
void  vbbio_set_heart_rate(float bpm);        /* beats/min              */
void  vbbio_set_resp_rate (float brpm);       /* breaths/min            */
void  vbbio_set_ecg_amp_uv(float uv);         /* R-peak amplitude       */
void  vbbio_set_resp_amp_uv(float uv);        /* resp demod amplitude   */
void  vbbio_set_ppg_scale (float s);          /* 0..1 perfusion scale   */
void  vbbio_set_leadoff   (bool off);         /* electrodes lifted      */
bool  vbbio_get_leadoff   (void);
void  vbbio_reset         (void);             /* all defaults           */

#ifdef __cplusplus
}
#endif

#endif /* BIOZ_FEED_H */
