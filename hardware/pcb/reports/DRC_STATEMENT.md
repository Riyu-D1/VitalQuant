# fix_v23 DRC statement — KiCad 10.0.4

Tool: `/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli` — **version 10.0.4**
(`kicad-cli version` verified 24 Oct 2026).

## Reviewer-equivalent baseline (fix_v23 inherited from fix_v22)

Command on a /tmp copy of the board with `fix_v19/vitalq_v2.kicad_pro` rules:

```
kicad-cli pcb drc --severity-error --schematic-parity --format json vitalq_v2.kicad_pcb
```

- **10 violations**, 0 unconnected, 0 schematic-parity issues:
  - 9 × `clearance`: actual 0.1000 mm vs required 0.1016 mm, **all between pads of
    the same footprint** — Q1 (pads ~ (3.01,50.06)/(2.66,50.06)/(2.83,50.46), nets
    IR_GATE/GND/IR_K), Q2 (~ (24.99,0.64)/(24.99,0.29)/(24.59,0.47),
    CHG_DIS/GND/TS), Q4 (~ (36.97,51.62)/(36.97,51.27)/(36.57,51.44),
    NIR730_GATE/GND/NIR_K).
  - 1 × `npth_inside_courtyard`: J8 (TC2030-IDC-NL) NPTH alignment hole at
    (28.99, 52.184) clipping U6 (AFE4900YZR, bottom, 27.2,50.8) courtyard.

Earlier "0 violations" runs used older KiCad and/or the auto-loaded
`vitalq_v2.kicad_dru` waiver file; the v22 docs claiming 0 were stale (checked —
fix_v22 board and repo main board are byte-identical, md5 3366e11b…).

## Fixes applied (real geometry/rule fixes)

### Q1/Q2/Q4 internal pad gaps — scoped rule, footprint kept

- Parts: **CSD13380F3T** (TI PicoStar/YJM0003A), LCSC **C2871092**,
  footprint `Texas_PicoStar_DFN-3_0.69x0.60mm` — the standard KiCad footprint,
  which reproduces TI's recommended land pattern with **0.100 mm pad-to-pad
  gaps** (checked against the package drawing).
- The 0.1016 mm requirement comes from the net-class clearance; no standard
  footprint for this part has a larger gap — the land pattern is dictated by the
  die-size package. Swapping footprints would be a wrong-part substitution.
- JLCPCB capabilities (jclpcb.com, checked 24 Oct 2026): multilayer minimum
  trace/space **0.09/0.09 mm**; local BGA capability 0.09 mm. The 0.100 mm gaps
  are manufacturable.
- Resolution: narrow custom rule in `fix_v23/vitalq_v2.kicad_dru`:

```scheme
(rule "Q1 Q2 Q4 internal pad clearance"
  (condition "A.Type == 'Pad' && B.Type == 'Pad' && A.Parent == B.Parent && (A.Parent == 'Q1' || A.Parent == 'Q2' || A.Parent == 'Q4')")
  (constraint clearance (min 0.09mm)))
```

  Scoped to pads **within the same footprint** of Q1, Q2, Q4 only. No global
  relaxation, no board/footprint geometry change, no waived inter-component
  check.

### J8 NPTH inside U6 courtyard — courtyard narrowed, J8 kept

- Measured: the offending NPTH edge sits ~0.005 mm inside U6's courtyard
  boundary but **0.265 mm clear of U6's actual package body outline**; the
  conflict is courtyard-only.
- Moving J8 was tested and rejected: candidate moves of +0.05…+0.75 mm in every
  direction produced 30–73 new violations (hole clearances against the dense
  via fan-out, shorting items, solder-mask bridges, unconnected pads) — the
  site was routed to ~0.2 mm margins on multiple layers. J1/USB-C grip
  corridor and the J8 keep-out notch around the fixed U6.E2 via also pin the
  site.
- Resolution: U6's courtyard polygon narrowed on the single grazed edge
  (relative x −1.30 → −1.15 mm) in both the embedded footprint and
  `vitalq.pretty/BGA30N40P5X6_260X210X50.kicad_mod`. No copper, pad, body or
  placement change; J8's electrical connections, keep-outs and the case access
  window alignment are untouched.

## Final results (fix_v23, KiCad 10.0.4)

| Check | Result |
|---|---|
| DRC `--severity-error --schematic-parity` | **0 violations / 0 unconnected / 0 parity** |
| ERC `--severity-error` | **0 violations** |
| `--severity-all` | 154 warnings, all warning severity, all pre-existing classes (52 lib-footprint issues, 5 lib-footprint mismatches, 15 silk-edge, 8 silk-over-copper, 3 silk-overlap, 9 text-height, 1 text-thickness, 59 dangling-track, 2 dangling-via). Baseline fix_v22 measured 164 incl. the 10 errors. |

Reports: `verify/drc_step4.json`, `verify/erc_v23.json`, `verify/drc_v23b.json`,
`verify/erc_v23b.json` (after schematic LCSC updates), plus severity-all output.
