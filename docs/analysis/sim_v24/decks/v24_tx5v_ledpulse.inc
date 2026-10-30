* Shared TX_5V boost circuit for v24 decks.
* Board parts (checked, sim_v24 netlist): L2=1.0uH, C46 2.2uF in, C47 4.7uF on
* TX_5V_RAW, R96 0R link, C14 1uF + C89 10uF on TX_5V.
* BEHAVIOURAL averaged boost (vendor TPS61240_TRANS.LIB macro does not
* regulate in ngspice-47: output tracks VIN at every corner - verified by
* re-running fullsim_v20 b06 and the v24 vendor deck v24_tx5v_vendor.sp).
* Model bounds from TPS61240 DS SLVSAE3:
*   - input valley current limit 500 mA (EC table)
*   - VOUT fixed 5.0 V, soft-start ~250 us, PFM below ~30 mA
*   - effective Iout_max ~= Ilim_valley*VIN*eta/VOUT (eta~0.85, inferred)
* Regulator: output current demand = Gm*(Vref-Vout), clamped 0..Ilim(VIN).
* Iin drawn from battery = Vout*Iout/(VIN*eta) (power conservation).
.param VINP=3.7
Vin  vbat 0 {VINP}
Rbat vbat vsn 0.05
Vimon vsn vin_r 0
Cin1 vin_r 0 1.6u
Cin2 vin_r 0 10u IC={VINP}
L2x  vin_r 0 1e9            ; placeholder: averaged model has no SW node
* soft-start: Vref ramps 0->5V in ~250us, 150us EN delay
Vss sref 0 PWL(0 0 150u 0 400u 5.0 100m 5.0)
* error amp + limiter (Gm=30 A/V -> ~7mV droop per 200mA in regulation)
Bd   0 e0  I = V(sref) - V(vraw)
Re0  e0 0  1
Ce0  e0 0  3n
* boost output stage into vraw: I = clamp(Gm*e0, 0, Ilim)
Breg 0 vraw I = (V(e0) <= 0) ? 0 :
+   ((V(e0)*30) > (0.5*V(vin_r)*0.85/5.0) ? (0.5*V(vin_r)*0.85/5.0) : (V(e0)*30))
* input current: Pout = Pin*eta -> Iin = Vraw*Ireg/(VIN*0.85)
Bisn vin_r 0 I = V(vraw)*(
+   (V(e0) <= 0) ? 0 :
+   ((V(e0)*30) > (0.5*V(vin_r)*0.85/5.0) ? (0.5*V(vin_r)*0.85/5.0) : (V(e0)*30))
+ )/V(vin_r)/0.85
* output network: vraw -> R96 -> vtx
C47x vraw 0 2.8u IC=0
R96x vraw vtx 0.02
C14x vtx 0 0.9u IC=0
C89x vtx 0 6.0u IC=0
Iq   vtx 0 0.001            ; quiescent/TX block bias ~1mA
