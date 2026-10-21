* R1-L1: BQ25170 TS window — NTC-only (fix_v5, R61 DNP) vs R61||NTC (fix_v4 bug)
* I_TS = 38uA typ current source out of TS pin. Suspend cold VTS>1.61(min)..1.69(max),
* suspend hot VTS<0.176(min)..0.20(max) typ 0.188. Pack NTC 10k@25C B=3435.
Vtemp t 0 DC 25
Its5 0 ts5 DC 38u
Its4 0 ts4 DC 38u
Rntc5 ts5 0 R='10k*exp(3435*(1/(v(t)+273.15)-1/298.15))'
Rntc4 ts4 0 R='10k*exp(3435*(1/(v(t)+273.15)-1/298.15))'
R61 ts4 0 10k
.control
dc Vtemp -10 60 1
wrdata ../simout/ts_window.csv v(ts5) v(ts4) dc1.sweep
meas dc t_cold5 when v(ts5)=1.61 cross=1
meas dc t_hot5 when v(ts5)=0.20 cross=1
meas dc t_hot5b when v(ts5)=0.176 cross=1
meas dc v0_5 find v(ts5) at=0
meas dc v45_5 find v(ts5) at=45
meas dc v25_5 find v(ts5) at=25
meas dc t_cold4 when v(ts4)=1.61 cross=1
meas dc t_hot4 when v(ts4)=0.20 cross=1
meas dc t_hot4b when v(ts4)=0.176 cross=1
meas dc v25_4 find v(ts4) at=25
meas dc v45_4 find v(ts4) at=45
print t_cold5 t_hot5 t_hot5b v0_5 v25_5 v45_5
print t_cold4 t_hot4 t_hot4b v25_4 v45_4
.endc
.end
