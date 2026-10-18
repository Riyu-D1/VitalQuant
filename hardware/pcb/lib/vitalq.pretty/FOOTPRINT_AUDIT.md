# hw_v2 Footprint Audit — `hardware/pcb/lib/vitalq.pretty/`

Audit of provisional custom footprints against manufacturer-recommended land
patterns. Branch `hw_v2`. Scope is limited to this directory; **no
schematic-side files (`design.py`, `build_pcb.py`, netlist, or board files)
were modified.**

Status legend:

- **VERIFIED** — geometry matches a manufacturer document/official library.
- **FIXED** — was wrong or provisional; corrected from an authoritative source.
- **UNVERIFIED** — no manufacturer land data could be obtained; geometry kept
  (not guessed) or is a design intent value with no datasheet to check against.

Validation: the entire library was parsed and re-saved cleanly by
`kicad-cli fp upgrade` (KiCad 10.0.4) with no errors, and every file passes a
balanced S-expression check.

---

## 1. MAX86178_WLP49.kicad_mod — MAX86178 (U22) — **FIXED** (ball-function map UNVERIFIED)

Sources:

- https://www.analog.com/en/products/max86178.html
- https://mds.analog.com/api/public/content/21-100400.pdf (package outline N492A2+1)
- https://vendor.ultralibrarian.com/adi/embedded?vdrPN=MAX86178ENJ%2B

Package (decoded from the Analog Devices outline drawing): 49-bump thin WLP,
7×7 array, **0.35 mm pitch**, body 2.568 × 2.768 mm, bump Ø ≈ 0.21 mm, A1–G7
naming, pin-1 corner index.

| Item | Was (provisional) | Now |
|---|---|---|
| Pitch | 0.40 mm (pads ±1.2 mm) | **0.35 mm (pads ±1.05 mm)** |
| Pad | Ø0.22 mm circle | Ø0.20 mm circle (WLP mask-defined land) |
| Courtyard | none | 2.9 × 3.1 mm, F.CrtYd + B.CrtYd |
| Pin-1 | none | chamfered silk + fab corner at A1 |
| Fab body | generic | 2.568 × 2.768 mm |

Pad names A1–G7 preserved for netlist compatibility with `design.py`.

**UNVERIFIED:** the ball *function* map. The public package outline gives only
mechanical data; the A1–G7 → signal mapping in the schematic comes from the
MAX86178 documentation set (register/pin table is effectively NDA-gated). The
schematic's use of A1–G7 names is consistent with the package naming
convention, but each ball's electrical function cannot be confirmed from
public sources — flagged in the footprint `descr`.

## 1b. MAX86141_WLP20.kicad_mod — MAX86141 (U22, hw_v2.1) — **NEW**

Sources:

- https://www.analog.com/en/products/max86141.html
- Analog Devices package outline 21-100134 (package code N201A2+1)

Package: 20-bump WLP, 5 columns × 4 rows, **0.4 mm pitch**, body
2.048 × 1.848 mm, pads A1–D5 (bumps-down view, A1 top-left). Round NSMD
lands Ø0.24 mm (within the suggested 0.22–0.25 mm window). Courtyard
2.3 × 2.1 mm, fab body outline, silk pin-1 mark at the A1 corner.

Bump map (per the 21-100134/N201A2+1 coordinate table): A1 VLED,
A2 SCLK, A3 SDO, A4 SDI, A5 CSB; B1 LED3_DRV, B2 INT, B3 GPIO1,
B4 GPIO2, B5 VREF; C1 LED2_DRV, C2 VDD_DIG, C3 GND_DIG, C4 GND_ANA,
C5 PD_GND; D1 LED1_DRV, D2 VDD_ANA, D3 PGND, D4 PD2_IN, D5 PD1_IN.

Note: on the board U22 sits on B.Cu rotated 180° — the embedded board
copy mirrors the library pad Y coordinates, exactly as the previous
WLP49 embedding did. Fanout: outer balls escape on the surface; inner
balls (B2–B4, C2–C4) need via-in-pad or escape routing (~0.15 mm drill /
0.25–0.3 mm pad, or ≤0.1 mm traces if rules allow) — flagged for
Quilter fanout.

## 2. ESP32_S3_MINI_1U.kicad_mod — ESP32-S3-MINI-1U — **VERIFIED/FIXED**

Sources:

- https://documentation.espressif.com/esp32-s3-mini-1_mini-1u_datasheet_en.pdf
- https://github.com/espressif/kicad-libraries (official Espressif footprint)

Official land: 60 peripheral pads on 0.85 mm pitch (0.4 × 0.8 mm), central
heatsink implemented as **nine pads sharing number 61** (≈3×3 grid of 1.2 mm
lands, pad-61 corner cell chamfered for pin 1), four corner pads 62–65,
courtyard ≈16 × 16 mm. **There is no pad 66 in the official footprint.**

| Item | Was | Now |
|---|---|---|
| Pads 1–60 | 0.85 mm pitch — correct | unchanged |
| Pad 61 | already 9-pad heatsink group | unchanged — matches official |
| Pads 62–65 | present | unchanged |
| Courtyard | 14.6 × 14.6 mm | **16 × 16 mm** on F.CrtYd, B.CrtYd added |
| Pad 66 | not present (correct) | — |

**Schematic-side discrepancy (not fixed — file is another agent's):**
`design.py` still carries a stale `EPAD 66` comment/reference for this module.
The footprint has pads 1–65 only. If the schematic symbol pins the EP as 66,
the EP net will not connect to the heatsink lands; the schematic symbol's EP
pin must be remapped to pad 61 (or to the intended pad 62–65 corner pads).

## 3. SHT45_DFN4.kicad_mod — Sensirion SHT45 — **FIXED**

Sources:

- https://sensirion.com/media/documents/33FD6951/67EB9032/HT_DS_Datasheet_SHT4x_5.pdf
- https://sensirion.com/products/catalog/SHT45?show_inventory=SHT45-AD1B-R2
- Official KiCad lib footprint
  `Sensor_Humidity:Sensirion_DFN-4_1.5x1.5mm_P0.8mm_SHT4x_NoCentralPad`

| Item | Was | Now |
|---|---|---|
| Pads | 0.30 × 0.50 mm (transposed) | **0.50 × 0.30 mm** at (±0.70, ±0.40) |
| Pitch | 0.8 mm — correct | unchanged |
| Central pad | none — correct (Sensirion: do *not* solder the die pad; heatsinking) | unchanged |
| Courtyard | none | 2.4 × 2.0 mm F.CrtYd + B.CrtYd |
| Pin-1 | none | silk indicator at pad 1 |

## 4. RV3028C7.kicad_mod — Micro Crystal RV-3028-C7 — **FIXED**

Sources:

- https://cdn.sparkfun.com/assets/6/8/2/b/3/RV-3028-C7_App-Manual.pdf
  (package dimensions + recommended solder pad layout)

Package SON-8, body 3.2 × 1.5 × 0.8 mm, 0.9 mm terminal pitch, metal lid tied
to VSS (pin 5), pin-1 index mark on the drawing.

| Item | Was | Now |
|---|---|---|
| Pads | 0.30 × 0.45 mm | **0.5 × 0.8 mm** (recommended land) |
| Pad centers | x = ±0.95/±0.32 (irregular) | x = ±0.45/±1.35 → 0.9 mm pitch |
| Rows | y = ±0.78 mm | y = ±0.60 mm (1.2 mm row spacing per layout fig.) |
| Courtyard | none | 3.7 × 2.5 mm F.CrtYd + B.CrtYd |
| Pin-1 | none | chamfered fab corner + silk notch at pad 1 |

Pad numbers preserved: top row 1–4 (pin 1 at −x/−y), bottom row 8,7,6,5.

## 5. IM69D130.kicad_mod — Infineon IM69D130 — **FIXED**

Sources:

- https://www.infineon.com/assets/row/public/documents/24/49/infineon-im69d130-datasheet-en.pdf
- https://www.infineon.com/part/IM69D130
- Official KiCad lib footprint `Sensor_Audio:Infineon_PG-LLGA-5-1`

Package PG-LLGA-5-1, 4.0 × 3.0 mm board land pattern, bottom-port PDM mic;
datasheet: PCB sound port Ø0.8 mm (must exceed mic port), SMD (solder-mask-
defined) pads. Pins: 1=DATA, 2=VDD, 3=CLOCK, 4=SELECT, 5=GND.

| Item | Was (provisional "VERIFY") | Now |
|---|---|---|
| Pads 1–4 | 0.80 × 0.50 mm at ±1.5/±1.0 mm | **0.45 × 0.70 mm** at (−1.5/−0.8, ±0.85) |
| Pad 5 (GND) | simple pad at (0, 1.35) | **annular ground ring** around the acoustic port (official custom pad) |
| Acoustic port | NPTH Ø1.0 mm at origin | **NPTH Ø0.8 mm at (+0.68, 0)** per datasheet |
| Paste | on pads | segmented official `F.Paste`-only apertures |
| Courtyard | none | 4.5 × 3.5 mm F.CrtYd + B.CrtYd |
| Pin-1 | none | silk corner mark + fab chamfer at pad 1 |

Pad numbers 1–5 preserved; the numeric pad→signal mapping (1=DATA … 5=GND)
matches the datasheet pin table.

## 6. Pads_1x02 / 1x03 / 1x04 / 1x06 — flex-tail pad rows — **UNVERIFIED** (pitch kept)

These are custom solder lands for flex tails — there is no manufacturer land
pattern to check against, so the geometry cannot be VERIFIED.

- Kept **2.2 mm pitch**, pad 1.15 × 1.70 mm — the value already documented in
  each file's `descr` and consistent with `design.py` (FP_PAD2/3/4/6 marked
  "VERIFY: tail pad land TBD").
- The earlier task note mentioned a "1.5 mm pitch" target. There is no drawing
  backing either value; 2.2 mm was kept rather than silently swapping one
  unsupported number for another. **Decision needed** from the tail/connector
  vendor or mechanical drawing before fab.
- Numbering 1..N left→right, unchanged (netlist-compatible).
- **Added** F.CrtYd + B.CrtYd courtyards (were absent): 3.9/6.1/8.3/12.7 × 2.2 mm.

---

## Summary

| Footprint | Status |
|---|---|
| MAX86141_WLP20 | NEW (hw_v2.1) — 20-bump WLP per outline 21-100134, 0.4 mm pitch, Ø0.24 mm NSMD pads, courtyard+pin-1 |
| MAX86178_WLP49 | FIXED (pitch 0.40→0.35 mm, pads Ø0.22→0.20 mm, courtyard+pin-1 added); ball function map UNVERIFIED (NDA); **superseded on board by MAX86141_WLP20 for U22** |
| ESP32_S3_MINI_1U | VERIFIED geometry; FIXED courtyard 14.6→16 mm; schematic-side "EPAD 66" stale — pads end at 65 |
| SHT45_DFN4 | FIXED (pad dims transposed → 0.50×0.30 mm, courtyard+pin-1 added) |
| RV3028C7 | FIXED (lands 0.30×0.45→0.5×0.8 mm, pitch normalized to 0.9 mm, courtyard+pin-1 added) |
| IM69D130 | FIXED (pads 1–4 → 0.45×0.70 mm, GND ring pad 5, port NPTH → Ø0.8 mm at (0.68,0), courtyard+pin-1 added) |
| Pads_1x02/03/04/06 | UNVERIFIED — 2.2 mm pitch kept pending tail drawing; courtyards added |

**No schematic-side, netlist, placement, or board files were changed.**
The only unresolved schematic-side items (documented, not edited):
`design.py` `EPAD 66` comment for the ESP32-S3-MINI-1U, and the "VERIFY: tail
pad land TBD" comments on FP_PAD2/3/4/6.
