# 10 — Summary: VitalQ v2 fix_v19 fab-gate audit → fixv19a verdict

Target audited: `feature/vitalq-firmware-v2` @ b9002188 · `vitalq_v2.kicad_pcb` sha 78441506…
Artifact now evaluated: **`fab/VitalQ_v2_fixv19a_fabrication.zip`** (sha cf3336f0…10a8,
37 members, 26 byte-identical to v19 + 11 intentional updates; original v19 preserved untouched).

## Severity counts (as-found → as-fixed)

| Severity | Found | Status |
|---|---|---|
| BLOCKER | 1 | U15 paste: REVISED to "54% undersized apertures" after gerber decode → **FIXED** (+6 Ø0.21 flashes → ~84% coverage; source pads carry F.Paste) |
| HIGH | 7 | TS parallel-NTC → **FIXED** (R61 DNP through all BOM/CPL); TPS61240 underspec → **FIXED** (U15→TPS61241YFFR, pin-compatible per SLVS806D §6); U14 source defect → **FIXED** (transpose, gaps +0.246mm); decap distances, J13 dead connector, POFV-0.15mm → documented residual/confirmation items |
| MEDIUM | ~20 | mostly residual/advisory — footprints corrected in source; shipped copper verified clean |
| COSMETIC | ~5 | D10 stale value → **FIXED**; others documented |

Pin matrix: 1,027 rows, 0 blank verdicts. Connectivity sweep: **970 pads, OPEN=0, SHORT=0** in shipped copper — the decisive "will it work when powered" answer for the v19/19a copper.

## Top 5 risks (probability × cost) — current state

1. ~~U15 paste~~ → **RESOLVED in fixv19a** (patched gerber verified; BOM swapped to TPS61241 — check LCSC stock ~67pcs for C544431 at order time).
2. **0.15mm POFV vs JLC published 0.2mm min (45/48 in-pad vias)** — open external question; FAB_NOTES now carries the full 48-via list + 5 unflagged vias. Attach to order; if fab can't do 0.15 POFV, expect surcharge/re-spin decision.
3. **TS suspend risk** → **RESOLVED** (R61 DNP) — verify the assembly note reached the order (it's baked into the CSVs now).
4. **Stackup gap 0.122mm + nonstandard prepregs** — fab must confirm ply availability; documented in FAB_NOTES.
5. **Decap margin findings (U3 VOUT 15.95mm, U4 IN)** — accepted residual risk; board may work but margins are thin. Next-rev items, not stoppers for this rev.

## Source-side residual (does NOT affect fixv19a order)

The audited .kicad_pcb is now corrected (U14 transpose, paste attrs, pad sizes, R61 DNP,
D10 value) but carries 3 latent opens from the shrink pass — U19.12, U11.2, U20.4 —
being closed as routing edits in-tree. Any future gerber regeneration must re-verify
connectivity (rerun tools/conn_sweep.py on new gerbers).

## Recommendation

Order `VitalQ_v2_fixv19a_fabrication.zip` — NOT the original v19 file — with the FAB_NOTES
assembly instructions (R61 DNP is already in the CPLs; U15 is already TPS61241) and the
POFV-48 list attached. Two external confirmations before payment: (a) fab can do 0.15mm
POFV (or accept surcharge), (b) TPS61241YFFR stock at C544431. Both are quote-time
questions, not design defects. Everything internal to the package is verified.

SHIP
