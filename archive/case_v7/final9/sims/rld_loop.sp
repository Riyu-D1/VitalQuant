* S2: RLD (right-leg-drive) loop stability + bias paths
* ADS1292 RLD amp modelled as op-amp: GBWP ~ 1 MHz, open-loop gain 100 dB
* Loop: RLDOUT -> R69 10k -> RLD_CLAMP -> R34 51k -> RLD_PAD -> body -> IN2 electrodes
* Feedback: body CM picked at RLDIN via 51k electrode returns (simplified single path)
* R15/R16 1M divider -> RLDREF = 1.65 V ; C66 1u decap
* R17 1M + C65 1.5n = integrator feedback on RLDINV

* RLD amplifier: single-pole model, Aol=1e5, fpole=10Hz -> GBWP=1MHz
.subckt RLDAMP inp inn out
e1 n1 0 inp inn 1e5
r1 n1 out 1k
c1 out 0 1.59n      ; dominant pole ~10 Hz into 1k -> GBWP ~1 MHz
.ends RLDAMP

* Body: RLD electrode to "patient common" (INx electrodes ganged)
* body impedance ~ 10k series + stray 100p, plus electrode 51k each side
Vref RLDREF 0 DC 1.65 AC 0
C66 RLDREF 0 1u

X1 RLDREF RLDINV RLDOUT RLDAMP
R17 RLDOUT RLDINV 1meg           ; integrator R (netlist R17)
C65 RLDOUT RLDINV 1.5n           ; integrator C (netlist C65)
R69 RLDOUT RLD_CLAMP 10k         ; series R (R69)
R34 RLD_CLAMP RLD_PAD 51k        ; electrode R (R34)
* body path: RLD_PAD -> body -> IN2P/IN2N electrode impedance
Rela RLD_PAD body 51k            ; RLD electrode contact
Rbody body pat_cm 10k            ; torso
Cbody pat_cm 0 200p              ; patient stray C
Relb pat_cm ecg_p 51k            ; ECG electrode contact
Relc pat_cm ecg_n 51k
Cstray ecg_p 0 100p
Cstray2 ecg_n 0 100p
* CM sense back to RLDINV through input lead resistors (simplified: one path)
Rfb1 ecg_p RLDIN_p 51k
Rfb2 ecg_n RLDIN_n 51k
* RLDIN averages P and N via 1M each into summing node RLDINV
Ravg1 RLDIN_p RLDINV 1meg
Ravg2 RLDIN_n RLDINV 1meg

* inject disturbance: 50 Hz CM interference at body via 1p source cap
Vcm cm_src 0 DC 0 AC 1 SIN(0 0.1 50)
Cinj cm_src pat_cm 1p

* ---------- AFE4900 ECG bias via R43/R45 5.11M (DC path only) ----------
* RLDOUT -> 5.11M -> AFE_INP / AFE_INM  (DC bias set point)
Rb_a RLDOUT AFE_INP 5.11meg
Rb_b RLDOUT AFE_INM 5.11meg
Cinp_a AFE_INP 0 100p            ; AFE input stray + ESD

.control
* open-loop-ish Bode: inject at feedback node, measure return ratio approx
ac dec 40 0.1 10meg
* closed-loop: CM rejection at pat_cm
wrdata ../simout/s2_cm.csv vdb(pat_cm) frequency
wrdata ../simout/s2_rldout.csv vdb(RLDOUT) frequency
meas ac cm_50 find vdb(pat_cm) at=50
meas ac cm_10k find vdb(pat_cm) at=10k
meas ac rld_g find vdb(RLDOUT) at=50
meas ac rld_peak max vdb(RLDOUT)
meas ac rld_pp pp vdb(RLDOUT)
tran 1u 100m
wrdata ../simout/s2_tran.csv v(pat_cm) v(RLDOUT) time
print cm_50 cm_10k rld_g rld_peak rld_pp
.endc
.end
