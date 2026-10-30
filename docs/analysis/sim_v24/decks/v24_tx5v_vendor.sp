* VENDOR-MODEL CHECK (evidence run): TI SLVMAT6 TPS61240_TRANS macro in
* ngspice-47. Result: the macro does not boost - TX_5V_RAW tracks VIN
* (pass-through) at every corner; same behaviour as fullsim_v20 b06.
* Therefore all v24 TX_5V results use the datasheet-bounded behavioural
* averaged model in v24_tx5v_ledpulse.inc (labelled BEHAVIOURAL).
.inc ../models/TPS61240_TRANS.LIB
.param VINP=3.7
Vin  vbat 0 {VINP}
Rbat vbat vin_r 0.05
Cin1 vin_r 0 1.6u
Cin2 vin_r 0 10u
L2x  vin_r sw 1.0u
XU en fb 0 sw vin_r vout TPS61240_TRANS
Ven en 0 {VINP}
Vs  vout fb 0
C89x vout 0 5.2u IC=0
Cout2 vout 0 90n
Iload vout 0 PWL(0 1m 0.8m 1m 0.85m 0.2 1.4m 0.2 1.45m 1m 2.5m 1m)
.tran 0.5u 2.5m UIC
.options reltol=1e-3 itl4=500
.control
foreach vv 3.2 3.7 4.2
  alter Vin = $vv
  alter Ven = $vv
  run
  meas tran vout_ss AVG v(vout) FROM=0.6m TO=0.8m
  meas tran vout_dip MIN v(vout) FROM=0.8m TO=2.4m
  echo "VENDOR VIN=$vv TX5V_ss=$&vout_ss dip=$&vout_dip (should be ~5.0 if regulating)"
end
echo "DONE_VENDOR_CHECK"
.endc
.end
