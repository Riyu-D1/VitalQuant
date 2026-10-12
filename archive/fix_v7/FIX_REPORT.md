# fix_v7 — FIX_REPORT

**Scope:** loop-round-4 BOM-only fix on top of fix_v6 per `verify/PROMPT_LOOP.md` step 2. **No copper changes** — 8,871/8,871 track/via blocks byte-identical to release. fix_v6 and earlier untouched.

## Changes (from verify/loop/R4_ISSUES.csv)

| Issue | Ref | Change | Part |
|---|---|---|---|
| L4 (MINOR) | R72 (ESP_TX→CP_RX series) | 1k → **10k** | C25744 — reuses existing BOM line (same fix as I07/R73) |

### Rationale

Round 4 identified that the I07/fix_v6 backfeed fix covered only the CP_TX→ESP_RX direction (R73). The symmetric path **ESP_TX → R72 → CP_RX** remained 1 kΩ: when the CP2102N is unpowered (battery operation, no USB) and firmware idles UART0 TX high (debug console, per `verify/round5/firmware_requirements.md`), ~2.7–3.3 mA is pushed into U5's unpowered RXD input — the same parasitic-power class I07 was fixed for. 10 kΩ drops it to ~0.27–0.33 mA, matching R73, at zero BOM cost (existing C25744 0402 line) and negligible edge impact (10 kΩ × ~15 pF ≈ 150 ns, valid ≤1 Mbaud). The schematic note "R72 and R73 are 1k…" was corrected to 10k accordingly.

## Verification

| Check | Result |
|---|---|
| DRC (refill + schematic parity, severity-all) | **0 errors, 0 unconnected**; 112 + 29 warnings — identical signature to fix_v4–v6, all local-override (`DRC_FIXV7.txt`) |
| Track/via identity | 8,871/8,871 identical to release (verify/loop/r3/identity.py) |
| Schematic↔PCB pad parity | 963/963 pin-net pairs, 0 mismatches |
| BOM | 77 groups, 248 placements; R72 joins the 10k group (C25744 ×28); 1k group 12→11 |
| CPL | 248 rows, coordinates identical |
| Fab zip | rebuilt — gerbers+drill byte-identical (no copper touched) |

DNP set (J13, R61, R104, R105, R107, R108), consigned parts (U12, D12), and all fix_v6 verification results carry forward unchanged.

Note: a fresh `kicad-cli pcb export gerbers` produces ~870 additional trailing flashes (via/PTH drill marks without net attributes — a plot-option difference vs the packaged pipeline, not copper). The packaged gerber set was verified to contain every copper object; it is carried forward unchanged.

FIX7 COMPLETE
