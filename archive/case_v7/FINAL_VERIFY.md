# case_v7 — FINAL VERIFICATION

Scope: `case_v7` enclosure (STL/STEP/renders) + `fix_v9` PCB re-verification
(read-only — every check ran on copies under `case_v7/final9/` or on the
source read-only; no fix_v9 file was written).

Tools: KiCad CLI 10.0.4 (DRC + zone refill + schematic parity), pcbnew 10.0.4
(board dump, crosstalk extraction), ngspice 47 (9 sim decks), Python 3.12 +
trimesh/shapely (case booleans, rays, overhang raster audit), numpy/scipy
(sepsis pipeline).

Board integrity: `fix_v9/vitalq_v2.kicad_pcb` SHA-256 begins
`2143d637953704641d25` — unchanged by this work.

---

## A. Case verification — `case_v7/FIT_REPORT.md`

**137/137 checks PASS → CASE7 COMPLETE.** Highlights by mandated area:

### A1. Fit

- Board pocket + component clearance: 0.000 mm³ interference at both the
  real 1.5386 mm and worst-case 1.6 mm stackups; +0.15 mm grown-board
  (PETG shrink) still 0 inter; 0.30 mm/side design clearance.
- All 20 electrode + 9 optical apertures open and centred to ±0.2 mm;
  every connector exit (J8/J9/J10/J12/J2), USB-C window (9.6×3.7),
  overmould corridor (0.95 mm stand-off), button bores, D25 pipe, vents
  and mic port verified by ray probes.
- Insertion paths swept: board, lid, cup, pin plate, contacts — only the
  designed snap/barb interferences.
- Board↔STEP: the same meshes checked by the fit booleans generate
  `case_v7.step` (13 named FACETED_BREP solids incl. `board_pcb` in
  assembled pose), so the STEP is fit-verified by construction.

### A2. Strap-tension stress on lugs

- 40 N design pull on the 30 mm strap: bar point-load σ = 29.1 MPa,
  distributed 14.6 MPa vs ~50 MPa PETG yield; post shear 1.2 MPa.
- 1e6-cycle lug fatigue at the distributed stress (14.6 ≤ 20 MPa): PASS.
- Pull line y27.5 vs face plan centroid y27.0 — even skin pressure.
- Strap-bearing edges all eased (outboard chamfers + inboard channel-top
  chamfer + R0.8 channel end walls); 0.2 mm pinch gap retains the loop.

### A3. 1.5 m drop / impact on snaps

- Worst-case estimate ~1500 g at the shell mass bound (24.4 g → ~36 N
  per-bead share) → bead shear τ ≈ 5.6 MPa vs 15 MPa limit: PASS.
- All 8 snap beads ray-audited individually: in-groove engagement
  0.55–1.27 mm³ per bead (min on the two −y beads flanking USB).
- Snap-fit durability: skirt strain 1.00 % (0.65 hoop + 0.35 bend) vs
  ~5 % PETG yield; cup pin prongs 2.88 %; cap barb catch 0.40.

### A4. Contact pressure — every skin contact, breathing ±5 mm

- 20 pogo pins (J5×5, J6×4, J7×4, J13×2, J11×3, J3×2) + U11 slug face:
  all 21 contact poles measured on the −0.60 mm plane within ±0.05 mm
  (preload uniformity → even force split across all springs).
- Working height 4.0 mm (pad→skin 3.40); P70-5100045 free reach puts the
  pole at −1.40 mm → 0.80 mm stroke reserve absorbs chest rise before
  any contact unloads; the 30 mm elastic strap is the compliant element
  that takes the bulk of the ±5 mm breathing excursion.
- Wipe reliability under motion: pole↔aperture-rim clearance ≥0.30 mm on
  all 20 pins (min 0.40 at J5) — the plunger never binds on the bore edge
  during lateral wipe; cavity-side 45° funnels re-centre tips.
- Correction applied this round: the tip pole now lands exactly on the
  −0.60 datum (v5/v6 models had the sphere centre there → 0.45 mm of
  excess skin poke).

### A5. Thermal coupling to TMP117 (U11)

- Slug face stands 0.6 mm proud on the same datum as the contacts
  (measured −0.60) — firm skin contact under strap preload; Ø3.8×0.30
  counterbore seat + Ø2.9 shaft to the TIM verified.
- Board-level thermal sim (final9, re-run identical): +~1 °C offset at
  the TMP117 pair — firmware-compensated (−1.0 °C), documented as a
  reporting condition, not a case blocker.

### A6. Sweat ingress at seals

- TPU gasket in true assembled pose (z −0.4..0, proud on the skin face):
  9 merged holes, min seal margin 0.29 mm around every aperture;
  contact tips 0.20 mm proud of the gasket underside.
- Seam-barrier rays: 6–8 shell walls crossed on every side incl. the USB
  wall — no straight sweat path into the cavity.
- Pry slots (v7 fix): notch the skirt only; the wall behind is sealed —
  first ray hit lands on the wall face on both sides.
- USB-C: TPU dust flap anchored (2.2 mm pocket), overmould corridor
  clear; bay-cup 0.4 mm drip-groove ring sheds pooled sweat off the cup
  seam (D25-pocket keepout respected).
- Deliberate openings (U13 vent, mic port, J8 window when unplugged,
  J2 wire portal, tail exits) are functional, documented, and routed
  away from the skin seam plane.

### A7. Printability

- Overhang raster audit at 0.2 mm pitch: **0 unsupported cells** for
  base (skin-down), top (top-down), bay cup (bottom-down), pin plate
  (pins-up) — no supports required.
- Min walls/floor/lid all measured 1.60 mm; TPU min feature ≥0.35
  (gasket 0.40, flap neck 0.90, cap wall 0.85); gasket web 0.45.
- Tolerances: 0.3 mm/side design clearance absorbs FDM ±0.1–0.2 mm;
  +0.15 mm grown board still 0 interference.
- Watertight meshes (manifold3d), envelope 62.2×82.2×9.6 mm
  (~15.8 mm assembled on-chest), ≤24.4 g solid bound (~20 g infill).

---

## B. PCB verification — `fix_v9` (read-only rerun)

All checks re-run against copies in `case_v7/final9/` on 2026-10-12.
Every result reproduces `verify/final9/FINAL9_REPORT.md`.

| # | Check | Method | Result |
|---|---|---|---|
| 1 | DRC + zone refill | `kicad-cli pcb drc --refill-zones --schematic-parity --severity-all` on `final9/drc/` copy | **PASS** — 0 errors, 0 unconnected pads; 105 warnings identical signature (23 track_dangling, 1 via_dangling, 53+4 lib fp, 14 silk, 9+1 text); 29 footprint attr notes (BOM-exclusion, J8+TP1–28) |
| 2 | Schematic↔PCB parity | fresh `kicad-cli sch export netlist` + pad-net comparison (`final9/parity.py`) | **PASS** — 963/963 pin↔pad nets, 0 mismatches, 0 orphans, 0 extra pads; 299 comps / 258 nets |
| 3 | Footprint side audit | pcbnew dump (`final9/dump_pcb.py`) — skin parts on B.Cu | **PASS** — J2,J3,J5,J6,J7,J9,J10,J11,J12,J13,D10–D12,U10–U12,U16,U22,U23 on B.Cu; 264 F / 35 B |
| 4 | Power + USB-C charging | 9 ngspice decks (`verify/loop/r4/sims`) re-run | **PASS** — meas values identical: CC 200 mA→CV 4.20–4.24 V; +3V3≥3.268, TX_5V≥4.984, +1V8≥1.798; I²C tr 503 ns → 400 kHz fails/100 kHz passes (firmware condition); TS window −8…+45 °C |
| 5 | Signal integrity | `crosstalk_v9.py` re-extraction on fix_v9 | **PASS** — 369 same-layer pairs / 188 adjacent overlaps, identical worst offenders (LED3_K→BIOZ_FN_PAD ~16.8 mV equiv class); no relayout-required coupling; same minor broadside notes |
| 6 | HV gaps / creepage | `j3j7_analysis.py` re-run | **PASS** — J7 pads inside hv_ownlayer+hv_inner tiles; J3 creepage ≥0.475 mm; new-via nearest foreign copper 0.176 mm (B.Cu I2C_SCL_1V8); antenna keepout 0 copper on all 8 layers |
| 7 | Sepsis simulation | full pipeline re-run (`final9/sepsis/`), 7 cases ×34 = 238 patients + 23-step trajectory | **PASS** — per-patient values identical; NEWS2p≥4 sens 1.00 (102/102), spec 1.00 healthy+fever; ≥5 → 0.95; trend EWS≥3 0.99; qSOFA-proxy 1.00; alerts ~14 min; false alarms 0/0/6/82 % (rest/fever/motion/exercise) |
| 8 | Board↔case_v7 fit | `geometry.json` re-extracted from fix_v9 (byte-identical) → `verify_fit.py` on the final STLs | **PASS** — 137/137 incl. all apertures, pockets, exits, contact reach |

### PCB conditions carried from final9 (no board change)

- I²C ≤100–300 kHz firmware constraint (400 kHz rise-time fails).
- LED pulses on TX_5V must interleave (~900 mA overlap > TPS61240).
- Single-fault patient leakage ≈29 µA — BF-acceptable, not CF.
- No SBP/mentation/lactate → NEWS2 11/17 cap, qSOFA 1/3.
- D12/U12 consigned, D10 site-fit pads; POFV mandatory for 0.15 mm vias.

### Not verifiable without hardware

JLC DFM (acid traps, mask slivers), consigned-part truth, real ESD/EMC,
RF detuning on body, thermal equilibrium on skin, SpO₂ calibration,
clinical accuracy — sims prove feasibility only.

---

## C. case_v7 changes verified this cycle

Two latent defects found and fixed (each covered by a new check):

1. **Gasket pose** — was modelled inside the floor solid; now proud on
   the skin face (z −0.4..0), zero base intersection.
2. **Pogo pole datum** — tips protruded 0.45 mm beyond the −0.60 spec;
   pole now lands on datum (21/21 measured).
3. **Pry-slot sweat windows** — 2.4×0.55 mm through-wall openings at the
   seam sealed; skirt still pryable.

Plus: aperture mouth chamfers + cavity funnels, 8th bead pair at USB,
3.0 mm strap bar, R0.8 channel ends, inboard channel chamfer, door grip
ribs, ECG pulse mark, bay-cup drip groove.

## Verdict

`case_v7` is fit-verified against the unmodified `fix_v9` board,
printable without supports, strap/drop/sweat/contact-verified within its
documented margins, and the PCB re-verification reproduces FINAL9 with
no new issue. `FIT_REPORT.md` ends **CASE7 COMPLETE**.
