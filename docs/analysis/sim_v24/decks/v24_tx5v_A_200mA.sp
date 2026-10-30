* V24 scenario A: single LED chain pulsed 200 mA x 60 us, 3 pulses @1 ms.
* Firmware rule (FIRMWARE_NOTES): one chain per slot -> this is the legal mode.
* Load model: constant-current sink (AFE4900 TX / MAX86141 are CC sinks).
* A CC load is approximated by a switched resistor R=V/I=25ohm at 5V;
* below ~3.5 V the sink cannot draw 200mA anyway, so this is conservative
* for the droop question (voltage collapse is what we measure).
.inc v24_tx5v_ledpulse.inc
Vg g 0 PWL(0 0 0.71m 0 0.715m 3 0.775m 3 0.78m 0
+        1.71m 0 1.715m 3 1.775m 3 1.78m 0
+        2.71m 0 2.715m 3 2.775m 3 2.78m 0 4m 0)
S1 vtx lp g 0 SWM
Rl lp 0 25
.model SWM SW(Ron=0.01 Roff=1e9 Vt=1.0 Vh=0.3)
.tran 0.5u 4m UIC
.options reltol=1e-3 itl4=500
.control
foreach vv 3.2 3.7 4.2
  alter Vin = $vv
  run
  meas tran v_ss   AVG v(vtx) FROM=0.5m TO=0.7m
  meas tran v_dip  MIN v(vtx) FROM=0.7m TO=2.8m
  meas tran v_rec  AVG v(vtx) FROM=3.6m TO=4.0m
  meas tran iin_pk MAX i(Vimon) FROM=0.4m
  meas tran iin_av AVG i(Vimon) FROM=0.7m TO=2.8m
  echo "A200 VIN=$vv ss=$&v_ss dip=$&v_dip rec=$&v_rec iinpk=$&iin_pk iinavg=$&iin_av"
end
* CSV trace for the 3.7 V corner (nominal cell)
alter Vin = 3.7
run
wrdata ../results/v24_tx5v_A.csv time v(vtx) v(vraw) i(Vimon) v(g)
echo "DONE_A200"
.endc
.end
