# VitalQ case_v7 — ASSEMBLY

Orientation note: **B.Cu (skin side) faces the base** — i.e. electrodes and
the optical sensors point down toward the chest; USB-C, buttons and the LED
face outward/up. The battery lives in a **separate snap-on bay cup** on the
lid — fully screw-free assembly, every joint is a verified snap fit.

## 1. Prep

1. Print `case_base`, `case_top` (PETG), `bay_cup`, `pin_plate`,
   `battery_door` (PETG), `gasket`, `usb_flap`, 2× `button_cap` (TPU),
   `light_pipe` (clear) — see PRINT_NOTES.md for orientation/settings.
   Inspect apertures and the snap groove/beads; deburr the skin-face edges.
2. **Skin contacts (per CONTACTS_BOM.md):** solder one Harwin
   **P70-5100045** pogo pin onto *each* electrode pad of J5 (5), J6 (4),
   J7 (4), J13 (2), J11 (3) and J3 (2) — 20 pins, all on B.Cu. The
   Ø1.14 mm barrels sit on the 1.15×1.7 mm pads; adjacent pads are
   1.7–2.2 mm pitch so use minimal solder and verify no bridges.
3. Press the **U11 thermal slug** (`thermal_slug.stl` — Ø3.7×0.9 flange,
   Ø2.9 shaft, Ni-plated C110) into the stepped bore at (20.2, 50.4) from
   the cavity side until the flange seats in its counterbore; the face
   stands **0.6 mm proud** of the skin plane (see FIT_REPORT note). Place
   the Ø3 × 0.5 mm 3M 5590H-05 TIM pad on the slug top.
4. Solder the **402535 LiPo** leads to J2 (B side): the wire pair runs in
   the floor groove (y 3.4–6.6, z 1.0–1.6) to the left-wall riser portal
   (y 3.6–7.0) — the leads reach the F cavity through the top-shell seam
   notch, then up through the Ø3.2 lid hole at (6.6, 8.2) inside the bay
   footprint. ~30 AWG or thin silicone wire. The two J2 strain-relief
   posts at (7.6/10.9, 9.0) pin the wire pair against the cavity ceiling
   — thread the wires between them before closing.
5. Place the Ø4.4 acoustic-mesh patch in the cavity-side recess over the
   Ø1.6 mic port at (28.94, 36.15) and the Ø4.6 membrane patch in the
   U23 recess at (7.6, 47.0); both recesses are 0.4 deep with the through
   bore inside (Ø1.6 mic / Ø2.0 vent). Peel-and-stick, flush fit.

## 2. Seat the board

6. Lay the **TPU gasket** (`gasket.stl`, 0.4 mm) on the base skin face
   over the sensor zone (x −0.45–48.55, y 33.0–65.0): the 9 merged holes
   line up with the electrode apertures, mic port, slug face and the
   J7/J13 opening. It is held flat by the skin, not glued — reposition if
   a hole edge covers a pad.
7. Drop the PCB B-side-down into the base — guide the 20 pogo tips into
   their floor apertures as the board seats. The two Ø1.9 boss pins pass
   through mounting holes **H1 (0.6, −2.19)** and **H2 (−0.15, −7.18)** —
   these set the board position; the board edge rests on the 45° seat
   ring (B surface at z = 3.4).
8. Check that nothing is pinched:
   - J12's FFC tail exits the left-wall slot (y 45.4–59.7) *below* the seam.
   - J9 (y 12.6–22.4) and J10 (y 38.8–53.0) tails route through the
     right-wall notches.
   - J1 USB-C body sits in its pocket; the receptacle mouth faces −y.
   - The four corner-relief wells accept the sharp board corners.
9. Verify contact protrusion: each pogo tip stands ~1.4 mm proud of the
   skin surface free-hanging and compresses to 0.6 mm proud at its 4.0 mm
   working height under strap pressure; the U11 slug face sits 0.6 mm
   proud with the TIM compressed between slug and package.

## 3. Close the case

10. Press the two **button caps** into the top-shell guide bores from the
    cavity side (dome toward the lid): the barb clicks into the 8.35–8.9
    counterbore, leaving ~0.4 mm travel (SW actuation ~0.25–0.3 mm).
    Push the **light pipe** into the Ø2.8 pocket over D25 (4.0, 42.9)
    until the dome seats in its counterbore — tip lands ~6.1, flush with
    the D25 top (6.04–6.10).
11. Align the top shell (USB-C window toward −y, over J1) and press down
    around the perimeter — the eight wedge snap beads click into the base
    groove (0.35 mm catch). Verify an even seam; to reopen, slip a tool
    into either skirt-bottom notch (−x edge y≈0.2, +y edge x≈39.2) — the
    notches cut the skirt only; the wall behind stays sealed, so lever
    the skirt lip outward and peel the bead off the groove.
12. **Bay cup:** set the `bay_cup` tray flat on the lid top (door-groove
    end toward −y). The 6 Ø3.2 floor holes sit over the lid's matching
    holes. Hold it down, flip the `pin_plate` pins-down, and press it
    into the cup interior until the 12 prong barbs click past the lid's
    cavity face (catch face at cup-z −1.65, 0.30 mm engagement, 2.9 %
    prong strain — reusable). The plate floor lands at cup-z 0.85.
13. Drop the **402535 cell** (≤24.3 × 35 × 4.0) onto the pin plate between
    the 0.35 press ribs; route its leads through the floor notch
    (x 5.0–8.2, y 6.6–9.8) to the lid hole at (6.6, 8.2). Slide the
    `battery_door` into the −y entry slot; its edge panel rides the
    x-side grooves (cup-z 5.15–6.05) until the leading-edge nub detents
    into the +y interior notch.
14. Optional **J8 plug**: press the TPU plug into the Tag-Connect recess
    in the lid top (22.0–30.9 × 49.2–57.7) when the port is not in use.
15. **USB dust flap:** push the flap's tether pin into the anchor pocket
    in the lid top at (34.5, −11.3) until the barb seats; fold the tongue
    into the USB-C window — the lip overlaps the outer face. TPU flexes;
    swing it aside to plug in.

## 4. Function check

16. USB-C: the window admits a standard plug (9.6 × 3.7 mm shell opening;
    the 12 × 6.5 mm overmould stops at the wall face, 0.95 mm stand-off
    to the receptacle mouth — verified clear).
17. RESET (SW1, x≈45.3 y≈34.6) and BOOT (SW2, x≈2.0 y≈57.2) click through
    the caps; D25 glows through the light pipe dome.
18. J8 Tag-Connect is reachable through the lid window (x 22.7–30.2,
    y 49.9–57.0) with the plug removed.
19. J7 BioZ pads (x 1.1–3.6, y 60.7–62.9) are on **B.Cu** — 4 pogo pins
    touch skin directly through the base aperture, the same as
    J5/J6/J11/J13. J3 FSR pads (42.0 / 44.2, 59.85) likewise get 2 pogos
    through their own aperture — the strap-side FSR element contacts the
    skin the same way; no wired-remote window is needed.

## 5. Wear

20. Fit a **30 mm elastic chest strap** (the common Polar/Garmin-style
    replacement width, ~1.3–1.5 mm thick — see STRAP_DESIGN.md). The
    debossed chevron on the lid's −y chin points the wear direction
    (+y / sensor end toward the neck). On each side: feed the strap end
    into the lug's open channel from below, up and over the top bar, and
    fold the end back onto the incoming leg — a hairpin wrap around the
    3.0 mm bar between the 3.6 mm end posts (all strap-bearing edges —
    outboard plus the channel's inboard top edge — are 45°-chamfered and
    the channel end walls are R0.8, so the elastic can't fray). Sew or
    bar-tack the fold for a permanent fit; the 0.2 mm pinch gap under
    each post keeps the loop captive. Narrower 20–28 mm straps also work
    (slightly looser).
21. The gasket zone (electrode apertures, optical wells, slug face, mic,
    U23 membrane) presses against the chest — the TPU gasket bridges the
    skin-side seams for sweat resistance. Do not cover the mic port
    (base, 28.9, 36.2), the BME280 vent (lid, 34.8, 32.0) or the U23
    humidity membrane (7.6, 47.0) with tape or adhesive.
