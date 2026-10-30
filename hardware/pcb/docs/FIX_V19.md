# fix_v19 — minimal corrections to fix_v18

> **Status note (fix_v24, 2026-10-25):** this document describes an earlier
> revision. Current board state: D10 = Nichia NF2W757G-F1 (consigned, no
> LCSC), U14 = LSM6DSV320XTR (LCSC C26633007, same LGA-14 land), J12 /
> U24 / U25 are **fitted** (in the v24 JLC BOM/CPL), and the dead MX-ECG
> chain D29 / D30 / R118 / R119 is DNP. See `../reports/CHANGES_V24.md`.

This file lists every delta vs fix_v18. Copper (tracks, vias, pads, zones)
is frozen: see `sims/fullsim_v19/copper_freeze/` for the object-level diff
proof (zero track/via/zone/pad-geometry changes).

## Change 1 — D10 BOM/footprint reconciliation (carried from first v19 pass)

Footprint `vitalquant:NF2W757G` was drawn per **Nichia STS-DA7-7098** land.
BOM/schematic ordered Cree JB3030UWT-B (C17610390), whose recommended land
differs. Fix: ordered part = **Nichia NF2W757G-F1 (Optisolis)** — the part
the land was designed for. Value/LCSC updated in sch + BOM + CPL +
`vitalquant.pretty/NF2W757G.kicad_mod` properties; pads untouched.

## Change 2 — R61 fitted as battery/board NTC (this run)

**Defect:** BQ25170 `TS` needs an NTC to ground (datasheet SLUSDJ8A §6.4:
"Connect an NTC directly from TS to GND (AT103-2 recommended)"). R61 was
DNP, so with the planned **2-wire protected LiPo** (J2 pin 3 open) TS
floats → clamps ~2.6 V > V_COLD → charger never charges.

**Fix (same-footprint part swap, zero copper):** R61 fitted as
**Murata NCP15XH103F03RC** — 10 kΩ ±1 %, B25/50 = 3380 K, 0402, LCSC
**C77131**. Board-mounted beside the J2 cell pads it provides the required
TS impedance. Verified TS window vs datasheet SLUSDJ8A §6.5
(I_TS_BIAS = 36.5–39.5 µA):

| Threshold | V_TS spec | R_NTC trip | Temp on B3380 NTC |
|---|---|---|---|
| V_COLD enter | 0.99–1.09 V | >25.1–28.6 kΩ | ≈ −1…+2 °C |
| Cold exit | 0.83–0.93 V | <21.0–24.4 kΩ | ≈ +3…+6 °C |
| V_HOT enter | 176–200 mV | <4.5–5.5 kΩ | ≈ 43…48 °C |
| Hot exit | 208–232 mV | >5.3–6.4 kΩ | ≈ 37…42 °C |
| V_TS_ENZ disable | <40–60 mV | <1.0–1.6 kΩ | ≈ >87 °C (or Q2 pull) |
| V_TS_EN enable | >65–85 mV | >1.6–2.3 kΩ | hysteresis |
| V_TS_CLAMP open | 2.3–2.9 V | TS open | suspend |

25 °C operating point: V_TS ≈ 38 µA × 10 kΩ = **0.38 V** — mid-window.
Q2 (CHG_DIS) still overrides by pulling TS < 50 mV. Board-side sensing
adequacy reported in FULLSIM_V19_REPORT.md (senses board, not cell core).

**Files:** `vitalquant_v2.kicad_sch` (dnp no, Value `NTC 10k B3380`, LCSC
`C77131`), `vitalquant_v2.kicad_pcb` (R61 attr `dnp` removed, Value/LCSC
properties — no copper), `fab/BOM.csv`, `fab/BOM_JLC.csv` (+1 row),
`fab/CPL_JLC.csv` (+1 row, 251 placements), `fab/ASSEMBLY_CHECKS.json`,
`audit_components.csv`, `FIRMWARE_NOTES.md` §12.

## Change 3 — D10 / D12 procurement resolution

- **D10 NF2W757G-F1** → **consigned**. No LCSC line exists for the Nichia
  757G land; stocked 3030s compared pad-for-pad (Cree JB3030 C17610390,
  Luminus MP-3030 C17455377 — both need symmetric 0.55 mm-gap lands,
  neither matches the drawn asymmetric Nichia land). See report §3.
- **D12 XL0603NRC730** (Sealand, 0603, 730 nm) → **consigned**. LCSC 0603
  IR emitters are 850/940 nm only; no 700–770 nm emitter ≤0603 exists at
  any catalogued distributor. Land is standard `LED_0603_1608Metric`.

`fab/ASSEMBLY_CHECKS.json` updated: 251 placed, consigned = D10, D12.
U12 MLX90632SLD-DCB-100-SP is a JLC extended part (C5349491) — listed in
BOM_JLC + CPL, NOT consigned.

## Change 4 — BOM_JLC LCSC-code verification sweep (this run)

Independent code-by-code check of every `fab/BOM_JLC.csv` line against
JLCPCB partdetail + LCSC wmsc detail API. **22 of 82 BOM groups carried
wrong or nonexistent LCSC codes inherited from fix_v18** — ordering would
have placed wrong values/packages (or failed outright) on: charge-ISET
R3 (147 Ω instead of 1.5 k), charge-VSET R75 (33 k instead of 27 k), ECG
input-bias R26–29 (107 Ω instead of 10 M), skin-path R118–122 (4.3 k/1206
instead of 51 k/2512), feedback R23 (0201/1.8 k instead of 560 k), R15–17
(681 k instead of 1 M), R18 (1.5 k instead of 200 k), R47/R109 (18 Ω
instead of 100 Ω), R12 (68 Ω array instead of 10 Ω), C1/C47 (88.7 Ω
resistor code on a 4.7 µF cap line!), C85 (681 Ω code on 47 µF line),
C46 (a 5-pin terminal-block code on a 2.2 µF cap line!), C63/C79 (100 nF
instead of 1 µF), C9 (4.7 µF instead of 10 µF), C65 (160 pF instead of
1.5 nF) plus 7 codes that resolve to nothing at LCSC (C23677, C15115,
C23692, C23691, C22299, C22314, C17275).

All replaced with verified in-stock JLC parts matching value, package,
tolerance and voltage rating (see `sims/fullsim_v19/COMPONENT_AUDIT_V19.csv`
for per-row evidence). The two 51 k 2512 rows were unified on
**C6669155 RV2512FK-0751KL** (Yageo HV 1 W 1 %) — higher working voltage
for the patient-contact paths, and C146900 had only ~31 pcs stock for
18 required. No schematic or copper change; values unchanged.
