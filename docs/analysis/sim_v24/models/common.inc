* common.inc — shared behavioural subcircuits for VitalQ compsim

* ---- IEC 61000-4-2 contact-discharge gun (8 kV level 4) ----
* 150 pF cap IC-charged to {VESD}; at t=1ns Sgun closes -> 330 ohm + 10 nH.
.subckt ESDGUN TIP GND PARAMS: VESD=8000
Cgun N_STORE GND  150p  IC={VESD}
Sgun N_STORE N_R  CTL GND GUNSW
Rgun N_R    N_L   330
Lgun N_L    TIP   10n
Vctl CTL GND PWL(0 0  0.9n 0  1.0n 1)
.model GUNSW SW(Ron=0.1 Roff=1e9 Vt=0.5 Vh=0.1)
.ends ESDGUN

* ---- ST USBLC6-2SC6 clamp (DS DocID11265 rev5 section 2.1) ----
.subckt USBLC6 IO1 IO2 VBUS GND
Du1 IO1   IO1C DVDU
Bup1 IO1C VBUS I = V(IO1C,VBUS)/1m
Dd1 GND   IO1   DVDD
Du2 IO2   IO2C DVDU
Bup2 IO2C VBUS I = V(IO2C,VBUS)/1m
Dd2 GND   IO2   DVDD
Btr VBUS GND I = (V(VBUS) > 7.2) ? (V(VBUS)-7.2)/0.5 : 0
Rlk1 IO1 GND 35meg
Rlk2 IO2 GND 35meg
Cio1 IO1 GND 3.5p
Cio2 IO2 GND 3.5p
.model DVDU D(Is=1e-9 Rs=0.5 n=1.3 Cjo=0.4p)
.model DVDD D(Is=1e-9 Rs=0.5 n=1.3 Cjo=0.4p)
.ends USBLC6

* ---- Li-ion 402535 pouch cell (300 mAh nominal) ----
* Terminal at P. Charging current integrates on N_SOC cap (CQ=1080 F;
* 1 V of N_SOC = 100 % SOC).  Voc piecewise TABLE of a Li-ion pouch.
* SOC_M pin outputs SOC in volts; IMON outputs current into cell in volts.
.subckt LIPO402535 P GND SOC_M IMON PARAMS: SOC0=0.15 RS=0.35
Cq   N_SOC GND 1080 IC={SOC0}
Gint GND N_SOC VALUE = { I(Vsen) }
Vsen P N_R DC 0
Rser N_R N_VOC {RS}
Evoc N_VOC GND TABLE { V(N_SOC) } =
+ ( 0.00, 3.00 ) ( 0.05, 3.30 ) ( 0.15, 3.50 ) ( 0.30, 3.62 )
+ ( 0.50, 3.72 ) ( 0.70, 3.85 ) ( 0.85, 4.00 ) ( 0.93, 4.10 )
+ ( 0.97, 4.16 ) ( 1.00, 4.20 ) ( 1.10, 4.35 )
Esoc SOC_M GND VALUE = { V(N_SOC) }
Bimon IMON GND V = { I(Vsen) }
Rdummy N_SOC GND 1e12
.ends LIPO402535

* ---- Ag/AgCl skin electrode (Geddes-Baker) ----
.subckt SKINELEC SKIN PAD PARAMS: RP=150k CP=22n RS=1k
Rs1 SKIN N_MID {RS}
Rp  N_MID PAD {RP}
Cp  N_MID PAD {CP}
.ends SKINELEC
