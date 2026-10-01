# hw_v2 Floorplan — 40 × 62 mm as placed

Status: **landed and re-measured (2026-09-30).** `build_pcb.py` places
all 291 parts on the 40.0 × 62.0 mm outline with every placement gate
at zero (`parts 291 clashes 0 edge 0 pth 0 pad-miss 0`). The 32 × 62
target did not close once the J11/J13 HV-boundary protection row was
added, so the board grew 8 mm east — J10 and J11 moved to the new
east edge; J13, J5, J6, J7 and J12 kept their sites. The board is
unrouted and not fab-ready. Coordinates are KiCad convention:
**origin lower-left, Y up**.

Board: single rigid 4-layer, JLC04161H-7628 stackup, double-sided
assembly. As-placed outline **40.0 × 62.0 mm = 2,480 mm²** (−3% vs
hw_v1's 2,555 mm²). The 36.5 × 70 fallback was not needed — the east
growth absorbed the overflow instead (§4).

## 1. What changed vs hw_v1 placement-wise

| hw_v1 | hw_v2 | Delta |
|---|---|---|
| WROOM-32E (18.7×19 mm) + x<6.5 mm full-height antenna keep-out | S3-MINI-1U (15.4×15.4 mm) + U.FL at top edge; keep-out collapses to a small connector zone | ~450 mm² of dead strip recovered |
| 245 footprints | **291** (new ICs, connectors, tails, J11/J13 protection row) | pad bbox absorbed into freed strip + 8 mm east growth |
| J5/J6/J7 tail pads only | + J9/J10/J11 pads + J12 FFC (DNP) + J13 ECG pair (DNP); J10/J11 sit on the new east edge, the rest stay on the bottom edge | +~75 mm² edge pad area |
| y50–60 bottom row nearly empty | absorbed by tails + SHT45 + ladder spacing | previously dead space now used |

## 2. Zone map — 40 × 62 mm (top side unless noted)

| Band | y-range | Contents |
|---|---|---|
| Antenna/U.FL | 0–4 | U.FL connector at top-left corner; keep-out = connector pad + coax dress only (external antenna off-board). No copper under U.FL per connector keep-out. |
| USB + comms | 0–14 | J1 USB-C (top edge center), R102/R103 strap links, R104/R105 DNP to CP2102, U5 CP2102N, U21 USBLC6, Q3 auto-program, L2 ferrite |
| Module | 4–22 | U1 S3-MINI-1U (top-left under U.FL), EN/IO0 buttons, R5/R6/C64, J8 TC2030 |
| Power | 18–32 | BQ25170, MAX17048, TPS63802, TPS7A2018 (+1V8), TPS61240 (TX_5V), LiPo pads J2, R93–R97 star links |
| TP field | scattered | TP1–TP28 distributed to edges/rail proximity — do NOT cluster into a block (TP28 RTC_INT is the new 28th) |
| Defib row A | 34–38 | R32–R36 + D1–D5 TVS (5-wide 2512 row), creepage slots retained |
| Skin cluster | 44–60, x≈8–24 | **BOTTOM side**, physics-fixed ~16×16 mm: SFH7072, AS7341, MLX90632, TMP117-U11 (+moat), D10 white, D11 860 nm, **D12 730 nm new**, **SHT45 new** (≥5 mm from temp island), J5 ECG pads |
| MAX86178 | cluster-adjacent | **BOTTOM**, right next to the J10 site + SFH7072 — keep LED/PD runs <15 mm |
| Defib row B | 48–52 | R76–R83 + D6–D9/D21–24 TVS, creepage slots |
| **HV-boundary column** | x≈34–39, full height, east | The 8 mm east growth. J10 satellite-PPG (38.8, 43.4) and J11 sweat (38.8, 55.0) on the new east edge; R118–R122 0 Ω cut-points in R_2512_6332Metric (FP_HV) — R118 F (35.7, 44.3), R119 F (35.7, 56.5), R120 B (35.0, 48.5), R121 B (34.3, 39.7), R122 B (34.3, 58.0); D26–D30 DNP TPD1E10B06 clamps on the IC side |
| Tail edge | 56–62, bottom | J6 EDA (9.0, 60.4), J7 bioZ (14.5, 60.35 F), J5 ECG (18.6, 60.4), **J9 distal-temp, J12 FFC (DNP, 3.3, 50), J13 ECG pair (DNP, 29.6, 60.4)**, J3 FSR |
| Misc bottom | distributed | AFE4900, ADS1292R, AD5940 + per-IC passive halos (existing devinpcb clustering), RTC U24 (DNP), U25 mic (DNP), TMP117-U20 top twin (aligned over U11, x/y same, ZHF pair), LSM6DSV80X, BME280 |

## 3. Fixed constraints (physics / mandate)

- **Skin cluster ~16×16 mm is not compressible** — optical + thermal
  co-registration requires the geometry. D12 and SHT45 slot in at its
  edge; the TMP117 moat stays.
- **Defib ladder kept** (mandate): two 2512 rows + 1.5 mm creepage
  keep-outs cost ~340 mm² incl. spacing. This is the single largest
  retained cost; with the J11/J13 row added it is why the floor
  grew to 40×62 and not smaller.
- **J11/J13 cut-point chains** (landed — see the HV-boundary column
  in §2): J11_WE/RE/CE → R120–R122 → SWEAT_WE/RE/CE → DNP clamps
  D26–D28 → AD5940 spares; J13_INP/INM → R118/R119 →
  MX_ECG_INP/INM → DNP clamps D29/D30 → U22. Same ladder order at
  0 Ω scale, and the 0 Ω parts are R_2512_6332Metric (FP_HV) — the
  same HV-boundary footprint pattern as R76–R83 — not ordinary 0402
  links; the connector-side nets joined HV_ELECTRODE, so these tail
  pads carry the pad-shadow keep-outs too. Placed in the east
  protection column, clear of the existing creepage slots.
- **Test points TP1–TP28 kept** — TP28 (RTC_INT) is new this round;
  distribute at edges; never block-route them.
- **U20 over U11** (ZHF zero-heat-flow thermometer pair): identical XY,
  opposite sides. Insulation dome over U20 is an assembly note, not a part.
- **U.FL coax dress**: keep the top-left corner + a 5 mm routing lane
  clear; do not place tall parts under the pigtail bend radius (~6 mm).

## 4. Honest size verdict

32 × 62 mm **did not close** — the J11/J13 HV-boundary protection row
(5 connector pads + five 2512 cut-points + five DNP clamps + their
pad-shadow keep-outs) needed more east width than the estimate
allowed, so the as-placed board is **40.0 × 62.0 mm = 2,480 mm²**. The
original binding-constraint list held otherwise:

1. Defib ladder rows + creepage keep-outs (~340 mm²)
2. Skin cluster (~16×16 mm fixed) + MAX86178 halo
3. Tail pad band along the bottom edge + the east HV-boundary column
4. USB-C + strap-link envelope at the top edge

The 33 × 62 / 36.5 × 70 fallbacks were not used — growing east to
40 mm kept the bottom-edge electrode field and the tail band exactly
where this map put them. This is a placed board, still unrouted —
not fab-ready.

## 5. What is NOT in this rev

No rigid-flex, no 6-layer, no 0201 passives (all ≥0402), no PMIC swap,
no defib-ladder removal, no test-point trim — all retained per mandate.

## 6. build_pcb.py TODO list (coordinate retargeting) — landed

Retarget is done; the list is kept for history. The outline landed at
**40 × 62** rather than the 32 × 62 originally listed here:

- [x] BOARD_W/BOARD_H constants + outline polys (→ 40.0 × 62.0)
- [x] Collapse x<6.5 full-height antenna keep-out → U.FL corner zone only
- [x] Retarget every footprint x/y into the new outline (291 parts)
- [x] New placements: U1 module, U.FL conn, SJ links R102–R105, U22
      MAX86178 + halo, U23 SHT45, U24 RTC(DNP), U25 mic(DNP), D12+Q4+R109/
      R110, J9–J13 pads, R107/R108 DNP — plus the late arrivals
      R114–R122, C84, D26–D30, TP28 in the east protection column
- [x] Re-anchor creepage slots + TMP117 moat to moved features
- [x] Re-run placement gates (courtyard, pad-slot, SMD-vs-TH) — all zero
