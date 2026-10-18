* S6: BQ25170 behavioural charge profile — CC 200mA then CV 4.2V
* Cell = 1080 F capacitor (300 mAh*3600/1V) whose voltage IS the OCV,
* starting at 3.5 V SOC, + 150 mohm ESR. Charger = CC 0.2 A / CV 4.24 V.

Bchg 0 vbat_sns I=(v(cell) < 4.18) ? 0.2 : max(0, 0.2*(4.24-v(cell))/0.06)
Rsn  vbat_sns vbat 0.1
Resr vbat cell 0.15
Ccell cell 0 1080 IC=3.5

.control
tran 30 9000 uic
let ichg = (v(vbat_sns)-v(vbat))/0.1
wrdata ../simout/s6_charge.csv v(cell) v(vbat) ichg time
meas tran tcv when v(cell)=4.18 cross=1
meas tran icc find ichg at=1000
meas tran iterm when ichg=0.02 cross=1
meas tran vend find v(cell) at=8800
print tcv icc iterm vend
.endc
.end
