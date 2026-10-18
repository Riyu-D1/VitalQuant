# hw_v2 routing — paused state (night of Oct 4)

## What's running RIGHT NOW (leave alive, they save incrementally)
- `/tmp/emit_flood.py` → routes 16 HV/electrode nets on `/tmp/hv_emit.kicad_pcb`,
  log `/tmp/emit_flood.log`. 3 rounds; round≥1 retries at 0.12mm track width.
- freerouting headless → `/tmp/hv_cur.dsn` → `/tmp/hv_cur.ses` (stale state,
  autoroute stage; useful only as fallback — geometry predates the rips).

## Ground truth established (do NOT rediscover)
- Netclasses live in `<stem>.kicad_pro` (netclass_patterns). SaveBoard writes a
  STUB pro if none exists → all classes become Default → WRONG clearances.
  Fix: `cp vitalq_hw_v1.kicad_pro /tmp/<stem>.kicad_pro` for every scratch board.
  emit_flood.py already re-copies the pro after every save.
- Creepage slots (Edge.Cuts pill cutouts, ~1×4.4mm) sit between row resistors
  — solid walls on ALL layers. HV crossings happen at each net's own resistor.
- `hv_ownlayer` (F+B, tracks allowed) + `hv_inner` (In1-4, no copper) = the
  reserved HV lanes. Router's lane relax: 0.4mm HV-vs-foreign in lanes,
  0.2mm sibling pads (refs=23), 1.5mm elsewhere. `.dru` has matching rules.
- Rip: 229 non-HV items inside lane boxes removed from `/tmp/hv_b2.kicad_pcb`
  (37 victim nets) + ~50 more nets suppressed via obs.dead in the router
  (full list = DEAD set in /tmp/emit_flood.py). ALL must be re-routed after.
- BFS flood router (`route_flood.py` + pad_cells/bfs) is the working engine.
  A* approach abandoned — too slow in dense corridors.
- HV net layer map: ECG/RLD/AFE/BIOZ = B.Cu pads; EDA = F.Cu pads;
  J5/J6/J11/J13 = B.Cu pads; J7 = F.Cu pads.

## Emit status at pause
- EMITTED (real copper, verified correct classes): ECG1_PAD(18segs), AFE_P_PAD(24segs)
- Earlier verified routes on hv_b2 (also emitted): AFE_N_PAD, J11_CE, J11_WE, J13_INP
- Round-0 UNREACHABLE (retry at 0.12mm pending): ECG2_PAD, RLD_PAD
- Not yet attempted tonight: EDA×4, BIOZ×4, J11_RE, J13_INM

## Next steps in order
1. Check `/tmp/emit_flood.log` — if nets still UNREACHABLE after round 2:
   analyze per-net (flood shows the reachable region; look for pads/keepouts
   sealing the goal pocket; J5.x pads share tight connector field).
2. Route victim nets: /tmp/worker_route.py <board> <out> <netlist>;
   net groups in /tmp/hv_workers/grp1-8.txt (GND solo=218pads, +3V3=65pads).
   Run on /tmp/hv_emit.kicad_pcb COPIES in parallel (one writer per file!),
   then merge tracks into master + re-run conflicts.
3. verify_connectivity.py — DONE, tested on hv_b2: 190 multi-pad nets,
   169 open mid-route (union-find incl. zone islands; safe-biased to OPEN).
   Usage: python3 verify_connectivity.py <board>. Last line = OPEN-NETS list.
4. regen_fab.py — DONE by subagent; produces <stem>.bom.csv/.cpl.csv/.dsn.
5. Zones refill (board_finish or route_import has refill code) → DRC with
   real .dru → fix/justify violations → VERIFICATION.md.
6. Copy verified board back to repo: hardware/pcb/vitalq_hw_v1.kicad_pcb.
   DO NOT commit until open-net list = only justified stragglers.

## Key files
- canonical: hardware/pcb/vitalq_hw_v1.kicad_pcb (UNTOUCHED — all work in /tmp)
- working: /tmp/hv_b2.kicad_pcb (ripped base) → /tmp/hv_emit.kicad_pcb (emitting)
- router: route_flood.py (BFS emit), route_hv_all.py (PLANS + sibling_wrap),
  route_all.py (Obstacles/cell_blocked/emit), /tmp/worker_route.py (victims)
- user priorities: functionality > size; all sensors co-located; 6 layers
  allowed; board extension allowed; placement verified sound (no moves).
