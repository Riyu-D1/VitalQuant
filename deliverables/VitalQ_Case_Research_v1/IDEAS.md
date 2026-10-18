# VitalQ case — improvement ideas

Baseline: `case/case.py` v2 — 50.3×74.3 mm body (55.5×78.4 w/ ears+loops),
~11.5 mm apex, R150 chest curve, 1.8 mm walls, 4× M2 corner-ear screws,
strap loops, USB-C opening, 452535 LiPo pocket, debossed QRST emblem,
placeholder sensor window + end notch. Board layout **not final** (Quilter)
— every idea below assumes connector/pad positions can move; flags mark
the ones that *require* PCB cooperation.

Ranking: **Impact** = how much it improves wearability/sealing/usability;
**Effort** = case-design + verification work (S ≤ half a day of case.py,
M = new subsystems or parts, L = new manufacturing process or dock).

## Ergonomics & profile

**I1 — Adhesive-wing skin skirt.** Add a 0.5–0.8 mm curved flange
(following the R_SKIN cylinder) extending ~8 mm around the base perimeter.
Accepts double-sided medical tape rings or hydrocolloid, gives the
Zio/CAM "rigid pod on flexible skirt" behaviour, and doubles as a sweat
gutter around the floor openings. FDM: print as separate TPU piece or
co-printed flexible lip; keep strap loops too — strap *or* adhesive, user
chooses. *Impact: high · Effort: M · PCB: no.*

**I2 — Tapered ends.** The outline is a uniform rounded rect; taper the
±Y end walls inward below the seam so the ends read thinner (covers the
"brick edge" feel). Pure outline change — one extra wedge cut in
`build_base`/`build_cover`. *Impact: medium · Effort: S · PCB: no.*

**I3 — Thinner deck over non-battery area.** Battery pocket forces
Z_APEX=11.5; over the rest of the board the deck only needs ~1.5 mm
headroom. Step the dome: high boss over the 452535 cell, lower shoulder
elsewhere (~2 mm visual slimming). *Impact: medium · Effort: M · PCB: no.*

**I4 — Chest-fit options.** R150 is midline sternum. Parameterise a
second R for female/lateral placement (R100–120) — it's already one
constant; ship two base variants. *Impact: medium · Effort: S · PCB: no.*

## Strap attachment & quick-release

**I5 — Snap-out strap loops → open hooks.** Closed loops mean threading
the band through; open-bottom hooks (like watch straps) let the user clip
in without re-threading. Small shape change to the ring code. *Impact:
medium · Effort: S · PCB: no.*

**I6 — Strap-carried electrodes (Polar/Garmin pattern).** Long-term:
moulded electrode pockets + snap studs in the elastic band itself, wired
to the case via the existing tail pads. Then the skin floor only needs
the optical window — the end-notch tails become the electrode leads.
*Impact: high · Effort: L (textile part) · PCB: no (tail pads exist).*

**I7 — Detachable strap carrier.** Instead of integral loops, a thin TPU
"saddle" that the case drops into and the strap threads through — the
Movesense holder pattern. Lets one case serve strap, adhesive, and clip
carriers. *Impact: medium · Effort: M · PCB: no.*

## Skin contact & electrodes

**I8 — Snap studs in the floor.** Two Ø3.9 mm male ECG snap studs set
into the skin floor let standard disposable hydrogel electrodes attach
directly — the dominant clinical interface. Requires studs at the ECG pad
positions. *Impact: high · Effort: M · PCB: YES — needs board-side snap
receptacles or pogo lands aligned to the studs (electrode pads currently
exit via the end notch instead).*

**I9 — Dry stainless electrode zones.** Exposed stainless pads inset in
the floor (BPM Core style) — zero consumables but motion-artefact-prone;
fine for spot-check mode only. *Impact: low-medium · Effort: M · PCB: YES
— electrode pads must face the skin, not exit as tails.*

**I10 — Flush optical window insert.** Replace the open SENSOR_WIN hole
with a cast-in clear window (PMMA rod or optical epoxy plug, Ø8–10 mm):
keeps skin oils/sweat off the MAX86141/AFE4900 optics, gains IP
credibility, and the flat outer face stays flush for skin contact.
*Impact: high · Effort: M · PCB: no (window position may need nudging
post-Quilter).*

**I11 — Sealed electrode pass-throughs.** Split the wide END_NOTCH into
per-tail grommet slots with a silicone gasket strip — keeps sweat out of
the cavity while leads exit. *Impact: medium · Effort: S · PCB: no
(placeholder geometry only).*

## Waterproofing / IP

**I12 — Perimeter gasket groove.** 1.0–1.2 mm groove around the seam on
the cover + 1.5 mm silicone cord → splash-proof (target IPx4). The seam
is flush and already located by the ear screws — groove is a swept cut,
well-contained change. *Impact: high · Effort: M · PCB: no.*

**I13 — Pogo-pin charging (seal the USB-C).** Two pogo pads on the shell
edge (Corsano magnetic-cable pattern) let the USB-C slot be deleted → the
case becomes gasketable to IP6x. Dock/cable is a separate part.
*Impact: high · Effort: L · PCB: YES — needs charge pads + keep J1
internal-only (or remove).*

**I14 — Ultrasonic-weld closure option.** For a sealed disposable-ish
variant: replace screw ears with a weld tongue on the base rim. Not
FDM-testable but the seam geometry can be prototyped for adhesive tape.
*Impact: medium (production) · Effort: M · PCB: no.*

**I15 — USB-C plug.** Cheap interim fix: moulded TPU plug in the existing
opening for sweat deflection. *Impact: low-medium · Effort: S · PCB: no.*

## Charging

**I16 — Charging cradle.** Stand-alone dock matching the base's R150
curve, case drops in USB-first; spring contact to the port or future
pogo pads. Kills the "lying flat on the cable" wear pattern. *Impact:
medium · Effort: M · PCB: no (works with today's USB-C; better with
I13's pogos).*

## RF & thermal

**I17 — Antenna keep-out.** ESP32 antenna sits at the board's −Y end
(J1 end). Rule for the case: no metal within ~10 mm of that end, thin
the end wall (1.2 mm) there, and keep the strap hooks symmetric so the
antenna end can't be buried under the buckle. Add `RF_KEEPOUT` block as
a documented no-material zone. *Impact: medium · Effort: S · PCB: no
(must be re-checked once Quilter fixes the antenna corner).*

**I18 — Thermal relief vs sealing.** AFEs + ESP32 in a sealed pocket
self-heat the skin-contact face (comfort limit ~41 °C per 60601).
Add `THERMAL_NOTES`: prefer conduction to the outer dome, never to the
skin floor; no vents through the skin side. If thermal testing demands
venting, put micro-slots on the −Y end wall (USB side), not the floor.
*Impact: medium · Effort: S (documentation) · PCB: no.*

## UX: LED, button, branding

**I19 — Light pipe for status LED.** Ø1.5 mm vertical pipe in the cover
from a board LED position → dome surface; doubles as emblem accent if it
exits inside the Q tail. *Impact: medium · Effort: S-M · PCB: YES —
needs an LED at a known position (or expose one on the +Z face).*

**I20 — Event button.** CAM/Zio-style patient event marker: a
cantilever-spring TPU button cap over a board tactile switch through the
cover, sealed by the cap's membrane. *Impact: medium · Effort: M · PCB:
YES — needs a switch on the board top face.*

**I21 — Emblem v2: light pipe "pulse".** The debossed QRST tail could
become a flush TPU inlay lit from below (the "key" tail glows on events).
Two-part insert in the existing grooves. *Impact: medium · Effort: M ·
PCB: YES (LED under the tail position).*

## Manufacturing & materials

**I22 — Heat-set inserts > self-tap pilots.** M2 brass inserts in the
base ears survive assembly cycles; self-tap pilots are fine for the first
5–10 builds. *Impact: low-medium · Effort: S (one constant + hole depth)
· PCB: no.*

**I23 — Printability path to IM.** FDM PETG now (documented); bridge via
MJF/SLS PA12 (better isotropy, IP-capable) then injection-moulded PC/ABS
or medical TPU. Design rules to add as comments: draft angles ≥1°, ribs
≤60% wall, uniform wall 1.5–2 mm — mostly satisfied already; the battery
pocket walls and ear bosses need draft. *Impact: medium · Effort: S-M ·
PCB: no.*

**I24 — Skin-side material note.** Add a README note: printed PETG is not
skin-qualified; for wear trials use ISO 10993-rated tape interface
(Tegaderm/hydrocolloid ring) so the case never touches skin directly —
same pattern as every adhesive patch surveyed. *Impact: medium · Effort:
S · PCB: no.*

## Ranking table

| # | Idea | Impact | Effort | PCB change? |
|---|---|---|---|---|
| I1 | Adhesive-wing skirt | ★★★ | M | no |
| I13 | Pogo charging, seal USB-C | ★★★ | L | **YES** |
| I10 | Flush optical window | ★★★ | M | no |
| I12 | Seam gasket groove | ★★★ | M | no |
| I8 | ECG snap studs in floor | ★★★ | M | **YES** |
| I6 | Strap-carried electrodes | ★★★ | L | no |
| I16 | Charging cradle | ★★ | M | no |
| I3 | Stepped/thinner dome | ★★ | M | no |
| I19 | LED light pipe | ★★ | S-M | **YES** |
| I17 | Antenna keep-out rule | ★★ | S | no |
| I2 | Tapered ends | ★★ | S | no |
| I4 | Alt. curvature variants | ★★ | S | no |
| I5 | Open-hook strap loops | ★★ | S | no |
| I7 | Detachable carrier | ★★ | M | no |
| I14 | Weld-closure variant | ★★ | M | no |
| I11 | Grommet tail slots | ★ | S | no |
| I15 | USB-C plug | ★ | S | no |
| I18 | Thermal notes | ★ | S | no |
| I20 | Event button | ★★ | M | **YES** |
| I21 | Glowing emblem inlay | ★ | M | **YES** |
| I22 | Heat-set inserts | ★ | S | no |
| I23 | IM design rules | ★★ | S-M | no |
| I24 | Skin-material note | ★ | S | no |
| I9 | Dry electrodes | ★ | M | **YES** |

**PCB-change flags:** I8, I9 (electrode interface), I13 (charge pads),
I19, I20, I21 (LED/switch placement). Everything else is pure enclosure.

## Recommended first tranche (no PCB dependency)

1. **I12 gasket groove** — biggest sealing win available today.
2. **I1 adhesive wing** — biggest comfort/attachment win; TPU-compatible.
3. **I10 flush optical window** — makes the skin side honest.
4. **I5 + I2** — cheap comfort/aesthetics wins.
5. **I17** — write the RF keep-out rule into constants before Quilter
   freezes the antenna corner.
