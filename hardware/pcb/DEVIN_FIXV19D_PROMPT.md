# Devin prompt — VitalQ fixv19d: four precision resistor substitutions

Copy everything below the line into Devin.

---

Last BOM revision. Four lines. Copper frozen, gerbers untouched.

## The four rows

All four are same-value, same-package, same-or-better tolerance. No design change - sourcing only. I verified every candidate against both the JLC component API and the wmsc detail endpoint today.

**R43, R45 — C283954, 5.11M ±1% 0402, stock 0.**
Replace with **C166448** (RTT025114FTH, 5.11MΩ ±1% 62.5mW 0402, JLC stock 7,449 / wmsc 7,400).
R43 and R45 are the ADS1292R input bias pair (RLDOUT to AFE_INP and AFE_INM). They must stay on the **same** part number so the two legs match. Do not split them across codes.
Backup if C166448 runs dry: C368375 (WR04W5114FTL, 4,600).

**R63 — C853138, 22.1k ±0.1% 0402, stock 0.**
Replace with **C163478** (RC0402FR-0722K1L, 22.1kΩ ±1% 62.5mW 0402, wmsc stock 1,724,900).
This is a deliberate tolerance downgrade from ±0.1% to ±1% and it is acceptable, for a specific reason: R63 is the upper leg of the CP2102N VBUS divider with R64 (47.5k, ±1%). The divider ratio is already bounded by R64's ±1%, so a 0.1% R63 buys nothing. At 5 V VBUS the divider sits at 3.41 V, and ±1% on R63 moves it by ~34 mV. Record that reasoning in `FAB_NOTES.md` so nobody later reads the downgrade as an oversight.
Backup: C43473 (0402WGF2212TCE, 17,100).

**R64 — C25896, 47.5k ±1% 0402, stock 0.**
Replace with **C100449** (CR0402FF4752G, 47.5kΩ ±1% 62.5mW 0402, wmsc stock 17,700).
Backup: C137974 (RC0402FR-0747K5L, 6,800).

**R66 — C6097400, 301k ±1% 0402, stock 0.**
Replace with **C102962** (RTT023013FTH, 301kΩ ±1% 62.5mW 0402, wmsc stock 10,000).
This one is a 6 µA bleeder from +1V8 to GND, so tolerance is not load-bearing, but keep ±1% anyway.
Backup: C172047 (WR04X3013FTL, 9,800).

## Rules

- Change only these four lines in `fab/BOM_JLC.csv`. Values, designators, quantities and packages do not move.
- Note each substitution in `FAB_NOTES.md` with the old code, new code, MPN, stock figure, and the reason (original out of stock at wmsc). Note the R63 tolerance reasoning specifically.
- Add the four spare recommendations below to `FAB_NOTES.md` as an assembly note.
- Produce `fab/VitalQ_v2_fixv19d_fabrication.zip`. It supersedes fixv19c and is the file I order. Preserve fixv19c untouched.
- Do not modify the pcb, schematic, gerbers, drills, CPLs or positions file. Prove it: every gerber and drill member byte-identical to fixv19a, sha256 in the commit message.
- Do not touch the six extended parts that show wmsc-0 (ESP32-S3, LSM6DSV80X, W25Q512, RV-3028, SHT45, IM69D130). Those are JLC-sourced, not LCSC-catalogue, and are confirmed at BOM upload. Leave them exactly as they are.
- Do not change J12, D10, D12 consignment.

## Spare-parts note to add

The thin-stock AFE parts are the ones that die during bring-up. Add to `FAB_NOTES.md`: source two extra of each of U7 AD5940 (15 in stock), U8 ADS1292R (16), U22 MAX86141 (84), U16 SFH7072 (39) and U12 MLX90632 (3) when placing the LCSC order. One dead AFE should not cost three weeks of waiting for a reel.

## Done when

- Four lines changed, all other rows identical.
- `fixv19d` zip exists, gerbers and drills byte-identical to fixv19a by hash.
- A four-line summary: the four part numbers now in the BOM, and confirmation that nothing else moved.