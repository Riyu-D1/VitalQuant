# VERIFY_REPORT — V21 verification sections re-run on fix_v22

Board: `fix_v22/vitalq_v2.kicad_pcb`. Baseline reference: `_work/vitalq_v2_pre_move.kicad_pcb`
(= fix_v21 final). All checks run after J1 move to (29.5, −7.8) and routing repair.

## §1 — Schematic ↔ board netlist — PASS

- Schematic nets 255; PCB nets 249; 244 names match directly.
- 16 remaining name mismatches are unconnected-pin artifacts: auto-generated
  `unconnected-(U1-IO35-Pad28)`-style net names and `{slash}` escaping on U5 — not real
  connectivity differences. Zero real pad/net mismatches.
- U1 pads 28–30 (IO35/36/37) unconnected — correct for octal PSRAM (N16R8).
- Board netlist export: `_work/v22_net.xml`.
- `--schematic-parity` DRC: 0 error-severity issues. 15 warning items are identical on
  the pre-move baseline (unconnected-pin net names on U1, LCSC field diffs, embedded-lib
  footprint naming) — pre-existing, unchanged by V22.

## §2 — Pads, islands, ERC — PASS

- ERC: 0 errors (`erc` on `vitalq_v2.kicad_sch`).
- DRC connectivity: 0 unconnected pads/routes after zone refill; every net touching a
  moved part (J1, R2, TP11, TP20, R26) is a single copper island.
- J1 pad→net map (board + gerber tags): A1/B1+SH = GND, A4/B4 = VBUS, A5 = USB_CC1,
  B5 = USB_CC2, A6/B6 = USB_DP, A7/B7 = USB_DM, A8/B8 = SBU1/SBU2 (NC by design).
- Antenna keep-outs empty: board strip x −2.5..25.5 / y −16.55..−15.25 and U1-internal
  x 2.5..20.5 / y −22.25..−16.3 — verified against filled zone polygons (not just bboxes).
- WROOM-route diff vs baseline: only CS_ADS1292, ESP_TX, SPARE_IN and GND stubs changed —
  all in the new J1 shield-hole/courtyard region. Nothing under the WROOM body.

## §3 — USB pair + antenna — PASS

- USB_DP 122.08 → 130.02 mm (5→7 vias); USB_DM 110.10 → 118.25 mm (6→6 vias).
- Skew 11.983 → 11.766 mm; closest midline 0.236 mm.
- Zdiff estimate (edge-coupled microstrip, w 0.129 / h 0.0994 / εr 4.1): ≈91.7 Ω @0.10 mm
  to ≈99.6 Ω @0.15 mm spacing — within 90 Ω ±15 %.
- CC pull-downs: R1 5.1 k @ (42.56,−9.24), R2 5.1 k @ (27.55,−1.40); pad-to-J1 distances
  13.02 mm (CC1) / 1.86 mm (CC2).
- ESD U21 USBLC6-2SC6 @ (22.793,11.888); J1→U21 20.80 mm.
- Antenna keep-outs both empty (see §2); no copper added in antenna strip by the reroute.

## §6 — Power / VBUS — PASS

- VBUS total length 162.03 → 168.47 mm; board via count 7 → 8.
- J1 pad feeds: F.Cu→In3 with 3 local transition vias — west pad column 2
  (27.55,−4.9; 27.15,−4.35), east pad column 1 (31.95,−4.4), trunk link 1 (26.63,0.8).
  Inner-layer feed segments widened 0.5 → 0.6 mm. East side cannot take a second via:
  boxed by J1 pads, NPTH peg hole, CC1 stub, B8 pad and the ADS1292_DRDY column —
  documented exception; remaining via ≈1 A vs 500 mA need.
- Path: J1 VBUS → C63 (11.73 mm) → U2 BQ25170DSGR (12.59 mm; C63→U2 2.01 mm);
  U21 clamps VBUS at 20.80 mm from J1. D33 1N5819WS / Q6 AO3401A unchanged.

## §14 — Fabrication outputs — PASS

- `fab/`: 8-layer gerbers + solder mask, silk, paste, Edge.Cuts, job file; drill
  `vitalq_v2-PTH.drl` (917 holes) + `vitalq_v2-NPTH.drl` (8 holes).
- BOM.csv + BOM_JLC.csv (82 groups, 0 missing LCSC, U1 = ESP32-S3-WROOM-1-N16R8 /
  vitalq:ESP32_S3_WROOM_1, J1 = C165948, R1/R2 = 5.1 k C25905).
- CPL.csv + CPL_JLC.csv (251 rows, 0 missing positions; consigned U12/D12).
- CPL rotations: J1 29.5, 7.8, Top, 180 — identical rotation to V21/JLC convention for
  C165948; R2 27.55,1.4,270; R26 32.0,1.9,180.
- ZIP `fab/VitalQ_v2_fixv22_fabrication.zip` (48 files, 8,299,550 B), SHA-256
  `43ef4a31bf3880ad4484e39c113643cfcade4fe6bba28a972c3e9c2c7fc267f`.
- Gerber-only continuity (existing `gerber_continuity.py`): 0 shorts / 0 opens on real
  nets; reported "OPEN N/C" = unconnected-pin pseudo-net; coverage noise = stale v19
  netmap.json — tool artifacts, documented.

## §15 — Renders — PASS

- `kicad-cli pcb render`: `_prompt_pics/final_top.png`, `final_bottom.png`,
  `final_iso.png`, `final_J1.png` (close-up — receptacle mouth at edge y −10.5,
  WROOM/tab to the left, grip corridor empty).
- Layer plots `_prompt_pics/final/J1area_*.png` redrawn with `j1pics.py` + `draw_j1.py`
  (dx = 0): 8 copper layers, J1 red box at x 29.5, old courtyard lime, magenta 12.4 mm
  corridor — visibly clear.

## Result

- DRC `--severity-error --schematic-parity`: **0 violations / 0 unconnected / 0 parity**.
- `--severity-all`: 102 warnings, all pre-existing classes (baseline 151; +3 net-new
  warning-severity dangling stubs in the rework area).
- 12.4 mm grip corridor PASS (0.80 / 1.66 mm margins); 8.3 mm PASS (0.49 / 1.60 mm).
- Grip axis to tab 7.00 mm; axis to U1 body 8.94 mm; nearest courtyard (R26) 5.60 mm.
