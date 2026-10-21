# DELTA_V22 — geometry changes vs case_v8/geometry.json (fix_v19 reference)

Extracted from `fix_v22/vitalq_v2.kicad_pcb` (pcbnew, /tmp copy — source untouched)
on the v10 pipeline: `extract_geometry.py` → `geometry.json`,
`parse_vrml.py` (KiCad 10 root-scale patch) → `vrml_boxes.json`,
`merge_heights.py` (board-centre remap patch) → merged heights.

All values **checked** by the extraction unless marked.

## Board outline

| item | v19 (case_v8 ref) | v22 | d |
|---|---|---|---|
| bbox | −2.05…48.05 × −10.55…64.55 | −2.075…53.65 × −16.575…64.55 | +5.62 x, −6.0 y |
| north antenna tab | none | x −2.05…22.5, y −16.55…−10.5 | new |
| east power tab | none | x 48.0…53.6, y 20.0…29.9 | new |
| stack-up | 1.6 nominal | 1.5386 real / 1.6 worst | same rule |

## Moved parts affecting case features

| ref | dx | dy | new pos | case consequence |
|---|---|---|---|---|
| J1 USB-C | **+3.500** | 0 | (29.5, −7.8) | window centre 26→29.5; 12.4 mm grip corridor x 23.3–35.7 must stay empty; dust-flap anchor and −y snap beads were inside it → relocated |
| U1 WROOM | −0.900 | −6.000 | (11.5, −6.5) | module body to y −22.31, overhangs board edge −16.55 by 5.8 → antenna bulge must reach inner face y ≤ −23; keep-out zone x −2.5…25.5, y −23…−10.5 |
| C64 | −20.496 | −9.779 | (1.45, −11.3) | inside bulge footprint → thin-shell only zone |
| C7, C8 | ≈−2 | ≈−14 | (−0.4…0.6, −14…−11.4) | same — inside bulge/keep-out |
| C63 | +2.268 | −8.056 | (28.6, 3.9) | VBUS cap moved near J1 |
| R2 | −4.774 | +3.813 | (27.55, −1.4) | J1-move ripple |
| R26 | −2.246 | +2.267 | (32.0, −1.9) | J1-move ripple |
| TP11 | +2.294 | 0 | (36.3, −8.474) | east of J1, near corridor edge |
| TP20 | +0.586 | +1.336 | (34.3, −0.4) | J1-move ripple |
| D33, Q6, R123 | — | — | east tab | new on east power tab → tab-shaped cavity needed |

## Unchanged case-relevant items (checked)

- Mounting holes H1 (0.6, −2.188) and H2 (−0.15, −7.184) — same as v19/v21.
- All §3 aperture parts (J3/J5/J6/J7/J9/J10/J11/J12/J13, U10–U13, U16, U20,
  U23, U25, D10–D12, D25, SW1/SW2, J8, J2) at their v19 positions within
  the tolerance band — aperture coordinates re-derived from v22 pads anyway.
- J1 model3d bbox (VRML): x 25.03…33.97, y −11.45…−3.55, h 3.25.
- U1 courtyard: x 2.5…20.5, y −22.55…4.05; module height 3.10 (datasheet,
  no VRML model assigned).
