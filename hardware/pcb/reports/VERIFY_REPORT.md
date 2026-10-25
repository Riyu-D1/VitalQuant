# VERIFY_REPORT — fix_v23

Board: `fix_v23/vitalquant_v2.kicad_pcb`. Tool: `kicad-cli` **10.0.4**
(`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`). Baseline: fix_v22.

## §1 — DRC / ERC / parity — PASS

- Reviewer-equivalent run (fix_v19 project rules + `vitalquant_v2.kicad_dru`):
  `kicad-cli pcb drc --severity-error --schematic-parity` →
  **0 violations, 0 unconnected, 0 shorts, 0 parity** (`verify/drc_v23b.json`,
  re-run after the schematic LCSC updates — identical result).
- ERC `--severity-error`: **0 violations** (`verify/erc_v23b.json`).
- `--severity-all`: 154 warnings (all pre-existing classes; fix_v22 measured 164
  including the 10 errors now fixed). No error-severity findings.
- Differences vs fix_v22: nine Q1/Q2/Q4 intra-footprint clearances resolved via
  the scoped 0.09 mm rule in `vitalquant_v2.kicad_dru` (TI PicoStar land pattern,
  JLCPCB multilayer minimum 0.09 mm); the J8 NPTH / U6 courtyard clash resolved
  by narrowing U6's courtyard edge −1.30 → −1.15 mm (hole remains 0.265 mm
  clear of the package body). No copper moved. See `DRC_STATEMENT.md`.

## §2 — Antenna keep-outs — PASS (checked)

- `antenna keepout` rule areas present: x −2.5…25.5 / y −16.55…−15.25 and
  x 2.5…20.5 / y −22.25…−16.3.
- Zero filled-zone polygons and zero tracks overlap either region on any layer
  (checked against the v23 board file geometry, not just bounding boxes).

## §3 — USB-C 12.4 mm plug-grip corridor — PASS

- `_work/check_corridor.py` (pcbnew, KiCad 10.0.4) on v23:
  J1 at (29.500, −7.800) rot 180 — unchanged.
  - 12.4 mm corridor x 23.3…35.7 / y −26.5…−11.5: **clear**.
  - 8.3 mm corridor: **clear**. Margins west 0.80 mm / east 1.66 mm.
  - `CORRIDOR OVERALL: PASS`.

## §4 — Gerber continuity — PASS

- `verify/gerber_continuity.py` on the regenerated v23 gerbers + PTH drill:
  primitives 16 117, components 255, nets 249, tagged pins 946.
  - **0 shorts; 0 opens on real nets.**
  - Reported "OPEN N/C" and the 11 pin-name mismatches are unconnected-pin
    pseudo-net artefacts (`unconnected-(…)` net names, `{slash}` escaping,
    U1 N/C pads) — the same tool artefacts documented in v21/v22.

## §5 — Fabrication package — PASS

- Regenerated from fix_v23: 8 copper layers + mask/silk/paste/Edge.Cuts gerbers
  (`.gbr` in `fab/gerbers/`, JLC extensions in `fab/jlc/`), PTH/NPTH drills
  (917 plated / 8 non-plated holes), merged drill, BOM.csv, BOM_JLC.csv,
  CPL.csv (251), CPL_JLC.csv (248), positions, ASSEMBLY_CHECKS.json,
  SHA256SUMS.txt, `VitalQuant_v2_fixv23_fabrication.zip`.
- Consistency (checked): `BOM.csv`, `CPL.csv`, `CPL_JLC.csv` are
  **byte-identical** to repository main's corrected assembly files;
  `BOM_JLC.csv` equals main's corrected file (U1 = ESP32-S3-WROOM-1-N16R8 /
  **C2913202**); every CPL_JLC ref appears in BOM_JLC; J12/U24/U25 excluded as
  DNP per main's BOM.
- Schematic LCSC fields updated to the corrected values (37 refs + D10);
  a fresh `kicad-cli sch export bom` reproduces main's BOM.csv exactly.
  See `BOM_CROSSCHECK.md`.

## Open items (honest)

- No physical prototype; board has not been fabricated or bench-tested.
- RF/antenna performance, case fit and assembly yield unverified on hardware.
- `--severity-all` warnings remain (silk/text/dangling-end classes and
  library-footprint mismatches) — cosmetic/audit items, documented.
- U6 courtyard margin on the J8 side is reduced locally; assembly placement
  clearance toward the J8 NPTH is unchanged physically (hole edge is 0.265 mm
  from the U6 body outline), but the fab/assembly reviewer should be aware.

This board is a research prototype; nothing here is a medical, safety or
regulatory statement.
