import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"


def checkpoint(tag, source=ROOT, refill=False):
    folder = ROOT / "backups" / (datetime.now().strftime("%Y%m%d_%H%M%S_") + tag)
    folder.mkdir(parents=False, exist_ok=False)
    hashes = {}
    for suffix in ("kicad_pcb", "kicad_pro", "kicad_sch", "kicad_dru"):
        path = source / ("vitalq_v2." + suffix)
        if path.exists():
            shutil.copy2(path, folder / path.name)
            hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    (folder / "manifest.json").write_text(json.dumps({"created": datetime.now().astimezone().isoformat(), "tag": tag, "source": str(source), "sha256_before_refill": hashes}, indent=2) + "\n")
    if refill:
        subprocess.run([CLI, "pcb", "drc", "--refill-zones", "--save-board", "--schematic-parity", "--format", "json", "-o", str(folder / "drc.json"), str(folder / "vitalq_v2.kicad_pcb")], check=True)
    print(folder, flush=True)
    return folder


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("tag")
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--refill", action="store_true")
    args = parser.parse_args()
    checkpoint(args.tag, args.source, args.refill)
