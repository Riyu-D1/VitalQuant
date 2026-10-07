# Task: VitalQuant (VitalQ) logo design

Do NOT git commit or push. Put all outputs in a NEW folder: ~/Downloads/VitalQ_Logo_v1/

## Context
VitalQ is a chest-worn wearable that quantifies vital signs (ECG, PPG/SpO2, bioimpedance and respiration). It streams data to a hub and is meant for ward and home use. The logo will also appear in funding applications, so it must look professional and carry meaning.

## 1. Deep research first: research.md, with source URLs
Cover:
- Medical and biosignal iconography and its history:
  - Einthoven's ECG triangle and the PQRST complex.
  - The Rod of Asclepius vs the common misuse of the caduceus.
  - The photoplethysmography waveform.
  - Impedance and the ohm symbol.
- Quantification and measurement symbols.
- Visual identities of medtech and wearable brands. Identify the clichés to avoid (generic heartbeat lines, hearts, crosses) and how to stand apart.
- Colour psychology in clinical settings, and accessibility (WCAG contrast, colour-blind safety).
- From niche references, derive AT LEAST 4 distinct concepts, each with a written rationale.

## 2. Build the logos
- Hand-author clean vector SVGs using geometric construction. No AI image generation and no stock clip art.
- For each concept, make:
  - A mark and a wordmark lockup.
  - Monochrome, colour and reversed versions.
- Each concept must stay legible at 16 px (favicon) and be simple enough to emboss on the 3D-printed case.
- Use an open-licence font (Inter, IBM Plex or Space Grotesk), or convert the text to paths so the SVGs are self-contained.
- Render PNGs with rsvg-convert or cairosvg run via uv. Keep installs tiny: reuse ~/VitalQuant-place/case/.venv if possible (e.g. `uv pip install --python ~/VitalQuant-place/case/.venv cairosvg`). No conda.

## 3. Deliverables in ~/Downloads/VitalQ_Logo_v1/
- research.md
- SVG and PNG files for every concept and variant, including 16 px and 32 px favicon tests.
- comparison_sheet.png: a one-page sheet showing all concepts side by side.
- RECOMMENDATION.md: the strongest concept and the reasons for choosing it.

When done, report the folder contents and your recommendation.
