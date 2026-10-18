# VitalQ case_v6 — skin-contact BOM

The B.Cu pads sit at z = 3.4 mm, i.e. 3.4 mm above the skin plane (z=0).
Conductive inserts bridge the gap so every contact reaches past the skin
plane (FIT_REPORT rows "contact reach" / "U11 thermal slug face").

| Ref / location | Qty | Part / spec | MPN / source | Height used | Skin reach |
|---|---|---|---|---|---|
| J5 pads (5) | 5 | SMD spring-loaded pogo pin, Ø1.14 mm, 4.8 mm free / 4.0 mm working, gold finish | **Harwin P70-5100045** (loose; P70-5100045R for T&R) | 4.0 mm working (0.8 mm compression) | tip z = −0.60 mm (0.6 proud) |
| J6 pads (4) | 4 | same | P70-5100045 | 4.0 mm | −0.60 |
| J7 pads (4) | 4 | same | P70-5100045 | 4.0 mm | −0.60 |
| J13 pads (2) | 2 | same | P70-5100045 | 4.0 mm | −0.60 |
| J11 pads (3) | 3 | same | P70-5100045 | 4.0 mm | −0.60 |
| J3 pads (2) | 2 | same | P70-5100045 | 4.0 mm | −0.60 |
| U11 TMP117 | 1 | Thermal slug: Ø3.7 × 0.9 flange + Ø2.9 × 1.65 shaft + Ø2.4 nose, C110 copper, Ni/Au plate; turned part per `thermal_slug.stl` (also usable as the machining drawing) | custom (e.g. Wieland/ME1 copper; Ni ≥2 µm + Au flash for skin contact) | 2.7 mm + TIM | face z = −0.60 mm (0.6 proud) |
| U11 TIM | 1 | Thermal interface pad 0.5 mm, Ø3.0 die-cut | **3M 5590H-05** (or T-Global TG-A6200 0.5) | compresses to ~0.2 mm | — |
| U25 mic | 1 | Acoustic mesh patch Ø4.4 × 0.4 (recess in base floor, cavity side) | e.g. **Gore/3M adhesive acoustic mesh**, generic | — | seals port, keeps IP path |
| U23 SHT45 | 1 | Vapour-permeable membrane patch Ø4.6 × 0.4 (recess, Ø2.0 vent bore inside) | e.g. **Gore vents / generic PTFE membrane** | — | humidity reaches sensor |
| Skin gasket | 1 | TPU seal sheet 0.4 mm over the sensor zone (9 merged holes; `gasket.stl`) | print in TPU 95A | 0.4 mm | sweat barrier between apertures |
| Seal (optional) | — | One drop neutral-cure silicone in each electrode slot after pin fit, skin side | e.g. Dow 3140 | — | sweat barrier |

## Why pogo pins here

- Pad pitch is 1.7–2.2 mm → Ø1.14 mm body keeps ≥0.56 mm air gap between
  adjacent contacts (no shorts).
- Soldered SMD mount on the existing pads — no board changes needed.
- At working height 4.0 mm the gold spherical tip sits 0.6 mm proud of the
  skin plane with rated spring force — electrodes stay in contact under
  strap pressure and chest motion (free tip −1.4 mm; stroke reserves
  0.8 mm). 0.6 mm is the minimum protrusion that tolerates FDM flatness
  (±0.2 mm), strap compression and skin relief, without over-travel of
  the plunger — same value is used for the slug face so the temperature
  sensor path (TIM→slug→skin) presses flat like the electrodes.
- Alternative considered: conductive-silicone studs (custom mould, no
  MPN) — rejected for this rev because pads are 1.15 mm wide; a moulded
  stud array would risk inter-pad creep of conductive rubber. Pogo pins
  are rigid, isolated, and a real catalog part.

## J7 / J3 note (fix_v9)

J7 BioZ pads are on **B.Cu** (staggered 2×2 at x 1.1–3.6, y 60.7–62.9)
and contact the skin through a base floor aperture, exactly like
J5/J6/J11/J13 — 4 P70-5100045 pogos.

**J3** FSR pads are on **B.Cu** (pads at 42.0 / 44.2, y 59.85) and
likewise get a base floor aperture + 2 P70-5100045 pogos —
**20 pins total**. The strap-side FSR element (or conductive member)
contacts the skin the same way as the electrodes.

## Assembly notes

- Solder pogos BEFORE placing the board in the base (reflow or low-temp
  hand solder; mind the Ø1.14 barrel vs the 1.15 mm pad — tack with
  flux + minimal solder).
- Insert the board tips-first: the 20 pins slide through their floor
  apertures; verify each tip protrudes ~1.4 mm before snapping the top
  shell.
- Press the thermal slug into its stepped bore from the cavity side
  (flange seats in the Ø3.8 counterbore, face 0.6 mm proud), then place
  the 3M TIM pad on the slug top before seating the board.
- Lay the TPU gasket last (skin face, over the sensor zone) — its holes
  index on the apertures; the sheet is held flat by the skin.
