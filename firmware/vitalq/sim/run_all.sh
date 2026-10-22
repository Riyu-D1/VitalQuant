#!/usr/bin/env bash
# run_all.sh — build every scenario under sim/tests/ and run them.
# Each scenario is a self-checking binary; the exit code encodes failures.
# Usage: sim/run_all.sh [time_scale]     (default VSIM_TIME_SCALE=2)
set -u
cd "$(dirname "$0")/.."          # firmware/vitalq
SCALE="${1:-2}"
export VSIM_TIME_SCALE="$SCALE"
BUILD="$(pwd)/sim/build"

# big-model blob is a prerequisite for scn_model
if [ ! -f "$BUILD/bigmodel/big15m.bin" ]; then
    echo "[run_all] generating 1.4M-param VQMB blob..."
    python3 sim/mock/gen_bigmodel.py "$BUILD/bigmodel" || exit 1
fi

PASS=0; FAIL=0; FAILED=()
for t in sim/tests/*.c; do
    name="$(basename "$t" .c)"
    bin="$BUILD/$name"
    echo "=== build $name ==="
    make -C sim "$bin" >/dev/null 2>&1 || { echo "  BUILD FAIL"; FAILED+=("$name(build)"); FAIL=$((FAIL+1)); continue; }
    echo "=== run   $name (scale=$SCALE) ==="
    timeout 600 "$bin" > "$BUILD/$name.log" 2>&1
    rc=$?
    if [ $rc -eq 0 ]; then echo "  PASS"; PASS=$((PASS+1));
    else echo "  FAIL rc=$rc (see $BUILD/$name.log)"; FAILED+=("$name"); FAIL=$((FAIL+1)); fi
done

echo
echo "=============================================="
echo " run_all: $PASS pass, $FAIL fail  (scale=$SCALE)"
for f in ${FAILED[@]+"${FAILED[@]}"}; do echo "   - $f"; done
exit $FAIL
