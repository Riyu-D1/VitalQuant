# fix_v8 — FIX_REPORT

**Scope:** owner-approved copper change on top of fix_v7 per `verify/PROMPT_FIX8.md`. J7's four chest-BioZ contact pads moved from F.Cu (away from skin) to B.Cu (skin side), matching J5/J6/J11/J13. Only the four `BIOZ_*_PAD` nets were rerouted; every non-J7 track, via, pad position and net assignment is identical to fix_v7 (verified diff below). fix_v7 and earlier untouched.

## Changes

| Item | fix_v7 | fix_v8 |
|---|---|---|
| J7 layer / anchor | F.Cu, (14.5, 62.85), 1×4 row | **B.Cu**, (2.35, 61.8), rot 180°, staggered 2×2 |
| Pad 1 `BIOZ_FP_PAD` | (11.2, 62.85) F.Cu | **(1.4, 60.7) B.Cu** |
| Pad 2 `BIOZ_FN_PAD` | (13.4, 62.85) F.Cu | **(3.6, 60.7) B.Cu** |
| Pad 3 `BIOZ_SP_PAD` | (15.6, 62.85) F.Cu | **(1.1, 62.9) B.Cu** |
| Pad 4 `BIOZ_SN_PAD` | (17.8, 62.85) F.Cu | **(3.3, 62.9) B.Cu** |
| J7-net segments | 135 | 123 (−27 old F.Cu escapes/dead trunk, +15 new B.Cu stubs + In1 routes) |
| J7-net vias | 20 | 22 (−1 FP microvia at (8.875, 60.9), +3 0.25/0.15 through-vias) |
| Rule areas | — | +8 J7 tiles: 4 `hv_ownlayer` on B.Cu + 4 `hv_inner` on In5/In6, matching the other electrodes |

Pad size and 2.2 mm F/S pair pitch are unchanged; the 2×2 staggers pad pairs to fit the only clear site.

### Why this location

- Same-XY flip is physically impossible: on B.Cu the old row (x11.2–17.8) would overlap J6's pad 4 (12.3) and J5's pads 1–2 (15.2, 16.9).
- An exhaustive scan of the skin-contact strip (y≈55–64.5) found **zero** legal 1×4 windows at 0.1 mm resolution: the row is fully tiled by via fields, escape tracks and the hv keepouts of the existing electrodes.
- The staggered 2×2 at x1.1–3.6, y60.7–62.9 is the only collision-free site in the sensor cluster, between the west board edge/J12 and J6. It keeps the 2.2 mm F/S spacings and sits in the same y-band as the other skin contacts.

### New routing (J7 nets only)

- `BIOZ_FP_PAD`: new B.Cu pad merges directly onto the existing FP trunk at (1.70, 60.73) — no new via.
- `BIOZ_FN_PAD`: B.Cu stub east to new 0.25/0.15 via (3.3, 59.4), In1 route to the existing through-via barrel at (11.845, 60.735).
- `BIOZ_SP_PAD`: B.Cu stub west to new via (−0.6, 62.9), In1 route to the existing barrel at (15.748, 60.532).
- `BIOZ_SN_PAD`: B.Cu stub along the south edge (y63.95) to new via (−0.6, 63.95), In1 route to the existing barrel at (20.263, 60.512).
- All In1 paths are clear of the `hv_inner` tiles (which apply to In5/In6 only) and clear of J12's mechanical pad, FID5, the GND via at (2.128, 63.039) and the I2C/VBUS_DET escape field. Two earlier via candidates that DRC flagged (FID5 at 0.196 mm; J12-MP collision) were relocated and re-verified.

## Non-J7 diff vs fix_v7 (owner requirement)

Every track, via, pad and footprint whose net is not `BIOZ_*_PAD`, object-normalised and set-diffed:

| Class | Removed | Added |
|---|---|---|
| Segments | **0** | **0** |
| Vias | **0** | **0** |
| Pads (incl. positions) | **0** | **0** |
| Footprints | **0** | **0** (299 = 299) |
| Zones | 0 | +8 (all J7 `hv_*` tiles) |

- **Pad→net assignments:** 0 differences across all footprints (J7's four pads keep the same net names).
- **Netlist:** identical net-ID→name table; schematic untouched; the 29 schematic-parity findings are the same accepted BOM-exclusion attribute warnings (J8, TP1–TP28) as fix_v4–v7.

## Verification

| Check | Result |
|---|---|
| DRC (`--refill-zones --schematic-parity --severity-all`) | **107 warnings, 0 errors, 0 unconnected, 29 parity issues** — vs fix_v7: 112/0/29. Strict subset of pre-existing classes; the only new violations raised during iteration (FID5 clearance/hole/mask-bridge, J7 silk) were fixed before packaging (`DRC_FIXV8.txt`) |
| J7 connectivity | 1 connected component per net, pad ↔ resistor: FP→R80.1, FN→R81.1, SP→R82.1, SN→R83.1 |
| Skin-side review | All five skin electrodes **J5/J6/J7/J11/J13 now on B.Cu**; optical sensors (D10–D12) and TMP117 (U11) B.Cu; outward parts (USB-C, Tag-Connect, SW2) F.Cu |
| Gerber spot-check | All 4 new pads + 3 new vias flashed on `Bottom Layer.gbl`; all 4 old pad flashes gone from `Top Layer.gtl`; J3's pads retained |
| BOM / CPL | Byte-identical to fix_v7 (77 groups / 248 placements) — J7 is an integral `Pads_*` contact, not a placed part |
| Fab zip | Rebuilt — 16 gerbers (JLC names) + separated PTH/NPTH drills + maps, regenerated from the v8 board |

## Known limitations / out of scope

- **Case compatibility:** `case_v3/features.json` still defines `J7_window` at (14.6, 62.9) — the old F-side position. No B.Cu spot exists beneath that window (J5/J6 pad rows occupy it), so any B.Cu fix inherently requires a **case revision** to expose contacts at the new site (x≈1.1–3.6, y60.7–62.9).
- **J3 (`Pads_FSR`) remains F.Cu** at the skin edge — same class of issue as J7 was, but untouched per the owner rule (not a BioZ/J7 net).
- Footprint silk labels/box were removed from the relocated J7 to avoid silk-over-copper/mask-aperture warnings; pad numbering is documented above.
- Remaining 107 DRC warnings are the same accepted classes as fix_v4–v7 (silkscreen/local-override/parity), none touching J7 nets.

FIX8 COMPLETE
