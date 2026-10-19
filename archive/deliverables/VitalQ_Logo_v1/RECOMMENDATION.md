# VitalQ logo — recommendation

## Recommended: **C1 — QRST monogram** (`c1_qrst_*`)

A geometric **Q** whose tail is a correctly-proportioned **PQRST complex**
exiting the bowl. It is the strongest candidate because it compresses the
entire brand idea into one letterform:

- **Q** = the name's own anchor letter ("quantified").
- The tail is the *canonical* cardiac complex — P bump, QRS spike, T wave —
  not the generic heartbeat zig-zag that saturates the category. Insiders
  (clinicians, reviewers, regulators) will recognise it; lay viewers just
  read "a Q with a pulse".
- Works in one stroke group + one circle: trivially embossable on the
  3D-printed enclosure cover (ring + tail survive at ~0.4 mm relief), and
  legible at 16 px (see `*_16.png` renders — reads as "Q + squiggle").
- Wordmark: **Space Grotesk 620** (OFL), converted to outlines — the SVGs
  are fully self-contained, no font dependency.

### Variants shipped
`c1_qrst_mark_{color,mono,reversed}` + `c1_qrst_lockup_{color,mono,reversed}`
in SVG and PNG, plus 16/32 px favicon tests.

### Palette
`#0B4EA2` (primary blue, ~8:1 on white — passes AAA for graphical marks),
`#0E8F8F` (teal accent for the trace — the optical/PPG channel), ink
`#0E1B2C`, white. Meaning never rides on hue alone; the mono variant is the
same geometry in a single ink, and reversed is white-on-ink.

## Runners-up

- **C2 — Einthoven triad**: the insider ECG reference (3 electrode nodes +
  central pulse). Very distinctive; slightly busier at 16 px and more
  "diagnostic-instrument" than "wearable brand". Strong choice if the
  audience is purely clinical.
- **C4 — Patch ring**: product-true (the device itself), simplest
  geometry, best embosser. Weaker story — reads "target/sensor" and is the
  least ownable.
- **C3 — Ohm wave**: clever (Ω arch = PPG pulse incl. dicrotic notch) but
  the waveform reading is subtle; risks reading as a generic arch.
- **C5 — Quantized PQRST**: bars sized to PQRST amplitudes; unique and
  honest, but "signal bars" flirts with telecom audio branding.

## Usage notes

- Use the **lockup** for documents/site headers; **mark** for favicons,
  app icon, and the enclosure emboss (mono version, stroke ≥ ~0.4 mm).
- On dark surfaces use `*_reversed` (white artwork on `VQ-INK`), never
  recolor the teal to white-adjacent hues — keep the pair or go full mono.
- Clearspace: ≥ the tail stroke width (≈7 % of mark width) on all sides.

See `comparison_sheet.png` for all concepts side-by-side and
`research.md` for the rationale sources.
