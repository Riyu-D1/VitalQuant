/* features.c — vq_features: windowed engineering-proxy feature extractor.
 *
 * Implements components/vq/include/features.h. Every output is an
 * engineering proxy (rate / variability / perfusion / quality indices
 * and unit-bearing deltas); nothing here is or claims to be a clinical
 * measurement. Feature slots that cannot be filled stay NaN with their
 * mask bit clear — the model's input mask handles absent channels.
 *
 * Cost contract (bring-up target < 50 ms on S3 @ 240 MHz):
 *   - no dynamic allocation: one static sample scratch plus small fixed
 *     side buffers. The large arrays are pinned to the .ext_ram.bss
 *     section explicitly — do NOT rely on
 *     CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY being set.
 *   - every extractor is a fixed O(n) or O(lags·n) pass; the only
 *     super-linear step is the PAT cross-correlation, bounded to
 *     ±400 ms on a decimated 250 Hz grid.
 *   - deterministic: fixed loop orders, double accumulators, no RNG,
 *     no data-dependent early exits beyond documented thresholds.
 *
 * NOT re-entrant (single shared scratch) — call from the decision task
 * only. Extractors are otherwise stateless; the sole persistent state
 * is s_last_motion_us, which the STILL_S feature semantics require.
 */
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#include "esp_timer.h"

#include "vq_types.h"
#include "pipeline.h"
#include "features.h"

/* =====================================================================
 * Tunables — all feature math constants live here, in one visible block.
 * ===================================================================== */

/* Scratch sizing: the fastest ring is the ADS1292R at 500 sps and the
 * pipeline keeps at most VQ_WIN_ECG_S (60 s) of it — 512 sps × 60 s +
 * margin covers every channel vq_pipeline_window() can return for any
 * window_s. (~750 KB of PSRAM; see cost contract in the header.) */
#define VQ_SCRATCH_N        (512 * 60 + 512)

/* Minimum samples before a waveform feature is attempted at all.
 * Below this a channel is treated as "no usable data" (mask stays 0). */
#define VQ_MIN_WAVE_SAMPLES 32

/* Peak detection — bring-up Schmitt detector.
 * Arm below the window mean, trigger on a local maximum above
 * mean + k·σ, refractory guards double-fires. Deliberately simple and
 * deterministic; the upgrade path is an adaptive band-pass + matched-
 * filter (Pan-Tompkins-style) detector with per-beat quality gating
 * once labelled data exists. */
#define VQ_PEAK_MAX          320        /* ≥300 bpm over 60 s, absurd   */
#define VQ_PEAK_K_PPG        0.35f      /* trigger = mean + k·σ         */
#define VQ_PEAK_K_ECG        0.60f      /* R spike dominates → higher k */
#define VQ_REFRACT_PPG_US    300000     /* ~200 bpm proxy ceiling       */
#define VQ_REFRACT_ECG_US    240000     /* ~250 bpm                     */

/* IBI plausibility band for the variability proxy. */
#define VQ_IBI_MIN_US        300000
#define VQ_IBI_MAX_US       2000000
#define VQ_HRV_MIN_DIFFS           2   /* need ≥2 successive IBI diffs  */

/* Respiration: cycle counter = upward crossings of mean+h·σ after a dip
 * below mean−h·σ (hysteresis kills noise chatter at the crossing). */
#define VQ_RESP_HYST           0.25f
#define VQ_RESP_MIN_CYCLES         2

/* PAT: both waveforms are re-gridded at VQ_PAT_FS Hz on a common time
 * origin, then normalised cross-correlation over ±VQ_PAT_LAG_US picks
 * the lag. 4 ms resolution is proxy-grade by design. */
#define VQ_PAT_FS              250
#define VQ_PAT_LAG_US       400000
#define VQ_PAT_MAX_S            64
#define VQ_PAT_MAX_N      (VQ_PAT_FS * VQ_PAT_MAX_S)
#define VQ_PAT_MIN_PAIRS        64    /* min overlapping valid samples */
#define VQ_PAT_MIN_R          0.05f   /* below → no coherent delay     */

/* SQI composite — see sqi_index() for the exact documented formula. */
#define VQ_SQI_CLIP_FRACTION   0.02f  /* ≥2 % of samples on a rail → 0 */

/* Motion: the accel window is folded into VQ_MOT_BINS equal bins
 * (≥ 1 s); per-bin |a| rms > VQ_MOT_ACTIVE_G counts as motion.
 * |a| rms ≈ 1 g at rest (static gravity), so the active threshold is
 * 1 g + margin — orientation changes alone move the vector only a few
 * hundredths of a g. */
#define VQ_MOT_BINS             64
#define VQ_MOT_ACTIVE_G      1.12f

/* =====================================================================
 * Static scratch + persistent state
 * ===================================================================== */

/* Place a large array in external-ram .bss (PSRAM). Same section the
 * IDF EXT_RAM_BSS_ATTR macro expands to, spelled out so we need no heap
 * component dependency; zero-initialised like normal .bss. */
#define VQ_PSRAM_BSS __attribute__((section(".ext_ram.bss")))

static VQ_PSRAM_BSS vq_sample_t s_win[VQ_SCRATCH_N];  /* window scratch */
static VQ_PSRAM_BSS float s_grid_a[VQ_PAT_MAX_N];   /* decimated ECG    */
static VQ_PSRAM_BSS float s_grid_b[VQ_PAT_MAX_N];   /* decimated PPG    */

static int32_t     s_peaks[VQ_PEAK_MAX];    /* detected peak indices    */

static float       s_mot_ms2[VQ_MOT_BINS];  /* Σ_axes per-bin mean sq   */
static double      s_bin2[VQ_MOT_BINS];     /* per-axis bin sum-squares */
static uint32_t    s_binc[VQ_MOT_BINS];     /* per-axis bin counts      */

/* STILL_S needs history beyond a single call: stamps persist, only ever
 * moving forward. 0 = never armed (handled on first use). */
static int64_t     s_last_motion_us;

/* =====================================================================
 * Small helpers
 * ===================================================================== */

static inline void set_f(vq_feature_vec_t *o, int idx, float v)
{
    o->v[idx] = v;
    o->mask  |= (1ULL << idx);
}

static inline float clamp01f(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

/* Pull last window_us of ch into the shared scratch; returns count. */
static size_t pull(vq_channel_id_t ch, int64_t window_us)
{
    return vq_pipeline_window(ch, window_us, s_win, VQ_SCRATCH_N);
}

/* Window mean + population std of .value (double accumulators). */
static void mean_std(const vq_sample_t *s, size_t n, float *mean, float *sd)
{
    double m = 0.0, q = 0.0;
    for (size_t i = 0; i < n; i++) m += s[i].value;
    m /= (double)n;
    for (size_t i = 0; i < n; i++) {
        double d = s[i].value - m;
        q += d * d;
    }
    *mean = (float)m;
    *sd   = (float)sqrt(q / (double)n);
}

/* Most recent sample by t_us (robust to unordered rings). */
static bool latest_val(const vq_sample_t *s, size_t n, float *v)
{
    if (n == 0) return false;
    size_t b = 0;
    for (size_t i = 1; i < n; i++)
        if (s[i].t_us >= s[b].t_us) b = i;
    *v = s[b].value;
    return true;
}

/* =====================================================================
 * Waveform extractors
 * ===================================================================== */

/* Schmitt peak detector (see tunables). Fills out_idx with sample
 * indices of detected peaks; returns peak count (≤ max_out).
 * Deterministic O(n). */
static size_t detect_peaks(const vq_sample_t *s, size_t n, float k,
                           int64_t refractory_us,
                           int32_t *out_idx, size_t max_out)
{
    if (n < VQ_MIN_WAVE_SAMPLES) return 0;
    float mean, sd;
    mean_std(s, n, &mean, &sd);
    if (sd <= 0.0f) return 0;

    const float thr_hi = mean + k * sd;
    const float thr_lo = mean;              /* re-arm level            */

    size_t  npk = 0;
    bool    armed = false;
    int64_t open_from = INT64_MIN;
    size_t  i = 0;
    while (i < n) {
        if (!armed) {
            if (s[i].value <= thr_lo && s[i].t_us > open_from) armed = true;
            i++;
            continue;
        }
        if (s[i].value <= thr_hi) { i++; continue; }
        /* Triggered: local max while the trace holds above thr_lo. */
        size_t pk = i++;
        while (i < n && s[i].value > thr_lo) {
            if (s[i].value >= s[pk].value) pk = i;
            i++;
        }
        if (npk < max_out) out_idx[npk] = (int32_t)pk;
        npk++;
        open_from = s[pk].t_us + refractory_us;
        armed = false;
        while (i < n && s[i].t_us <= open_from) i++;
    }
    return npk > max_out ? max_out : npk;
}

/* Peak-count rate normalised to the observed inter-peak span:
 * N peaks hold N−1 full intervals, so rate = 60·(N−1)/(t_N−t_1).
 * NaN when fewer than 2 peaks or a degenerate stamp. */
static float rate_per_min(const vq_sample_t *s, const int32_t *pk, size_t npk)
{
    if (npk < 2) return NAN;
    double span = (double)(s[pk[npk - 1]].t_us - s[pk[0]].t_us);
    if (span <= 0.0) return NAN;
    return (float)(60.0e6 * (double)(npk - 1) / span);
}

/* RMSSD over successive IBIs, in ms. IBIs outside the plausibility band
 * reset the "successive" pairing so dropouts do not inject false diffs. */
static float rmssd_ms(const vq_sample_t *s, const int32_t *pk, size_t npk)
{
    if (npk < 3) return NAN;
    double acc = 0.0;
    size_t m = 0;
    int64_t prev = -1;
    for (size_t i = 1; i < npk; i++) {
        int64_t ibi = s[pk[i]].t_us - s[pk[i - 1]].t_us;
        if (ibi < VQ_IBI_MIN_US || ibi > VQ_IBI_MAX_US) { prev = -1; continue; }
        if (prev >= 0) {
            double d = (double)(ibi - prev);
            acc += d * d;
            m++;
        }
        prev = ibi;
    }
    if (m < VQ_HRV_MIN_DIFFS) return NAN;
    return (float)(sqrt(acc / (double)m) / 1000.0);     /* µs → ms */
}

/* Respiration proxy: hysteresis-guarded cycle counter on the demeaned
 * waveform → cycles/min over the first→last cycle span. */
static float resp_rate_per_min(const vq_sample_t *s, size_t n)
{
    if (n < VQ_MIN_WAVE_SAMPLES) return NAN;
    float mean, sd;
    mean_std(s, n, &mean, &sd);
    if (sd <= 0.0f) return NAN;
    const float up = mean + VQ_RESP_HYST * sd;
    const float dn = mean - VQ_RESP_HYST * sd;

    size_t  cyc = 0;
    bool    armed = false;
    int64_t t_first = 0, t_last = 0;
    for (size_t i = 0; i < n; i++) {
        float v = s[i].value;
        if (!armed && v <= dn) {
            armed = true;
        } else if (armed && v >= up) {
            if (cyc == 0) t_first = s[i].t_us;
            t_last = s[i].t_us;
            cyc++;
            armed = false;
        }
    }
    if (cyc < VQ_RESP_MIN_CYCLES) return NAN;
    double span = (double)(t_last - t_first);
    if (span <= 0.0) return NAN;
    return (float)(60.0e6 * (double)(cyc - 1) / span);
}

/* Perfusion-index style ratio: AC rms / DC mean × 100, where AC rms is
 * the window population std and DC is the window mean. Engineering
 * signal-strength proxy only. NaN on non-positive DC. */
static float perfusion_idx(const vq_sample_t *s, size_t n,
                           float *mean_out, float *sd_out)
{
    if (n < VQ_MIN_WAVE_SAMPLES) return NAN;
    float mean, sd;
    mean_std(s, n, &mean, &sd);
    if (mean_out) *mean_out = mean;
    if (sd_out)   *sd_out   = sd;
    if (mean <= 0.0f) return NAN;
    return 100.0f * sd / mean;
}

/* Normalised autocorrelation at a fixed sample lag, clamped to [0,1].
 * Used as the periodicity term of the SQI composite. */
static float autocorr_at_lag(const vq_sample_t *s, size_t n, size_t lag,
                             float mean)
{
    if (lag == 0 || lag >= n / 2) return 0.0f;
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i + lag < n; i++) {
        double a = s[i].value - mean;
        num += a * (s[i + lag].value - mean);
        den += a * a;
    }
    if (den <= 0.0) return 0.0f;
    return clamp01f((float)(num / den));
}

/* Composite PPG signal-quality index ∈ [0,1] — deterministic heuristic:
 *
 *   sqi = clip · badfrac · (0.6·periodicity + 0.4·snr)
 *
 *   periodicity : normalised autocorrelation at the mean detected IBI
 *                 (1 = perfectly periodic pulse train; 0 when no beats
 *                 were detected at all)
 *   snr         : 1 − clip01(resid_rms / ac_rms) where the residual is
 *                 sample-vs-5-tap-moving-average — a smooth pulsatile
 *                 trace has low residual relative to its own amplitude
 *   clip        : 1 − clip01(frac_at_rails / VQ_SQI_CLIP_FRACTION), with
 *                 "rail" = value equal to the observed window min/max
 *                 (ADC saturation plateaus and flat-lines)
 *   badfrac     : 1 − fraction of VQ_SAMPLE_F_QUALITY_BAD samples       */
static float sqi_index(const vq_sample_t *s, size_t n,
                       const int32_t *pk, size_t npk, float mean, float sd)
{
    if (n < VQ_MIN_WAVE_SAMPLES || sd <= 0.0f) return NAN;

    float periodicity = 0.0f;
    if (npk >= 3) {
        double dt  = (double)(s[n - 1].t_us - s[0].t_us) / (double)(n - 1);
        double ibi = (double)(s[pk[npk - 1]].t_us - s[pk[0]].t_us)
                     / (double)(npk - 1);
        if (dt > 0.0)
            periodicity = autocorr_at_lag(s, n, (size_t)(ibi / dt + 0.5), mean);
    }

    /* 5-tap moving-average residual (edge taps use the clipped span). */
    double racc = 0.0;
    for (size_t i = 0; i < n; i++) {
        size_t lo = (i >= 2) ? i - 2 : 0;
        size_t hi = (i + 2 < n) ? i + 2 : n - 1;
        double m = 0.0;
        for (size_t k = lo; k <= hi; k++) m += s[k].value;
        m /= (double)(hi - lo + 1);
        double d = s[i].value - m;
        racc += d * d;
    }
    float snr = clamp01f(1.0f - (float)sqrt(racc / (double)n) / sd);

    float vmin = s[0].value, vmax = s[0].value;
    for (size_t i = 1; i < n; i++) {
        if (s[i].value < vmin) vmin = s[i].value;
        if (s[i].value > vmax) vmax = s[i].value;
    }
    size_t rails = 0, badf = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i].value == vmin || s[i].value == vmax) rails++;
        if (s[i].flags & VQ_SAMPLE_F_QUALITY_BAD) badf++;
    }
    float clip = clamp01f(1.0f - ((float)rails / (float)n) / VQ_SQI_CLIP_FRACTION);
    float good = 1.0f - (float)badf / (float)n;

    return clip * good * (0.6f * periodicity + 0.4f * snr);
}

/* Re-grid a stamped window onto a uniform fs-Hz lattice anchored at a
 * shared absolute origin t0_us, by nearest-sample within ±dt/2. Slots
 * with no nearby sample stay NAN, so the cross-correlator can skip
 * gaps and channel-overlap edges. Assumes t_us ascending. O(n + gn). */
static void resample_grid(const vq_sample_t *s, size_t n, int64_t t0_us,
                          float fs, float *g, size_t gn)
{
    const int64_t dt_us = (int64_t)(1.0e6 / (double)fs + 0.5);
    const int64_t half  = dt_us / 2;
    size_t j = 0;
    for (size_t i = 0; i < gn; i++) {
        int64_t tt = t0_us + (int64_t)i * dt_us;
        while (j < n && s[j].t_us < tt - half) j++;
        g[i] = (j < n && s[j].t_us <= tt + half) ? s[j].value : NAN;
    }
}

/* PAT proxy: argmax of the normalised cross-correlation of the two
 * re-gridded, demeaned waveforms over ±400 ms. Positive lag = optical
 * pulse edge arriving after the electrical R edge — the physiologic
 * direction. NaN when the overlap or the peak correlation is too weak.
 * This is a waveform-alignment index, not a pulse-transit measurement. */
static float pat_from_grids(size_t gn)
{
    double ma = 0.0, mb = 0.0;
    size_t ca = 0, cb = 0;
    for (size_t i = 0; i < gn; i++) {
        if (!isnan(s_grid_a[i])) { ma += s_grid_a[i]; ca++; }
        if (!isnan(s_grid_b[i])) { mb += s_grid_b[i]; cb++; }
    }
    if (ca < VQ_PAT_MIN_PAIRS || cb < VQ_PAT_MIN_PAIRS) return NAN;
    ma /= (double)ca; mb /= (double)cb;
    for (size_t i = 0; i < gn; i++) {
        if (!isnan(s_grid_a[i])) s_grid_a[i] -= (float)ma;
        if (!isnan(s_grid_b[i])) s_grid_b[i] -= (float)mb;
    }

    const int64_t lag_max = (VQ_PAT_LAG_US / 1000) * VQ_PAT_FS / 1000;
    float   best_r = -1.0f;
    int64_t best_lag = 0;
    for (int64_t lag = -lag_max; lag <= lag_max; lag++) {
        double sab = 0.0, saa = 0.0, sbb = 0.0;
        size_t c = 0;
        for (size_t i = 0; i < gn; i++) {
            int64_t j = (int64_t)i + lag;
            if (j < 0 || j >= (int64_t)gn) continue;
            float x = s_grid_a[i], y = s_grid_b[j];
            if (isnan(x) || isnan(y)) continue;
            sab += (double)x * y;
            saa += (double)x * x;
            sbb += (double)y * y;
            c++;
        }
        if (c < VQ_PAT_MIN_PAIRS) continue;
        double d = saa * sbb;
        if (d <= 0.0) continue;
        float r = (float)(sab / sqrt(d));
        if (r > best_r) { best_r = r; best_lag = lag; }
    }
    if (best_r < VQ_PAT_MIN_R) return NAN;
    return (float)best_lag * (1000.0f / (float)VQ_PAT_FS);
}

/* Ordinary-least-squares slope of value-vs-time, units per second. */
static float slope_per_s(const vq_sample_t *s, size_t n)
{
    if (n < 3) return NAN;
    const double t0 = (double)s[0].t_us;
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (size_t i = 0; i < n; i++) {
        double x = ((double)s[i].t_us - t0) * 1.0e-6;
        double y = s[i].value;
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double d = (double)n * sxx - sx * sx;
    if (d == 0.0) return NAN;
    return (float)(((double)n * sxy - sx * sy) / d);
}

/* =====================================================================
 * Public API
 * ===================================================================== */

/* PLACEHOLDER z-score table — identity normalisation (mean row = 0,
 * std row = 1) until the trained model blob ships its real per-feature
 * statistics. The model owns these numbers; keep this single table the
 * ONLY place they are defined and swap both rows in together. */
const float vq_feature_scale[2][VQ_FEATURE_DIM] = {
    { [0 ... VQ_FEATURE_DIM - 1] = 0.0f },    /* row 0: means            */
    { [0 ... VQ_FEATURE_DIM - 1] = 1.0f },    /* row 1: stds             */
};

/* Stable names for logging / the status endpoint / bundle headers.
 * Wording stays engineering-grade; index-aligned with vq_feature_idx_t. */
static const char *const s_names[VQ_F_COUNT] = {
    [VQ_F_HR_BPM]                = "hr_bpm_ppg_rate",
    [VQ_F_HRV_RMSSD]             = "hrv_rmssd_ms",
    [VQ_F_HR_ECG]                = "hr_ecg_rate",
    [VQ_F_RESP_RATE]             = "resp_rate_proxy",
    [VQ_F_PAT_MS]                = "pat_ms_proxy",
    [VQ_F_PPG_PI_RED]            = "ppg_pi_red_pct",
    [VQ_F_PPG_PI_IR]             = "ppg_pi_ir_pct",
    [VQ_F_PPG_SQI]               = "ppg_sqi_0_1",
    [VQ_F_SPO2_PROXY]            = "spo2_loglog_ratio_proxy",
    [VQ_F_TEMP_SKIN]             = "temp_skin_c",
    [VQ_F_TEMP_DOME_MINUS_SKIN]  = "temp_dome_minus_skin_c",
    [VQ_F_TEMP_DISTAL_MINUS_SKIN]= "temp_distal_minus_skin_c",
    [VQ_F_TEMP_OBJ_MINUS_SKIN]   = "temp_obj_minus_skin_c",
    [VQ_F_SKIN_RH]               = "skin_rh_pct",
    [VQ_F_EDA_LEVEL]             = "eda_level",
    [VQ_F_EDA_SLOPE]             = "eda_slope_per_s",
    [VQ_F_BIOZ_MAG]              = "bioz_mag",
    [VQ_F_MOT_RMS]               = "mot_rms_g",
    [VQ_F_STILL_S]               = "still_s",
    [VQ_F_SPEC_STO2_IDX]         = "spec_sto2_idx_proxy",
    [VQ_F_CONTACT]               = "contact_level",
    [VQ_F_BATT_SOC]              = "batt_soc_pct",
    [VQ_F_TEMP_BOARD]            = "temp_board_c",
};

const char *vq_feature_name(int idx)
{
    if (idx < 0) return NULL;
    if (idx < VQ_F_COUNT) return s_names[idx];
    if (idx < VQ_FEATURE_DIM) return "reserved";
    return NULL;
}

void vq_features_normalise(vq_feature_vec_t *f)
{
    if (!f) return;
    for (int i = 0; i < VQ_FEATURE_DIM; i++) {
        if (!(f->mask & (1ULL << i))) continue;
        float sd = vq_feature_scale[1][i];
        if (sd == 0.0f) continue;              /* degenerate std → skip */
        f->v[i] = (f->v[i] - vq_feature_scale[0][i]) / sd;
    }
}

vq_err_t vq_features_compute(vq_feature_vec_t *out, uint32_t window_s)
{
    if (!out || window_s == 0) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < VQ_FEATURE_DIM; i++) out->v[i] = NAN;
    out->mask     = 0;
    out->window_s = window_s;

    const int64_t now      = esp_timer_get_time();   /* window end stamp */
    const int64_t wus      = (int64_t)window_s * 1000000;
    const int64_t t_origin = now - wus;              /* common grid orgn */
    out->t_us = now;

    size_t n;
    float  v, v2;

    /* ---- ECG lead-II: rate proxy + decimated grid for PAT ---------- */
    bool have_ecg_grid = false;
    n = pull(VQ_CH_ECG_II, wus);
    if (n >= VQ_MIN_WAVE_SAMPLES) {
        size_t npk = detect_peaks(s_win, n, VQ_PEAK_K_ECG,
                                  VQ_REFRACT_ECG_US, s_peaks, VQ_PEAK_MAX);
        float r = rate_per_min(s_win, s_peaks, npk);
        if (!isnan(r)) set_f(out, VQ_F_HR_ECG, r);
        resample_grid(s_win, n, t_origin, VQ_PAT_FS, s_grid_a, VQ_PAT_MAX_N);
        have_ecg_grid = true;
    }

    /* ---- PPG IR: rate, variability, PI, SQI + PAT grid ------------- */
    float ir_mean = 0.0f, ir_sd = 0.0f;
    bool  have_ir = false;
    n = pull(VQ_CH_PPG_IR, wus);
    if (n >= VQ_MIN_WAVE_SAMPLES) {
        size_t npk = detect_peaks(s_win, n, VQ_PEAK_K_PPG,
                                  VQ_REFRACT_PPG_US, s_peaks, VQ_PEAK_MAX);
        float r = rate_per_min(s_win, s_peaks, npk);
        if (!isnan(r)) set_f(out, VQ_F_HR_BPM, r);
        float h = rmssd_ms(s_win, s_peaks, npk);
        if (!isnan(h)) set_f(out, VQ_F_HRV_RMSSD, h);

        float pi = perfusion_idx(s_win, n, &ir_mean, &ir_sd);
        if (!isnan(pi)) set_f(out, VQ_F_PPG_PI_IR, pi);
        float q = sqi_index(s_win, n, s_peaks, npk, ir_mean, ir_sd);
        if (!isnan(q)) set_f(out, VQ_F_PPG_SQI, q);

        resample_grid(s_win, n, t_origin, VQ_PAT_FS, s_grid_b, VQ_PAT_MAX_N);
        have_ir = true;
    }

    /* ---- PAT proxy: waveform-alignment lag, ECG edge → PPG foot ---- */
    if (have_ecg_grid && have_ir) {
        size_t gn = (size_t)(wus * (int64_t)VQ_PAT_FS / 1000000);
        if (gn > VQ_PAT_MAX_N) gn = VQ_PAT_MAX_N;
        float p = pat_from_grids(gn);
        if (!isnan(p)) set_f(out, VQ_F_PAT_MS, p);
    }

    /* ---- PPG RED: PI + log-log R-ratio proxy ------------------------ */
    n = pull(VQ_CH_PPG_RED, wus);
    if (n >= VQ_MIN_WAVE_SAMPLES) {
        float r_mean, r_sd;
        float pi = perfusion_idx(s_win, n, &r_mean, &r_sd);
        if (!isnan(pi)) set_f(out, VQ_F_PPG_PI_RED, pi);

        /* "SpO2 proxy" — strictly the dimensionless log-log R-ratio
         *   R = ln(ACr/DCr) / ln(ACi/DCi)
         * NOT a calibrated saturation estimate; kept only as a model
         * input feature. Both AC/DC ratios sit in (0,1) for pulsatile
         * data, so R is positive; degenerate ratios → NaN. */
        if (have_ir && r_mean > 0.0f && ir_mean > 0.0f) {
            double ac_r = (double)r_sd / r_mean;
            double ac_i = (double)ir_sd / ir_mean;
            if (ac_r > 0.0 && ac_r < 1.0 && ac_i > 0.0 && ac_i < 1.0) {
                double rr = log(ac_r) / log(ac_i);
                if (isfinite(rr)) set_f(out, VQ_F_SPO2_PROXY, (float)rr);
            }
        }
    }

    /* ---- Respiration waveform → cycles/min proxy -------------------- */
    n = pull(VQ_CH_RESP, wus);
    if (n >= VQ_MIN_WAVE_SAMPLES) {
        float r = resp_rate_per_min(s_win, n);
        if (!isnan(r)) set_f(out, VQ_F_RESP_RATE, r);
    }

    /* ---- Temperatures: latest values + trend deltas ----------------- */
    float skin_t = 0.0f;
    bool  have_skin = false;
    n = pull(VQ_CH_TEMP_SKIN, wus);
    if (latest_val(s_win, n, &v)) {
        skin_t = v; have_skin = true;
        set_f(out, VQ_F_TEMP_SKIN, v);
    }
    if (have_skin) {
        n = pull(VQ_CH_TEMP_DOME, wus);
        if (latest_val(s_win, n, &v))
            set_f(out, VQ_F_TEMP_DOME_MINUS_SKIN, v - skin_t);
        n = pull(VQ_CH_TEMP_DISTAL, wus);
        if (latest_val(s_win, n, &v))
            set_f(out, VQ_F_TEMP_DISTAL_MINUS_SKIN, v - skin_t);
        n = pull(VQ_CH_TEMP_OBJ, wus);
        if (latest_val(s_win, n, &v))
            set_f(out, VQ_F_TEMP_OBJ_MINUS_SKIN, v - skin_t);
    }

    /* ---- Skin RH ---------------------------------------------------- */
    n = pull(VQ_CH_SKIN_RH, wus);
    if (latest_val(s_win, n, &v)) set_f(out, VQ_F_SKIN_RH, v);

    /* ---- EDA: latest level + OLS slope ------------------------------ */
    n = pull(VQ_CH_EDA, wus);
    if (n > 0) {
        if (latest_val(s_win, n, &v)) set_f(out, VQ_F_EDA_LEVEL, v);
        float sl = slope_per_s(s_win, n);
        if (!isnan(sl)) set_f(out, VQ_F_EDA_SLOPE, sl);
    }

    /* ---- BioZ: latest magnitude ------------------------------------- */
    n = pull(VQ_CH_BIOZ, wus);
    if (latest_val(s_win, n, &v)) set_f(out, VQ_F_BIOZ_MAG, fabsf(v));

    /* ---- Motion: |a| rms over the window + seconds since motion -----
     * rms of the magnitude decomposes exactly into per-axis mean-of-
     * squares (mean of sum = sum of means), so axes need not be
     * time-aligned. Per-bin rms feeds the STILL_S edge tracker.        */
    {
        const int64_t bin_us =
            (wus / VQ_MOT_BINS > 1000000) ? wus / VQ_MOT_BINS : 1000000;
        memset(s_mot_ms2, 0, sizeof s_mot_ms2);
        size_t axes_ok = 0;
        static const vq_channel_id_t ax_ch[3] = {
            VQ_CH_ACC_X, VQ_CH_ACC_Y, VQ_CH_ACC_Z,
        };
        double mu_sum[3] = {0, 0, 0};
        double mu_cnt[3] = {0, 0, 0};
        for (int a = 0; a < 3; a++) {
            n = pull(ax_ch[a], wus);
            if (n == 0) continue;
            axes_ok++;
            memset(s_bin2, 0, sizeof s_bin2);
            memset(s_binc, 0, sizeof s_binc);
            for (size_t i = 0; i < n; i++) {
                int64_t b = (s_win[i].t_us - t_origin) / bin_us;
                if (b < 0) b = 0;
                if (b >= VQ_MOT_BINS) b = VQ_MOT_BINS - 1;
                double x = s_win[i].value;
                s_bin2[b] += x * x;
                s_binc[b]++;
                mu_sum[a] += x;
                mu_cnt[a]++;
            }
            for (int b = 0; b < VQ_MOT_BINS; b++)
                if (s_binc[b]) s_mot_ms2[b] += (float)(s_bin2[b] / s_binc[b]);
        }
        if (axes_ok == 3) {
            /* Remove gravity: E[a²] = Var[a] + |μ|², so per-bin dynamic
             * energy is ms2 − |μ_window|².  Without this a motionless
             * device reports |a|≈1 g forever and the gate always says
             * "moving" — the decision layer was gated off permanently. */
            double mu2 = 0.0;
            for (int a = 0; a < 3; a++) {
                double m = mu_cnt[a] ? mu_sum[a] / mu_cnt[a] : 0.0;
                mu2 += m * m;
            }
            double acc = 0.0;
            int bins_used = 0, last_active = -1;
            for (int b = 0; b < VQ_MOT_BINS; b++) {
                if (s_mot_ms2[b] <= 0.0f) continue;
                float ex = s_mot_ms2[b] - (float)mu2;   /* dynamic part */
                if (ex <= 0.0f) continue;
                bins_used++;
                acc += ex;
                if (sqrtf(ex) > VQ_MOT_ACTIVE_G) last_active = b;
            }
            if (bins_used > 0) {
                set_f(out, VQ_F_MOT_RMS, (float)sqrt(acc / bins_used));
                if (last_active >= 0)
                    s_last_motion_us = t_origin + (int64_t)(last_active + 1) * bin_us;
                else if (s_last_motion_us == 0)
                    s_last_motion_us = t_origin;   /* never armed: window start */
                double still = (double)(now - s_last_motion_us) / 1.0e6;
                if (still < 0.0) still = 0.0;
                set_f(out, VQ_F_STILL_S, (float)still);
            }
        }
    }

    /* ---- Spectral tissue-index proxy ---------------------------------
     * Prefer the pipeline's own derived tissue.sto2 channel (nir ratio
     * gated vs dark frame, per hardware profile). Fall back to a
     * normalised NIR-vs-ambient difference
     *     idx = (μ_nir − μ_dark) / (μ_nir + μ_dark)
     * with the AS7341 flicker/ambient channel standing in for a true
     * LED-off dark frame until one is plumbed. Derived index only. */
    n = pull(VQ_CH_TISSUE_STO2, wus);
    if (latest_val(s_win, n, &v)) {
        set_f(out, VQ_F_SPEC_STO2_IDX, v);
    } else {
        float nir = 0.0f, dark = 0.0f;
        bool ok = false;
        n = pull(VQ_CH_SPEC_NIR, wus);
        if (n > 0) {
            mean_std(s_win, n, &v, &v2);
            nir = v;
            n = pull(VQ_CH_SPEC_FLICKER, wus);
            if (n > 0) {
                mean_std(s_win, n, &v, &v2);
                dark = v;
                ok = (nir + dark) > 0.0f;
            }
        }
        if (ok) set_f(out, VQ_F_SPEC_STO2_IDX, (nir - dark) / (nir + dark));
    }

    /* ---- Contact / battery / board temp: latest ---------------------- */
    n = pull(VQ_CH_CONTACT, wus);
    if (latest_val(s_win, n, &v)) set_f(out, VQ_F_CONTACT, v);

    n = pull(VQ_CH_BATT_SOC, wus);
    if (latest_val(s_win, n, &v)) set_f(out, VQ_F_BATT_SOC, v);

    n = pull(VQ_CH_ENV_TEMP, wus);          /* BME280 = board ambient  */
    if (!latest_val(s_win, n, &v)) {
        n = pull(VQ_CH_TEMP_AMB, wus);      /* MLX90632 ambient fallback */
        latest_val(s_win, n, &v);
    }
    if (n > 0 && !isnan(v)) set_f(out, VQ_F_TEMP_BOARD, v);

    return ESP_OK;
}
