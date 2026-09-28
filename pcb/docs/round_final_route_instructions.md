# FINAL routing round (VitalQ hw_v1). Start from local commit 487a418c.

This is the last automatic routing attempt. The owner approved it with a hard stop. Credits are limited, so do not polish anything until the board is fully connected.

## 0. Start clean
- `git status`. Any uncommitted work from the crashed previous run goes on a side branch (`git stash` or `git branch wip-route-6l`). Then work from 487a418c (or from a later local commit only if it is a strict improvement and passes ERC). State which one you used.

## 1. Diagnosis (already decided, do not re-investigate)
The two previous runs failed from placement congestion, not layer count: 245 parts in 36.5 mm width, BGAs under the ESP32 module, a big test-pad block. The last run also wasted its time length-matching USB while about 240 nets were still open. Do not repeat that.

## 2. Re-layout (decided, do not ask)
- Outline: rounded rectangle about 45 mm wide. Length as needed, target 60 mm or less, 70 mm maximum. The ESP32 antenna stays at an edge with the Espressif keep-out.
- Stack-up: JLC 6-layer 1.6 mm. L1 signal, L2 solid GND, L3 signal, L4 power pours plus signal, L5 solid GND, L6 signal and skin side. POFV via-in-pad allowed.
- Re-place in functional groups, each group with 1 to 1.5 mm of fan-out margin around it: (a) power (BQ25170, MAX17048, TPS63802, TPS7A2, XC6206, TPS61240 and its inductor) with tight switcher loops; (b) ESP32 plus USB-C J1, USBLC6 within 3 mm of J1, CP2102, buttons; (c) ECG and impedance AFEs (ADS1292R, AD5940, AFE4900) near their electrode connectors; (d) optical (SFH 7072, AS7341, MLX90632, TMP117) on the skin side, clustered; (e) the rest (IMU, BME, flash, TCA6408A).
- No fine-pitch BGA or CSP on either side under the ESP32 module footprint. No vias under module pads.
- Electrodes J5, J6 and J7 with their 51k DPCR, ESD and 1k chains in a separate zone. HV_ELECTRODE net class at 1.5 mm minimum, target 2.5 mm to unrelated nets, inner-layer keep-outs under those pads and traces. The connector pitch may change; the pin count and nets stay the same.
- Test points: 1.0 mm SMD pads with no paste, spread along free edges near their signals, not in a block. J8 Tag-Connect and both buttons reachable at an edge. C7 within 2 mm of ESP32 pin 2.
- Silk: 1.0 mm text height, 0.15 mm stroke, none over pads or the edge, back side mirrored.

## 3. Routing order, with a local commit after each block
1. Power nets and switcher loops (short, wide, done by script or by hand).
2. BGA and CSP fan-out (0.09/0.09 mm allowed only inside fan-out areas).
3. USB D+ and D- as a 90 ohm pair. Length-matching comes at the END only (step 6).
4. PPG photodiode pair with GND guard. Electrode lines under HV_ELECTRODE.
5. Freerouting for everything else. Discard any result with shorts. Fill zones.
6. Only when unconnected = 0: USB length match within 1 mm, silk clean-up, then final DRC.

## 4. HARD STOP (mandatory)
- After step 5, run DRC. If unconnected > 0 after ONE re-placement plus Freerouting retry, STOP. Do not keep trying.
- Also stop if you have spent the equivalent of roughly 2 hours of work without reaching 0 unconnected.
- When you stop: commit the best state locally, list the open nets by block, give the root cause, and say whether human interactive routing in KiCad would finish it (and roughly how many nets remain).
- Never tune lengths, silk or cosmetics while any net is open.

## 5. Checks and outputs (only if 0 unconnected)
- kicad-cli ERC, and DRC with `--schematic-parity --refill-zones` against JLC 6-layer rules plus HV_ELECTRODE. Report exact counts. Each accepted warning needs a reason.
- Regenerate jlc_gerbers.zip (including drill), jlc_bom.csv, jlc_cpl.csv (with JLC rotation corrections checked against pin 1), vitalq_hw_v1.pdf, renders, and VERIFICATION.md (6-layer, new size, the stack-up, net classes, the consigned-part list).
- Even when you stop early, still export the renders and VERIFICATION.md for the best state.

## 6. Rules
- Remove no parts. Change no part values. Commit locally only. DO NOT PUSH. Do not touch PR #3.
- Final report: name ONLY these artifact files: jlc_gerbers.zip, jlc_bom.csv, jlc_cpl.csv, vitalq_hw_v1.pdf, pcb-top.png, pcb-bottom.png, pcb-iso.png, VERIFICATION.md.
- Final report must state: the commit hash, the board size, the layer count, the ERC/DRC counts, and the unconnected count.
