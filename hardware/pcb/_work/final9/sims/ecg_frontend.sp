* S1: ADS1292R ECG front-end — CH1 (AC-coupled resp) and CH2 (DC ECG) transfer
* Source values from netlist export verify/netlists/vitalq_v2.spice + pin audit
* Skin-electrode model per Geddes/Baker: R_s 1k + (R_p 150k || C_p 22n) typical Ag/AgCl
* ADS1292 inputs modelled as 10 Meg per datasheet input-impedance (lead-off off)

* ---------------- CH1: AC-coupled respiration channel ----------------
* PAD -> R32 51k -> ECG_P -> C50 100n -> IN1P_AC -> R67 10k -> IN1P
* IN1P biased to 1.65 V via R26/R27 10M divider
Ve1 ecg1_pad 0 DC 0 AC 1
Rel1 ecg1_pad e1b 1k            ; skin series R
Rp1  e1b 0 150k                 ; skin parallel R
Cp1  e1b 0 22n                  ; skin parallel C
R32  e1b ECG_P 51k              ; series protection R (netlist R32)
C52  ECG_P 0 2.2n               ; RFI shunt (netlist C52)
C50  ECG_P IN1P_AC 100n         ; AC coupling (C50)
R67  IN1P_AC IN1P 10k           ; series R at IC (R67)
R26  IN1P v33a 10meg            ; bias up (netlist R26)
R27  IN1P 0 10meg               ; bias down (netlist R27)
V33A v33a 0 DC 3.3              ; +3V3_ANA
Rin1 IN1P 0 10meg               ; IC input R (lead-off off)

* ---------------- CH2: DC-coupled ECG channel ----------------
* PAD -> R33 51k -> ECG_N -> IN2N (direct); C53 2.2n RFI
Ve2 ecg2_pad 0 DC 0 AC 1
Rel2 ecg2_pad e2b 1k
Rp2  e2b 0 150k
Cp2  e2b 0 22n
R33  e2b ECG_N 51k
C53  ECG_N 0 2.2n
Rin2 ECG_N 0 10meg

.control
ac dec 40 0.01 1meg
let fc1 = vdb(IN1P)
let fc2 = vdb(ECG_N)
wrdata ../simout/s1_ch1.csv fc1 frequency
wrdata ../simout/s1_ch2.csv fc2 frequency
meas ac ch1_peak find vdb(IN1P) at=100
let ch1_ref = ch1_peak - 3.0103
meas ac ch1_fc when vdb(IN1P)=ch1_ref CROSS=1
meas ac ch1_g  find vdb(IN1P) at=10
meas ac ch1_g1 find vdb(IN1P) at=1
meas ac ch1_g05 find vdb(IN1P) at=0.5
meas ac ch1_g500 find vdb(IN1P) at=500
meas ac ch2_peak find vdb(ECG_N) at=10
let ch2_ref = ch2_peak - 3.0103
meas ac ch2_fc when vdb(ECG_N)=ch2_ref CROSS=LAST
meas ac ch2_g500 find vdb(ECG_N) at=500
meas ac ch2_g2k find vdb(ECG_N) at=2k
print ch1_peak ch1_g05 ch1_g1 ch1_g ch1_g500 ch1_fc ch2_peak ch2_g500 ch2_g2k ch2_fc
.endc
.end
