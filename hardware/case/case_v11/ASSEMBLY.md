# VitalQ case_v11 — pogo-electrode assembly

Orientation: B.Cu (skin side) faces the base; USB-C, buttons and the D25
status LED face outward. The battery occupies the separate snap-on bay
cup on the lid. Skin contact is by **20 spring pogo pins** that rise
through the floor apertures onto the board's electrode lands
(J5×5, J6×4, J7×4, J11×3, J13×2, J3×2) — the v8 wired-lead scheme is
gone; there are no loose signal wires.

> **Prototype safety:** this enclosure and contact scheme are not a
> validated medical device or clinical-use instruction. Verify electrical
> topology, current limiting, contact materials and patient protection
> independently. Begin with bench testing; skin-contact use requires its
> own biocompatibility and safety review.

## 1. Parts and preparation

1. Print `case_base`, `case_top`, `bay_cup`, `pin_plate`,
   `battery_door` in PETG; print `gasket`, `usb_flap` and two
   `button_cap` parts in TPU 95A as in `PRINT_NOTES.md`. Inspect all 20
   electrode apertures (through-slot + skin-side chamfer + cavity-side
   45° funnels), the optical/vent/mic openings, snap features, strap
   lugs and the R-side strap tunnel; deburr without thinning the 1.6 mm
   floor or the aperture edges.
2. Source 20 spring pogo pins matching the modeled envelope
   (Harwin P70-5100045 class: Ø1.14 barrel, ~4.8 mm free length,
   ~4.0 mm working height, rounded tip). Confirm the barrel seats in
   each aperture with the tip 0.6 mm proud of the skin face at working
   compression.
3. Identify and mark each of the 20 lands and nets before inserting
   anything: J5 (1–5), J6 (1–4), J7 (1–4), J11 (1–3), J13 (1–2),
   J3 (1–2). The land-to-net table is in `CONTACTS_BOM.md`.
4. Press the U11 thermal slug (`thermal_slug.stl`, C110 Ni/Au) into the
   stepped bore at (20.2, 50.4) from the cavity side until seated;
   verify the face is 0.60 mm proud. Place the Ø3.0 × 0.5 mm TIM pad on
   the slug top. Install the acoustic mesh and the U23 membrane in their
   cavity-side recesses.
5. Lay the 0.40 mm TPU gasket on the skin face over the sensor zone;
   align the optical, humidity, microphone and slug openings. The gasket
   leaves every electrode aperture open (merged holes around J7/J6 and
   J3/J11 where the webs were sub-printable).

## 2. Seat the board and set the contacts

6. Insert the 20 pogos into their apertures from the skin side (barrel
   in the bore, tail toward the board). The cavity-side funnels
   self-centre each land's pad onto the pogo tail during board drop-in.
   Each pogo tail should sit on its land; a conductive-rubber or
   soldered tail, per the chosen contact method, must be verified
   before skin use.
7. Hold the PCB horizontally, B.Cu facing down. Align H1 (0.6, −2.19)
   and H2 (−0.15, −7.18) with the base locating pins and seat the board
   on its peripheral support. The north antenna tab and east power tab
   drop into their shaped pockets — do not force it.
8. With the board unpowered, verify each pogo's continuity to its
   intended land and pairwise isolation between adjacent lands. Record
   results; repeat after a gentle press-cycle.
9. Solder the protected 402535 battery leads to J2 with polarity
   verified (red VBAT, black GND); route the pair through its floor
   groove and the lid feed-through hole inside the bay footprint.
   Tie the pair around the two integral posts flanking the J2 exit
   (strain relief) without pinching insulation.

## 3. Close and finish the enclosure

10. Press the two TPU button caps into the top-shell guide bores from
    the cavity side and seat the light pipe over D25. Align the lid's
    USB-C opening toward −y (the wear chevron/ECG mark points −y) and
    press the perimeter until all eight snap beads engage.
11. Set the bay cup on the lid; flip the pin plate pins-down and press
    until its prong barbs engage. Install the protected battery and
    slide the battery door into its tracks.
12. Press the USB dust-flap pin into its anchor pocket (east of the
    grip corridor); install the J8 plug if desired. Thread the
    30 × 1.3 mm strap: **L side** loops under the staple bar as before;
    **R side** threads straight through the raised tunnel (z 6.5–8.5)
    that crosses the east power tab — it is a closed bore, so feed the
    strap through rather than wrapping the bar.
13. After closure, repeat continuity and pairwise isolation on every
    pogo-to-land path and a gentle-flex check. Then perform a
    current-limited, low-level functional bench test before any skin
    contact. Electrode materials, contact pressure, analog performance
    and skin compatibility remain independent validations.
