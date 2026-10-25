# F_Paste Hand-Patch — U15 (TPS61240YFFR, DSBGA-6) Paste Apertures

**File patched:** `gerbers_patched/vitalquant_v2-F_Paste.gbr`
(extracted from `VitalQuant_v2_fixv19_fabrication.zip`; original archive untouched)
**Companion file:** `gerbers_patched/vitalquant_v2-B_Paste.gbr` — extracted UNCHANGED
(md5 `744185e28e9df646e81aaf3875f5bd95`, identical to archive member)

## Defect

U15's six Ø0.23 mm BGA pads had **no flashed paste aperture** in the shipped
F_Paste gerber. The only U15 paste present was six 0.15 × 0.15 mm `fp_rect`
regions drawn with aperture D10 + G36/G37 fills (gerber lines 163–242,
inside `%TO.C,U15*%`) — undersized squares covering only ~54 % of each
pad's area (0.0225 mm² vs. 0.0415 mm² for Ø0.23). That is below standard
WLP stencil coverage (~80–90 %) and risks insufficient solder / opens.

The patch adds proper Ø0.21 mm circular flashes centered exactly on the six
pads. The existing 0.15 mm squares union into the flashes harmlessly
(their corners poke out of the Ø0.21 circle by ~1 µm, adding <1 % extra area).

## Source data (verified twice)

From `hardware/pcb/vitalquant_v2.kicad_pcb` (lines 28489–28829):

- Footprint `vitalquant:TPS61240_YFF`, layer F.Cu
- `(at 40.797369 38.824994)` — **no rotation token → rot = 0°**
  (cross-check: `positions_kicad.csv` lists U15 at 40.797369 / −38.824994 / rot 0.000000, top)
- Pads (smd circle, size 0.23 × 0.23, `pad_prop_bga`):

| Pad | Offset (mm)  | Net       |
|-----|--------------|-----------|
| A1  | (−0.2, −0.4) | VBAT_SYS  |
| A2  | (+0.2, −0.4) | GND       |
| B1  | (−0.2,  0.0) | TX_SW     |
| B2  | (+0.2,  0.0) | TX_5V_RAW |
| C1  | (−0.2, +0.4) | TX5_EN    |
| C2  | (+0.2, +0.4) | TX_5V_RAW |

## Computed flash positions

board = fp_origin + R(0°)·offset;  gerber = (board_x, −board_y);  FS 4.6 mm
(⇒ integer = mm × 10⁶, e.g. X40597369 = 40.597369 mm)

| Pad | Board (mm)           | Gerber (mm)            | Flash line                    |
|-----|----------------------|------------------------|-------------------------------|
| A1  | 40.597369, 38.424994 | 40.597369, −38.424994  | `X40597369Y-38424994D03*`     |
| A2  | 40.997369, 38.424994 | 40.997369, −38.424994  | `X40997369Y-38424994D03*`     |
| B1  | 40.597369, 38.824994 | 40.597369, −38.824994  | `X40597369Y-38824994D03*`     |
| B2  | 40.997369, 38.824994 | 40.997369, −38.824994  | `X40997369Y-38824994D03*`     |
| C1  | 40.597369, 39.224994 | 40.597369, −39.224994  | `X40597369Y-39224994D03*`     |
| C2  | 40.997369, 39.224994 | 40.997369, −39.224994  | `X40997369Y-39224994D03*`     |

**Independent cross-check vs. copper layer:** `gerbers/vitalquant_v2-Top Layer.gbr`
flashes U15's copper pads (`%TO.P,U15,<pad>*%` + `%TO.N,<net>*%`) at **byte-identical
X/Y coordinates** for all six pads (`D65*` select, `%ADD65C,0.230000*%`), with net
names matching the pcb pads above. Paste flashes now land exactly on the copper pads.

## Aperture chosen

`%ADD84C,0.210000*%` — new D-code **D84** (file used D10–D83; D55 suggested in the
work order was already taken, next free is D84).

- Ø0.21 mm on Ø0.23 mm pads = 8.7 % linear / ~17 % area reduction — standard
  conservative stencil aperture for WLP/DSBGA (≈10 % linear reduction).
- Circle (`C`) matches the circular pad shape, unlike the pre-existing square regions.
- Combined with the pre-existing 0.15 mm squares, effective coverage ≈ 84 % of pad area.

## Changes applied (additions only — nothing existing modified)

```
161a162
> %ADD84C,0.210000*%           (inside aperture list, before "G04 APERTURE END LIST*")
1773a1775,1781
> D84*                          (aperture select — file's convention: bare "Dnn*" line)
> X40597369Y-38424994D03*       A1
> X40997369Y-38424994D03*       A2
> X40597369Y-38824994D03*       B1
> X40997369Y-38824994D03*       B2
> X40597369Y-39224994D03*       C1
> X40997369Y-39224994D03*       C2
```

Diff vs. original: **8 lines added, 0 removed/changed** — exactly one AD line,
one D-code select, six flashes. (The added flashes sit after the final `%TD*%`,
directly before `M02*`; they are intentionally not wrapped in a second
`%TO.C,U15*%` block to keep the diff to the specified lines — the existing U15
block at lines 163–242 already carries the component attribution.)

## Validation output (re-parse of patched file)

```
lines=1782  apertures=75 D10-D84  flashes=792
parse errors: 0
FS4.6 range violations: 0
D84 flashes: 6, all exactly on computed pad centers: True
max deviation: 0.000 um (limit 50um)
RESULT: PASS
```

- Flash count: **792** = original 786 + 6 ✓
- All 6 new flashes within ±0.05 mm of computed positions (deviation 0.000 µm) ✓
- `%ADD84` (line 162) precedes `D84*` select (line 1775) precedes flashes (1776–1781) ✓
- `M02*` remains the last line; every pad hit exactly once (A1,A2,B1,B2,C1,C2) ✓
- Coordinate format FSLAX46Y46 consistent; aperture defined before use ✓
- B_Paste untouched: md5 `744185e28e9df646e81aaf3875f5bd95` = archive member ✓

## Note for downstream stage

The original zip is unmodified. If repacking `gerbers_patched/`, also note the
pre-existing U15 0.15 mm square paste regions (lines 164–243 of patched file)
remain in place — intentional per "additions only" constraint. If the assembler
prefers apertures with component attributes, the six flashes can equivalently be
wrapped in `%TO.C,U15*%` … `%TD*%` — left as-is here to keep the minimal diff.
