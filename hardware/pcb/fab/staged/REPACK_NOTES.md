# Repack Notes — VitalQ_v2_fixv19a_fabrication.zip

**Date:** 2026-10-22
**Built from:** `VitalQ_v2_fixv19_fabrication.zip` (original kept untouched — provenance record)
**New archive:** `fab/VitalQ_v2_fixv19a_fabrication.zip` (1,913,742 bytes, 37 members)

Method: member list taken from the original zip in original order; unchanged
members copied byte-identical (payload + zip metadata incl. UT/ux extra fields
and original timestamps); replaced members carry the same paths, new content and
today's member timestamp. `SHA256SUMS.txt`/`.json` regenerated over the new
member set — format replication proven by regenerating the *original* sums from
the original payload and obtaining byte-identical files before repack.

## Member-by-member change table (37 members)

| Member | Status | Source / change |
|---|---|---|
| `gerbers/vitalq_v2-F_Paste.gbr` | **REPLACED** | `gerbers_patched/vitalq_v2-F_Paste.gbr` — +6 Ø0.21 mm flashes (new aperture D84) on U15 BGA pads; 786→792 flashes; see `gerbers_patched/PATCH_NOTES.md` |
| `BOM.csv` | **REPLACED** | edited standalone — R61 DNP flag set; U15 → TPS61241YFFR |
| `BOM_JLC.csv` | **REPLACED** | edited standalone — R61 row removed; U15 → TPS61241YFFR, LCSC C544431 |
| `CPL.csv` | **REPLACED** | edited standalone — R61 row removed; U15 val → TPS61241YFFR |
| `CPL_JLC.csv` | **REPLACED** | edited standalone — R61 row removed |
| `FAB_NOTES.md` | **REPLACED** | edited standalone — POFV list, assembly notes, provenance fix |
| `FAB_VERIFICATION.json` | **REPLACED** | edited standalone — audit block added. Note: `"zip"` field still names `VitalQ_v2_fixv19_fabrication.zip` — it identifies the verified baseline archive, kept verbatim per fix-round commit 0efddf3e. Its `cpl_rows:270`/`bom_rows:266` describe the pre-repack counts; shipped CPL.csv now has 269 data rows |
| `ASSEMBLY_CHECKS.json` | **REPLACED** | `staged/ASSEMBLY_CHECKS.json` — see below |
| `positions_kicad.csv` | **REPLACED** | `staged/positions_kicad.csv` — see below |
| `SHA256SUMS.txt` | **REGENERATED** | 33 entries `<sha256>␣␣fab/<member>` (sums files never self-list) |
| `SHA256SUMS.json` | **REGENERATED** | same 33-entry map, `json.dumps(indent=1)`, no trailing newline |
| other 26 members (2 dir entries + 24 files: 15 gerbers, 4 drill files, 3 renders, DRILL_REPORT.txt, vitalq_v2-drill.rpt) | **byte-identical** | copied from original zip |

## Staged edits detail

### `staged/ASSEMBLY_CHECKS.json`
- `excluded.dnp_not_placed`: `["R93"]` → `["R61","R93"]`
- `fix_v19_changes.R61`: "fitted (was DNP)…2-wire packs" → DNP-decision text:
  parallel-NTC risk — pack 10k NTC on J2.3 ∥ R61 ⇒ R_TS ≈ 5 kΩ, V_TS ≈ 190 mV
  vs BQ25170 V_HOT 188 mV typ ⇒ hot-suspend at 25 °C (matches FAB_NOTES.md §R61)
- Counters updated for consistency with shipped CSVs:
  `placed_components` 251→250, `cpl_rows` 251→250 (CPL_JLC data rows),
  `bom_groups` 82→81 (BOM_JLC data rows)
- Added `fix_v19a_repack` block: U15→TPS61241YFFR record, F_Paste patch record,
  counts note. (No U15 key existed in `fix_v19_changes`; conditional "change U15
  entry" was void, so the substitution is recorded in this new block instead.)

### `staged/positions_kicad.csv`
- `"U15","TPS61240YFFR",…` → `"U15","TPS61241YFFR",…`
- `"R61","10k",…` row **REMOVED** — chosen over marking, consistent with the new
  CPL convention where DNP parts are absent from placement data (R61 was removed
  from CPL.csv/CPL_JLC.csv, not flagged; the file has no DNP column to mark).
  `R93` (pre-existing DNP) remains listed — it shipped that way in the original
  export; removing it was out of scope and would diverge further from a KiCad
  position export. Result: file = CPL.csv ref set + R93.

## Verification output

```
unzip -t:                      No errors detected (37/37 members OK)
member list vs original:       37 = 37, names AND order identical
byte-identical members:        26 of 37 (the 11 changed = 9 replacements + 2 sums)

F_Paste (from new zip):
  %ADD84C,0.210000*%           line 162
  D84*                         line 1775
  X40597369Y-38424994D03*      A1   (6 flashes, lines 1776–1781)
  X40997369Y-38424994D03*      A2
  X40597369Y-38824994D03*      B1
  X40997369Y-38824994D03*      B2
  X40597369Y-39224994D03*      C1
  X40997369Y-39224994D03*      C2
  total D03 flashes: 792  (786 + 6)   last line: M02*   lines: 1782
B_Paste (from new zip):        md5 744185e28e9df646e81aaf3875f5bd95 = original

R61 in zip'd members:
  BOM.csv       "R61","NTC 10k B3380",…,"1","DNP"     present as DNP ✓
  BOM_JLC.csv   no R61 designator row                  ✓
  CPL.csv / CPL_JLC.csv / positions_kicad.csv          0 R61 rows ✓

U15 in zip'd members: TPS61241YFFR in BOM.csv, BOM_JLC.csv (LCSC C544431),
  CPL.csv, positions_kicad.csv; CPL_JLC position row unchanged

JSON validity:   ASSEMBLY_CHECKS.json parses (placed=250, groups=81, cpl=250,
                 dnp=['R61','R93']);  FAB_VERIFICATION.json parses
SHA256SUMS:      sha256sum -c → 33/33 OK; JSON map 33/33 match extracted bytes
```

Original zip `VitalQ_v2_fixv19_fabrication.zip` was opened read-only throughout;
board file `hardware/pcb/vitalq_v2.kicad_pcb` not touched.
