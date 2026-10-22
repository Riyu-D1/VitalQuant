# 10 — Summary: VitalQ v2 fix_v19 fab-gate audit

Target: `feature/vitalq-firmware-v2` @ b9002188 · `vitalq_v2.kicad_pcb` 78441506… · zip 5f976845…
Method: 1027-pad extraction, 37 fingerprinted datasheets, 12 parallel audit groups,
gerber-level verification (aperture decode, not just board-file claims).
Provenance: zip carries no source hash — treated as fab authority; see 09 (divergence scope).

## Severity counts

| Severity | Count | Headline |
|---|---|---|
| BLOCKER | 1 | U15 TPS61240: zero paste apertures — assembler cannot solder it → TX_5V dead |
| HIGH | 7 | TS parallel-NTC; TPS61240 underspec; U3/U4 bulk-cap distances; J13 dead connector; U14 source footprint shorts (gerbers OK); 0.15mm POFV below fab's published min |
| MEDIUM | ~20 | decap margins, footprint oversize cluster (U8/U19/U26/U9/U11/U4), SHT45 keepout, LED headroom (D10/D25), WLP mask webs at limit, IM69D130 strap-vs-firmware, ESD gaps, stackup gap |
| COSMETIC | ~5 | stale D10 footprint value, isolated CP2102N 9.5mA, fiducial-edge claim wrong |

Pin matrix: 1,027 rows, 0 blank verdicts — 549 PASS / 12 FAIL / 466 UNVERIFIABLE (passive pads, per-pad datasheet out of scope; BOM-level values verified).

## Top 5 risks (probability × cost)

1. **U15 no-paste (certainty ~100%, verified at gerber)** — order the current zip → TPS61240 can't attach → optical rail dead on every board. Fix: add F.Paste, regen zip. No copper re-spin, but this zip cannot be ordered.
2. **Source↔fab provenance (certainty: proven)** — U14's source footprint produces shorts while shipped gerbers are correct → the audited board is NOT provably the gerber source. Any regeneration (required for fix #1) must first fix the U14 footprint and re-verify the whole zip, or new divergences may ship.
3. **TS parallel NTC (high probability)** — R61 populated + standard pack NTC → charger hovers at hot-suspend threshold at 25°C. Fix is free (DNP R61 in CPL) but only if flagged to fab before assembly.
4. **TPS61240 200mA spec vs ~300mA LED load (high probability)** — TX_5V droop/shutdown at worst-case drive. Free fix: BOM swap to TPS61241YFFR before ordering.
5. **0.15mm POFV vias below JLC published 0.2mm minimum (45 of 48 in-pad vias)** — order-execution risk: rejection, surcharge, or silent substitution. Needs written fab confirmation + the 5 unflagged in-pad vias called out explicitly.

## Recommendation

Do NOT order the zip as it stands. The required actions are small and cheap — none is a
copper re-spin: (a) fix U15 paste + U14 footprint in source, regenerate fab zip; (b) assembly
notes: DNP R61, swap U15→TPS61241; (c) written fab confirmation: 0.15mm POFV, stackup plies,
unflagged in-pad vias. Re-verify the regenerated zip's gerbers (paste + U14 specifically)
before release. Everything else verified clean: ball maps, straps, buses, rails, protection
topology, DNP/exclusion set, via/drill arithmetic.

HOLD
