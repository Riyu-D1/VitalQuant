# case_v6 — strap design decision

## Research summary (~20 min)

- **Polar H10 / Garmin HRM-Dual / Wahoo TICKR:** the pod attaches to the
  elastic strap with **two metal snap studs at 45 mm spacing** — the studs
  double as the ECG electrodes. Straps are soft elastic **~30 mm × 1.3 mm**
  (the dominant replacement-strap standard; some older Polar soft straps
  are 25 mm). Retention is hardware, not printed geometry.
- **Clip-in cradles** (e.g. running-pod clips) add a second rigid part and
  bulk — rejected for a 62×82 mm pod meant to lie flat on the sternum.
- **Thread-through slots/staple lugs** are the only fully-printed option:
  a generic elastic strap loops a bar, or threads a closed slot, and is
  retained by its own tension (plus a pinch gap or sewn loop).

## Decision: two open-staple slot lugs, 30 mm strap

One lug on each ±x side wall — the strap runs horizontally around the
chest; the pod is worn vertically (USB-C down). Chosen over:

- **Snap studs (45 mm):** would match the Polar/Garmin standard, but needs
  metal hardware pressed into the case (new BOM parts, galvanic sweat
  corrosion, extra assembly). Our contacts are pogo pins, so studs give no
  electrical benefit. Rejected — but the slot pitch (38 mm) is compatible
  with screwing two 45 mm-spaced studs into the lug posts later if ever
  wanted.
- **Closed slot ring:** structurally strongest, but in the available
  5.25 mm wall band (skirt top z4.35 → lid z9.6) a closed ring cannot fit
  a ≥3 mm slot plus ≥2 mm top+bottom chords without exceeding ZLID or
  burying the snap skirt. Rejected on fit grounds, not principle.
- **v5-style 4 narrow staples (2×13 mm/side):** fit no standard strap;
  two thin straps would be awkward to source and wear.

## Geometry (per side, mirrored)

- One staple fin standing proud of the wall: x OX±(−0.4..4.2) —
  **4.6 mm deep**, same protrusion as v5 → envelope unchanged 62.2 mm.
- Two end **posts 3.6 mm thick** (y8.5–12.1 and 42.9–46.5), full fin
  depth × z4.55–9.6 — no thin weak bridges; root is the whole wall-face
  bond plus the skirt-top land.
- **Top bar 2.7 mm thick** (z6.9–9.6), spanning the posts — the member
  the strap loops. Strap wrap edge: fin plan corners R1.2 + a 45° chamfer
  on the outboard top edge (no knife edge to fray elastic).
- **Strap channel interior 30.8 mm** (y12.1–42.9) × 2.35 mm clear height
  — sized for the standard **30 mm** elastic (1.3–1.5 mm thick), also
  takes 20–28 mm straps (looser).
- **Pinch retention 0.2 mm:** post bottoms at z4.55 vs skirt top z4.35 —
  a wrapped loop cannot slip off the post ends.
- Placement clears every shell exit in 3-D: posts miss the J12 mouth
  (y47+), J2 riser (y3.6–7.0), J9 channel (y12.6–22.4 exits *below* the
  fin at z≤4.5), and only grazes J10's upper mouth void — the tail exits
  at z≤4.5, 1.25 mm under the post bottom.

## Load path → even skin pressure

- Strap tension is radial (±x); the pull line passes within **0.5 mm** of
  the skin-face plan centroid (lug channel center y27.5 vs face centroid
  y≈27) — pressure spreads evenly over the flat contact face.
- The sensor/contact zone (y45–65) sits inside the same rigid shell;
  the flat face + TPU gasket even the pressure, and the pogo pins' 0.8 mm
  stroke reserve absorbs residual unevenness from breathing/motion.
- **Skin face stays flat** — a camber was evaluated and rejected: the
  sternum's transverse curve over a 62 mm pod is small, and a curved
  face would unload the edge-row contacts (J7 x1–4, J3 x42–47) vs the
  centre, breaking even protrusion. The generous perimeter loft (45°
  chamfer + bezier) provides the comfort contour instead.
- Bar strength (computed in FIT_REPORT): 2.7×4.6 mm section, 30.8 mm
  span, 40 N design pull → worst-case point-load σ ≈ 33 MPa < PETG
  ~50 MPa yield (distributed strap load ≈ 17 MPa). Posts in shear ~2 MPa.
