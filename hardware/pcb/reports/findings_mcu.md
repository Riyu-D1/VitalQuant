# MCU / USB / Level-Translation Audit — fix_v19 @ b9002188

Scope: **U1** ESP32-S3-MINI-1-N4R2 · **U5** CP2102N-A02-GQFN28R · **U21** USBLC6-2SC6 ·
**U26** SN74AXC2T245RSWR · **J1** USB-C (HRO TYPE-C-31-M-12) · plus domain-crossing
inventory incl. **U9** PCA9306 and auto-reset pair **Q3** (BC847BS).

Method: pad↔net extracted directly from `vitalq_v2.kicad_pcb` (custom `(net "name")`
pad syntax — repo `01_netlist_pinmap.csv` net column is empty, nets re-parsed here).
Cross-checked vs `fab/BOM_JLC.csv` (fit/values) and `fab/BOM.csv` (DNP flags).

## Verdict summary

| Severity | Count |
|---|---|
| BLOCKER | 0 |
| HIGH | 0 |
| MEDIUM | 2 (observations, not wiring errors) |
| COSMETIC/INFO | 6 |

**No board-bricking defects found in this group.** All ESP32 strapping pins, all
in-package flash/PSRAM reserved pins, USB D+/D- mapping, CC terminations, the EN RC,
the auto-reset topology and both level translators are verified correct.

---

## Verified correct (the classic brick-the-board items — all clean)

### U1 ESP32-S3-MINI-1-N4R2 — all 65 pads checked against module DS Table 3-1 (p.11-12)
- **Strapping GPIO0** (pad 4, `ESP_IO0`): 10 kΩ pull-up (R6) to +3V3, SW2 button to
  GND, Q3.C2 open-collector, J8.6 test. Default state 1 → **SPI boot** ✓ (§4 Table 4-1).
- **Strapping GPIO45** (pad 41): unconnected → internal weak pull-down = 0 →
  **VDD_SPI = 3.3 V**, correct for the N4R2's 3.3 V flash ✓.
- **Strapping GPIO46** (pad 44): unconnected → 0 → normal boot with GPIO0=1 ✓.
- **Strapping GPIO3** (pad 7, `EXP_INT`): 10 kΩ pull-up (R53) → 1 at reset → JTAG
  source = USB Serial/JTAG — consistent with the native-USB design; no boot impact
  (INFO only).
- **PSRAM/flash reserved pins**: pad 26 (IO26 = in-package PSRAM CS on -N4R2, DS
  footnote b p.12), pads 28/29 (IO33/34), 31/32/33 (IO35/36/37), 27 (IO47),
  30 (IO48) — **all literally unconnected** ✓. None are driven.
- **Native USB**: pad 23 = IO19 = `USB_DM_S3` (D-), pad 24 = IO20 = `USB_DP_S3`
  (D+) — correct pins per Table 3-1 bold defaults; routed via fitted 0 Ω R102/R103
  to J1 ✓.
- **EN** (pad 45): R5 10 kΩ pull-up + C64 1 µF → ~10 ms RC, SW1 reset button, Q3.C1
  collector, J8.5, U19.1 (TCA6408A RESET input — passive sense, cannot fight EN) ✓.
- **3V3** (pad 3): `+3V3_ESP` = +3V3 via R94 0 Ω; decoupled C7 22 µF + C8 100 nF ✓.
- **Antenna**: module overhangs board edge (edge cut y=-10.5; module body to -13.4);
  dedicated 8-layer `antenna_keepout` zone (no tracks/vias/pads/pour) covers
  x[4.55,20.25] y[-10.5,-8.05]; zero copper in overhang region ✓.
- Land pattern: 60×0.4×0.8 perimeter pads @0.85 pitch + 4×0.8×0.8 corners +
  9×1.2×1.2 EP grid = DS Fig 10-1/11-1 recommended land ✓.

### U5 CP2102N-A02-GQFN28R — all 29 pads vs DS Table 5.1 (Rev 1.3, p.27-28)
- Pin map is **QFN-28 variant** (not -24): 4=D+, 5=D-, 6=VDD, 7=VREGIN, 8=VBUS,
  9=RSTb, 23=CTS, 24=RTS, 25=RXD, 26=TXD, 27=DSR, 28=DTR, center=GND — matches
  board pad-for-pad ✓.
- **VREGIN (7) = VBUS** — canonical bus-powered hookup (DS Fig 2.1) ✓.
- **VBUS sense (8)**: divider R63 22.1 kΩ / R64 47.5 kΩ → **exactly the
  datasheet-recommended divider** (DS §2.3 note; required when VIO absent) ✓.
- **RSTb (9)**: R62 1 kΩ pull-up to VDD — datasheet-mandated value (DS §2.1) ✓.
- **UART direction**: TXD(26)=CP_TX→R73→ESP_RX (U1.40 RXD0) ✓;
  RXD(25)=CP_RX←R72←ESP_TX (U1.39 TXD0) ✓.
- **Disconnect topology**: D+/D- leave the chip on `USB_DP_CP`/`USB_DM_CP` and stop
  at **DNP R104/R105**; ESP32 native USB owns J1 via fitted R102/R103 — isolation
  is real, intended, and consistent (BOM.csv marks R104/R105 DNP) ✓.
- EP (pad 29) = GND; land 3.35×3.35 mm = DS Table 6.2 ✓.
- VDD bypass C9 10 µF + C62 100 nF ≥ DS-required 4.7 µF+0.1 µF ✓.

### U26 SN74AXC2T245RSWR — all 10 pads vs DS SCES879A §4 Table 4-1
- **Supplies**: pin 7 VCCA = +1V8, pin 6 VCCB = +3V3 — correct domain assignment
  (A port = 1.8 V, B port = 3.3 V) ✓.
- **Direction**: pin 10 DIR1 = +1V8 (tied to VCCA = logic high) → A→B: the single
  used channel translates `MX_SDO_1V8` (U22 1.8 V SDO) up to `MISO_MX` (3.3 V) ✓.
- **OE**: pin 2 = `CS_MAX86178` (3.3 V). Legal — control inputs are 4.2 V-rated
  (§5.1). Functionally elegant: translator outputs tri-state whenever MAX86141 is
  deselected, so the shared `SPI_MISO` bus is never contested ✓.
- Unused channel 2: DIR1 pin 1 = GND (B→A), B2 pin 4 = GND (defined input), A2
  pin 9 NC (driven output) — all per DS guidance ✓.

### U21 USBLC6-2SC6 — all 6 pads vs ST CD00050750 Fig.1
- 1/6 = I/O1 → `USB_DP`; 3/4 = I/O2 → `USB_DM`; 2 = GND; 5 = `VBUS` — exact
  functional pinout ✓. Sits on the connector side of R102/R103 (protects the pair
  before the ESP32) and also clamps VBUS ✓.

### J1 USB-C TYPE-C-31-M-12
- Contact mapping A1/A12/B1/B12=GND, A4/A9/B4/B9=VBUS, A5=CC1, B5=CC2,
  A6/B6=D+, A7/B7=D-, A8/B8=SBU — matches USB-C receptacle assignment ✓.
- **CC1→R1 5.1 kΩ→GND, CC2→R2 5.1 kΩ→GND** — correct independent Rd for a UFP
  (BOM: R1/R2 = 5.1 k, C25905). No 2.2 k Rp anywhere on CC nets (the only 2.2 k
  parts are I²C pull-ups R19-R22) ✓.
- SBU open (allowed for USB-2.0 device) ✓. Shell → 4 grounded stakes; 2 NPTH
  mid-mount holes ✓.
- **VBUS → charge path**: J1 VBUS → U2.1 (BQ25170 IN) and → Q6 ideal-diode /
  VBAT_SYS OR-ing, plus U5.7 REGIN, U21.5 clamp, C1/C10/C63 input caps,
  R50/R51 `VBUS_DET` divider (3.0 V into U19.9) ✓.

### Domain-crossing inventory (3.3 V ↔ 1.8 V) — every crossing accounted for
| Net pair | Path | Verdict |
|---|---|---|
| I2C_SDA ↔ I2C_SDA_1V8 | **U9 PCA9306** ch.SDA — VREF1=+1V8, VREF2/EN→+3V3 via R18 200 k (canonical) | OK |
| I2C_SCL ↔ I2C_SCL_1V8 | **U9 PCA9306** ch.SCL | OK |
| MX_SDO_1V8 → MISO_MX | **U26** A1→B1, DIR1=high, OE=CS-gated | OK |
| SPI_SCK, SPI_MOSI, CS_MAX86178 → U22 (1.8 V part) | no translator — **legal**: MAX86141 SCLK/SDI/CSB rated −0.3…+6 V (DS abs-max, r7 audit) | OK |
| MAX86178_INT (U22, 1.8 V) → U1.35 | open-drain output, pulled to +3V3 via R113 — pin 6 V-tolerant | OK |
| I²C 1V8 pull-ups R21/R22 → +1V8; 3V3 pull-ups R19/R20 → +3V3 | correct rails on correct sides | OK |
| U22 VDD (C2/D2) = +1V8; U10/U12 supplies = +1V8/+3V3 mix per part | no 3V3 net reaches a 1.8V-only pin | OK |

No unprotected 3.3 V→1.8 V crossing exists.

---

## MEDIUM — observations (not blocking, worth a decision)

1. **U21 placement ~15-18 mm from J1.** ESD clamp topology is correct (connector
   side of R102/103) but the part sits mid-route rather than at the connector
   mouth (J1 y≈-3.8 vs U21 y≈11-13). IEC 61000-4-2 energy traverses ~15 mm of
   data-line trace before clamping. Layout-frozen copper in fix_v19 — accept or
   note for next spin.

2. **CP2102N is fitted but functionally isolated (R104/R105 DNP).** With VBUS
   present, REGIN powers it (~9.5 mA typ, DS §3 operating current) while it can
   never enumerate — dead power. When battery-powered it's unpowered and benign.
   Also `ESP_RX` (U1.40) has no pull-up — floats when U5 unpowered (noise into
   RXD0 only; not a boot risk), and ESP_TX can weakly phantom-feed the dead chip
   through R72 10 k (~0.3 mA — harmless). If the bridge is truly spare-only,
   consider DNP-ing U5 itself to save BOM cost + VBUS current.

## COSMETIC / INFO

- **Q3 auto-reset is the cross-wired variant** (E1→CP_RTS, E2→CP_DTR; B1←DTR,
  B2←RTS): EN=0 iff DTR=1∧RTS=0, IO0=0 iff RTS=1∧DTR=0 — matches esptool's
  ClassicReset toggling and inherently prevents both-lines-asserted latch-up.
  Verified against BC847BS pinning (1=E1,2=B1,3=C2,4=E2,5=B2,6=C1).
- **U5 pins 1/2/23/27** (DCD, RI/CLK, CTS, DSR — all inputs) tied to VDD_CP2102:
  deasserted, legal, though unconventional (these are inputs, not power pins).
- **SPARE_IN** (U1.38/IO42): both bias resistors R107/R108 DNP → floating input;
  enable internal pull in firmware or fit one.
- **U5 pads** 0.8×0.25 mm vs Silabs-recommended 0.95×0.30 mm (Table 6.2): slightly
  smaller lands — KiCad generic footprint; workable at JLC but note for next rev.
- J8 (6-pad programming land) carries +3V3/GND/TX/RX/EN/IO0 — complete manual
  recovery path independent of CP2102N ✓ (mitigates any auto-reset doubt).

## UNVERIFIABLE / limits
- **HRO TYPE-C-31-M-12** connector drawing not independently obtained; pad count
  (16 SMT + 4 shell + 2 NPTH) and contact names match the standard mid-mount
  M-12 and KiCad stock footprint. Mech geometry judged consistent, not proven.
- `vitalq_v2.kicad_pcb` vs fix_v19 zip provenance cannot be cryptographically
  proven (per `00_target.md`) — BOM/CPL inside the zip were used as fab authority
  for fitted/DNP status and values.
