* R2: I2C rise time 4.7k vs 2.2k pullup, worst bus C=270pF, rail 3.3V
* tr spec = time 0.3VDD(0.99V) -> 0.7VDD(2.31V); open-drain releases at t=10n
V33 pu 0 3.3
V18 pu18 0 1.8
R47 pu b47 4.7k
R22 pu b22 2.2k
R18 pu18 b18 2.2k
C47 b47 0 270p IC=0.4
C22 b22 0 270p IC=0.4
C18 b18 0 80p IC=0.3
.control
tran 10n 3u uic
meas tran tr47 trig v(b47) val=0.99 rise=1 targ v(b47) val=2.31 rise=1
meas tran tr22 trig v(b22) val=0.99 rise=1 targ v(b22) val=2.31 rise=1
meas tran tr18 trig v(b18) val=0.54 rise=1 targ v(b18) val=1.26 rise=1
print tr47 tr22 tr18
.endc
.end
