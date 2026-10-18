* R1-L2: status LED D25 (KT-0603G) through R88=150R at 3V3 (fix_v5)
* also fix_v4 case R88=1k for comparison
.model DGRN D IS=1e-19 N=3.1 RS=3
V3 n33 0 3.3
R88  n33 led 150
D25  led k  DGRN
Vs   k 0 0
V3b n33b 0 3.3
R88b n33b ledb 1000
D25b ledb kb DGRN
Vsb  kb 0 0
.control
op
print i(vs) v(led) i(vsb) v(ledb)
.endc
.end
