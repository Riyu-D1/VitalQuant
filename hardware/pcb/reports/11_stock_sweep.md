# Stock sweep — BOM_JLC.csv vs LCSC

Swept 2025-07-03, updated for **fixv19c**. Full data: `11_stock_sweep.csv`
(81 rows — every BOM_JLC line, with `stock_source` column).

## ⚠ Methodology caution — read this first

**The lcsc.com HTML product page is NOT reliable for stock.** Its "Out of
Stock / Notify Me" block is not live state: C1525 displayed it while carrying
**7,144,700 pcs** in the wmsc detail API
(`https://wmsc.lcsc.com/ftps/wm/product/detail?productCode=<CODE>`, field
`stockNumber`). The first sweep (fixv19b) scraped the HTML and was wrong on
that part. All flagged rows below were **re-verified through the wmsc API** —
treat the CSV's `stock_source` column as the record of which figure came from
which endpoint. Always confirm with the API or the JLC BOM tool; never trust
the HTML banner.

A second caveat: wmsc `stockNumber=0` on extended/sourcing codes means "not
stocked for direct sale", not necessarily "JLC cannot assemble" — JLC's SMT
library sources some extended codes to order. The definitive check is the
JLC BOM tool at order upload.

## fixv19c substitutions (all wmsc-verified)

| Line | Old (stock) | New (stock) | Notes |
|---|---|---|---|
| 100 nF 0402 ×32 | C1525 → ~~C71629~~ → **C1525** (7,144,700) | reverted | fixv19b swap was based on the stale HTML banner |
| 10 kΩ 0402 ×28 | C25744 (0) | **C407753** UNI-ROYAL 0402WGD1002TCE (1,382,700) | ±0.5 %, 62.5 mW — better than required |
| 22 µF 0603 ×2 | C86295 (0) | **C2762594** Samsung CL10A226MO7JZNC (86,665) | 16 V X5R; ~14–15 µF effective @3.3 VDC per Samsung's typical DC-bias curve |
| J12 FH12-14S | C5378715 (0) | **consigned** — no substitute by design | source Mouser/DigiKey; J12 not fitted by assembler |

## Still unresolved — needs a human decision (wmsc stock = 0, catalog parts)

| LCSC | Value | Refs | Qty |
|---|---|---|---|
| C283954 | 5.11M ±1% 0402 | R43, R45 | 2 |
| C853138 | 22.1k ±0.1% 0402 | R63 | 1 |
| C25896 | 47.5k ±1% 0402 | R64 | 1 |
| C6097400 | 301k ±1% 0402 | R66 | 1 |

(The first three are the CP2102N VBUS divider + precision rows — substitute
tolerance must be ≥ the ±0.1 %/±1 % originals, not jellybean grade.)

## Extended/sourcing codes — wmsc stock = 0, verify inside JLC BOM tool

C3013941 ESP32-S3-MINI-1 (U1) · C46898862 LSM6DSV80X (U14) · C2986165
W25Q512 (U18) · C2829066 RV-3028 (U24) · C6705435 SHT45 and C536262
IM69D130 return **no record at all** in both endpoints (sourcing-only codes).

## Thin but orderable (wmsc stock < 100, qty-1 lines)

C5349491 MLX90632 **3** · C2649390 TCA6408 **13** · C650308 AD5940 **15** ·
C882777 ADS1292R **16** · C2655172 SFH7072 **39** · C544431 TPS61241 **67**
(user-confirmed adequate) · C5328762 MAX86141 **84** · C2871092 CSD13380F3
**40** for qty 3.

## Consigned

D10 NF2W757G-F1 · D12 XL0603NRC730 · **J12 FH12-14S-0.5SH(55)** (added in
fixv19c) — customer-supplied; assembler leaves unpopulated.
