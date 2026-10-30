# fix_v25 — test points + bring-up plan (report)

KiCad CLI **10.0.4** (`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`)
throughout. Labels: **checked** = measured/executed; **inferred** = reasoned
from checked facts; **guess** = unverified.

## What changed

- `fix_v24/` copied to `fix_v25/` (checked — base copies byte-identical,
  pcb sha `e5e04d13…`, sch `d27bfa23…`).
- **12 new test points TP29–TP40** added, all on **F.Cu (top/lid side)**,
  footprint `TestPoint:TestPoint_Pad_D1.0mm`, each with one short 0.15 mm
  stub to existing same-net copper. No vias needed (checked — top-side
  same-net copper was reachable for every placed pad).
- Schematic: matching `vitalq:TestPoint` symbols + net labels added
  (in_bom = no, on_board = yes — checked).
- PCB footprint attrs on all 12: `dnp`, `exclude_from_bom`,
  `exclude_from_pos_files` (checked).
- Zones refilled after placement; connectivity verified (0 unconnected —
  checked). Fill shapes changed **only** as clearance voids around the new
  pads (inferred — 0 unconnected + 0 parity prove net topology unchanged;
  zone outlines byte-identical per the copper diff).
- New documents: `BRINGUP.md`, this `REPORT.md`, updated `PROGRESS.md`,
  `fab/FAB_NOTES.md`.
- Fabrication package regenerated (see below).

## Test-point map

| TP | Net | Position (x,y mm) | TP | Net | Position (x,y mm) |
|---|---|---|---|---|---|
| TP29 | RLDREF | 46.88, 7.60 | TP35 | +3V3_ESP | 1.52, 10.80 |
| TP30 | RLDOUT | 48.16, 28.56 | TP36 | IOVDD | 45.04, −9.44 |
| TP31 | VBAT_SYS | 46.64, 26.32 | TP37 | CHG_ISET | 26.80, 10.80 |
| TP32 | +3V3_ANA | 42.56, 7.12 | TP38 | CHG_VSET | 29.92, 10.56 |
| TP33 | TX_5V_RAW | 40.24, 41.36 | TP39 | VBUS_DET | 4.80, 57.04 |
| TP34 | RTC_VBK | 29.68, 22.32 | TP40 | TS | 28.88, 8.00 |

All positions checked against `vitalq_v2.kicad_pcb` after the final move of
TP38 (originally 28.80, 9.20 — moved to clear a courtyard overlap with TP40;
the only DRC fix needed).

## Coverage vs the requested list

Already probed in fix_v24 (checked): VBUS TP1 · VBAT TP2 · +3V3 TP3 ·
+1V8 TP4 · TX_5V (boost/LED supply) TP5 · VDD_CP2102 TP6 · GND TP7+TP8 ·
ESP_EN TP9 · ESP_IO0 TP10 · ESP_TX/RX TP11/12 · I2C_SDA/SCL TP13/14 ·
I2C 1V8 TP15/16 · SPI + CS + DRDY/RDY TP17–25 · EXP_INT TP26 ·
CHG_STAT TP27 · RTC_INT TP28.

Added by this revision: AFE reference/output nodes (RLDREF TP29, RLDOUT
TP30), power-path nodes (VBAT_SYS TP31, VBUS_DET TP39), derived rails
(+3V3_ANA TP32, +3V3_ESP TP35, IOVDD TP36, TX_5V_RAW TP33), charger
config (CHG_ISET TP37, CHG_VSET TP38), battery/RTC housekeeping
(RTC_VBK TP34, TS TP40).

### Skipped nets (7) and why — all checked by exhaustive search

- **AFE_BG, AVDD_REG, DVDD_REG** (internal AFE rail/bandgap nodes): no
  DRC-safe 1.0 mm pad site nor viable via-tap on F.Cu — every candidate
  location violated clearance/courtyard or required moving copper.
- **VREF2, CHG_PG**: same — no free top-side copper in reach of those nets.
- **USB_DP/USB_DM**: deliberately skipped. UART console already probed at
  TP11/TP12 (spec prefers UART); a 1 mm pad adds capacitance to a 480 Mb/s
  pair (inferred SI risk); and the pads' nets are isolated anyway —
  R104/R105 are DNP so D+/D− only reach the ESP32 (checked).

## Placement rationale

All 12 pads on **F.Cu**: the bottom/skin side (B) carries the case-contact
electrodes and J-pad rows and is physically covered in the enclosure, so
top side is the accessible face under the lid (checked — B-side pads are
case contacts; reasoned from board geometry per instructions, not from
case_v10 dimensions). Every pad sits adjacent to existing same-net copper
on board edges or free areas; nothing existing was moved.

## Checks — commands and numbers

Rules: `fix_v19/vitalq_v2.kicad_pro` design rules + `fix_v23/vitalq_v2.kicad_dru`
(the v25 `.kicad_dru`/`.kicad_pro` are byte-identical to those — checked,
sha-verified).

```
kicad-cli pcb drc --severity-error --schematic-parity --format json \
    -o _work/drc_v25_final.json vitalq_v2.kicad_pcb
kicad-cli sch erc --severity-error --format json \
    -o _work/erc_v25_final.json vitalq_v2.kicad_sch
```

Results (checked):
- **DRC errors: 0** · **unconnected items: 0** · **shorts: 0** ·
  **schematic parity mismatches: 0** · **ERC errors: 0**.
- Iteration history: first placement attempt had a copper-modelling bug
  (46 violations incl. shorts) — reverted fully, rebuilt on corrected
  geometry; second pass 1 courtyard overlap (TP38/TP40) — fixed by moving
  TP38; final pass clean.

### Existing-copper diff (fix_v25 vs fix_v24)

`_work/copper_diff.py` (pcbnew API, KiCad 10.0.4) matched every pre-existing
item by UUID and compared start/end/width/net/layer for tracks and vias,
pad positions/nets/layers, footprint reference/position/rotation/attrs,
zone outlines, and Edge.Cuts:

- **pre-existing items changed: 0** (checked)
- new: 12 footprints (TP29–TP40), 12 segments (0.15 mm stubs), 0 vias,
  0 zones; Edge.Cuts identical set.
- Full output: `_work/copper_diff.txt`.

### Fabrication regeneration (checked)

```
kicad-cli pcb export gerbers -o fab/gerbers --layers \
    "F.Cu,In1.Cu..In6.Cu,B.Cu,F/B.Paste,F/B.Silkscreen,F/B.Mask,Edge.Cuts" ...
kicad-cli pcb export drill -o fab/jlc vitalq_v2.kicad_pcb
kicad-cli pcb export drill --excellon-separate-th -o fab/drill ...
kicad-cli pcb export pos -o fab/assembly/vitalq_v2-pos.csv --format csv --units mm --use-drill-file-origin ...
kicad-cli sch export bom -o fab/assembly/BOM.csv vitalq_v2.kicad_sch
python3 export_assembly.py      # BOM_JLC.csv, CPL_JLC.csv, ASSEMBLY_CHECKS.json
```

- Gerbers: 15 layers + job file (15 FilesAttributes — matches v24 set).
- Drill: PTH 917 holes, NPTH 8 — hole coordinates **identical to fix_v24**
  (checked by sorted-coordinate diff; TPs are SMD, no new holes).
- BOM.csv 257 rows; BOM_JLC 79 groups; CPL_JLC 247 rows;
  ASSEMBLY_CHECKS: 247 placed, 0 missing-LCSC, 0 missing positions,
  consigned = {D10, D12}.
- **TP exclusion grep-verified**: zero `TP<digit>` designators in BOM.csv,
  BOM_JLC.csv, CPL.csv, CPL_JLC.csv, vitalq_v2-pos.csv,
  positions_kicad.csv (new and pre-existing TPs alike).
- Zips: `fab/gerbers/VitalQ_v2_fixv25_gerbers.zip` (16 files),
  `fab/VitalQ_v2_fixv25_fabrication.zip` (49 files, same layout as v24),
  `fab/SHA256SUMS.txt` regenerated.
- Renders `_work/render_top.png` / `render_bottom.png`
  (`kicad-cli pcb render`): all 12 new pads visible on top, none on bottom
  (checked visually).

## Immutable files

`JOURNAL.md` sha1 `b5135a07…` and `LICENSE` sha1 `fa609308…` — verified
byte-identical before and after (checked; they live at repo root, untouched).

## Side used

Top/lid side (F) exclusively — see Placement rationale. No pads on B.

## Left open

- Nothing blocking. Notes: TS TP40 expects ≈0.19–0.38 V (R61 on-board NTC
  plus pack NTC — checked fitted in BOM); battery cut-off ≈3.2 V remains a
  guess pending firmware confirmation (BRINGUP.md Step 7).
- `--severity-all` warning-class findings are pre-existing classes,
  unchanged from fix_v24 (inferred — no new warning classes introduced).

## Push decision

All pass criteria true → branch `hw/v25-testpoints` (based on
`fix/bom-v24`, PR #19 head, commit `842de500`) pushed and PR opened into
`main`: https://github.com/Riyu-D1/VitalQuant/pull/21 (checked — PR #21,
commit `0a17db5e`). Repo files carry the `vitalquant` rebrand rename; the
transform was verified byte-exact against the fix/bom-v24 tree before
staging. `JOURNAL.md`/`LICENSE` unchanged in the commit.
