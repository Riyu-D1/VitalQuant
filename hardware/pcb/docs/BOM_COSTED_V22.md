# V22 — Costed BOM & UK sourcing guide

> **Status note (fix_v24, 2026-10-25):** this document describes an earlier
> revision. Current board state: D10 = Nichia NF2W757G-F1 (consigned, no
> LCSC), U14 = LSM6DSV320XTR (LCSC C26633007, same LGA-14 land), J12 /
> U24 / U25 are **fitted** (in the v24 JLC BOM/CPL), and the dead MX-ECG
> chain D29 / D30 / R118 / R119 is DNP. See `../reports/CHANGES_V24.md`.

Prices checked 2026-10-22 (1-off qty, USD unless noted — verify at order
time; prices drift). All listed suppliers **ship to the UK**.

**Recommended route:** JLCPCB assembly — the whole BOM is LCSC-numbered,
so parts + PCBA + UK shipping come in one order. JLC/LCSC ship worldwide
via DHL/FedEx/special lines. Anything JLC can't stock goes consigned or
bought from a UK distributor (see flags).

| Supplier | Ships UK? | Notes |
|---|---|---|
| **JLCPCB/LCSC** | yes (DHL/FedEx, ~$10–30) | cheapest for most parts; "Extended" parts = assembly-only, ~$3 setup each |
| **RS UK** (uk.rs-online.com) | UK domestic, next-day | Sensirion, TI, ADI stocked; free-ish shipping |
| **Farnell UK** (uk.farnell.com) | UK domestic | broad IC coverage |
| **DigiKey UK** | ships from US, free >£33 | fast, huge stock, prices in £ |
| **Mouser UK** | ships from US, free >£33 | TI/ADI/Melexis authorized |

## Cost drivers (26 ICs/modules) — ~£75–95/board total

| Ref | Part | ~Unit $ | Source | Flag |
|---|---|---|---|---|
| U1 | ESP32-S3-WROOM-1-N16R8 | 5.40 | LCSC **C2913202** (15k stock) | **BOM CSV had wrong C# (C3013941=MINI-1-N4R2) — FIXED** |
| U7 | AD5940BCBZ-RL7 | 12.94 | JLC C650308 (Extended) | 2nd-most expensive; no cheaper drop-in |
| U12 | MLX90632SLD-DCB-100 | ~19.00 | LCSC C5349491 / Mouser | **Most expensive part.** Order the **1.8 V I²C** suffix (board runs it on the 1.8 V bus) — verify exact variant before buying |
| U10 | AS7341-DLGM | 8.56 | LCSC C2649486 (408 stock) | — |
| U13 | BME280 | 8.14 | LCSC C92489 | **£3–4 cheaper at DigiKey/Mouser (~$4) — consign or buy UK-side** |
| U8 | ADS1292RIRSMT | ~8.00 | JLC C882777 | TI AFE — Mouser/DigiKey similar |
| U22 | MAX86141ENP+ | ~5.60 | JLC C5328762 / Analog ~$5.56@1k | — |
| U23 | SHT45-AD1B-R2 | ~5.50 / £4.16 | **RS UK £4.16 inc VAT** — cheapest | LCSC similar; RS is UK-domestic |
| U6 | AFE4900YZR | ~4.00–8.00 | JLC C2651595 (Extended, assembly-only) | **Not for separate sale at JLC — via PCBA or Mouser ~$6–8** |
| U16 | SFH 7072 | ~3.75 | JLC C2655172 / DigiKey 475-3345 | — |
| U14 | LSM6DSV80XTR | ~5–7 | LCSC C46898862 | specialty high-g IMU — check stock |
| U18 | W25Q512JVEIQ | ~3–5 | LCSC C2986165 | — |
| U5 | CP2102N-A02 | ~2.00 | LCSC C964632 | — |
| U15 | TPS61240YFFR | ~1.50 | LCSC C2070904 | — |
| U17 | MAX17048G+T10 | ~2.20 | LCSC C2682616 | — |
| U2 | BQ25170DSGR | ~1.50 | LCSC C5208201 | — |
| U3 | TPS63802DLAR | ~2.00 | LCSC C2845237 | — |
| U19 | TCA6408ARSVR | ~0.50 | LCSC C2649390 | — |
| U26 | SN74AXC2T245RSWR | ~0.80 | LCSC C1882550 | — |
| U9 | PCA9306DCUR | ~0.40 | LCSC C33196 | — |
| U4 | TPS7A2018PDBVR | ~0.20 | LCSC C963430 | — |
| U21 | USBLC6-2SC6 | ~0.15 | LCSC C7519 | — |
| U11,U20 | TMP117AIDRVR ×2 | ~2.50 ea | LCSC C699536 | — |

## Optical emitters

| Ref | Part | ~Unit $ | Source | Flag |
|---|---|---|---|---|
| D10 | JB3030 white LED | ~1.50 | LCSC C17610390 | — |
| D11 | SFH 4053 IR emitter | ~1.50 | LCSC C2900307 | — |
| D12 | XL0603NRC730 730 nm | ~0.50–1.00 | **no LCSC — generic 0603 NIR** | spec-class SFH 4735 is ~$11.50 at Distrelec — **buy a generic 730 nm 0603, don't overpay** |
| D25 | KT-0603G green | 0.06 | LCSC C12624 | — |

## Discretes & connectors

- Q1/Q2/Q4 CSD13380F3T ~$0.40 ea · Q3 BC847BS ~$0.05 · Q6 AO3401A ~$0.05
- J1 USB-C ~$0.30 (C165948) · J12 FFC ~$0.80 (C5378715, DNP) · L1/L2 DFE201612E ~$0.30 ea · SW ×2 ~$0.20
- TPD1E10B06 ESD ×18 ~$0.10 ea
- **All 0402/0603 passives + 2512 links: ~$4 total** (LCSC jelly parts)

## Do NOT buy (DNP)

U24 RV-3028, U25 IM69D130, J12 FFC, R104/R105, J13 chain (D29/D30,
R118/R119). Saves ~$10 vs blindly ordering the full schematic BOM.

## Board cost estimate (1-off, parts only)

| Bucket | ~USD |
|---|---|
| ICs & modules (fitted) | ~85–95 |
| Optics & discretes | ~6 |
| Connectors & passives | ~6 |
| **Parts total** | **~$95–107 (~£75–85)** |
| JLC 8-layer PCB (5×, ~56×81 mm) | ~$100–140 |
| JLC SMT assembly + stencil + extended-part fees | ~$70–130 |
| UK shipping (DHL/FedEx) | ~$25–45 |

## Case-side extras (not in PCB BOM)

- 20× pogo pins, Harwin P70-5100045-class: ~$0.30–0.50 ea at LCSC
  (don't pay £2+/pin at Farnell for a prototype — verify dimensions vs
  `hardware/case/docs/CONTACTS_BOM.md`) → **~$8–10**
- 402535 protected LiPo (~24×35×4 mm): ~$3–5 (any UK hobby vendor)
- 30 mm elastic chest strap: ~$3–5
- PETG + TPU (case + gasket) filament: ~$5 worth
- **Case-side total: ~$20–25**

## Buying checklist

1. Upload `fab/assembly/BOM_JLC.csv` to JLCPCB BOM tool — it resolves
   stock + flags Extended/consign-needed parts automatically.
2. **Fix applied**: U1 must be C2913202 (WROOM-1-N16R8), not C3013941.
3. Consign or UK-buy: BME280 (DigiKey ~$4 vs LCSC $8.14), D12 generic
   730 nm LED, MLX90632 (confirm 1.8 V I²C variant suffix).
4. RS UK for SHT45 (£4.16) if doing a UK consign parcel anyway.
5. Skip every DNP line above.

Research prototype — no medical-device or sourcing warranty implied.
