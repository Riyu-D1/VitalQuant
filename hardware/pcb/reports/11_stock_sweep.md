# Stock sweep — BOM_JLC.csv vs lcsc.com product pages

Swept 2025-07-03 against the **fixv19b** BOM (C71629 already substituted for C1525).
Source: each part's `lcsc.com/product-detail/<code>.html` embedded product JSON
(`stockNumber`, `minBuyNumber`, `isForeignOnsale`, domestic/overseas ship buckets).
Full data: `11_stock_sweep.csv` (81 rows — every BOM_JLC line).

**Methodology caveat:** lcsc.com catalog stock ≠ JLC SMT assembly stock for
extended/sourcing codes. A `stockNumber=0` on a 7+ digit code often means "JLC
sources to order" rather than truly unavailable. The definitive check is the JLC
BOM tool at order upload — flagged rows below are grouped by confidence.

## Confirmed problems — normal catalog codes, genuinely 0 stock

| LCSC | Value | Refs | Qty | Note |
|---|---|---|---|---|
| C86295 | 22µF X5R 0603 | C7, C45 | 2 | The ESP32 bulk + U3 VOUT bulk — the very caps H3/E1 flagged |
| C25744 | 10k ±1% 0402 | R5..R11, R14, R49, R52, R66... | 28 | Highest-count resistor line |
| C283954 | 5.11M ±1% 0402 | R43, R45 | 2 | CP2102N VBUS divider top leg |
| C853138 | 22.1k ±0.1% 0402 | R63 | 1 | CP2102N VBUS divider (precision) |
| C25896 | 47.5k ±1% 0402 | R64 | 1 | CP2102N VBUS divider |
| C5378715 | FH12-14S-0.5SH(55) | J12 | 1 | FPC connector — not a jellybean, no drop-in |
| C6097400 | 301k ±1% 0402 | R66 | 1 | PRESALE flag |

## Extended/sourcing codes — lcsc.com stock 0, likely JLC-sourced (verify in JLC BOM tool)

| LCSC | Part | Refs | Qty |
|---|---|---|---|
| C3013941 | ESP32-S3-MINI-1-N4R2 | U1 | 1 |
| C46898862 | LSM6DSV80XTR | U14 | 1 |
| C2986165 | W25Q512JVEIQ | U18 | 1 |
| C2829066 | RV-3028-C7 | U24 | 1 |
| C6705435 | SHT45-AD1B-R3 | U23 | 1 | **no catalog page at all** |
| C536262 | IM69D130V01 | U25 | 1 | **no catalog page at all** |

## Real but thin stock (< 100 pcs, qty-1 lines — orderable but fragile)

| LCSC | Part | Ref | Stock |
|---|---|---|---|
| C5349491 | MLX90632SLD-DCB-100-SP | U12 | 3 |
| C2649390 | TCA6408ARSVR | U19 | 13 |
| C650308 | AD5940BCBZ-RL7 | U7 | 15 |
| C882777 | ADS1292RIRSMT | U8 | 16 |
| C2655172 | SFH 7072 | U16 | 39 |
| C544431 | TPS61241YFFR | U15 | 67 (already flagged in FAB_NOTES) |
| C5328762 | MAX86141ENP+T | U22 | 84 |
| C2871092 | CSD13380F3T | Q1, Q2, Q4 | 40 for qty 3 — 13x cover, OK marginally |

## Consigned (no LCSC code — expected)

D10 NF2W757G-F1, D12 XL0603NRC730 — customer-consigned, stock is on the user.

## Note on the C1525 → C71629 substitution

The BOM change was made per explicit instruction (user observed C1525 OOS,
min-order 100, "Notify Me"). At sweep time C1525's own page reports
**7,146,900 in stock** (shipImmediately, min buy 100) — the OOS reading could
not be reproduced. C71629 (767,300 stock, identical 100nF/16V/X7R/0402 spec)
is verified orderable either way, so the substitution is harmless; revert is
trivial if the Samsung part is preferred. Fallback C105883 confirmed on the
alternate list at 991,900 stock.

## What still needs a decision

1. **C86295 (22µF), C25744 (10k), C283954, C853138, C25896, C6097400** —
   genuine catalog OOS; jellybean values with drop-ins everywhere, but per
   instruction no substitutes were chosen. Awaiting user picks.
2. **C5378715 (FH12-14S connector)** — OOS and not a jellybean; alternatives
   need footprint verification (0.5 mm bottom-contact FPC family).
3. Extended-code zeros — verify inside the JLC BOM tool at upload; if JLC
   shows them sourced, they're fine.
4. Thin-stock ICs — qty-1 lines with 3–84 pcs; fine for a small run, risky
   for a second run or attrition.
