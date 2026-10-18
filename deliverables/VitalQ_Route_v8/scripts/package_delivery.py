import argparse
from datetime import datetime
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("destination",type=Path)
parser.add_argument("--verify",action="store_true")
args=parser.parse_args()
destination=args.destination.resolve()

def digest(path,compressed=False):
    result=hashlib.sha256()
    with (gzip.open(path,"rb") if compressed else path.open("rb")) as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b""):
            result.update(chunk)
    return result.hexdigest()

if args.verify:
    manifest=json.loads((destination/"delivery_manifest.json").read_text())
    for record in manifest["files"]:
        path=destination/record["stored_path"]
        if digest(path)!=record["stored_sha256"] or digest(path,record["gzip"])!=record["source_sha256"]:
            raise ValueError("Delivery bytes changed: "+record["stored_path"])
    actual={str(p.relative_to(destination)) for p in destination.rglob("*") if p.is_file() and "__pycache__" not in p.parts}
    expected={r["stored_path"] for r in manifest["files"]}|{"delivery_manifest.json"}
    if actual!=expected:
        raise ValueError("Unexpected delivered paths: "+str(actual^expected))
    print("Delivery byte verification PASS:",len(manifest["files"]),"files; excluded pristine input and local environment",flush=True)
    raise SystemExit(0)

result=json.loads((root/"final_result.json").read_text())
board_hash=digest(root/"vitalq_v2.kicad_pcb")
if board_hash!=result["pcb_sha256"]:
    raise ValueError("Final report hash is stale")
for name,key in (("geometry_audit_final.json","source_sha256"),("hv_uncapped_final.json","source_sha256"),("pad_connectivity_final.json","after_sha256")):
    if json.loads((root/name).read_text())[key]!=board_hash:
        raise ValueError("Stale final audit: "+name)
if board_hash not in (root/"REPORT.md").read_text():
    raise ValueError("Report does not describe the final PCB")
for name in ("final_top.png","final_bottom.png","final_3d.png"):
    with (root/"renders"/name).open("rb") as stream:
        if stream.read(8)!=b"\x89PNG\r\n\x1a\n":
            raise ValueError("Invalid render: "+name)
ignored_dirs={"quilter_raw",".venv","_delivery_worktree","__pycache__",".git"}
files=[];ignored=[]
for directory,dirs,names in os.walk(root):
    dirs[:]=sorted(d for d in dirs if d not in ignored_dirs)
    for name in sorted(names):
        path=Path(directory)/name
        rel=path.relative_to(root)
        if name in (".DS_Store","delivery_manifest.json") or name.endswith((".pyc",".kicad_prl",".lck")) or name.startswith("~"):
            ignored.append(str(rel));continue
        if path.is_symlink():
            raise ValueError("Unexpected symlink in source: "+str(rel))
        size=path.stat().st_size
        compressed=path.suffix==".json" and size>50*1024*1024
        stored=Path(str(rel)+".gz") if compressed else rel
        target=destination/stored
        target.parent.mkdir(parents=True,exist_ok=True)
        if compressed:
            if (destination/rel).exists():
                raise ValueError("Refuse to remove/overwrite an existing uncompressed delivery artifact: "+str(rel))
            with path.open("rb") as src,target.open("wb") as dst:
                with gzip.GzipFile(filename="",mode="wb",fileobj=dst,mtime=0,compresslevel=6) as stream:
                    shutil.copyfileobj(src,stream,1024*1024)
        else:
            shutil.copy2(path,target)
        source_hash=digest(path)
        if digest(target,compressed)!=source_hash:
            raise ValueError("Delivery copy integrity failure: "+str(rel))
        files.append({"source_path":str(rel),"stored_path":str(stored),"gzip":compressed,"source_bytes":size,"stored_bytes":target.stat().st_size,"source_sha256":source_hash,"stored_sha256":digest(target)})
        if compressed:
            print("Lossless geometry package",str(rel),size,"->",target.stat().st_size,flush=True)
manifest={"date":datetime.now().astimezone().isoformat(),"repository":"Riyu-D1/VitalQuant","branch":"vitalq-deliverables","directory":"deliverables/VitalQ_Route_v8","pcb_sha256":board_hash,"excluded_directories":sorted(ignored_dirs),"excluded_local_files":ignored,"files":files,"source_bytes":sum(f["source_bytes"] for f in files),"stored_bytes":sum(f["stored_bytes"] for f in files),"note":"Generated geometry above 50 MiB is losslessly gzip-compressed in the delivery copy only. All local source/design files are retained. Manifest excludes itself."}
text=json.dumps(manifest,indent=2)+"\n"
(root/"delivery_manifest.json").write_text(text)
(destination/"delivery_manifest.json").write_text(text)
print("Packaged",len(files),"files; source bytes",manifest["source_bytes"],"stored bytes",manifest["stored_bytes"],flush=True)
