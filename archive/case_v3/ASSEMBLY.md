# VitalQ case_v3 — ASSEMBLY

Orientation note: **B.Cu (skin side) faces the base** — i.e. electrodes and
the optical sensors point down toward the chest; USB-C, buttons and the LED
face outward/up.

## 1. Prep

1. Print `case_base`, `case_top`, 2× `button_plunger`, `light_pipe`
   (see PRINT_NOTES.md). Inspect apertures and the snap groove/beads.
2. **Skin contacts (per CONTACTS_BOM.md):** solder one Harwin
   **P70-5100045** pogo pin onto *each* electrode pad of J5 (5), J6 (4),
   J13 (2) and J11 (3) — 14 pins, all on B.Cu. The Ø1.14 mm barrels sit on
   the 1.15×1.7 mm pads; adjacent pads are 1.7–2.2 mm pitch so use minimal
   solder and verify no bridges.
3. Press the **U11 thermal slug** (`thermal_slug.stl` — Ø3.55×0.62 flange,
   Ø2.95 shaft, Ni-plated Cu) into the stepped bore at (20.2, 50.4) from the
   cavity side until its flange seats; face ends flush with the skin plane.
   Place the Ø3 × 0.5 mm 3M 5590H-05 TIM pad on the slug top.
4. Solder the **402535 LiPo** leads to J2 (B side) — or leave the battery
   leads routed for the bay: wires run in the floor groove (y 3.4–6.6,
   z 1.0–1.6), up the left-wall riser portal (y 3.6–7.0) and through the
   top-shell notch into the F cavity. ~30 AWG or thin silicone wire.
5. Place an adhesive acoustic gasket (Ø4 mm, ≤0.7 mm) over the Ø1.6 mic
   port in the base floor at (28.94, 36.15) — it aligns with the Ø0.8 NPTH
   sound hole in the PCB under U25.

## 2. Seat the board

6. Drop the PCB B-side-down into the base — guide the 14 pogo tips into
   their floor slots as the board seats. The two Ø1.9 boss pins pass
   through mounting holes **H1 (0.6, −2.19)** and **H2 (−0.15, −7.18)** —
   these set the board position; the board edge rests on the 1.5 mm
   perimeter ledge (B surface flush at z = 3.4).
7. Check that nothing is pinched:
   - J12's FFC tail exits the left-wall slot (y 45.4–59.7) *below* the seam.
   - J9 (y 12.6–22.4) and J10 (y 38.8–53.0) tails route through the
     right-wall notches.
   - J1 USB-C body sits in its pocket; the receptacle mouth faces −y.
   - The four corner-relief wells accept the sharp board corners.
8. Verify contact protrusion: each pogo tip should stand ~1.4 mm proud of
   the skin surface free-hanging and compress to ~0.6 mm proud at its 4.0 mm
   working height under strap pressure. The U11 slug face is flush with
   the skin plane (TIM compressed between slug and package).

## 3. Close the case

9. Press the **button plungers** into the top-shell guide holes from the
   inside (head toward the board). Place the **light pipe** in its Ø2.8
   pocket over D25 (4.0, 42.9).
10. Lay the 402535 cell in the battery blister bay (25.8 × 35.8 mm);
   route its leads through the Ø3.5 hole at (6.6, 8.2) down to J2.
11. Align the top shell (USB-C window toward −y, over J1) and press down
   around the perimeter — the six snap beads click into the base groove
   (~0.5 mm engagement each). Verify an even seam.
12. Optional: drive M2 self-tap screws through the top pilot holes into
    the H1/H2 bosses for a serviceable-permanent joint.

## 4. Function check

13. USB-C: the window admits a standard plug (window 9.6 × 3.8 mm; the
    12 × 6.5 mm overmould stops at the wall face, 1.15 mm stand-off to the
    receptacle mouth — verified clear).
14. RESET (SW1, right side y≈34.6) and BOOT (SW2, left side y≈57.2) click
    through the plungers; D25 glows through the light pipe.
15. J8 Tag-Connect is reachable through the lid plug opening
    (x 22.7–30.2, y 49.9–57.0) — keep a small TPU plug or Kapton patch
    over it when not in use.
16. J7 BioZ pads (11.2–17.8, 62.85) are on **F.Cu** — they cannot touch
    skin. Use the top-edge window to attach **wired remote chest
    electrodes** (soldered tail or conductive-fabric pigtail). J3 FSR pads
    (25.4–27.6, 62.85) share the adjacent window for the same purpose.

## 5. Wear

17. Thread a 20–25 mm elastic chest strap through the four lug slots
    (two per side). The optical well (U16/D10–D12/U10), thermal window
    (U11/U12) and SHT45 vent must contact skin; the light-barrier webs
    press lightly to prevent optical crosstalk.
18. Do not cover the mic port (base, 28.9, 36.2) or the BME280 vent
    (lid, 32.4, 32.0) with tape or adhesive.
