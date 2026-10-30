* v18_pp.inc — fix_v18 charger load-sharing power path (ngspice-47 port)
* Q6 AO3401A P-ch MOSFET: pin1 G = VBUSN, pin2 S = VSYS, pin3 D = VBAT
*   body diode anode D(VBAT) -> cathode S(VSYS)
*   enhancement: ON when Vgs = V(VBUSN)-V(VSYS) < -1.2 V  -> Ron 0.05 ohm
*   gate control node realised via G-source + 1 ohm (real matrix node)
* D33 1N5819WS: anode VBUSN -> cathode VSYS
* R123 100k: VBUSN (gate net) -> GND
.model DSCH18 D(IS=3e-5 RS=0.08 N=1.1)         ; ~1N5819WS Vf ~0.25-0.45 V
.model DBODY18 D(IS=2e-9 RS=0.10 N=1.2)        ; AO3401A body diode Vf ~0.55 V
.model QMOD18 SW(VT=1.2 VH=0.1 RON=0.05 ROFF=1e9)
Gctl QCTL 0 VBUSN VSYS 1                       ; V(QCTL) = V(VSYS)-V(VBUSN) = -Vgs
Rctl QCTL 0 1
Sq6 VBAT VSYS QCTL 0 QMOD18
Dbody VBAT VSYS DBODY18
D33 VBUSN VSYS DSCH18
R123 VBUSN 0 100k
