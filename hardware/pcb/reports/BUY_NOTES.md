# BUY_NOTES — fix_v24

Stock/prices read from lcsc.com and jlcpcb.com part pages on
**2026-10-25** (checked). Need for a run = 3 boards' worth (2 assembled +
1 spare) plus assembly attrition. USD→GBP 0.748 (ECB ref 2026-10-23).

## Parts needing action / attention at order time

### Zero or delisted at LCSC **and** zero/not-stocked at JLC

| Ref | Part | LCSC | JLC | Action |
|---|---|---|---|---|
| U14 | LSM6DSV320XTR (C26633007) | 0 | 0 | **Order-time risk.** Documented fallback LSM6DSV16XTR (C5267406, LCSC 6,393) — NOT applied: needs a one-line WHO_AM_I change (0x70) and loses the unused high-g channel. Owner decides. |
| J12 | FH12-14S-0.5SH(55) (C5378715) | 0 | not stocked | **Hand-fit or consign.** Unverified alternates on LCSC's own list: C19273929 (HC-FPC-0.5-14P-FH20, stock 47,530), C262663 (AFC01-S14FCA-00, stock 58,970) — 14-pos 0.5 mm RA FFC, but land compatibility with the FH12 land is *not verified*. |
| U24 | RV-3028-C7 (C2829066) | 0 | not stocked | **Hand-fit or consign.** No second source exists for this DFN-8 RTC. Distributor price ~£2.50 (guess). |
| U25 | IM69D130 (C536262) | LCSC page delisted (404) | 73 | JLC stock covers 3 boards; if LCSC is preferred buy IM69D130V01 from a distributor. |
| U23 | SHT45-AD1B-R2 (C5221601) | LCSC page delisted (404) | 692 | JLC stock covers; or RS UK ~£4.16 (v22 note). |

### LCSC 0 but JLC assembly stock OK — no swap applied (per rule)

| Ref | Part | LCSC | JLC | Note |
|---|---|---|---|---|
| R23 | 560 kOhm 0402 1% (C132339) | 0 | 40 | JLC stock covers 3 boards. Spec-matched LCSC alternates all also 0 (e.g. C966765 CQ02WGF5603TCE). |
| R32-36, R76-83, R118-122 | 51 kOhm 2512 1W 1% (C30412) | 0 | 818 | JLC stock covers. LCSC-listed alternates (C2775971 etc.) also 0. |
| 28× 10 kOhm 0402 1% (C25744) | 0 | 7,912,466 | JLC stock covers. Verified alternate: **C407753** (0402WGD1002TCE, ±0.5% — better tolerance, LCSC 1,383,100). |
| R64 | 47.5 kOhm 0402 1% (C25896) | 0 | 75 | JLC stock covers. LCSC alternates C2773928/C2782267 also 0. |

### Thin JLC stock (< 3 boards' worth + attrition)

| Ref | Part | LCSC | JLC | Action |
|---|---|---|---|---|
| Q1, Q2, Q4 | CSD13380F3T PicoStar N-FET (C2871092) | 30 | **3** | JLC has ~3 for 9 needed — hand-fit from LCSC stock or consign. No verified drop-in alternate (CSD17585F5 is not one). |
| U19 | TCA6408ARSVR (C2649390) | 27 | **2** | Hand-fit/consign ~1 pc short. |
| U22 | MAX86141ENP+T (C5328762) | 82 | **2** | Hand-fit/consign ~1 pc short. |
| U26 | SN74AXC2T245RSWR (C1882550) | 33 | **3** | Exactly 3 — zero margin for attrition; consign a spare. |
| U10 | AS7341-DLGM (C2649486) | 316 | **4** | Covers 3 boards, thin. |
| U12 | MLX90632SLD-DCB-100-RE (C17235844) | **9** | 0 | LCSC 9 covers 3 boards; LCSC-only supply. -BCB variant C5382520 (stock 4) is *not* the same I2C level variant — do not substitute. |
| U8 | ADS1292RIRSMR (C456281) | 251 | 20 | OK. |
| U7 | AD5940BCBZ-RL7 (C650308) | 15 | 9 | OK for 3, thin margin. |
| U16 | SFH 7072 (C2655172) | 40 | 8 | OK for 3. |

## Consignments (see CONSIGNED_PARTS.md)

- D10 NF2W757G-F1 (Nichia) — consigned, order ~5 pcs (attrition guess).
- D12 XL0603NRC730 — consigned (existing convention).
- Hand-fit/consign candidates above: J12, U24, U14 (if 320X still 0 at
  order time), plus Q1/Q2/Q4, U19, U22 top-ups.

## Assembly notes

- J1 USB-C has through-hole shell pins — expect a JLC manual/hand-solder
  fee (~$3.58/order class, checked v22 note).
- U11/U20 TMP117 exposed pads intentionally have no paste — answer JLC's
  query "no paste on EP intended".
- JLC placement preview still needs a human rotation check for U6, U7,
  U22 (WLCSP), U14, Q1/Q2/Q4 (per audit M31).

## Exchange rate

USD→GBP **0.748** — ECB reference rate via frankfurter.app, dated
2026-10-23 (last business day before the 25 Oct reads).
