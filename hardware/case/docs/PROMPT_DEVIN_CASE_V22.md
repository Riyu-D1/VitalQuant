# VitalQuant case_v10: regenerate the case to fit board fix_v22 (ESP32-S3-WROOM-1 + moved J1)

You have full authority inside `/Users/deepakdiwan/Downloads/VitalQuant_Hand/case_v10/` only. No time limit, no limit on passes. Keep iterating until EVERY check in CHECKS.md says PASS. Do not stop to ask; if a choice is unclear, pick the safer one and log it in `case_v10/PROGRESS.md`.

## 0. Hard rules
- Board source (READ-ONLY): `fix_v22/vitalquant_v2.kicad_pcb` (+ `fix_v22/*.kicad_sch`). Never edit, save or re-export anything in any `fix_*` folder. Never touch `fix_v19`. Copy the PCB to `/tmp` if a tool wants to write.
- Never delete files. Superseded files go to `case_v10/_stale/`.
- Keep installs light (CadQuery is already used by case_v9; reuse that environment).
- No ordering, pricing, contacting anyone, firmware work. Do not claim patient safety or compliance.
- British English, metric units. In reports label claims **checked** (measured by code), **inferred**, or **guess**.

## 1. Starting point
- `case_v10/` already exists: a copy of `case_v9/` (the newest case; v9 was a fix_v21 adaptation that was started but **never finished or verified**, so treat its numbers as unproven). The proven method is case_v7/case_v8 (137/137 checks PASS in case_v7): reuse `case_lib.py`, `build_case.py`, `extract_geometry.py`, `verify_fit.py`, `render_views.py`, `export_step.py` and the full v7/v8 check list (snap beads, gasket pose, electrode funnels, strap lug fatigue, aperture chamfers, etc.), and add the new checks in §5.
- Pictures for you in `case_v10/_prompt_pics/`:
  - `v22_case_features.png` — fix_v22 outline + every case-relevant part bbox (blue = top/F, red = bottom/skin B), USB-C grip corridor (magenta), WROOM body and antenna keep-out zone. KiCad coordinates, y grows downwards (south).
  - `v22_top.png`, `v22_bottom.png` — 3D renders of fix_v22.
- First step: run `extract_geometry.py` against fix_v22 (point it at a /tmp copy), regenerate `geometry.json`/`features.json`, and write `case_v10/DELTA_V22.md` listing every part / opening whose position changed vs `case_v8/geometry.json` (fix_v19 board) by more than 0.05 mm. Every feature in the case must be driven from this extracted geometry, not hard-coded old numbers.

## 2. Board facts (from fix_v22, KiCad coords in mm, y down = south)
All **checked** by reading the PCB with pcbnew today unless marked.
- Thickness 1.6 mm nominal (case_lib already models stack-up 1.54 real / 1.6 worst; keep).
- Outline bbox x −2.075…53.65, y −16.575…64.55 (≈55.7 × 81.1 mm):
  - Main rectangle x −2.0…48.0, y −10.5…64.5.
  - **North antenna tab** x −2.05…22.5, y −16.55…−10.5, chamfered/rounded NW and NE corners (≈R0.5–1).
  - **East power tab** x 48.0…53.6, y 20.0…29.9 (D33/Q6 area).
  - Internal Edge.Cuts slots (isolation/stress slots, 1 mm wide): x 6.825–7.825, 13.275–14.275, 19.725–20.725, 26.175–27.175 at y 24.8–29.2; x 6.825–7.825, 13.275–14.275, 20.1–21.1 at y 40.5–44.9; plus small cut-outs (7.3–8.3, 51.55–52.6), (9.3–11.5, 47.6–48.6), (11.9–12.9, 49.5–52.3), (22.0–23.0, 53.5–54.5). Case must not put locating ribs through these unless intended; they are free to leave as is.
- Mounting holes: **only two, both M2 NPTH 2.2 mm drill**: H2 (−0.15, −7.184), H1 (0.6, −2.188). Both are in the NW corner, so the case must locate the board mainly by its outline + snap beads (as v7/v8 do), and use H1/H2 as locating pins (Ø2.0 pin, 0.1 mm radial clearance) or M2 screws into heat-set bosses. Hole-alignment check: case pin/boss centre within 0.10 mm of hole centre.
- **U1 ESP32-S3-WROOM-1-N16R8** top side, pos (11.5, −6.5), rot 0. Courtyard x 2.0…21.0, y −22.61…4.11. Module body ≈18.0 × 25.5 mm, x ≈2.44…20.56, y ≈−22.31…3.19 (**inferred** from courtyard and datasheet), height 3.1 mm above board (3.25 incl. tolerance; lid cavity ceiling must clear by ≥0.3 mm). The antenna end overhangs the north board edge (y −16.55) by ≈5.8 mm (**checked**: edge −16.55 vs body −22.31).
- **Antenna keep-out (case)**: zone x −2.5…25.5, y −23.0…−10.5, all z (**inferred** from the Espressif guideline: no metal and no ground within ~15 mm of the antenna sides, plastic is fine). Inside this zone: no metal (no screws, heat-sets, magnets, pogo pins, foil, conductive paint), no electrode material, and only the thin antenna bulge shell (≤1.6 mm wall, solid, no thickened ribs/bosses directly over or under the antenna). Keep ≥1.0 mm air gap between the module antenna area (y < −16.55) and any case wall (goal; minimum 0.3 mm).
- **Antenna bulge**: the case must bulge north to cover the overhanging module: inner face reaches y ≤ −23.0, so outer face ≈ −24.6. **East wall of the bulge must stay at x ≤ 22.8 (outer face)** so it never enters the USB-C grip corridor (x 23.3…35.7). Inner face then ≈ x 21.2 vs module east edge ≈20.56 → 0.6 mm gap (check ≥0.3).
- **J1 USB-C** (GCT USB4105-style, top side) pos (29.5, −7.8), rot 180, opening faces −y (north). Bbox x 24.155…34.845, y −11.975…−2.505. Receptacle front face ≈ y −11.5 at the board-edge line y −10.5, i.e. ≈1 mm proud of the main board edge (**checked** from fix_v22 work). Receptacle mouth ≈ 8.94 × 3.26 mm, mouth centre z ≈ board top + 1.6 mm (**inferred**; measure from the 3D model / pads).
  - Case opening: ≥ 9.6 × 3.7 mm centred on x 29.5 at the mouth height.
  - **Plug-grip recess / corridor**: the user's plug overmould is modelled as **12.4 mm wide × 6.5 mm thick** (centred on x 29.5 and the mouth centre z) and must slide fully home. So from the case outer face back to within ≤0.5 mm of the receptacle front (y −11.5) the case must be open over x 23.3…35.7 (12.4 + 2 × 0.3 clearance → open 23.0…36.0 recommended) and z mouth-centre ± 3.55 (6.5 + 2 × 0.3). If the lid/base can't stay that open, make the area a through-notch in the wall. Nothing (antenna bulge east wall, dust-flap anchor, snap beads, strap lug) may enter that corridor.
  - The case_v8 dust-flap anchor at (34.5, −11.3) now sits inside the grip recess: relocate it outside the corridor (e.g. x ≥ 36.5) or drop the flap and log it.
  - Snap beads that flanked the old USB window at x≈26 must move so they do not intrude into x 23.0…36.0.
- Top-side tall parts: U1 3.1 mm, J1 ≈3.3 mm, SW1/SW2 TS-1088 (tact) — use VRML/3D heights via `parse_vrml.py`/`merge_heights.py` for everything else.

## 3. Openings (derived from fix_v22 positions; **checked** positions, sizes inferred = same rule as case_v8 unless stated)
Bottom = skin side (B). Pos = footprint origin; bbox = footprint bbox incl. courtyard.
| Feature | Ref | Side | Pos (x, y) | Bbox x / y | Case feature |
|---|---|---|---|---|---|
| ECG electrode pads | J5 | B | (18.6, 62.9) | 14.03–23.18 / 61.33–64.48 | electrode pogo apertures + funnels (v8 rule). **ECG leads = on-board pads J5 via snap/pogo contacts; there are no separate ECG lead wires** (**inferred** from netlist names "ECG pads"; J13 "MX ECG pads" (29.6, 62.9), bbox 27.63–31.58 / 61.15–64.65, is the second/alt ECG pad set — give it an aperture too). |
| EDA pads | J6 | B | (9.0, 62.9) | 4.48–13.53 / 59.93–64.43 | electrode apertures |
| Chest bio-Z pads | J7 | B | (2.35, 61.8) | 0.03–4.48 / 59.28–64.33 | electrode apertures |
| Sweat site | J11 | B | (46.8, 57.5) r90 | 45.05–48.55 / 54.43–60.58 | aperture |
| FSR pads | J3 | B | (43.1, 59.85) | 41.23–44.98 / 58.88–60.88 | FSR window / pressure pad |
| PPG emitter/detector | U16 SFH7072 | B | (13.4, 56.0) | 9.23–17.58 / 53.53–58.48 | optical aperture, opaque divider between emitters and detector (v8 rule) |
| PPG AFE | U6 AFE4900 | B | (27.2, 50.8) | 25.88–28.53 / 49.22–52.39 | no aperture (die, not optical) — confirm vs v8 |
| PPG AFE 2 | U22 MAX86141 | B | (26.1, 47.55) | 24.93–27.4 / 46.45–48.63 | no aperture (confirm) |
| White/green LED | D10 | B | (24.75, 57.5) r90 | 22.83–26.48 / 55.18–59.73 | optical aperture |
| IR LED / red LED | D11 / D12 | B | (14.0, 48.8) / (15.94, 50.6) | 13.05–15.19 / 48.31–49.30 ; 14.82–17.13 / 49.98–51.23 | shared aperture "D11_12" |
| Spectral sensor | U10 AS7341 | B | (20.0, 55.5) | 17.85–22.55 / 52.85–58.15 | optical aperture |
| IR thermometer | U12 MLX90632 | B | (10.3, 50.8) | 7.75–12.85 / 48.25–53.35 | IR window (open aperture, ≥ sensor FOV cone, no plastic over it) |
| Skin temp | U11 TMP117 | B | (20.2, 50.4) | 18.58–21.83 / 49.08–51.98 | copper slug seat (v8 U11_slug) |
| Board temp | U20 TMP117 | F | (20.2, 50.4) | — | none (top side, same xy as U11) |
| Humidity | U23 SHT45 | B | (7.6, 47.0) | 6.38–9.14 / 45.98–48.03 | vent + recess (v8 rule), PTFE membrane seat |
| Pressure | U13 BME280 | F | (32.393, 31.97) | 30.62–33.83 / 30.2–33.54 | lid vent (top side) |
| Microphone | U25 IM69D130 | F | (28.256, 36.154) | 25.98–30.53 / 34.38–37.93 | IM69D130 is bottom-port: sound comes through the PCB hole to the B side → skin-side port on case base under it (**inferred**; check for a board drill at its port pad — keep v8 U25_port logic) |
| Status LED | D25 | F | (4.0, 42.9) | 2.46–5.51 / 42.11–43.70 | light pipe through lid |
| Button | SW1 | F | (45.3, 34.611) r90 | 43.53–47.08 / 31.59–37.64 | button cap through lid |
| Button | SW2 | F | (2.025, 57.17) r90 | 0.25–3.8 / 54.15–60.20 | button cap through lid |
| Tag-Connect | J8 TC2030 | F | (26.45, 53.2) | 22.93–29.98 / 50.07–55.23 | lid access window / removable plug |
| IMU | U14 LSM6DSV | F | (20.207, 36.765) | — | none; keep board rigidly held near it |
| I2C tail pads | J9 | B | (46.8, 17.5) r90 | 45.05–48.55 / 13.33–21.68 | tail notch in base (v8 J9_notch) |
| Optical tail pads | J10 | B | (46.8, 45.9) r90 | 45.05–48.55 / 39.53–52.28 | tail notch (v8 J10_notch) |
| FFC connector | J12 FH12-14S | B | (1.3, 52.5) r90 | −3.63–4.33 / 45.92–59.09 | FFC slot through west wall (overhangs board edge to x −3.6!) |
| LiPo pads | J2 | B | (12.4, 5.003) r180 | 8.98–15.83 / 2.03–6.53 | wire route to battery bay (v8 J2_wire, wire_exit, wire channels, tie posts) |
| USB-C | J1 | F | (29.5, −7.8) r180 | 24.16–34.85 / −11.98–−2.51 | §2 |
Tolerance: every aperture centre within 0.15 mm of its part (optical/IR/slug) or 0.25 mm (vents, buttons, tails); aperture ≥ part active area + 0.3 mm per side unless v8 had a tighter, verified rule.

## 4. Battery, strap, print
- Battery 402535 LiPo 4.0 × 25 × 35 mm (+ protection PCB): reuse the v8/v9 lid-top battery bay concept (bay interior ≥ 25.6 × 35.6 × 4.4 mm). Re-position it so it sits over the main rectangle, clear of the antenna keep-out (y ≥ −10.5 + 5 mm recommended) and of the J1 corridor. Route the J2 wires (from x 9–16, y 2–6.5 on B side) to the bay through a channel with ≥0.3 mm clearance and a strain-relief tie post; no wire may cross the antenna zone.
- Strap: two open-staple slot lugs on the ±x side walls for a **30 mm × 1.3 mm elastic chest strap** (case_v6/v8 STRAP_DESIGN.md: slot 31 × 2.0 mm, bar 3.0 mm, fatigue check ≤20 MPa). The east lug must clear the east power tab (x up to 53.6, y 20–29.9) and the west lug must clear the J12 FFC slot (y 45.9–59.1). Centre the strap line on the electrode row so J5/J6/J7 are pressed onto the skin.
- Print: FDM (PETG/PLA) and resin variants. Min wall 1.2 mm anywhere, 1.6 mm structural/outer shell; overhangs ≤45° or bridged ≤5 mm, or self-supporting chamfers; min hole 1.0 mm; no closed voids.

## 5. Required outputs and checks
1. Build with CadQuery: `case_base`, `case_top` (lid), `battery_door`, `gasket`, `button_cap`(s), `light_pipe`, `contacts_assy`/pogo parts, any `plug` for J8. Export each as STL and an assembly STEP `case_v10.step` (with board model placed).
2. Renders (PNG in `case_v10/renders/`): exploded, assembled top/bottom/iso, and **section views with the board in the case**: (a) YZ section through x 29.5 (USB-C + plug model), (b) XZ section through y −19 (antenna bulge), (c) section through the electrode row y 62.9, (d) section through U16/U10 optics y 55.5, (e) battery bay section. Plus a top-down overlay of every opening on the v22 bottom render.
3. Automated fit checks in `verify_fit.py` (all v7/v8 checks carried over and re-pointed to v22 geometry, plus):
   - board outline incl. both tabs fits cavity with 0.2–0.4 mm clearance; no case solid intersects the board or any 3D component (min clearance ≥0.3 mm, report the worst 10).
   - H1/H2 pin/boss alignment ≤0.10 mm.
   - every opening in §3 aligned to its part within tolerance and sized ≥ active area + 0.3 mm.
   - **USB-C corridor**: boolean-intersect a 12.4 × 6.5 mm plug overmould (+0.3 mm clearance) and a full USB-C plug tongue model inserted to full depth at x 29.5 → intersection volume with any case part must be 0 mm³.
   - **Antenna keep-out**: no case solid thicker than 1.6 mm, and no metal part, inside zone x −2.5…25.5, y −23.0…−10.5; bulge east outer face ≤ x 22.8; air gap to module ≥0.3 mm (goal 1.0).
   - lid ceiling ≥ tallest top part + 0.3 mm (WROOM 3.25).
   - battery fit (402535 + 0.3 mm each side) and J2 wire channel continuous.
   - strap slot size, lug fatigue, snap bead engagement, gasket pose.
   - printability: min wall, overhang map, closed voids, manifold STL (watertight) for every part.
4. `case_v10/CHECKS.md`: table | # | check | value | limit | PASS/FAIL | checked/inferred |, all rows PASS at the end, plus `DELTA_V22.md`, `PROGRESS.md` (every round, what failed, what changed), `ASSEMBLY.md` and `PRINT_NOTES.md` updated for v22.
5. Open questions you can't resolve go at the bottom of CHECKS.md (do not stop for them).

When every row in CHECKS.md is PASS and all files are exported, print exactly:
VITALQUANT CASE V10 COMPLETE
