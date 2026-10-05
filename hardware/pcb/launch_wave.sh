#!/usr/bin/env bash
# Launch one detached route_all.py worker per nets_XX.txt in <netsdir>.
#
# For every nets_XX.txt found, runs:
#   nohup <kicad-python> route_all.py <base> --only <netsfile>
#         --out <outdir>/out_XX.kicad_pcb > <outdir>/log_XX.log 2>&1 &
# copies the <base> .kicad_pro sibling to <outdir>/out_XX.kicad_pro first
# (so each output opens as a full KiCad project) and prints the worker PID.
#
# Usage: launch_wave.sh <base.kicad_pcb> <netsdir> <outdir>
#
# KICAD_PY env var overrides the KiCad-bundled python.
set -u

KICAD_PY="${KICAD_PY:-/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3}"

if [ $# -ne 3 ]; then
    echo "usage: $0 <base.kicad_pcb> <netsdir> <outdir>" >&2
    exit 1
fi
BASE="$1"; NETSDIR="$2"; OUTDIR="$3"

# canonicalize inputs
BASE="$(cd "$(dirname "$BASE")" && pwd)/$(basename "$BASE")"
NETSDIR="$(cd "$NETSDIR" && pwd)"
mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"

# locate route_all.py: the pcb directory — next to this script, falling
# back to the directory holding <base>.
PCBDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ ! -f "$PCBDIR/route_all.py" ]; then
    PCBDIR="$(dirname "$BASE")"
fi
if [ ! -f "$PCBDIR/route_all.py" ]; then
    echo "route_all.py not found (script dir nor $(dirname "$BASE"))" >&2
    exit 1
fi

PRO="${BASE%.kicad_pcb}.kicad_pro"
if [ ! -f "$PRO" ]; then
    echo "warning: no .kicad_pro sibling for $BASE" >&2
fi

n=0
for NF in "$NETSDIR"/nets_*.txt; do
    [ -e "$NF" ] || { echo "no nets_*.txt found in $NETSDIR" >&2; exit 1; }
    XX="$(basename "$NF" .txt)"; XX="${XX#nets_}"
    OUT="$OUTDIR/out_$XX.kicad_pcb"
    [ -f "$PRO" ] && cp "$PRO" "$OUTDIR/out_$XX.kicad_pro"
    nohup "$KICAD_PY" "$PCBDIR/route_all.py" "$BASE" \
        --only "$NF" --out "$OUT" \
        > "$OUTDIR/log_$XX.log" 2>&1 &
    echo "nets_$XX: pid $! -> $OUT (log $OUTDIR/log_$XX.log)"
    n=$((n + 1))
done
echo "launched $n worker(s)"
