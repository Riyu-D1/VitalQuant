* BQ25170 behavioural charger model (datasheet SLUSF19) — ngspice-47 port
* Pins: IN OUT GND TS STAT PG ;  monitors IMON (A), DONE (1 V = terminated)
* ngspice-47 fails inside B-source conditionals on (a) V(x)-vs-V(y) or
* expression comparisons and (b) V() of nodes driven only by E/B sources
* ("bad node type 10").  Internal nodes here are realised through
* G-source + 1 ohm or a state capacitor — plain conductance-matrix nodes —
* and every comparison is V(node)>const on such nodes.
* Functionally identical to bq25170.inc.
.subckt BQ25170_BHV IN OUT GND TS STAT PG IMON DONE PARAMS: VREG=4.2 ICHG=0.2 ITERM=0.02

Gts  GND TS VALUE={38u}
Bcl  TS GND I = (V(TS)>2.6) ? (V(TS)-2.6)*100 : 0

* V(N_VO) = V(IN)-V(OUT)   (G-source difference -> real node)
Gvio N_VO 0 OUT IN 1
Rvo  N_VO 0 1

* V(N_TAP) = taper current A   (G-source -> real node)
Gtap 0 N_TAP VALUE = { MIN( {ICHG}, MAX(0,( {VREG}-V(OUT) )/0.005) ) }
Rtap N_TAP 0 1

* charger output current on a real node: V(N_IO)=I(Vmon)
Gio  0 N_IO VALUE = { I(Vmon) }
Rio  N_IO 0 1

* DONE latch: Cdone holds state on N_DONE (real node)
*   terminate when OUT near VREG and measured OUT current < ITERM
Cdone N_DONE GND 1n IC=0
Blatch GND N_DONE I =
+ ( ( V(OUT)>{VREG}-0.005 ) * ( V(N_IO)<{ITERM} ) ) ? 1u :
+ ( -10u * ( (V(OUT)<{VREG}-0.1) + (V(IN)<3.0) + (V(TS)>1.5) ) )
Edone DONE GND VALUE = { V(N_DONE) }

Bchg IN N_CG I =
+ ( (V(IN)>3.05) * (V(N_VO)>0.135) * (V(TS)>0.06) * (V(TS)<1.04) * (V(N_DONE)<0.5) )
+   ? ( V(OUT)<2.2 ? 0.016
+     : V(OUT)<2.8 ? 0.2*{ICHG}
+     : MIN( {ICHG}, MAX(0,( {VREG}-V(OUT) )/0.005 ) ) )
+   : 0
Vmon N_CG OUT DC 0
Brep IMON GND V = { I(Vmon) }

Rdum1 N_DONE GND 1e12
Rdum2 N_TAP  GND 1e12
Rdum3 N_VO   GND 1e12

* STAT/PG open-drain behavioural outputs omitted from sim (monitored via
* DONE/IMON).  STAT low while charging; PG low while VIN valid.
.ends BQ25170_BHV
