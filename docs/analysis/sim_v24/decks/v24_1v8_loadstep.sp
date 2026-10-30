* V24 +1V8 rail: TPS7A2018 LDO fed from +3V3 (U4). VENDOR model
* (tps7a20-adj_trans.lib, SBVM961) with V_out=1.8 = fixed die variant.
* Loads (checked): AS7341 300uA, MAX86141 ~1.2mA, PCA9306/AXC ~30uA,
* pull-ups ~1.5mA -> burst ~20mA when AS7341 LDR LED runs.
* Also checks headroom at VIN=3.0 (worst-case 3V3 droop).
.inc ../models/tps7a20-adj_trans.lib
.param V_out=1.8
Vin  v33 0 3.3
Rsrc v33 vin 0.02
Cin  vin 0 4.7u IC=3.3
Ven  en 0 3.3
XU vin 0 en nc vout TPS7A20_ADJ_TRANS
Cnc nc 0 10n
Cout vout 0 1.56u IC=1.8
Iload vout 0 PWL(0 5m 1.9m 5m 1.92m 20m 2.6m 20m 2.62m 5m 3.5m 5m)
.tran 1u 3.5m UIC
.options reltol=1e-3 itl4=500
.control
foreach vv 3.0 3.3 3.43
  alter Vin = $vv
  alter Ven = $vv
  run
  meas tran vout_ss  AVG v(vout) FROM=1.6m TO=1.9m
  meas tran vout_dip MIN v(vout) FROM=1.9m TO=2.65m
  meas tran vout_pk  MAX v(vout) FROM=1.9m TO=2.65m
  let vh = v(vin)-v(vout)
  meas tran vdo      MIN vh FROM=1.9m TO=2.65m
  echo "V18 VIN=$vv v1v8=$&vout_ss dip=$&vout_dip pk=$&vout_pk headroom=$&vdo"
end
alter Vin = 3.3
alter Ven = 3.3
run
wrdata ../results/v24_1v8_loadstep.csv time v(vout) v(vin)
echo "DONE_V18"
.endc
.end
