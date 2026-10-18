#!/usr/bin/env python3
"""Regenerate fab artifacts for a .kicad_pcb: BOM CSV, CPL CSV, Specctra DSN.

Usage (KiCad bundled Python):
    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 \
        regen_fab.py path/to/board.kicad_pcb

Writes <stem>.bom.csv, <stem>.cpl.csv, <stem>.dsn next to the board file.

Board-native filtering (mirrors the intent of build_pcb.write_jlc):
  - footprints flagged exclude-from-BOM (FID*, H*, J8, TP*) stay off the BOM
  - footprints flagged exclude-from-position-file stay off the CPL
  - DNP footprints (FP_DNP) stay off both — they are not assembled
"""

import csv
import re
import sys
from pathlib import Path

import pcbnew


def ref_key(ref):
    """Natural designator sort: letter prefix, then numeric suffix."""
    m = re.match(r"([A-Za-z]*)(\d*)", ref)
    return (m.group(1), int(m.group(2) or 0))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    board_path = Path(sys.argv[1]).resolve()
    board = pcbnew.LoadBoard(str(board_path))
    stem = board_path.with_suffix("")  # drops ".kicad_pcb"
    bom_path = stem.parent / f"{stem.name}.bom.csv"
    cpl_path = stem.parent / f"{stem.name}.cpl.csv"
    dsn_path = stem.parent / f"{stem.name}.dsn"

    bom_groups = {}  # (value, footprint) -> [refs]
    cpl_rows = []
    skipped = {"dnp": 0, "bom": 0, "pos": 0}
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        attr = fp.GetAttributes()
        dnp = bool(attr & pcbnew.FP_DNP)
        if dnp:
            skipped["dnp"] += 1
        if attr & pcbnew.FP_EXCLUDE_FROM_BOM:
            skipped["bom"] += 1
        elif not dnp:
            key = (fp.GetValue(), fp.GetFPIDAsString())
            bom_groups.setdefault(key, []).append(ref)
        if attr & pcbnew.FP_EXCLUDE_FROM_POS_FILES:
            skipped["pos"] += 1
        elif not dnp:
            pos = fp.GetPosition()
            side = "top" if fp.GetLayer() == 0 else "bottom"
            cpl_rows.append((
                ref,
                fp.GetValue(),
                str(fp.GetFPID().GetLibItemName()),
                f"{pos.x / 1e6:.4f}",
                f"{pos.y / 1e6:.4f}",
                f"{fp.GetOrientationDegrees() % 360.0:.1f}",
                side,
            ))

    # ---- BOM: qty-grouped by (value, footprint); refs comma-joined.
    rows = []
    for (value, fpname), refs in bom_groups.items():
        refs.sort(key=ref_key)
        rows.append((",".join(refs), value, fpname, len(refs)))
    rows.sort(key=lambda r: ref_key(r[0].split(",")[0]))
    with bom_path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Ref", "Value", "Footprint", "Qty"])
        w.writerows(rows)

    # ---- CPL: one row per assembled footprint.
    cpl_rows.sort(key=lambda r: ref_key(r[0]))
    with cpl_path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Ref", "Value", "Package", "X", "Y", "Rotation", "Side"])
        w.writerows(cpl_rows)

    # ---- Specctra DSN for the Freerouting round-trip (route_import.py).
    if not pcbnew.ExportSpecctraDSN(board, str(dsn_path)):
        sys.exit(f"DSN export failed for {board_path}")

    total = sum(1 for _ in board.GetFootprints())
    print(f"board: {board_path} ({total} footprints)")
    print(f"bom:   {bom_path} ({len(rows)} groups; skipped {skipped['bom']} excluded + {skipped['dnp']} DNP)")
    print(f"cpl:   {cpl_path} ({len(cpl_rows)} rows; skipped {skipped['pos']} excluded + DNP)")
    print(f"dsn:   {dsn_path}")


if __name__ == "__main__":
    main()
