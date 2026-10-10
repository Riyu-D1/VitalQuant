"""Append/refresh the R2 section of REPORT.md and write fab/FAB_NOTES.txt from the final refilled DRC (drc_final_R2.json)."""
import json
from collections import Counter
from pathlib import Path
from session import ROOT, summary

final = summary(ROOT / 'drc_final_R2.json')
drc = json.loads((ROOT / 'drc_final_R2.json').read_text())
st = json.loads((ROOT / 'state_R2.json').read_text())
pofv = json.loads((ROOT / 'pofv_vias_R2.json').read_text())
opens = ['- ' + ' | '.join(e['description'] for e in it['items']) for it in drc['unconnected_items']]
progress = [l for l in (ROOT / 'PROGRESS.md').read_text().splitlines() if l.startswith('R2-')]
accepted = [l.split(' | ')[0] for l in progress if 'ACCEPTED' in l or 'ADOPTED' in l]
real_types = {k: v for k, v in final['types'].items() if k not in ('silk_edge_clearance', 'lib_footprint_issues', 'lib_footprint_mismatch', 'text_height', 'text_thickness', 'silk_overlap')}
sec = f"""

# R2 phase (structural fix + 8-layer re-route) — status {Path(ROOT / 'drc_final_R2.json').stat().st_mtime and 'final'}

## NOT FABRICATION-READY
Final refilled DRC (`drc_final_R2.json`, saved board, --refill-zones --schematic-parity): **{final['unconnected']} unconnected**,
**{final['real']} real violations** {real_types}, {final['parity']} accepted schematic-parity items, presentation warnings separate.
Start of R2: 43 unconnected / 253 real (6-layer). The board is an engineering checkpoint; fab outputs in `fab/` are for review/quote only.

## Structural changes (all logged in PROGRESS.md, state_R2.json, candidates/*/ledger.json)
- R2-1 restack S/G/S/S/P/S (6L); R2-28/29/30 (supervisor order, user-authorised) **8 copper layers** F / In1 GND / In2 / In3 / In4 +3V3+VBAT_SYS /
  In5 / In6 GND / B; In6 GND plane = copy of the In1 outline; all rule areas that covered In1-In4 extended to In5/In6 (shapes unchanged).
- Moves (supervisor-authorised): **U6 (AFE4900, locked) +1.5 mm east**, rot 180 unchanged (the +2.5 mm order put the F-row balls on the J8
  Tag-Connect NPTH); **U18 dx -0.31 mm** (frees U22 C1/D1 POFV); **U11 (locked TMP117) rotated 180° in place** so U11.5 sits under same-net U20.5
  (POFV +3V3 stitch) and U11.3 gets a 0.5 mm dog-bone via. No parts added/removed; netlist identical; skin cluster kept; HV rule areas untouched.
- J8 Tag-Connect no-via area: per-via notches (annulus + 0.002 mm) only for U6 ball POFVs (SUPERVISOR_R2_05); everything else in J8 restored.
- Rules: board minimum clearance 0.09 mm (= JLC minimum) used only by a custom intra-footprint pad-gap rule (SOT-23 pads 0.100 mm apart);
  netclass 0.1016 mm unchanged; via hole-to-copper / hole-to-hole 0.2 mm; through vias only.
- Via-in-pad: {len(pofv)} filled+capped vias (U7, U6, U22 ball vias at 0.25/0.15, U11/U20 stitch) — **POFV mandatory**, list in `pofv_vias_R2.json`.

## Batches accepted/adopted in R2
{', '.join(accepted)}

## Remaining opens
{chr(10).join(opens)}

## Remaining real violations
- items_not_allowed: copper inside keepout rule areas (mostly inherited HV / hv_ownlayer / hole_keepout hits; rule areas are never edited).
- hole_clearance / dangling / npth_inside_courtyard: listed in drc_final_R2.json.

## Process notes
- The 3-attempt rule was overridden by the supervisor for the U6 wall and reset for the 8-layer board; voided/aborted runs are archived in
  archive/aborted_tool_errors with reasons in state_R2.json.
- Tool fixes made during R2: rule-file guard for every KiCad job, exact JLC via check (pad holes to via copper), per-route 60 s budget,
  dangling-repair cascade cap, corridor/lane reservations for BGA escapes.
"""
rep = ROOT / 'REPORT.md'
text = rep.read_text()
if '# R2 phase' in text:
    text = text[:text.index('\n\n# R2 phase')]
rep.write_text(text + sec)
notes = f"""VitalQ v2 - JLCPCB fabrication notes (R2)
STATUS: NOT RELEASED - {final['unconnected']} unconnected, {final['real']} real DRC violations remain (see drc_final_R2.json / REPORT.md).
Layers: 8 copper (F sig / In1 GND / In2 sig / In3 sig / In4 power +3V3 VBAT_SYS / In5 sig / In6 GND / B sig), 1.6 mm, ENIG.
Cost note: 8-layer + POFV approx. USD 80 per 5 boards (confirm JLC quote).
Vias: through only. 0.40/0.20 mm standard; 0.25/0.15 mm via-in-pad. ALL via-in-pad = POFV (epoxy filled + copper capped, planar) - mandatory,
incl. U6 vias under the Tag-Connect J8 landing.
Min track / gap: 0.1016 mm netclass; 0.09 mm minimum (JLC) only for pad-to-pad gaps inside one footprint.
Hole to copper >= 0.20 mm, hole to hole >= 0.20 mm. Board outline 50 x 75 mm.
Files: vitalq_v2_gerbers.zip (8 copper, masks, paste, silk, Edge.Cuts, Excellon PTH/NPTH + maps), vitalq_v2_BOM.csv, vitalq_v2_CPL.csv.
BOM/CPL generated from the board (identical reference sets). LCSC numbers missing for most parts - fill before assembly order.
"""
(ROOT / 'fab' / 'FAB_NOTES.txt').write_text(notes)
print(final)
