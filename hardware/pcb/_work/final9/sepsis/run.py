#!/usr/bin/env python3
"""run.py — orchestrate the sepsis pipeline and emit results JSON."""
import json, sys, time
import numpy as np
import synth, hw, algo, detect

FS = synth.FS
rng = np.random.default_rng(7)

def measure(pat):
    """Full HIL pass -> measured vitals dict."""
    fs = FS
    ecg = hw.ecg_front(pat, fs)
    respz = hw.resp_channel(pat, fs)
    mot = pat['motion']
    mask = algo.clean_mask(mot)
    # HR/HRV on clean-ish signal
    hm = algo.hr_metrics(ecg, fs, None)
    # RR: median of impedance-pneumo + EDR + PPG estimates on CLEAN windows
    r_est = [algo.rr_from(respz, fs, mask=mask),
             algo.rr_from(hw.edr_signal(pat, ecg, fs), fs, mask=mask)]
    ac_r, dc_r, ac_i, dc_i = hw.ppg_channels(pat)
    tp_fs = synth.FS_PPG
    ac_r_hp = ac_r - np.convolve(ac_r, np.ones(40) / 40, 'same')
    ac_i_hp = ac_i - np.convolve(ac_i, np.ones(40) / 40, 'same')
    motp = np.interp(pat['tp'], pat['t'], mot)
    maskp = motp < 0.8
    r_est.append(algo.rr_from(ac_i_hp, tp_fs, mask=maskp))
    r_est = [r for r in r_est if r == r]
    rr_meas = float(np.median(r_est)) if r_est else float('nan')
    spo2_meas = algo.spo2_ror(ac_r_hp, dc_r, ac_i_hp, dc_i, tp_fs,
                              mask=maskp if maskp.mean() > 0.3 else None)
    spo2 = spo2_meas[0] if isinstance(spo2_meas, tuple) else spo2_meas
    pi = algo.perfusion_index(ac_i, dc_i)
    tmp117, mlx, skin = hw.temps(pat, rng)
    tcore = algo.core_temp(tmp117, mlx)
    eda = hw.eda_read(pat, rng)
    bioz = hw.bioz_read(pat, rng)
    return dict(hr=hm['hr'], rmssd=hm['rmssd'], sdnn=hm['sdnn'], rr=rr_meas,
                spo2=spo2, tcore=tcore, pi=pi, eda=eda, bioz=bioz,
                nbeats=hm['nbeats'],
                clean_frac=float(np.mean(mask)))

def run_case(case, n, dur=120.0):
    out = []
    for i in range(n):
        pat = synth.make_patient(case, rng, dur)
        m = measure(pat)
        m['truth'] = pat['truth']; m['case'] = case
        out.append(m)
    return out

def main():
    t0 = time.time()
    results = {}
    per = 34
    for case in synth.CASES:
        rs = run_case(case, per)
        results[case] = rs
        print(case, 'done', time.time() - t0)

    # deterioration trajectory: compensated baseline -> septic shock over 45 min
    traj = []
    span = np.linspace(0, 1, 23)          # 2-min windows
    for f in span:
        p = dict(
            hr=78 + f * 57, rr=(16 + f * 18) / 60, spo2=97.5 - f * 10.4,
            tcore=37.2 + f * 2.0, rmssd=35 - f * 27, pi=3.5 - f * 3.27,
            eda=4 + f * 26, bioz=1.0 - f * 0.25, motion=0.3, sbp=118 - f * 38,
            avpu=0 if f < 0.8 else 1)
        class P: pass
        pat = synth.make_patient('early_sepsis', rng, 120)
        pat['truth'].update(p); pat['truth']['rr'] = p['rr'] * 60
        # re-scale physiology to interpolated params
        hr_scale = p['hr'] / pat['truth']['hr'] if False else 1
        pat['truth']['hr'] = p['hr']
        beats = synth.rr_times(120, p['hr'], max(p['rmssd'], 3), rng)
        pat['beats'] = beats
        pat['ecg'] = synth.mcsharry_ecg(pat['t'], beats)
        pat['resp'] = np.sin(2 * np.pi * p['rr'] * pat['t'])
        tp = pat['tp']
        ac, dc = synth.ppg_wave(tp, beats, max(p['pi'], 0.15) / 100.0, rng)
        pat['ppg_ac'], pat['ppg_dc'] = ac, dc
        m = measure(pat)
        m['f'] = f; m['tmin'] = f * 45
        traj.append(m)
    results['trajectory'] = traj

    with open('results.json', 'w') as fp:
        json.dump(results, fp, indent=1)
    print('TOTAL', time.time() - t0)

if __name__ == '__main__':
    main()
