* V24 BQ25170 charge profile: ICHG from R3=1.5k, VREG from R75=27k=4.20V
* BEHAVIOURAL model models/bq25170_v47.inc (datasheet SLUSDJ8A bounds),
* cell model LIPO402535 = 300mAh pouch (models/common.inc).
* Board check (checked): ISET pin -> R3 1.5k -> ICHG = 300/1.5k = 200mA
*                        VSET pin -> R75 27k  -> VREG = 4.20V (Table 7-1)
.include ../models/common.inc
.include ../models/bq25170_v47.inc
Vin VBUS 0 5.0
Rsrc VBUS VBUSN 0.1
Xchg VBUSN VBAT 0 TS STAT PG IM DONE BQ25170_BHV VREG=4.2 ICHG=0.2 ITERM=0.02
Rntc TS 0 10k                    ; cell NTC 10k fitted on board (R61 DNP)
Xbat VBAT 0 SOC IMONB LIPO402535 SOC0=0.05 RS=0.35
Cvbat VBAT 0 53.8u IC=2.9
.tran 1.0 5400 UIC
.control
run
meas tran ichg_cc  FIND v(IM)  AT=10
meas tran vbat_60  FIND v(VBAT) AT=60
meas tran soc_1500  FIND v(SOC) AT=1500
meas tran soc_5000  FIND v(SOC) AT=5000
meas tran t_done   WHEN v(DONE)=0.5 CROSS=1
echo "ICHG_cc=$&ichg_cc A vbat@60s=$&vbat_60 soc@150s=$&soc_1500 soc@280s=$&soc_5000 t_done=$&t_done"
wrdata ../results/v24_bq25170_charge.csv time v(VBAT) v(IM) v(SOC) v(DONE)
echo "NOTE: sim time is compressed - BHV model uses real 300s; SOC in V=frac"
echo "DONE_BQ"
.endc
.end
