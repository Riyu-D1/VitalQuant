* S4: LED drive circuits — transient DC operating point
* (a) discrete IR/NIR chains (Q1/Q4 CSD13380F3 + 100R from +3V3)
* (b) AS7341 LDR flash LED headroom check (+3V3 vs TX_5V)
* (c) SFH7072 green-LED headroom on TX_5V through AFE4900 sink
* LED I-V: SPICE diode model with forward Vt adjusted to part Vf

* IR LED SFH4053: Vf~1.35V @20mA ; NIR SFH4735 730nm: Vf~2.0V @20mA
* white NF2W757G: Vf~3.05V @20mA ; green SFH7072: Vf~3.2V @20mA (TIDA table)
.model DIRL  D IS=1e-12 N=1.6 RS=2.5
.model DNIR  D IS=1e-14 N=1.9 RS=3
.model DWHT  D IS=1e-18 N=2.9 RS=4
.model DGRN  D IS=1e-19 N=3.1 RS=3

* NMOS sink model ~ CSD13380F3T (Rds~35mOhm @3.3Vgs)
.model NFET NMOS VTO=0.9 KP=200m L=0.35u W=1

V3V3 n33 0 3.3
V5V  n55 0 5.0
Vg   gate 0 3.3

* (a1) IR chain: 3V3 -> R47 100 -> IR_AN -> D11 -> IR_K -> Q1.D
Ra1 n33 IR_AN 100
D1 IR_AN IR_K DIRL
Vs1 IR_K ks1 0
M1 ks1 gate 0 0 NFET

* (a2) NIR chain: 3V3 -> R109 100 -> NIR_AN -> D12 -> NIR_K -> Q4.D
Ra2 n33 NIR_AN 100
D2 NIR_AN NIR_K DNIR
Vs2 NIR_K ks2 0
M2 ks2 gate 0 0 NFET

* (b1) AS7341 flash on +3V3: 3V3 -> D10 -> LDR ; sink modelled as FET Rds
Rb1 n33 W_AN 0.1
D3 W_AN LDR DWHT
Vg2 g2 0 3.3
Vs3 LDR ks3 0
M3 ks3 g2 0 0 NFET
* (b2) same LED on TX_5V for comparison
Rb2 n55 W_AN5 0.1
D4 W_AN5 LDR5 DWHT
Vs4 LDR5 ks4 0
M4 ks4 g2 0 0 NFET
* (c) SFH7072 green on TX_5V, AFE4900 TX driver sink (FET model)
Rc1 n55 G_AN 0.1
D5 G_AN TX1K DGRN
Vs5 TX1K ks5 0
M5 ks5 g2 0 0 NFET

.control
op
print i(Vs1) i(Vs2)
print i(Vs3) i(Vs4) i(Vs5)
print v(LDR) v(LDR5) v(TX1K) v(W_AN) v(W_AN5) v(G_AN)
.endc
.end
