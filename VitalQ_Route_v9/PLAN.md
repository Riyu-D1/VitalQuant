# VitalQ v9 execution plan

## Source and protection

Start from the hand-edited v9 board dated 2026-10-09 10:19. Do not use the newer autosave. The B0_original backup preserves the starting board/project/schematic/local project. v8 is read-only; Python imports from it must use -B/PYTHONDONTWRITEBYTECODE. No parts or net assignments may change. All locked parts, connectors, electrodes, mounting holes, outline, skin-side sensor placement and rule areas remain unchanged. U19, or U18 only if needed, may move with a complete reconnection and logged before/after. Unlocked small passives may move no more than 1 mm, logged individually.

## Rules

Source checked: https://jlcpcb.com/capabilities/pcb-capabilities (2026-10-09).

- Exactly six copper layers, ENIG, 1 oz capability basis. No 8-layer conversion.
- Global and netclass track width/clearance remain 0.1016 mm; never below 0.09 mm.
- Presets: 0.25 mm diameter / 0.15 mm hole for BGA POFV; 0.40 / 0.20 mm ordinary vias.
- Minimum diameter 0.25 mm, drill 0.15 mm, annular ring 0.05 mm, matching the explicitly requested 0.25/0.15 geometry. Larger existing lawful vias need not all be replaced.
- Via hole-to-foreign-copper and drilled hole-to-hole edge separation: 0.20 mm, enforced by project minima and vitalq_v2.kicad_dru. Same-net attached tracks/annuli are necessarily connected to the hole and are not foreign-copper clearance checks. Hole-to-hole applies regardless of net.
- Copper-to-outline/slot clearance remains 0.30 mm.
- Through vias only. Blind, buried and micro vias disabled and prohibited by custom rule.
- All via-in-pad must be explicitly flagged filled/capped and listed for epoxy-filled, copper-capped POFV ordering. JLC advertises this process on 6-layer boards; confirm actual order options and any small-hole surcharge rather than assuming every service is free.
- Never add vias or inner copper in hv_inner areas; never modify rule areas, including J8. A geometrically blocked pad is not authorization to notch a keepout.

## Ordered work

1. Back up, establish rules/presets, save/refill baseline DRC. Also retain inherited-rule comparison.
2. Correct the five named 0.6/0.3 vias and move LED2_K off In1.Cu, preserving existing connections.
3. Local GND repairs; C59/U18 +3V3 stitches; sensor bridge and TMP117_ALERT; J12 MP; evaluate fully reconnected U19 relocation.
4. U7 exact 0.25/0.15 POFV escapes on In2/B, with a local MISO_AD detour only if required.
5. U6/U22 authorized local rip-up window and POFV, restoring all removed-net connections; In3 fallback only with connected power pours.
6. Remaining outer-layer HV-safe long runs.
7. Keepout, via spacing, dangling copper and pad-clearance cleanup without rule relaxation.
8. Final saved/refilled DRC, geometry/protected-placement/connectivity audit, renders and complete report.

## Attempts and acceptance

Review v9 PROGRESS.md and v8 PROGRESS.md/REPORT.md before each attempt. Keep v8 failed approaches in the ledger; the explicit v9 strategies are not permission to repeat exhausted v8 searches. At most three genuinely distinct v9 attempts per net; inherited exhaustion remains visible. No grid-resolution-only retries. Count routing batches with no accepted open reduction, including search-only failures. Two consecutive non-improving routing batches stop routing and send all remaining work to the report. Rule establishment is the baseline, not a routing attempt. Artifact correction counts as a routing batch if copper is changed.

Every batch gets a dated backup first and exactly one outcome line afterward in PROGRESS.md. Candidate projects retain the same PCB/project/rules basename. Never accept stale-fill DRC. Accept only candidates preserving all original connected pad groups and protected geometry, with no new real DRC defects; a capped keepout count must be audited geometrically. Rejected candidates remain archived, not deleted. Track every via, segment change/removal, nudge, attempt and blocked reason in JSON ledgers. Freerouting, if justified, is one named-BLOCKED-net-only run with fixed original copper and a 10-minute timeout; not a fallback that bypasses the mandatory stop.

DRC command: /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb drc --refill-zones --save-board --all-track-errors --schematic-parity --format json -o <report> vitalq_v2.kicad_pcb.

Reported real violations exclude only library and silk/text presentation findings; parity is separate. KiCad's per-code cap means native totals are lower bounds and cannot replace the uncapped geometry audit. No manufacturing-release claim while opens or real violations remain.
