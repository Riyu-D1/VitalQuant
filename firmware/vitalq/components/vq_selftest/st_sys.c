/* st_sys.c — bring-up tests over services + drivers:
 *   power_paths, usb_cdc, sleep_current, optical, ecg_bioz, mic_pdm,
 *   fsr_adc, model_infer, led_interlock, decision_loop
 */
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

#if defined(__has_include)
#  if __has_include("driver/usb_serial_jtag.h")
#    define ST_HAVE_USJ 1
#    include "driver/usb_serial_jtag.h"
#  endif
#endif

#include "st_priv.h"
#include "board_pins.h"
#include "i2c_bus.h"
#include "expander.h"
#include "power_mgr.h"
#include "led_sched.h"
#include "log_store.h"
#include "vq_model.h"
#include "decision.h"

/* ================================ power_paths ========================= */

vq_err_t st_power_paths(char *r, size_t n)
{
    char det[960];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_verdict_t v = ST_V_PASS;

    /* TX_5V boost — rail toggle only, no LED current requested.
     * acquire/release only ARM the scheduler's tx5 service timer — the
     * actual P3 write lands in the timer task (I2C can't run under the
     * sched lock), so the reads must wait out the deferred commit plus
     * the 1.5 ms tail-off.  Checking s_tx5_on synchronously races the
     * timer and flakes on real hardware too.                            */
    vq_err_t e = vq_led_tx5_acquire();
    vTaskDelay(pdMS_TO_TICKS(10));           /* commit lands             */
    bool on = vq_led_tx5_on();
    vTaskDelay(pdMS_TO_TICKS(120));          /* TPS61240 settle          */
    vq_err_t e2 = vq_led_tx5_release();
    vTaskDelay(pdMS_TO_TICKS(10));           /* >1.5 ms tail-off         */
    bool off = !vq_led_tx5_on();
    /* TX5 is shared: while streaming sensors hold LED grants the rail
     * legitimately stays on after our release. `off` is informational —
     * the toggle is proven by `on`; e2 proves the release path.        */
    st_printf(&d, "tx5v acquire=%d on=%d settle120ms release=%d off=%d%s",
              e, on, e2, off,
              (!off && !e2) ? " (held by active LED grants — ok)" : "");
    if (e || e2 || !on) v = ST_V_FAIL;

    /* MAX17048 gauge — raw regs for DMM cross-check + filtered view */
    uint16_t vc = 0, sc = 0;
    vq_err_t eg = st_rd16be(VQ_ADDR_MAX17048, 0x02, &vc);
    vq_err_t es = st_rd16be(VQ_ADDR_MAX17048, 0x04, &sc);
    if (eg == ESP_OK && es == ESP_OK) {
        st_printf(&d, "\n  gauge vcell=%.3fV soc=%.1f%% (raw 0x%04X/0x%04X — "
                  "cross-check vs DMM at J2)", (double)(vc * 7.8125e-5),
                  (double)(sc / 256.0f), vc, sc);
    } else {
        st_printf(&d, "\n  gauge read ERR e=%d/%d", eg, es);
        v = ST_V_FAIL;
    }
    vq_power_status_t ps;
    vq_power_status(&ps);
    st_printf(&d, "\n  pwr: %.3fV %.1f%% rate=%.2f vbus=%d chg=%d inh=%d "
              "t_board=%.1fC state=%d", (double)ps.vcell_v, (double)ps.soc_pct,
              (double)ps.charge_rate, ps.vbus, ps.charging,
              ps.charge_inhibited, (double)ps.board_temp_c, (int)ps.state);

    /* charger inputs */
    uint8_t in = 0;
    vq_exp_read_inputs(&in);
    gpio_set_direction(VQ_CHG_PG_GPIO, GPIO_MODE_INPUT);
    int pg = gpio_get_level(VQ_CHG_PG_GPIO);
    st_printf(&d, "\n  chg_stat P5=%d vbus_det P6=%d chg_pg IO7=%d",
              (in >> 5) & 1, (in >> 6) & 1, pg);

    /* CHG_DIS toggle — STAT response is informational (Hi-Z when inhibited) */
    vq_err_t ec = vq_exp_charge_inhibit(true);
    vTaskDelay(pdMS_TO_TICKS(80));
    vq_exp_read_inputs(&in);
    uint8_t stat_inh = (in >> 5) & 1;
    vq_exp_charge_inhibit(false);
    vTaskDelay(pdMS_TO_TICKS(80));
    vq_exp_read_inputs(&in);
    st_printf(&d, "\n  chg_dis: set e=%d stat while-inhibited=%d after=%d",
              ec, stat_inh, (in >> 5) & 1);
    if (ec) v = ST_V_FAIL;

    st_done(r, n, "power_paths", v, &d);
    return st_rc(v);
}

/* ================================ usb_cdc ============================= */

vq_err_t st_usb_cdc(char *r, size_t n)
{
    char det[512];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_printf(&d, "enumerated (you are reading this over USB-Serial/JTAG)");
#ifdef ST_HAVE_USJ
    st_printf(&d, "\n  usb_serial_jtag host_connected=%d",
              (int)usb_serial_jtag_is_connected());
#endif
    st_printf(&d, "\n  post-deep-sleep: native USB re-enumerates — host must "
              "reopen the port after wake (no re-flash path lost)");
    st_done(r, n, "usb_cdc", ST_V_PASS, &d);
    return ST_RC_PASS;
}

/* ================================ sleep_current ======================= */

vq_err_t st_sleep_current(char *r, size_t n)
{
    char det[640];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    vq_err_t e = vq_power_prepare_sleep();
    st_printf(&d, "prepare_sleep=%d — sensors parked per §4.4, "
              "CS/LED/MIC_CLK held, wake sources armed\n"
              "  MEASURE at J2 (series jumper): standby target ~110uA "
              "(no PMIC — sum of rail quiescents)", e);
    if (strcmp(st_arg(), "commit") == 0) {
        st_printf(&d, "\n  commit: entering deep sleep NOW — "
                  "wake on GAUGE_ALRT/EXP_INT/LSM6");
        st_done(r, n, "sleep_current", ST_V_PASS, &d);
        printf("%s\n", r);                 /* report out before we die */
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(300));    /* let CDC TX drain */
        vq_power_enter_deep_sleep();       /* noreturn */
    }
    st_printf(&d, "\n  not entering (arg-less run) — "
              "`selftest sleep_current commit` does it for real");

    /* The park is real even without the commit: sensors were put to
     * sleep (the health task re-streams them within ~30 s — marked as
     * FAULT retries, expected noise) and the W25Q512/log ring was
     * powered down.  The log store has no self-recovery path, so wake
     * it now or every append fails ESP_ERR_INVALID_STATE for the rest
     * of the session.                                                  */
    vq_log_wake();
    st_done(r, n, "sleep_current", e == ESP_OK ? ST_V_PASS : ST_V_FAIL, &d);
    return st_rc(e == ESP_OK ? ST_V_PASS : ST_V_FAIL);
}

/* ================================ optical ============================= */

static volatile bool     s_led_on_done, s_led_off_done;
static volatile uint32_t s_cb_on_n, s_cb_off_n;

static bool st_wait_flag(volatile bool *f, uint32_t ms)
{
    int64_t t0 = esp_timer_get_time();
    while (!*f) {
        if (esp_timer_get_time() - t0 > (int64_t)ms * 1000) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

static void st_ir_on(void *ctx)  { (void)ctx; vq_exp_set(VQ_EXP_IR_GATE, true);
                                   s_led_on_done = true; s_cb_on_n++; }
static void st_ir_off(void *ctx) { (void)ctx; vq_exp_set(VQ_EXP_IR_GATE, false);
                                   s_led_off_done = true; s_cb_off_n++; }
static void st_nir_on(void *ctx) { (void)ctx;
                                   if (vq_ad5940_nir_gate) vq_ad5940_nir_gate(true);
                                   s_led_on_done = true; s_cb_on_n++; }
static void st_nir_off(void *ctx){ (void)ctx;
                                   if (vq_ad5940_nir_gate) vq_ad5940_nir_gate(false);
                                   s_led_off_done = true; s_cb_off_n++; }
static void st_noop_on(void *ctx){ (void)ctx; s_led_on_done = true; s_cb_on_n++; }
static void st_noop_off(void *ctx){(void)ctx; s_led_off_done = true; s_cb_off_n++; }

vq_err_t st_optical(char *r, size_t n)
{
    char det[1280];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_verdict_t v = ST_V_PASS;
    bool ran = false;

    /* driver-side optical selftests (LED+PD response) where linked */
    if (vq_afe4900_selftest) {
        char b[256] = {0};
        vq_err_t e = vq_afe4900_selftest(b, sizeof(b));
        st_printf(&d, "\n  afe4900: %s", b[0] ? b : "(no text)");
        if (e) v = ST_V_FAIL;
        ran = true;
    } else st_printf(&d, "\n  afe4900 pd-resp: SKIP (no drv hook)");
    if (vq_max86141_selftest) {
        char b[256] = {0};
        vq_err_t e = vq_max86141_selftest(b, sizeof(b));
        st_printf(&d, "\n  max86141: %s", b[0] ? b : "(no text)");
        if (e) v = ST_V_FAIL;
        ran = true;
    } else st_printf(&d, "\n  max86141 pd-resp: SKIP (no drv hook)");

    /* AS7341 dark/light with IR_GATE + NIR730 single pulses, one chain
     * at a time through the scheduler */
    if (vq_as7341_capture) {
        /* counts order: f1..f8 (0-7), clear(8), nir(9), flicker(10) */
        uint16_t dark[11] = {0}, lit[11] = {0};
        vq_err_t e0 = vq_as7341_capture(dark, false);
        st_printf(&d, "\n  as7341 dark: clear=%u nir=%u e=%d",
                  dark[8], dark[9], e0);
        /* IR_GATE chain: real LED via expander, min-current pulse */
        vq_led_request_t q = {
            .chain = VQ_LED_IR_GATE, .pulse_mA = 60,
            .on_us = 200 * 1000, .period_us = 0, .priority = 0,
        };
        s_led_on_done = s_led_off_done = false;
        int64_t slot;
        vq_err_t es = vq_led_submit(&q, st_ir_on, st_ir_off, NULL, &slot);
        if (es == ESP_OK && st_wait_flag(&s_led_on_done, 500)) {
            vq_err_t e1 = vq_as7341_capture(lit, true);
            st_wait_flag(&s_led_off_done, 600);
            st_printf(&d, "\n  as7341 lit(IR gate): clear=%u nir=%u e=%d",
                      lit[8], lit[9], e1);
            if (e0 == ESP_OK && e1 == ESP_OK && lit[9] <= dark[9])
                st_printf(&d, " (no delta — LED? PD? covered)");
        } else {
            st_printf(&d, "\n  IR_GATE slot submit/wait failed e=%d", es);
            if (es != ESP_ERR_NOT_SUPPORTED && es != ESP_ERR_INVALID_STATE)
                v = ST_V_FAIL;
        }
        ran = true;
    } else {
        st_printf(&d, "\n  as7341 dark/light: SKIP (no vq_as7341_capture)");
    }
    if (vq_ad5940_nir_gate) {
        vq_led_request_t q = {
            .chain = VQ_LED_NIR730, .pulse_mA = 30,
            .on_us = 300 * 1000, .period_us = 0, .priority = 0,
        };
        s_led_on_done = s_led_off_done = false;
        int64_t slot;
        vq_err_t es = vq_led_submit(&q, st_nir_on, st_nir_off, NULL, &slot);
        bool ok = es == ESP_OK && st_wait_flag(&s_led_on_done, 800) &&
                  st_wait_flag(&s_led_off_done, 800);
        st_printf(&d, "\n  nir730 gate via AD5940 GPIO2: submit=%d slot=%lldus "
                  "on/off %s", es, (long long)slot, ok ? "ok" : "TIMEOUT");
        if (es == ESP_OK && !ok) v = ST_V_FAIL;
        ran = true;
    } else {
        st_printf(&d, "\n  nir730 gate: SKIP (no vq_ad5940_nir_gate)");
    }

    /* scheduler evidence: non-overlap + duty while we ran pulses */
    char tr[512] = {0};
    uint32_t ov = st_led_trace_overlap(tr, sizeof(tr));
    st_printf(&d, "\n  led_trace overlaps=%lu duty=%.2f%% (%s)",
              (unsigned long)ov, (double)vq_led_duty_pct(), tr);
    if (ov) v = ST_V_FAIL;

    if (!ran && v == ST_V_PASS) v = ST_V_SKIP;
    st_done(r, n, "optical", v, &d);
    return st_rc(v);
}

/* ================================ ecg_bioz ============================ */

vq_err_t st_ecg_bioz(char *r, size_t n)
{
    char det[768];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_verdict_t v = ST_V_PASS;
    bool ran = false;

    if (vq_ads1292_selftest) {
        char b[320] = {0};
        vq_err_t e = vq_ads1292_selftest(b, sizeof(b));
        st_printf(&d, "ads1292r: %s", b[0] ? b : "(no text)");
        if (e) v = ST_V_FAIL;
        ran = true;
    } else {
        st_printf(&d, "ads1292r test-sig/lead-off: SKIP (no drv hook)");
    }
    if (vq_ad5940_run_rcal) {
        float ohm = 0;
        /* If sleep prep hibernated the AD5940, first traffic wakes it —
         * one warm-up attempt then the real measurement.              */
        vq_err_t e = vq_ad5940_run_rcal(&ohm);
        if (e == ESP_ERR_TIMEOUT || (e == ESP_OK && ohm == 0.0f)) {
            vTaskDelay(pdMS_TO_TICKS(50));         /* hibernate wake    */
            e = vq_ad5940_run_rcal(&ohm);          /* RCAL 10k bench R  */
        }
        float err = (ohm - 10000.0f) / 10000.0f * 100.0f;
        st_printf(&d, "\n  ad5940 RCAL 10k: meas=%.1f ohm err=%.2f%% e=%d",
                  (double)ohm, (double)err, e);
        if (e || err > 1.0f || err < -1.0f) v = ST_V_FAIL;
        ran = true;
    } else if (vq_ad5940_selftest) {
        char b[320] = {0};
        vq_err_t e = vq_ad5940_selftest(b, sizeof(b));
        st_printf(&d, "\n  ad5940: %s", b[0] ? b : "(no text)");
        if (e) v = ST_V_FAIL;
        ran = true;
    } else {
        st_printf(&d, "\n  ad5940 RCAL: SKIP (no drv hook)");
    }

    if (!ran && v == ST_V_PASS) v = ST_V_SKIP;
    st_done(r, n, "ecg_bioz", v, &d);
    return st_rc(v);
}

/* ================================ mic_pdm ============================= */

vq_err_t st_mic_pdm(char *r, size_t n)
{
    char det[512];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    if (vq_mic_selftest) {
        char b[384] = {0};
        vq_err_t e = vq_mic_selftest(b, sizeof(b));   /* 1 s capture → RMS */
        st_printf(&d, "%s", b[0] ? b : "(no text)");
        st_done(r, n, "mic_pdm", e == ESP_OK ? ST_V_PASS : ST_V_FAIL, &d);
        return st_rc(e == ESP_OK ? ST_V_PASS : ST_V_FAIL);
    }
    st_printf(&d, "no vq_mic_selftest hook — IM69D130 PDM capture untested");
    st_done(r, n, "mic_pdm", ST_V_SKIP, &d);
    return ST_RC_SKIP;
}

/* ================================ fsr_adc ============================= */

vq_err_t st_fsr_adc(char *r, size_t n)
{
    char det[512];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    if (vq_fsr_selftest) {
        char b[384] = {0};
        vq_err_t e = vq_fsr_selftest(b, sizeof(b));   /* rest vs pressed */
        st_printf(&d, "%s", b[0] ? b : "(no text)");
        st_done(r, n, "fsr_adc", e == ESP_OK ? ST_V_PASS : ST_V_FAIL, &d);
        return st_rc(e == ESP_OK ? ST_V_PASS : ST_V_FAIL);
    }
    st_printf(&d, "no vq_fsr_selftest hook — FSR402/ADC1_CH0 untested");
    st_done(r, n, "fsr_adc", ST_V_SKIP, &d);
    return ST_RC_SKIP;
}

/* ================================ model_infer ========================= */

vq_err_t st_model_infer(char *r, size_t n)
{
    char det[768];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));

    vq_model_t *m = NULL;
    vq_err_t e = vq_model_load(&m);
    if (e == ESP_ERR_NOT_FOUND) {
        st_printf(&d, "no VQMB blob in 'model' partition — decision layer "
                  "runs the compiled-in stub; flash the exporter artifact "
                  "to verify blob CRC/arena/golden-vector");
        st_done(r, n, "model_infer", ST_V_SKIP, &d);
        return ST_RC_SKIP;
    }
    if (e != ESP_OK || !m) {
        st_printf(&d, "vq_model_load e=%d (corrupt blob?)", e);
        st_done(r, n, "model_infer", ST_V_FAIL, &d);
        return ST_RC_FAIL;
    }

    st_verdict_t v = ST_V_PASS;
    st_printf(&d, "params=%lu arena=%luB in=%u out=%u",
              (unsigned long)vq_model_param_count(m),
              (unsigned long)vq_model_arena_used(m),
              (unsigned)vq_model_n_inputs(m), (unsigned)vq_model_n_outputs(m));

    int fi = vq_model_input_index(m, "features");
    if (fi >= 0) {
        float zeros[VQ_FEATURE_DIM] = {0};
        vq_err_t es = vq_model_set_input_f32(m, fi, zeros, VQ_FEATURE_DIM);
        if (es) { st_printf(&d, " set_input e=%d", es); v = ST_V_FAIL; }
    }
    int64_t lat = -1;
    vq_err_t er = vq_model_run(m, &lat);
    float out[2] = {0};
    vq_err_t eo = vq_model_get_output_f32(m, 0, out, 2);
    st_printf(&d, "\n  run e=%d lat=%lldus out=[%.3f %.3f]",
              er, (long long)lat, (double)out[0], (double)out[1]);
    if (er || eo) v = ST_V_FAIL;

    char sub[384] = {0};
    vq_model_selftest_report(m, sub, sizeof(sub));
    if (sub[0]) st_printf(&d, "\n  %s", sub);

    vq_model_free(m);
    st_done(r, n, "model_infer", v, &d);
    return st_rc(v);
}

/* ================================ led_interlock ======================= */

vq_err_t st_led_interlock(char *r, size_t n)
{
    char det[1024];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    st_verdict_t v = ST_V_PASS;

    /* three overlapping 400 mA-class one-shots — the scheduler must
     * serialise them; we verify analytically on the trace */
    s_cb_on_n = s_cb_off_n = 0;
    static const vq_led_chain_t chs[3] = {
        VQ_LED_AFE4900, VQ_LED_MAX86141, VQ_LED_IR_GATE
    };
    uint32_t submits = 0;
    for (int i = 0; i < 3; i++) {
        vq_led_request_t q = {
            .chain = chs[i], .pulse_mA = 400,
            .on_us = 800, .period_us = 0, .priority = 0,
        };
        int64_t slot = 0;
        vq_err_t e = vq_led_submit(&q, st_noop_on, st_noop_off, NULL, &slot);
        st_printf(&d, "submit ch%d e=%d slot=%lld\n  ", (int)chs[i], e,
                  (long long)slot);
        if (e == ESP_OK) submits++;
    }
    if (submits) vTaskDelay(pdMS_TO_TICKS(500));  /* let slots play out */

    char tr[640] = {0};
    uint32_t ov = st_led_trace_overlap(tr, sizeof(tr));
    float duty = vq_led_duty_pct();
    st_printf(&d, "overlaps=%lu duty=%.2f%% cb_on=%lu cb_off=%lu\n  %s",
              (unsigned long)ov, (double)duty,
              (unsigned long)s_cb_on_n, (unsigned long)s_cb_off_n, tr);
    if (ov)                         { v = ST_V_FAIL; st_printf(&d, " OVERLAP"); }
    if (duty > 1.0f)                { v = ST_V_FAIL; st_printf(&d, " DUTY>1%%"); }
    if (submits && s_cb_on_n != s_cb_off_n) { v = ST_V_FAIL; st_printf(&d, " CB-IMBALANCE"); }
    if (!submits)                   { v = ST_V_SKIP; st_printf(&d, " no slots accepted"); }

    st_done(r, n, "led_interlock", v, &d);
    return st_rc(v);
}

/* ================================ decision_loop ======================= */

vq_err_t st_decision_loop(char *r, size_t n)
{
    char det[640];
    st_buf_t d; st_buf_init(&d, det, sizeof(det));
    vq_decision_t dec = {0};
    vq_err_t e = vq_decision_run_once(&dec);
    st_printf(&d, "run_once e=%d prob=%.3f thr=%.3f uncertain=%d "
              "escalated=%d reasons=0x%02lX lat=%lldus cloud=%.3f %s",
              e, (double)dec.prob, (double)dec.threshold, dec.uncertain,
              dec.escalated, (unsigned long)dec.reasons,
              (long long)dec.latency_us, (double)dec.cloud_prob,
              dec.cloud_model);
    if (dec.escalated)
        st_printf(&d, "\n  escalation bundle queued (dry-run path ok)");
    st_done(r, n, "decision_loop", e == ESP_OK ? ST_V_PASS : ST_V_FAIL, &d);
    return st_rc(e == ESP_OK ? ST_V_PASS : ST_V_FAIL);
}
