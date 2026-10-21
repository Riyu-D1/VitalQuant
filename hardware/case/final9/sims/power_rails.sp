* S5: power-rail load transients — behavioural converter models
* (a) +3V3 from TPS63802: ideal 3.3V source behind ~80mohm (buck-boost
*     output impedance incl. inductor DCR ~120m), L=0.47uH is inside the IC
*     loop, so external model = source + Rout + Cout network (22u + 1u + 8x100n)
* (b) TX_5V from TPS61240: 5V + ~200mohm + Cout (4.7u + 10u + 1u)
* (c) +1V8 from TPS7A2018 LDO: 1.8V + ~140mohm + 1uF
* Load steps represent worst-case bursts from power_budget.py.

.subckt MLCC v g c=100n esr=5m
c1 v g {c} ic=0
r1 v vx {esr}
c2 vx g {c}
.ends MLCC

* ---------------- (a) +3V3 rail ----------------
V33 src33 0 3.3
Ro33 src33 p33 0.08
Cp33a p33 0 22u IC=3.3     ; C45
Cp33b p33 0 1u             ; C4
Cp33c p33 0 800n           ; 8x 100n spread
Resr p33 e33 0.01
Ce33 e33 0 1u
* ESP32 WiFi TX burst: 0 -> 400 mA in 10us, hold 100us, off (per Espressif burst)
Iesp p33 0 PWL(0 0 99u 0 100u 0.4 300u 0.4 310u 0 600u 0 610u 0.4 900u 0.4 910u 0)

* ---------------- (b) TX_5V rail ----------------
V5 src5 0 5.0
Ro5 src5 p5 0.2
Cp5a p5 0 4.7u IC=5.0      ; C47
Cp5b p5 0 10u              ; C89
Cp5c p5 0 1u               ; C14
* PPG LED pulse train: 4 x 20 mA x 60 us at 25 Hz (worst case 80 mA)
Iled p5 0 PWL(0 0 9u 0 10u 0.08 70u 0.08 80u 0 100u 0 110u 0.08 170u 0.08 180u 0)

* ---------------- (c) +1V8 LDO rail ----------------
V18 src18 0 1.8
Ro18 src18 p18 0.14
Cp18a p18 0 1u IC=1.8      ; C6
Cp18b p18 0 10u            ; C87
Cp18c p18 0 300n           ; C36/C86/C88
* MAX86141 + AS7341 load step 0 -> 15 mA
I18 p18 0 PWL(0 0 49u 0 50u 0.015 300u 0.015 310u 0)

.control
tran 200n 1.2m
wrdata ../simout/s5_3v3.csv v(p33) v(src33) time
wrdata ../simout/s5_tx5.csv v(p5) time
wrdata ../simout/s5_1v8.csv v(p18) time
meas tran v33_min min v(p33) from=80u to=400u
meas tran v33_min2 min v(p33) from=600u to=1.2m
meas tran v33_dip pp v(p33) from=90u to=1.2m
meas tran v5_min min v(p5) from=50u
meas tran v5_dip pp v(p5) from=50u to=500u
meas tran v18_min min v(p18) from=40u
meas tran v18_dip pp v(p18) from=40u to=500u
print v33_min v33_min2 v33_dip v5_min v5_dip v18_min v18_dip
.endc
.end
