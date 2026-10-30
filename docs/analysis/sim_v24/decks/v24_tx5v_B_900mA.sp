* V24 scenario B: all LED chains fired simultaneously ~900 mA x 60 us.
* Prior analysis bound: AFE4900 4x200=800 + MAX86141 3x124=372 = up to
* 1172 mA; ~900 mA is the design figure -> R=5V/0.9A=5.6ohm.
.inc v24_tx5v_ledpulse.inc
Vg g 0 PWL(0 0 0.71m 0 0.715m 3 0.775m 3 0.78m 0
+        1.71m 0 1.715m 3 1.775m 3 1.78m 0 4m 0)
S1 vtx lp g 0 SWM
Rl lp 0 5.6
.model SWM SW(Ron=0.01 Roff=1e9 Vt=1.0 Vh=0.3)
.tran 0.5u 4m UIC
.options reltol=1e-3 itl4=500
.control
foreach vv 3.2 3.7 4.2
  alter Vin = $vv
  run
  meas tran v_ss   AVG v(vtx) FROM=0.5m TO=0.7m
  meas tran v_dip  MIN v(vtx) FROM=0.7m TO=1.8m
  meas tran v_rec  AVG v(vtx) FROM=3.6m TO=4.0m
  meas tran iin_pk MAX i(Vimon) FROM=0.4m
  echo "B900 VIN=$vv ss=$&v_ss dip=$&v_dip rec=$&v_rec iinpk=$&iin_pk"
end
alter Vin = 3.7
run
wrdata ../results/v24_tx5v_B.csv time v(vtx) v(vraw) i(Vimon) v(g)
echo "DONE_B900"
.endc
.end
