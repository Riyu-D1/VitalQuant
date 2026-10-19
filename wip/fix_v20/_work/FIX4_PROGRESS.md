# FIX4 progress

Pipeline:
- `_work/fix4.py` — full rebuild from `pre_promote_backup.kicad_pcb`: tab outline, KO strip, U1@ (11.5,-6.5) rot0, cap moves, blocker-net 2a reroutes, whole-seg delete + walk-back to anchors (`_work/anchors.csv`), pad→anchor routes, stale-keepout delete, tab GND pours (L2/L7), FID4→(-0.4,-10.3), blocker via-stub bridges, island-merge (delete padless dead islands, bridge pad-bearing ones).
- `_work/fix4_p3.py` — vias at pad-adjacent inner-layer track ends, cap respacing (C7 -14.0, C8 -12.1, C64 1.6/-11.3 rot90), cap rewire.
- `_work/fix4_p4.py` — off-pad vias + stubs for 6 pad↔inner-track joins.

After each board edit: refill zones once → save → `cp ../fix_v19/vitalq_v2.kicad_pro .` → `kicad-cli pcb drc`.
DRC path: `_work/drc_fix4{d,e,...}.rpt`. Last: 401 viol / 2 uncon (p4 just ran, not yet DRC'd).

Remaining known issues: shorting/clearance on long bridge routes + pad-end joins; hole_clearance from vias near pads; solder_mask/silk/text items mostly pre-existing v19 baseline (61 violations in v19 baseline DRC).
- `_work/fix4_p5.py` — island-merge A* (superseded: under-dilated raster caused shorts)
- `_work/fix4_p6.py` — surgical pass: parse DRC → delete flagged non-original segs/vias → A*-route unconnected pairs with exact endpoint stubs. Radii: seg 0.30, via 0.70, pad +0.40. Looped by `_work/fix4_loop.sh` (6 cycles, ckpt per cycle `_work/ckpt_p6_N.kicad_pcb`).
DRC trail: d 401/2 → e 413/0 → f 1436/63 → g 1222/82 → h 539/381 → i 817/327
