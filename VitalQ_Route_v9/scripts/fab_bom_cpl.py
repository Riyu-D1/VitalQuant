"""BOM + CPL from the board itself (same footprint set, same attributes) for JLC (run with KiCad python)."""
import csv, json, sys
import wx
APP = wx.App(False)
import pcbnew
board = pcbnew.LoadBoard(sys.argv[1])
out = sys.argv[2]
origin = board.GetDesignSettings().GetAuxOrigin()
groups, cpl, skipped = {}, [], []
for fp in board.GetFootprints():
    ref = fp.GetReference()
    attrs = fp.GetAttributes()
    no_bom = bool(attrs & pcbnew.FP_EXCLUDE_FROM_BOM) or fp.IsDNP()
    no_pos = bool(attrs & pcbnew.FP_EXCLUDE_FROM_POS_FILES) or fp.IsDNP() or ref.startswith('FID')
    lcsc = ''
    for fld in fp.GetFields():
        if fld.GetName() in ('LCSC', 'LCSC Part', 'JLCPCB Part #', 'LCSC#'):
            lcsc = fld.GetText()
            break
    fpname = str(fp.GetFPID().GetLibItemName())
    if no_bom or no_pos:
        skipped.append({'ref': ref, 'exclude_bom': no_bom, 'exclude_pos': no_pos})
    if not no_bom and not no_pos:
        groups.setdefault((fp.GetValue(), fpname, lcsc), []).append(ref)
        p = fp.GetPosition()
        cpl.append([ref, '%.4fmm' % ((p.x - origin.x) / 1e6), '%.4fmm' % (-(p.y - origin.y) / 1e6), 'Top' if fp.GetLayer() == pcbnew.F_Cu else 'Bottom', '%.2f' % (fp.GetOrientationDegrees() % 360)])
with open(out + '/vitalq_v2_BOM.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC'])
    for (val, fpn, lc), refs in sorted(groups.items()):
        w.writerow([val, ','.join(sorted(refs)), fpn, lc])
with open(out + '/vitalq_v2_CPL.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
    w.writerows(sorted(cpl))
json.dump({'bom_lines': len(groups), 'placed_parts': len(cpl), 'excluded': skipped, 'missing_lcsc': sorted(r for (v, f, l), rs in groups.items() if not l for r in rs)}, open(out + '/bom_cpl_check.json', 'w'), indent=1)
print('BOM lines', len(groups), 'CPL parts', len(cpl), 'excluded', len(skipped))
