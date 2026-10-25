# CONSIGNED_PARTS — fix_v24

Parts supplied by the customer (consigned to JLC stock, or hand-fit)
rather than bought by JLC from LCSC. Convention follows the existing D12
handling: the row stays in BOM_JLC.csv with a **blank LCSC Part #** and
the MPN in the Manufacturer Part Number column, and stays in CPL_JLC.csv
so JLC places it from consigned stock. If the part is not in the JLC
consignment inventory at order time it is hand-fit.

| Ref | MPN | Manufacturer | Footprint | Purpose | Qty |
|---|---|---|---|---|---|
| D10 | NF2W757G-F1 | Nichia | vitalq:NF2W757G | White illuminator for U10 AS7341 spectral sensor | 1/board |
| D12 | XL0603NRC730 | (generic) | LED_0603_1608Metric | 730 nm deep-red/NIR auxiliary LED | 1/board |

## D10 — Nichia NF2W757G-F1

- Package: 3.0 x 3.0 x 0.65 mm, mid-power white, Vf typ 2.9 V @ 65 mA
  (0.18 W class). **checked** (Nichia product page)
- Colour/CRI: the NF2W757G-F1 family is sold in 5000 K and 6500 K ranks
  with Ra >= 95 (Optisolis build, R9 >= 85) — the design brief called it
  "80 CRI class"; the Nichia data shows a higher-CRI part. **checked**
- Quantity to buy: 1 per board x (2 assembled + 1 spare) = **3**, plus
  JLC consigned-part attrition. JLC's published consignment-attrition
  rule was not found on their site on 25 Oct 2026 — recommend ordering
  **5** (**guess**).
- Sourcing: not stocked at LCSC or JLC (checked 25 Oct). Distributor
  price typically ~£0.30–0.50 (**guess**).

## D12 — XL0603NRC730

- 0603 730 nm LED already consigned in the v23 flow; unchanged.
- Same attrition guidance applies — order 5 (**guess**).

## Also flagged "hand-fit or consign" in BUY_NOTES

J12 (FH12-14S-0.5SH FFC), U24 (RV-3028-C7 RTC), U14 (LSM6DSV320XTR if
still zero-stock), and the thin-JLC-stock top-ups (Q1/Q2/Q4, U19, U22,
U26) are *in* BOM_JLC/CPL_JLC with their LCSC codes; they are listed in
BUY_NOTES.md for a consign/hand-fit decision at order time — they are
**not** consigned parts in the export sense.
