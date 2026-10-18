# VitalQ logo — design research

VitalQ: chest-worn wearable quantifying ECG, PPG/SpO2, bioimpedance and
respiration; ward + home use; mark must survive 16 px favicons, funding
documents and embossing on the 3D-printed enclosure.

## 1. Biosignal iconography & history

### Einthoven's triangle & the PQRST complex
- Willem Einthoven recorded the first useful human ECGs (string
  galvanometer, ~1901) and labelled each cardiac cycle's deflections
  **P, Q, R, S, T** — deliberately choosing mid-alphabet letters after
  Descartes' convention for points on a curve, leaving room at both ends.
  Sources: https://www.ahajournals.org/doi/10.1161/01.CIR.98.18.1937 ,
  https://pmc.ncbi.nlm.nih.gov/articles/PMC2435435/
- **Einthoven's triangle**: the three limb-lead electrode sites (right arm,
  left arm, left leg) form an *inverted equilateral triangle* with the heart
  at its centre. https://en.wikipedia.org/wiki/Einthoven%27s_triangle
- Take-away for the mark: the PQRST morphology is *canonical and specific* —
  small P bump, sharp Q dip, tall R spike, S dip, rounded T wave. Drawn
  accurately it reads as "cardiac science"; drawn as a zig-zag it reads as
  clip-art. The electrode triangle is a genuinely niche, insider reference.

### Rod of Asclepius vs caduceus
- The **Rod of Asclepius** (rough-hewn staff, *one* snake) is the true
  Greco-Roman symbol of healing. The **caduceus** (winged staff, *two*
  snakes) is Hermes' herald staff — trade/commerce — misapplied to medicine
  since the 19th c.; a 1992 survey found 76% of US hospitals misuse it while
  only 6% of doctors knew the correct one.
- Sources: https://www.thelancet.com/journals/lancet/article/PIIS0140-6736(05)77199-3/fulltext ,
  https://www.psychiatryonline.org/doi/10.1176/appi.ajp.2012.11121800 ,
  https://www.acpjournals.org/doi/10.7326/0003-4819-138-8-200304150-00016 ,
  https://pmc.ncbi.nlm.nih.gov/articles/PMC9582002/
- Design rule for us: if a serpent/staff were ever used it must be
  *single-snake* — but we avoid it entirely (overused, and a snake embosses
  poorly at 16 px).

### PPG waveform
- The photoplethysmogram has a steep **anacrotic** rise (systole) and a
  slower **catacrotic** fall carrying the **dicrotic notch** (aortic valve
  closure) — a distinctive asymmetric silhouette, unlike the ECG spike.
  AC pulsatile component rides on a DC baseline.
- Sources: https://www.frontiersin.org/journals/physiology/articles/10.3389/fphys.2021.808451/full ,
  https://pmc.ncbi.nlm.nih.gov/articles/PMC3394104/
- Take-away: a PPG curve is a different *shape language* than ECG — smooth,
  asymmetric, with a notch. Using it signals optical sensing, not "generic
  heartbeat".

### Impedance / bioimpedance
- VitalQ measures bioimpedance (EDA/respiration channels). The SI symbol is
  **Ω (ohm)** — a letterform that doubles as a horseshoe/arch and can host a
  waveform. (Impedance plethysmography: https://en.wikipedia.org/wiki/Bioelectrical_impedance_analysis)

### Quantification
- "Q" in VitalQ = quantification. Ticks, ruled scales, and measured
  intervals are the visual language; a clean geometric **Q** as container is
  the obvious letterform anchor.

## 2. Category audit — clichés and openings

Clichés saturating the space (avoid or subvert):
- **Generic heartbeat zig-zag line** through a heart/circle — the dominant
  trope (listed as the default concept in template guides:
  https://windowscape.org/9-logo-concepts-for-clinics-labs-and-medtech/).
  Neocordis explicitly rebranded *away* from it:
  https://www.felipesamir.com/work/neocordis
- **Shield + pulse + circuit nodes** AI-health mashup:
  https://contra.com/p/2vPekUWO-brand-identity-system-for-an-ai-health-tech-startup
- **Red cross**, heart silhouettes, stethoscopes, hexagon-pills.
- Competitors keep it abstract-minimal: **iRhythm** uses a low-key indigo
  wordmark with a small rhythm glyph (https://www.irhythmtech.com/us/en);
  Butterfly Network uses an abstract wing/identity, not a waveform.
- Opening: almost nobody uses the *accurate* PQRST morphology, the
  dicrotic-notch PPG curve, the Ω impedance form, or Einthoven's triangle —
  those are the insider references worth building from.

## 3. Colour & accessibility

- Healthcare defaults to **blue** (~2/3 of healthcare logos; trust, calm,
  competence). Best-practice hue ≈ 200–220°, moderate saturation; teal is
  the accepted differentiator that keeps blue's credibility while adding
  vitality. https://colorarchive.org/guides/color-palette-for-healthcare/ ,
  https://www.progress.com/blogs/using-color-psychology-healthcare-web-design ,
  NHS identity: https://www.england.nhs.uk/nhsidentity/identity-guidelines/colours/
- WCAG: aim high — clinical users skew older; target **AAA (7:1)** for
  critical use, ≥3:1 for graphical marks; never encode meaning in hue alone
  (~8 % of males are colour-blind — red/green pairs are out).
  GE HealthCare's brand rules echo this: https://brand.uat.gehealthcare.com/brand-foundations/color/
- **Chosen palette**:
  - `VQ-BLUE  #0B4EA2` (primary — ~8:1 on white, AAA)
  - `VQ-TEAL  #0E8F8F` (accent — PPG/optical channel)
  - `VQ-INK   #0E1B2C` (near-black, reversed backgrounds)
  - `VQ-WHITE #FFFFFF`
  Blue is the trust anchor; teal differentiates the optical/SpO2 half.
  Pair is safe for common colour-vision deficiencies (blue/teal vs ink
  differ in luminance, not just hue).

## 4. Concepts derived

1. **QRST monogram** — bold geometric **Q**; its tail is a correctly
   proportioned **PQRST complex** exiting the bowl. "Quantified cardiac",
   letterform-native, embosses well.
2. **Einthoven triangle** — three electrode nodes on an inverted triangle
   with the pulse node at centre; the insider ECG reference, abstract and
   clean at 16 px.
3. **Ohm wave** — **Ω** horseshoe drawn as one continuous **PPG curve**
   (anacrotic rise → dicrotic notch → catacrotic fall): bioimpedance +
   optical pulse in one stroke.
4. **Patch ring** — the product itself: rounded-square patch outline,
   concentric measurement rings, single sensor dot at centre — "the
   quantified point on skin". Most product-true; simplest geometry.
5. *(rejected)* Asclepius rod — overused and reads badly at 16 px;
   caduceus avoided on principle.

Recommended direction is argued in `RECOMMENDATION.md`.
