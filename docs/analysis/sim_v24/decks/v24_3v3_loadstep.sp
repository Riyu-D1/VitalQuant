* V24 +3V3 rail: TPS63802 buck-boost under Wi-Fi TX burst load steps.
* BEHAVIOURAL model (same approach as fullsim_v20 b05; vendor
* TPS63802_TRANS.LIB does not converge in ngspice-47 - retested, see
* results/v24_tps63802_vendor_fail.log). Bounds from SLVSEP0 EC table:
*   VFB=0.5V via real R23=560k/R24=100k divider -> VOUT=3.30V
*   soft-start ~0.8ms, load-step droop ~60mV @300mA, Ipk lim ~2.5A
*   Cin=C44 10u+C46* ; Cout=C45 22u(derated 13.6u)+C61 100n on +3V3
* Load profile (inferred from DS Table 11 figures):
*   idle ~15mA -> 355mA Wi-Fi TX burst 0.8ms -> 95mA RX 0.5ms -> idle
.param VINP=3.7
Vin  vbat 0 {VINP}
Rbat vbat vim 0.05
Vimon vim vin_r 0
Cin1 vin_r 0 10u IC={VINP}
Cin2 vin_r 0 1.6u
* feedback divider (checked: R23 560k, R24 100k)
R23x vout fb 560k
R24x fb 0 100k
C45x vout 0 13.6u IC=0
C61x vout 0 90n
* soft-start ramp + integrator + power stage (gm=33 A/V, 2.5A limit)
Vref vref 0 PWL(0 0 50u 0 850u 0.5 100m 0.5)
Bd   0 e0  I = V(vref) - V(fb)
Re0  e0 0  1
Ce0  e0 0  1n
Breg 0 vout I = (V(e0)>0 ? (V(e0)*33 > 2.5 ? 2.5 : V(e0)*33) : 0)
Bisn vin_r 0 I = V(vout)*(V(e0)>0 ? (V(e0)*33 > 2.5 ? 2.5 : V(e0)*33) : 0)/(V(vin_r)*0.92)
* Wi-Fi burst load: 15mA idle, 355mA TX x0.8ms, 95mA RX x0.5ms, x3
Iload vout 0 PWL(0 0.015
+ 1.0m 0.015 1.01m 0.355 1.81m 0.355 1.82m 0.095 2.32m 0.095 2.33m 0.015
+ 3.0m 0.015 3.01m 0.355 3.81m 0.355 3.82m 0.095 4.32m 0.095 4.33m 0.015
+ 5.0m 0.015 5.01m 0.355 5.81m 0.355 5.82m 0.095 6.32m 0.095 6.33m 0.015 8m 0.015)
.tran 1u 8m UIC
.options reltol=1e-3
.control
foreach vv 3.0 3.2 3.7 4.35
  alter Vin = $vv
  run
  meas tran v_ss  AVG v(vout) FROM=0.9m TO=1.0m
  meas tran v_dip MIN v(vout) FROM=1.0m TO=6.4m
  meas tran v_pk  MAX v(vout) FROM=1.0m TO=6.4m
  meas tran iin_pk MAX i(Vimon) FROM=0.9m
  echo "V33 VIN=$vv ss=$&v_ss dip=$&v_dip pk=$&v_pk iinpk=$&iin_pk"
end
alter Vin = 3.7
run
wrdata ../results/v24_3v3_loadstep.csv time v(vout) i(Vimon)
echo "DONE_V33"
.endc
.end
