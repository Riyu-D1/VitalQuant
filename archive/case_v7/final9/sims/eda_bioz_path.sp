* S3: EDA (J6) and BioZ (J7) coupling-path transfer vs frequency
* Quantifies the AC-coupling capacitor choices (C54 15n CE, C55 470n SE,
* C67-70 470n/15n BioZ) across EDA (1-100 Hz) and BioZ (10-100 kHz) bands.
* Excitation modelled at the AD5940 pin; skin-electrode per path ~ 51k+skin.

* ---------- J6 EDA CE path: CE0 -> R39 1k -> C54 15n -> R76 51k -> PAD ----------
Vce CE0 0 DC 0 AC 1
R39 CE0 CE_ISO 1k
C54 CE_ISO CE_SURGE 15n
R76 CE_SURGE EDA_CE_PAD 51k
* electrode contact + skin to return path
Rel_c EDA_CE_PAD skin 10k
Rp_c skin 0 100k
Cp_c skin 0 22n
* return via RE0 path (DC coupled, 1k + 51k)
Rret skin ret1 10k
R78 ret1 RE_SURGE 51k
R41 RE_SURGE RE0 1k
Rin_re0 RE0 0 1meg             ; RE0 input impedance (TIA input ~ hi-Z)

* ---------- J7 BioZ FP force path: AIN1-side, C67 15n ----------
Vfp AIN1_in 0 DC 0 AC 1
R84 AIN1_in FP_ISO 1k
C67 FP_ISO FP_SURGE 15n
R80 FP_SURGE BIOZ_FP_PAD 51k
Rel_fp BIOZ_FP_PAD bz 10k
Rp_fp bz 0 100k
Cp_fp bz 0 22n

* ---------- J7 SN sense path: 470n ----------
Vsn SN_drv 0 DC 0 AC 1
R87 SN_drv SN_ISO 1k
C70 SN_ISO SN_SURGE 470n
R83 SN_SURGE BIOZ_SN_PAD 51k
Rel_sn BIOZ_SN_PAD bz2 10k
Rp_sn bz2 0 100k
Cp_sn bz2 0 22n

.control
ac dec 30 0.1 1meg
wrdata ../simout/s3_eda.csv vdb(EDA_CE_PAD) frequency
wrdata ../simout/s3_fp.csv vdb(BIOZ_FP_PAD) frequency
wrdata ../simout/s3_sn.csv vdb(BIOZ_SN_PAD) frequency
meas ac eda_peak find vdb(EDA_CE_PAD) at=300
let eda_ref = eda_peak - 3.0103
meas ac eda_1hz   find vdb(EDA_CE_PAD) at=1
meas ac eda_10hz  find vdb(EDA_CE_PAD) at=10
meas ac eda_100hz find vdb(EDA_CE_PAD) at=100
meas ac eda_1k    find vdb(EDA_CE_PAD) at=1k
meas ac eda_50k   find vdb(EDA_CE_PAD) at=50k
meas ac eda_fc when vdb(EDA_CE_PAD)=eda_ref CROSS=1
meas ac fp_peak find vdb(BIOZ_FP_PAD) at=50k
let fp_ref = fp_peak - 3.0103
meas ac fp_50k    find vdb(BIOZ_FP_PAD) at=50k
meas ac fp_10k    find vdb(BIOZ_FP_PAD) at=10k
meas ac fp_1k     find vdb(BIOZ_FP_PAD) at=1k
meas ac fp_fc when vdb(BIOZ_FP_PAD)=fp_ref CROSS=1
meas ac sn_50k    find vdb(BIOZ_SN_PAD) at=50k
meas ac sn_10k    find vdb(BIOZ_SN_PAD) at=10k
meas ac sn_1k     find vdb(BIOZ_SN_PAD) at=1k
meas ac sn_fc when vdb(BIOZ_SN_PAD)=fp_ref CROSS=1
print eda_peak eda_1hz eda_10hz eda_100hz eda_1k eda_50k eda_fc fp_peak fp_1k fp_10k fp_50k fp_fc sn_1k sn_10k sn_50k sn_fc
.endc
.end
