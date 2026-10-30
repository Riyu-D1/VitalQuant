# case_v8 — strap design decision

> Frame note (v11): v8-era design document — any coordinates are the
> KiCad frame; physical `phys = (x, −y, z)`.

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
  corrosion, extra assembly). The current case_v8 contacts are conductive foam
  pads rather than strap-mounted electrodes, so studs add no electrical benefit. Rejected — but the slot pitch (38 mm) is compatible
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
- **Top bar 3.0 mm thick** (z6.6–9.6), spanning the posts — the member
  the strap loops. Strap wrap edges: fin plan corners R1.2 + 45° chamfers
  on all three outboard bearing edges AND the channel's inboard top edge;
  channel end-wall radii R0.8 (no knife edge to fray elastic).
- **Strap channel interior 30.8 mm** (y12.1–42.9) × 2.05 mm clear height
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
  the flat face + TPU gasket spread pressure; actual contact loading and its
  variation with breathing/motion require physical verification.
- **Skin face stays flat** — a camber was evaluated and rejected: the
  sternum's transverse curve over a 62 mm pod is small, and a curved
  face would unload edge contacts (including J7) relative to the centre, changing
  contact loading. The generous perimeter loft (45°
  chamfer + bezier) provides the comfort contour instead.
- Bar strength (computed in FIT_REPORT): 3.0×4.6 mm section, 30.8 mm
  span, 40 N design pull → worst-case point-load σ ≈ 29 MPa < PETG
  ~50 MPa yield (distributed strap load ≈ 14.6 MPa — also the 1e6-cycle
  lug fatigue check). Posts in shear ≈ 1.2 MPa.

## v22 update (case_v10): R lug becomes a raised closed tunnel

The V22 board's east power tab (x≤53.6, y20–29.9, carrying Q6/D33 to
z≈6.1) bulges the +x wall to x55.5 — exactly where the R staple's open
channel would sit. The strap line cannot step in y (it must stay centred
on the electrode row), so the R lug is redesigned as a **closed tunnel**:
channel raised to z 6.5–8.5 (clears the tallest tab part +0.3 mm air)
and bored straight through the bulge wall + clamp ledge (x 49.4–57.8).
The fin grows to cover (top 10.65 mm; bar 2.1×4.8 mm → σ=38.2/19.1 MPa
@40 N, still under limits). The strap threads the tunnel rather than
wrapping a bar — functionally identical retention, slightly slower to
thread. L lug is unchanged (open staple, bar 3.55 mm). Envelope:
64.8 × 92.15 × 10.65 mm.
