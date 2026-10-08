# VitalQ Route v8 — Board Analysis (Quilter candidate 1.1)

Board: `vitalq_v2.kicad_pcb`, KiCad 10.0.4. Outline ≈ 50 × 75 mm
(X −2…48, Y −10.5…64.5), 6 copper layers, 299 footprints,
7450 track segments, 660 vias, 1020 pads, 6 zones, 80 rule areas.
Baseline DRC (`drc_baseline_quilter_1_1.json`): **56 unconnected items on 46
nets, 747 violations**, 29 accepted schematic-parity (BOM-flag) items — left alone.

## Final audit disposition — 2026-10-08

The final saved/refilled board has **43 opens, 212 reported/capped real checks, 298 total native violations and 29 unchanged accepted parity items**. An uncapped scan finds **248 keepout intersections across 142 items**. Routing stopped after R8/R9; zero-open/zero-real goals were not achieved. REPORT.md records all unresolved work and exact final endpoints.

All six authorized fiducials are now on-board; no pad copper remains outside the valid, unchanged 50 × 75 mm outline. J8 is restored except two ball-specific U6 filled/capped POFV exceptions. Other placements, schematic/net assignments and board-zone geometry are unchanged. All 359 pad-bearing restart connected groups were preserved. Fifteen geometry/packaging regression tests now pass; the earlier seven-test statement below is historical.

Final manufacturing screening additionally flags 74 small-via hole-to-copper observations across 20 vias, one conservative mixed-hole gap while three other initially unspecified via-in-pad process sites were subsequently marked filled/capped in metadata-only M3. The native 0.1016 mm hole rule is not sufficient for JLC's 0.20 mm hole-to-copper requirement. A generic 0.1000 mm SMD pad gap cannot be certified merely by citing 0.09 mm trace/space capability; JLC separately lists 0.15 mm different-net SMD pad spacing. **Not fabrication-ready.** These findings remain visible rather than being waived or hidden by rule changes.

## Refilled restart audit — 2026-10-08

This section supersedes the stale-fill connectivity claims and the optimistic keepout triage below. The current board was re-read and its SHA-256 matched `ec42e59834e5c53a40899233748c56231567ae9747893e64160814c8a545c0b2`. An untouched PCB/project/schematic checkpoint is in `backups/20261008_195914_resume_unmodified/`; a separately refilled copy is in `backups/20261008_195914_resume_refilled/`. The pristine Quilter PCB remains untouched.

**Truth baseline:** `drc_resume_refilled.json`, run with `--refill-zones --schematic-parity`, reports **56 opens, 340 violations, and the same 29 accepted schematic-parity items**. No further batch may use stale-fill DRC as its acceptance result.

- 5 GND opens and 9 of the 11 +3V3 opens are existing branch vias stranded by the HV exclusion areas. Refill removes the apparent plane connection. These are not solved by weakening thermal rules or merely re-adding vias at the same positions.
- The other two +3V3 opens are U11.5 versus U20.5 and the isolated top pour versus the power pour. The latter DRC positions are zone anchors, not the closest copper points; the reported 65.685 mm distance is NOT a proposed route length.
- The remaining 40 opens are unfinished signal/fanout routes, including the four unconnected U7 signal-via branches and the J12 MP pair. The two MP pads remain on their existing net; no NC exclusion or netlist change is assumed.
- **All 56 entries** are enumerated in `analysis_refilled.txt` and `analysis_refilled.json`: endpoint descriptions/UUIDs/coordinates/layers, connected-component membership, reported-position distance, foreign-copper/keepout/edge blockers on every layer, nearest geometrically legal through-via candidates, and distances to the actual same-net filled plane. A candidate via location is not a verified route or an approved placement.
- The 200 keepout DRC entries comprise **33 via entries and 167 track entries, 126 distinct reported items**. This is a **capped subset**, not the full inventory: KiCad's per-code reporting limit is about 199. An independent uncapped polygon scan finds **260 item/keepout pairs across 154 distinct items** at restart (`hv_uncapped_restart.json`). Repeated reports must not be confused with independent segments, and newly surfaced entries on unchanged copper must not be mistaken for newly introduced geometry faults. All remain real work, not mismatches to suppress.

| Refilled class | Count | Treatment |
|---|---:|---|
| Keepout violations | 200 | Relocate prohibited vias; locally reroute affected inner tracks onto permitted surfaces |
| Pad-to-pad clearance | 9 | Actual project-rule violations (0.1000 vs 0.1016 mm), although above JLC's 0.09 mm capability; do not silently exclude |
| Dangling tracks | 27 | Connect required stubs; remove only demonstrably redundant copper with a logged local edit |
| Dangling vias | 18 | Inspect after refill; some are the stranded plane branches |
| Library/environment checks | 56 | Record separately; do not change netlist/BOM |
| Silk/text checks | 30 | Cosmetic |
| **Copper/routing violations** | **254** | Conservative count including all dangling copper and the 9 clearance checks |

The previous grid model is not an acceptance oracle. It discarded holes in filled-zone polygons, omitted footprint keepouts, ignored internal PTH geometry, used approximate slot rectangles (one slice was reversed), inferred cross-layer connectivity from coincident copper without requiring a via, and could stop a route short of its target. The replacement extraction uses actual pcbnew pad polygons, all copper layers, real outline/slot contours, zone holes, footprint keepouts, and physically connected components. Foreign copper is never erased by subtracting overlapping own-net copper. Seven synthetic geometry regression tests pass.

Mechanical audit: the real outline is closed; R122 is not identified as outside the actual outline. Six FID pad polygons lie outside it. The earlier claims that these were J12 MP pads or proof of five particular Quilter collisions were unverified. Their intended panel-fiducial use and the original five/three Quilter issue identities still need explicit resolution. The earlier J8 keepout notches also lack mechanical authorization; a clean DRC obtained by notching that keepout is not evidence of a mechanically valid fix.

## 1. Stackup and layer use

| idx | Layer | Role |
|-----|-------|------|
| F | Top Layer | Mixed routing + component pads + 2 small `+3V3` island pours |
| G2 | Ground Layer 2 | **GND plane** — `GND.LAYER_1` pour, broad/continuous |
| L3 | Layer 3 | **Main inner signal layer** (dense Quilter routing; `sensor_nopower` areas forbid *fills* here only) |
| P4 | Power Layer 4 | **Power pours** — `VBAT_SYS.LAYER_3` + `+3V3.LAYER_3` zones |
| G5 | Ground Layer 5 | **GND plane** — `GND.LAYER_4` pour, broad/continuous |
| B | Bottom Layer | Mixed routing + pads (U6/U22 BGAs live here) |

Where the planes/pours are broken:

- The `+3V3` pour on P4 is **disconnected from the two `+3V3.F.Cu` islands**
  (unconnected item: zone↔zone, needs a stitching via).
- The GND planes are mostly continuous; they pull back around keepouts,
  the HV corridor, and the routed thermal-isolation "sensor island"
  (moat at X 17.2–23.1, Y 46.2–52.7 with an FR4 bridge ~X 18.3–20.7,
  Y 51.7–52.7). I2C and GND already cross that bridge; `+3V3` and
  `TMP117_ALERT` must cross it the same way.
- The fill in the shipped file is **stale**: it was generated before the
  last via placement pass, so ~410 violations are via↔zone pairs at 0.0 mm.
  A zone refill resolves them; refill must be run with `vitalq_v2.kicad_pro`
  present or KiCad silently applies default design rules (0.2 mm width /
  0.5 mm via), which earlier produced 1834 spurious violations.

## 2. Triage of the 747 violations

| class | count | what | fix needed for JLC |
|-------|-------|------|--------------------|
| stale zone fill | 211 | `clearance` via↔zone, actual 0.0 mm | refill zones |
| stale zone fill | 199 | `hole_clearance` via↔zone, actual 0.0 mm | refill zones |
| rule-area mismatch | 199 | `items_not_allowed` — Top-layer tracks inside `hv_inner`/`hv_ownlayer`. Quilter expanded `hv_inner` to **all 6 layers**; the human design applies it only to inner layers (human board has top copper in the same areas). | set `hv_inner` areas to inner layers only |
| real copper | 9 | `clearance` pad↔pad on Q1/Q2/Q4 SOT-23s, actual **0.1000 mm** vs rule 0.1016 | none — passes JLC min space 0.09 mm; leave |
| real copper | 1 | `hole_clearance` NPTH (U25 mounting hole) ↔ GND track 1.05 mm at (28.9,37.2), actual 0.0 mm | trim/redirect stub |
| real copper | 39 | `track_dangling` — Quilter stub ends (some are the unrouted stubs we extend; dead ones get deleted) | fix in Phase 3 |
| real copper | 3 | `copper_sliver` | fix with refill/small edits |
| cosmetic | 15 | `text_height` | no |
| cosmetic | 14 | `silk_edge_clearance` | no (silk clips at edge — cosmetic only) |
| cosmetic | 1 | `text_thickness` | no |
| environmental | 53 | `lib_footprint_issues` — missing `vitalq`/`snap` libs in this machine's lib table | no — not a board defect |
| environmental | 3 | `lib_footprint_mismatch` | no |

**Bottom line:** ~410 stale-fill + 199 keepout-layer-set = ~609 of 747 are
process artifacts. Real copper work: 1 NPTH short, ~39 dangling stubs,
3 slivers, plus the routing itself. The 9 SOT-23 pad pairs are a
0.0016 mm rule miss that already passes JLC's 0.09 mm minimum space.

## 3. The 56 unconnected items (46 nets) — classification

Per-endpoint escape test (0.05 mm occupancy grid, clearance 0.1016 +
½·0.1016 dilation): `esc` = distance a same-layer flood can reach from the
pad. esc≈0 → sealed (BGA/QFN interior pad — no legal surface escape).
Full data: `analysis_unconn.json`.

| net / pair | class | why |
|---|---|---|
| GND: J8.2↔U6.D5, U6.B4↔D5, U6.B4↔D1, C43.2↔U7.C4, U7.E6↔stub, U7.D5↔C4, stub↔stub | PLANE | stitching via into G2/G5; BGA pads need via-in-pad |
| +3V3: U11.5↔U20.5 | PLANE | route across FR4 bridge (precedent: I2C/GND) |
| +3V3 zone↔zone | PLANE | stitching via F islands → P4 pour |
| +3V3_ANA ×2 (U6.C4 pad + track↔track) | HARD | U6 via-in-pad + inner run |
| CS_MAX86178, CS_AD5940, CS_AFE4900 | HARD | BGA pads / inner stubs |
| CP_RX U5.25↔R72.2, CP_TX U5.26↔R73.1 | HARD | U5 0.5-pitch QFN pads sealed (flood=pad only); via-in-pad or rip-up of USB_DM/DP/ESP_EN corridor |
| SPI_MOSI ×2 (U6.F2↔U22.A4; U7.E7↔B stub) | HARD/MED | via-in-pad pair + inner run |
| SPI_SCK ×2 (U6.F3↔U22.A2; stub↔U22.A2) | HARD/MED | same |
| AD5940_GPIO0 U1.18↔U7.F5 | MEDIUM | U1 end escapes 1.5 mm |
| VREF_2V5 C21.1↔U7.D7 | MEDIUM | C end free |
| TMP117_ALERT stub↔U11.3 | HARD | must cross island bridge |
| MAX86178_INT, MISO_FL, BIOZ_SP_PAD, EDA_RE_PAD, SE_SURGE, J11_RE, RLD_PAD, J12.MP, LED1/2/3_K, AFE_INM, SWEAT_WE, AFE_N_PAD, ADS1292_PWDN, AD5940_RESET, IR_GATE, TX2 | HARD/MED | BGA pads sealed or long stub runs |
| AIN4_LPF0, VBIAS0, VZERO0, BIOZ_FP/SP/FN, DE0 (U7 pads) | MEDIUM | B-side free, U7 pad sealed → via-in-pad |
| PD_INP/INM, PD2_INP/INM, TX1, TX3 (U16↔U6) | MEDIUM | U16 end free on B; U6 end via-in-pad |
| MISO_AFE U6.E2↔R90.1, AFE_BG U6.D2↔C15.1 | MEDIUM | same |

**Key structural finding:** U6 (5×6), U7 (8×7), U22 (5×4) are **0.4 mm-pitch
BGAs** (pads 0.214–0.24 mm). Inter-pad gap ≈ 0.17 mm < the 0.3048 mm needed
for a trace, and no dogbone channel exists (diagonal gap ≈ 0.34 mm < via +
clearances). The only legal fanout is **via-in-pad** — Quilter knew this
(three permissive `bga_fanout` rule areas) but never placed the vias.
JLC 6-layer minimum via is 0.15 mm hole / 0.25 mm diameter and via-in-pad is
supported, so 0.3/0.15 vias fit (0.135 mm via↔pad clearance ≥ 0.1016).

## 4. Congestion map

`renders/congestion.png` — copper-density heatmap with cyan unconnected
endpoints. Hot spots:

- **U7 BGA** (X 36–39, Y 9.8–12.3) — ~16 endpoints, needs ~15 vias-in-pad
- **U6 + U22 BGAs** (X 24–28, Y 45.6–52.8) — ~20 endpoints
- **U16 region** (X 10–16, Y 47–52) — free on B.Cu
- **U5 QFN** (X 11–14, Y 14–15) — 2 sealed pads, dense USB/UART corridor
- **Sensor island** (X 18–23, Y 46–53) — 2 nets across the FR4 bridge
- Scattered 0.4–3 mm stub↔stub gaps along left edge and bottom connectors

## 5. Outline collisions / dimension violations

- `R122.1` pad extends ≈ 0.64 mm outside the nominal bounding box (edge-adjacent
  resistor — fix: nudge ≤ 1 mm or accept if inside real Edge.Cuts; verify).
- Fiducial/MP items far outside outline (library coordinate artifacts —
  they are `unconnected-(J12-PadMP)` pads on B; the two MP pads are a real net
  to be bridged, but their reported positions are on-board; the out-of-outline
  items are connector NPTH/fiducial graphics → verify against Edge.Cuts).
- Edge-adjacent connectors (J12 etc.) overlap the outline by design
  (edge-mount connectors) — normal.
- The 1 NPTH↔track hole-clearance at U25 (28.9,36.2–37.2) is the real
  "collision" to fix.
