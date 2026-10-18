#!/usr/bin/env python3
"""Write the VitalQ hw_v1 KiCad sheets."""

from pathlib import Path

from design import build_design
from schutil import emit_root, emit_sheet_file, load_lib


def main():
    root = Path(__file__).resolve().parent
    lib = load_lib()
    design = build_design(lib)
    root_uuid = design.root.uuid
    (root / design.root.filename).write_text(emit_root(design.root, lib, design.children, root_uuid), encoding="utf-8")
    for child in design.children:
        path = f"/{root_uuid}/{child.uuid}"
        (root / child.filename).write_text(emit_sheet_file(child, lib, root_uuid, path), encoding="utf-8")
        print(child.page, child.filename, child.title)
    print("root", design.root.filename)


if __name__ == "__main__":
    main()
