# AFE-group PCB audit — findings (fix_v19 @ b9002188)

Scope: U6 AFE4900YZR · U7 AD5940BCBZ-RL7 · U8 ADS1292RIRSMT · U9 PCA9306DCUR · U22 MAX86141ENP+ · connectors J5/J6/J7/J9/J10/J11/J12/J13.
Sources: `reports/01_netlist_pinmap.csv`, `pinmap.json`, `audit_netlist.xml`, `audit_components.csv`, `vitalquant_v2.kicad_pcb` + `vitalquant.pretty/*`, fetched datasheets (see `datasheets/manifest_afe.csv`).

## Ball-map verification (highest-risk item — done first)

All three WLP/DSBGA ball maps were extracted from the footprints and checked against the datasheet
pin-configuration figures **including view convention** (datasheets draw "TOP VIEW, ball side down";
B.Cu-mounted parts must show A1 top-left when the board is viewed from the bottom, i.e. top-*right*
in pcbnew's through-board view — a single horizontal mirror is correct, any other pattern is a
dead-board defect).

| Part | Footprint | Grid | Orientation check | Result |
|------|-----------|------|-------------------|--------|
| U6 AFE4900 | `vitalquant:BGA30N40P5X6_260X210X50` (B.Cu) | 5×6 @0.4 mm, body 2.06×2.60 mm vs DS 2.065×2.565 (YZ0030) | pcbnew A1 at top-right → physical bottom view A1 top-left = SBAS857/SBAS861B top view | **VERIFIED** |
| U7 AD5940 | `vitalquant:BGA56C40P8X7_416X356X55` (F.Cu) | 8 cols×7 rows @0.4 mm, body 4.16×3.56 mm = CB-56-3 | local map identical to DS Fig.4 (A1 top-left, A–G, 1–8); all 56 mnemonics match Table 7 | **VERIFIED** |
| U22 MAX86141 | `vitalquant:MAX86141_WLP20` (B.Cu) | 5×4 @0.4 mm, DS body 2.05×1.85 mm (N201A2+1) | pcbnew A1 top-right → physical top-left = DS TOP VIEW (A1=VLED) | **VERIFIED** |

Name-level cross-check: every ball mnemonic on U6 (via SBAS857 pin-functions text recovered from
web mirrors — selective-disclosure part), all 56 on U7 (Table 7 verbatim), all 20 on U22 (pin
description table) match the schematic symbol pin names. No swapped rows/columns anywhere.

## BLOCKER

None.

## HIGH

1. **J13 "MX ECG pads" is dead hardware.** J13.1→R118 51 kΩ→`MX_ECG_INP`→D29 TVS→(nothing);
   J13.2→R119 51 kΩ→`MX_ECG_INM`→D30 TVS→(nothing). Both nets contain exactly one resistor and one
   TVS diode — no path to any AFE. This is the documented leftover of the MAX86178→MAX86141 swap
   (commit `31e72e61`: "MAX86178 ECG/BioZ channels dropped; D29/D30/R118/R119 left in place but now
   unused"). The connector is still fitted (audit_components.csv: J13 `Pads_1x02`, not DNP) and is
   labelled "MX ECG pads", so an integrator will attach ECG electrodes that can never produce data.
   Action: mark J13 DNP/dead in docs, or respin MX_ECG_INP/INM to a live input.

## MEDIUM

2. **U6 RX_SUP ≤ TX_SUP rule vs gated TX_5V.** SBAS857 §5 (C1): with CONTROL1=0 the allowed RX_SUP
   window is 2.0–3.6 V *with maximum ≤ TX_SUP*. Board: RX_SUP=+3V3_ANA (always-on), TX_SUP=TX_5V from
   the TPS61240 boost gated by TX5_EN (U19). Whenever the boost is off and the AFE is not in PWDN,
   RX_SUP (3.3 V) > TX_SUP (~0 V) — outside recommended operating conditions (abs-max is not
   violated). Mitigation exists and is required: RESETZ must stay low (>200 µs → PWDN, SBAS857 E1)
   while TX_5V is off. Firmware driver keeps RESETZ low by default via R9 and never owns TX5_EN —
   the constraint is FW-manageable but must be enforced at the led_sched/afe boundary
   (`vq_drv_afe4900.c` header notes confirm the split ownership).
3. **U22.D4 PD2_IN left floating.** MAX86140/41 DS pin table (p12): "PD2_IN — Photodiode Cathode
   Input. **Tie to PD_GND when this pin is not used.**" Board leaves D4 unconnected (correctly named
   PD2_IN on the symbol — it is *not* the MAX86140-only N.C. variant). Channel 2 is unused so this
   is benign, but it violates an explicit datasheet directive; fix is a one-net tie to PD_A.
4. **Mixed pad diameters inside single WLP footprints.**
   - U6: 21 pads ⌀0.23 mm (exactly the TI recommended land, SBAS861B p5, NSMD-preferred) **plus**
     9 pads ⌀0.25 mm (B2,B3,B4,C4,D2,D4,E2,E3,E4) with +0.025 mm mask margin → 0.30 mm mask
     openings → 0.10 mm mask web between pads = exactly `solder_mask_min_width 0.1` limit.
   - U7: 36 pads ⌀0.214 mm + 20 pads ⌀0.25 mm (+0.025 mm margin).
   - U22 is uniform ⌀0.24 mm. All diameters sit inside AN-1891's 200–260 µm acceptable window for
     0.4 mm pitch, but per-pad geometry inconsistency inside one footprint is a paste/mask
     uniformity defect class worth normalizing at the next respin (does not block fab).
5. **U8 CLK tied to GND.** With CLKSEL=1 the internal oscillator is used and CLK is a 3-state pin
   *only while* `CONFIG2.CLK_EN=0` (default, SBAS502C Table 9/CONFIG2). If firmware ever sets
   CLK_EN=1 the pin outputs the 512 kHz oscillator into a dead short to GND. Prefer leaving CLK
   unconnected (or a strap) — also preserves the daisy-chain clock-out option. FW-constraint, not a
   copper defect today.
6. **U8 AVDD/DVDD decap gap** (carried from R7, confirmed): only C28 100 nF local; nearest 1 µF
   (C80) ~11.5 mm. +3V3_ANA is plane-fed and VCAP1/VCAP2 handle the internal LDOs — accepted
   deviation, recorded.
7. **U6 RX_SUP decap gap** (carried from R6): SBAS857 asks 1 µF at C1; board has C79 100 nF @3.1 mm,
   nearest 1 µF ~37 mm. Accepted deviation on a 4×1 µF rail.
8. **J12 sweat lines tap the IC side of the J11 series resistors.** J12.11/12/13 land on
   `SWEAT_WE/RE/CE` = U7.C5/A2/A1 directly (with D26–D28 TVS). The 51 kΩ series (R120–R122) only
   exists on the J11 pads. If the flex-tail sweat electrodes are meant to use J12, they reach the
   AD5940 with TVS-only protection; if J11-or-J12 is a mutually-exclusive assembly option this is
   fine — needs a note in the assembly doc.
9. **U9 VREF2 filter cap.** SCPS113O §9.2.2.1: "A 100-pF filter capacitor connected to VREF2 is
   recommended" — not fitted. Same class as the R7 VREF1 decap gap (nearest +1V8 cap 18.4 mm).
   Both VREF pins are µA-bias nodes — cosmetic-to-minor.

## COSMETIC

10. Stale net names from the MAX86178 era: `CS_MAX86178`, `MAX86178_INT`, `MX_ECG_*` while U22 is
    MAX86141 — rename at next respin for doc hygiene.
11. U22 footprint courtyard/fab drawn 2.3×2.1 mm vs package 2.05×1.85 mm (generous outline).
12. U9: pin-7/pin-8 (VREF2/EN) names are correct in the schematic symbol (prior R7 note had them
    swapped in its table — the netlist is right: DCU pinout 7=VREF2, 8=EN per SCPS113O Fig.5-2).

## Confirmed-good highlights

- **PCA9306 topology textbook** (SCPS113O Fig.9-1): EN tied to VREF2 node, pulled to +3V3 through
  R18=200 kΩ; 2.2 kΩ pull-ups on *both* sides (R19/R20→3V3, R21/R22→1V8); side-1 ports face the
  1.8 V devices (U10, U12); J9/J12 tails expose the 3.3 V side — internally consistent.
- **U22 SPI domain**: only SDO (1.8 V swing) is level-shifted via U26 (VCCA=1V8→VCCB=3V3, OE on
  CS); SCLK/SDI/CSB/INT driven directly at 3.3 V — legal because those pins are −0.3…+6.0 V rated
  (DS abs-max p10) independent of VDD.
- **AD5940 DVDD-before-IOVDD** satisfied by R12 10 Ω + C19 1 µF feed (DS Table 7 G2 note).
- **Electrode protection topology consistent**: every skin-contact pad → 51 kΩ 2512 (pulse-rated
  package) → TVS (TPD1E10B06) at IC-adjacent node → AFE pin; AFE4900 ECG leg adds 100 kΩ + 100 nF
  AC coupling + 5.11 MΩ RLD bias from the shared ADS1292R RLDOUT.
- **ADS1292R**: CLKSEL=1 internal osc; START=GND (legal — START opcode mode, DS §SPI); PWDN/RESET
  default-low via R14 until U19 drives; VCAP1/VCAP2 1 µF, VREFP 10 µF, RLD integrator R17 1 M +
  C65 1.5 n (~106 Hz), RLDREF R15/R16 1 M divider — all match TI guidance.
- **Supply legality**: AFE4900 RX/IO_SUP=3.3 V (2.0–3.6 / ≤RX_SUP); TX_SUP=5.0 V (3.0–5.25);
  CONTROL1=0 (LDO on, correct for RX≥2.0 V); I2C_SPI_SEL=1 → SPI mode confirmed against SBAS857
  ("1 = SPI interface enabled") — resolves the polarity question flagged in the firmware header.
- **AD5940 decap set** matches the datasheet pin table exactly: VREF_1V82=4.7 µF, VREF_2V5=470 nF,
  VBIAS_CAP=470 nF, VBIAS0/VZERO0=100 nF, DVDD_REG=470 nF, AVDD_REG=1 µF, RCAL=1 kΩ fitted.
- AFE4900 ball **names** verified against SBAS857 §5 text (all 30); package outline verified
  against public SBAS861B — full parametric datasheet remains selective-disclosure (see manifest).

## UNVERIFIABLE items

- **AFE4900 full parametric datasheet** (SBAS857, 152 pp): TI distributes under NDA/selective
  disclosure. The public artefact is only the 8-page brief SBAS861B (fetched, sha256 in manifest).
  The pin *map* and pin *functions* were verified via SBAS857 text fragments on public mirrors
  (scribd/pdfcoffee) and agree with the prior R6 full-datasheet audit — but abs-max tables, timing
  specs and register maps could not be re-verified line-by-line this round. Where SBAS857-specific
  values mattered (pin functions, CONTROL1/RESETZ semantics, RX≤TX rule, decap asks) the mirror
  text was quoted verbatim in `03_matrix_afe.csv`.
- **SFH7072 ↔ AFE4900 PD polarity** relies on the R6 audit of the SFH7072 datasheet (on disk:
  `datasheets/SFH7072.pdf`) — cross-checked cathode→INP, anode→INM: consistent.
- J12 FPC tail cable pinout (mating-side numbering) — internally consistent on the board side;
  cable drawing not in repo.
