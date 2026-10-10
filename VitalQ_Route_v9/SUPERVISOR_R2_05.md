# Supervisor instructions R2 #05 — decision on the J8 no-via area over U6

J8 = Tag-Connect TC2030-IDC-NL on F at (26.45,53.2), locked. Its footprint no-via/no-fill F rule area covers x 25.18–27.72, y 50.06–51.34, i.e. U6 balls B1–B4, C1–C4, D1–D4 (and E-row annuli touch it). v8 already granted two POFV exceptions there (D1/B4 GND) using per-via notches (see ../VitalQ_Route_v8 j8_pofv_policy.json, scripts/j8_pofv.py).

DECISION (user granted structural authority): extend the SAME v8 mechanism to every U6 via-in-pad you need under J8.
- Allowed: 0.25/0.15 via-in-pad exactly at U6 ball centres, POFV (epoxy-filled + copper-capped, planar), each with its own notch in the J8 F no-via area of exactly the via annulus + 0.002 mm, recorded in j8_pofv_policy / j8_permitted_vias (new R2 file is fine). Rationale: the area exists so the Tag-Connect head sits on a flat surface; a POFV cap is planar and soldermask-covered.
- Not allowed: any non-POFV via, any between-ball via, any track/via change elsewhere in the J8 area, any change to hv_* rule areas.
- Flag all of them in REPORT.md and fab/FAB_NOTES.md as "POFV mandatory (planar, under Tag-Connect J8 landing)".
- Fallback only if this proves impossible: rotate J8 180 deg and shift dx +0.50 mm (-> 26.95,53.2): verified courtyard-legal, its keepout then lands at x 25.68–28.22, y 55.06–56.34 covering 0 U6 balls and 0 existing vias; but it needs re-attachment of all 6 J8 nets and relocation of the GND via (25.798,52.987), +3V3 via (24.589,54.532) and ESP_EN via (29.6,53.283) which would collide with the moved pads/NPTH holes. Use only if the notch approach fails.

So: proceed with the full U6 plan of SUPERVISOR_R2_04 including the J8-covered balls (B2, B3, C4, D2, D4, E2, E3, E4) using notched POFV exceptions.
