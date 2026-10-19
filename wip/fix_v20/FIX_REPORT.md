# fix_v9 — FIX_REPORT

**Scope:** owner-approved copper change on top of fix_v8 per `verify/PROMPT_FIX9.md`. J3's two FSR402 force-sensor contact pads (`+3V3`, `FSR_ADC`) moved from F.Cu (away from skin) to B.Cu (skin side). Only J3's two nets were rerouted; every non-J3 track, via, pad position and net assignment is identical to fix_v8 (verified diff below). fix_v8 and earlier untouched. J3 is **not** a patient-contact electrode, so **no `hv_*` rule areas were added**.

## Changes

| Item | fix_v8 | fix_v9 |
|---|---|---|
| J3 layer / anchor | F.Cu, (26.5, 62.85), 1×2 row | **B.Cu**, (43.1, 59.85), rot 0°, 1×2 row |
| Pad 1 `+3V3` | (25.4, 62.85) F.Cu | **(42.0, 59.85) B.Cu** |
| Pad 2 `FSR_ADC` | (27.6, 62.85) F.Cu | **(44.2, 59.85) B.Cu** |
| J3-net segments | — | −8 dead J3-feed segs (4 `+3V3` stub, 4 `FSR_ADC` stub tail), FSR stub diagonal trimmed to end at the attach via; +3 new In1 segments |
| J3-net vias | — | +3 0.25/0.15 POFV through-vias: `+3V3` in-pad (42.0, 59.85), `FSR_ADC` in-pad (44.2, 60.15), `FSR_ADC` trunk-attach (26.3, 61.895) |
| Rule areas | — | none (FSR pads are not patient-contact electrodes) |

Pad size (1.15 × 1.7 mm) and 2.2 mm pitch are unchanged.

### Why this location

- **Same-XY flip impossible:** on B.Cu the old pad sites (x 25.4 / 27.6, y 62.85) collide with J13's pads (x 27.62–31.57) and the `I2C_SCL_1V8` / `RLD_PAD` B.Cu tracks.
- **First candidate east of J13 (35.3, 61.8) passed copper clearance but failed courtyard DRC:** it sits inside R122's courtyard — R122 is a B.Cu 2512 at (34.65, 60.5) rot 90°, courtyard x 32.7–36.6 × y 56.7–64.4. `courtyards_overlap` is an error-class violation, so the site was rejected after the first DRC run.
- **Chosen site (43.1, 59.85)** is the least-blocked B.Cu spot near the skin edge: a whole-board scan of the strip (both pad orientations, 0.05–0.25 mm grids, copper + courtyard + via obstacles) found it the only free pocket — inside the gap between R122's courtyard (x≤36.6) and J11 (x≥45.05), clear of the `J11_WE`/`J11_CE`/`AFE_P_SER` diagonals, `LED2_K`/`PD_A`/`AFE_P_SER` vias and all `hv_inner` tiles (which end at x 40.3 and resume at x 45.0).

### New routing (J3 nets only)

- `+3V3`: 0.25/0.15 via-in-pad at pad 1 (42.0, 59.85) → board-wide `+3V3` pour on In4.Cu.
- `FSR_ADC`: via-in-pad at pad 2 (44.2, 60.15) → In1 run (44.2, 60.15)→(44.2, 62.0)→(26.3, 62.0)→(26.3, 61.895) → attach via at (26.3, 61.895) → trimmed end of the original F.Cu stub diagonal (24.907, 60.503)→(26.3, 61.895) → existing FSR trunk → R8.1 / U1.5.
- The In1 lane at y=62.0 threads between the via rows (y 60.35–61.55 above, y 62.59–62.79 below); nearest approaches: `J13_INM` via 0.45 mm, `AFE_P_SER` via 0.59 mm, `PD2_INM` via 0.70 mm — all ≥0.31 mm required.
- Old `+3V3` stub (4 segs to via at (24.35, 62.86)) and old `FSR_ADC` stub tail (4 segs east of the attach point) removed; the `+3V3` via at (24.354, 62.864) keeps its In1 feed and stays connected to the net.

## Non-J3 diff vs fix_v8 (owner requirement)

Every track, via, pad and footprint whose net is not `+3V3`/`FSR_ADC`, object-normalised and set-diffed:

| Class | Removed | Added |
|---|---|---|
| Segments | **0** | **0** |
| Vias | **0** | **0** |
| Pads (incl. positions) | **0** | **0** |
| Footprints | **0** | **0** (298 = 298) |
| Zones | **0** | **0** |

- **Pad→net assignments:** 0 differences across all footprints (J3's pads keep the same net names).
- **Netlist:** identical; schematic untouched; the 29 schematic-parity findings are the same accepted BOM-exclusion attribute warnings (J8, TP1–TP28) as fix_v4–v8.

## Verification

| Check | Result |
|---|---|
| DRC (`--refill-zones --schematic-parity --severity-all`) | **105 warnings, 0 errors, 0 unconnected, 29 parity issues** — vs fix_v8: 107/0/29. Strict subset of pre-existing classes (`../verify/DRC_FIXV9.txt`); intermediate courtyard/silk/mask issues from the first site were fixed before packaging |
| J3 connectivity | Pad 1 `+3V3` → 66-pad +3V3 net; Pad 2 `FSR_ADC` → R8.1 + U1.5 (correct endpoints) |
| Skin-side audit (every footprint) | All 14 skin-facing parts on B.Cu: J3, J5, J6, J7, J11, J13, D10–D12, U10, U11, U12, U16, U23 — **none left on F.Cu**. All outward-facing parts on F.Cu: J1, J8, SW1, SW2, D25, U13, U25. No other footprint flagged |
| Gerber spot-check | New pads flashed at (42.0, 59.85)/(44.2, 59.85) on `Bottom Layer.gbl`; old pad flashes gone from `Top Layer.gtl`; new vias flash on all copper layers |
| Drill | PTH +3 holes at 0.150 mm (89 → 92) — exactly the three new POFV vias; `vitalq_v2-drill.rpt` / `DRILL_REPORT.txt` regenerated |
| BOM / CPL | Identical to fix_v8 (77 groups / 248 placements) — J3 is an integral `Pads_*` contact, not a placed part |
| Fab zip | Rebuilt — 16 gerbers (JLC names) + separated PTH/NPTH drills + maps, regenerated from the v9 board |

## Known limitations / out of scope

- **J3 pad pitch vs the sensor pair:** the FSR402 is a single sensing element bridged across the two pads; net order preserved (pad 1 `+3V3` west, pad 2 `FSR_ADC` east).
- **Case compatibility:** the case work is being regenerated separately against fix_v9 (`case_v4`) — J3 now needs a skin-side contact window at (≈42–44.5, 59.0–60.7) in addition to the J7 apertures.
- Footprint silk box/labels removed from the relocated J3 (same as J7 in fix_v8) to avoid silk-over-copper/mask-aperture warnings; pad numbering documented above.
- Remaining 105 DRC warnings are the same accepted classes as fix_v4–v8 (silkscreen/local-override/parity), none touching J3 nets.

FIX9 COMPLETE
