# Footprint / Land-Pattern Audit — fix_v19 (vitalq_v2)

**Scope:** every IC footprint + J1 USB-C + J12 FFC + J8 Tag-Connect + all WLP/QFN/BGA parts + representative discretes (SOD-323, SOT-23/363, FemtoFET PicoStar, 0402/0603).
**Method:** direct S-expression parse of `vitalq_v2.kicad_pcb` (302 footprints / 1027 pads), then every pad size/orientation was **verified against the released fab Gerbers** (`fab/gerbers/`, KiCad PCBNEW 10.0.4) by decoding aperture macros (`RoundRect` = radius + 4 corner XY + rotation) and flash coordinates. All geometry quoted is as-rendered copper. Datasheet values from `reports/datasheets/*.txt`.
**Verdict rule:** PASS = within ~±15 % of manufacturer value / inside tolerance; FAIL = outside, missing fab data, or materially inconsistent; UNVERIFIABLE = no manufacturer land pattern available.

Detail rows: `05_footprints.csv` (one row per measured dimension).

---

## 1. Confirmed defects

### 1.1 U15 — TPS61240YFFR (DSBGA-6, 0.4 mm): **NO STENCIL APERTURES** — release blocker
Six Ø0.23 mm pads at 0.4 mm pitch match TI's YFF land exactly, but all six are `F.Cu,F.Mask` **with no `F.Paste`**. The `F_Paste` gerber contains zero flashes at U15's site (x≈40.6–41.0, y≈−38.4..−39.2) while adjacent U7 has a full set. The part receives no solder paste → cannot reflow-attach. Confirmed in the fabricated output, not just source.

### 1.2 U8 — ADS1292R (RSM0032B, QFN-32 4×4): pads +36 %, EP −26 %
Signal pads render 0.2 × 0.75 mm vs TI land 0.2 × 0.55 (+36 %); EP 2.85 × 2.85 mm vs 3.85 × 3.85 (−26 %). Pads are correctly oriented radially — **no pad-to-pad overlap** (earlier working hypothesis of a 0.2 mm overlap was a rotation-parse artifact and is retracted). Over-length lands increase solder wicking toward the package edge while the undersized EP shrinks the thermal/mechanical joint. Fix `vitalq:QFN40P400X400X100-33N-D` → 0.2 × 0.55 pads, EP 3.85.

### 1.3 U19 — TCA6408A (RSV0016A, UQFN-16): pads +33…50 %, centre pads missing
Pads render 0.2 mm along pitch × 0.8–0.9 mm radial vs TI RSV0016A example ≈0.2 × 0.6 (+33–50 %). Orientation is correct — no overlap (verified in gerber: rows gap 0.2 mm). The RSV package outline shows two ~1.2 mm centre metal areas that the footprint omits — confirm whether the package exposes them. Fix `vitalq:TCA6408A_RSV` length to ~0.6–0.7 and review EP.

### 1.4 U26 — SN74AXC2T245 (RSW0010A, UQFN-10): pads +33–50 %
Pads render 0.2 × 0.8–0.9 vs TI RSW0010A example 0.2 × 0.6. Correct orientation, no overlap — dimensional excess only.

### 1.5 U9 — PCA9306 (DCU0008A, VSSOP-8): pads +47 %
Render 0.35 × 1.25 vs TI land 0.3 × 0.85 (length +47 %, width +17 %). Long lands reach under the package body; stencil volume elevated. No overlap.

### 1.6 U11 / U20 — TMP117 (DRV0006B): pads +33 %
Render 0.35 × 0.6 vs TI 0.3 × 0.45. The EP **deliberately unpasted** (`TMP117_DRV_NOPASTE`) is correct — TI says do not solder the thermal pad for measurement accuracy. Signal pads still oversized.

### 1.7 U4 / U21 / Q6 — SOT-23-5/6 pads +20 %
Render 1.325 × 0.6 vs TI DBV example 1.1 × 0.6. Radial direction → excess is heel-side only; marginal fail (>15 %), low assembly risk.

### 1.8 U12 — MLX90632: oversized stencil apertures
Signal pads 0.3 × 0.8 @0.5 pitch + 2.5 × 2.1 centre pad render correctly, but the board pastes pads 1:1 while the Melexis drawing specifies 0.25 × 0.25 mm stencil openings. FLAG for stencil review.

### 1.9 U2 — BQ25170 (DSG0008A): within tolerance but note
0.25 × 0.6 rendered vs TI example ~0.5 long — ~+20 %, borderline; EP 1.6 × 0.9 + segmented paste OK. Kept PASS in CSV, listed here for awareness.

---

## 2. 0.4 mm WLP/DSBGA — pad type (NSMD vs SMD) analysis

Industry guidance for 0.4 mm pitch wafer-level parts (ADI WLCSP notes, NXP AN3846, TI AN-1112, Microchip DS-2091): **NSMD copper lands Ø≈0.20–0.25 mm with mask openings pad+0.05…0.10 mm** (a real mask web between openings).

Board state — `solder_mask_min_width 0.1`, `allow_soldermask_bridges_in_footprints no`, and **no global pad mask margin** → any pad lacking an explicit `solder_mask_margin` gets mask opening = pad edge, i.e. effectively mask-defined (SMD) once fab registration is applied:

| Ref | Pads | Pad Ø (gerber) | Mask margin | Assessment |
|-----|------|----------------|-------------|------------|
| U6 AFE4900 YZ-30 | 30 | Ø0.23 ×21 + Ø0.25 ×9 | only the nine 0.25 pads have smm 0.025 (→Ø0.30 opening, 0.10 mm web — exactly at min limit); the 21 smaller pads have none | **FAIL** — two pad sizes AND mixed SMD/NSMD inside one array |
| U7 AD5940 BCBZ-36 | 56 | Ø0.214 ×36 + Ø0.25 ×20 | same pattern: 20 large get margin, 36 small get none | **FAIL** — mixed sizes/definition |
| U22 MAX86141 WLP-20 | 20 | Ø0.24 uniform | none anywhere | **FAIL** — size OK vs AN1891 range, but mask is SMD-equivalent where NSMD is expected |
| U15 TPS61240 YFF-6 | 6 | Ø0.23 | mask present, **paste absent** (§1.1) | **FAIL** |

No copper shorts exist on these arrays (0.4 pitch vs ≤0.25 Ø leaves ≥0.15 mm gap). The risks are mask-web fragility at the 0.1 mm minimum and pad-edge encroachment. Recommend: unify each array to one pad Ø (~0.22–0.24) and one explicit NSMD margin (~0.04–0.05 mm) so openings are pad+0.08…0.10.

---

## 3. Paste / mask handling — general

- Segmented stencil constructs verified as intentional: U5 EP (4× 1.1 mm squares over 3.35 EP), U17 EP (4× 0.32×0.48), U18 EP (4× 1.37×1.73), U25 IM69D130 (custom paste shapes + SMD pads + Ø0.8 acoustic NPTH per Infineon), Q1/Q2/Q4 PicoStar (paste-only apertures over Cu lands per TI CSD13380F3), J12 mech tabs.
- U2/U3 EPs carry 2-piece paste apertures each — consistent with TI stencil examples.
- **U15 is the only part with zero paste.**
- 8 `np_thru_hole` pads total (J1 locators, J8 locators, U25 port) — all legitimate.

---

## 4. Connectors — all PASS

- **J1 USB-C** `USB_C_Receptacle_HRO_TYPE-C-31-M-12`: 16 contacts render 1.45 × 0.6/0.3 mm; the coincident pairs A1/B12, A4/B9, A9/B4, A12/B1 are **same-net duplicates by design** (reversible connector). 4 oval SH stakes (1.0×2.1 / 1.0×1.6, drills ~0.6×1.7/0.6×1.2) + 2 NPTH locators.
- **J8 TC2030-IDC-NL**: 6× Ø0.787 @1.27×1.27 + 3× Ø0.99 NPTH — matches Tag-Connect spec exactly.
- **J12 FH12-14S-0.5SH**: 14 contacts render 1.3 × 0.3, long axis ⊥ 0.5 pitch row (gerber col x=3.15, y-pitch 0.5) + 2 MP 2.2×1.8 — matches Hirose land. (Marked DNP in BOM; geometry is correct if populated.)

---

## 5. UNVERIFIABLE — no manufacturer land pattern in the datasheet corpus

| Ref | Part | Board geometry |
|-----|------|----------------|
| U10 | AS7341 OLGA-8 | 8× 0.488×0.575, pitch 0.8 / col sep 1.276 |
| U14 | LSM6DSV80XTR LGA-14L | 14× 0.254×0.787 radial, pitch 0.5/0.41 (ST TN0018 generic only) |
| U16 | SFH7072 | 12× 0.9×1.0 @1.2, rows 2.5 |
| U18 | W25Q512 WSON-8 8×6 | 8× 0.5×0.65 @1.27, EP 4.3×3.4 (Winbond ds has outline only; no vias under EP ✓) |
| U24 | RV-3028-C7 | 8× 0.5×0.8 @0.9 / rows 1.2 |
| D10 | NF2W757G | 2× ~1.95×0.89 / 2.4×1.28 |
| J1 | HRO TYPE-C-31-M-12 | vendor land drawing not in corpus (KiCad official footprint) |

---

## 6. Cross-check of prior `audit_footprints.csv`

Reproduced where it had coverage (pad counts/min dims for AS7341, BGA30/56, ESP32, IM69D130, MAX86141, MLX90632, NF2W757G, RV3028, SFH7072, SHT45, TMP117, TPD1E10B06, TPS61240, W25Q512, LSM6DSV, QFN40). Corrections:
- `TCA6408A_RSV` min_pad "0.200x0.200" — misleading; pads are 0.2×0.8/0.9.
- Prior file omitted connectors and ~10 IC/passive footprints (U2,U3,U5,U9,U17,SOT-23 family,SOD-323,0603,PicoStar…).
- Intermediate "overlap" findings for U8/U14/J12 were rotation-parse artifacts — **retracted**; no copper-to-copper pad overlaps exist in the fab output. The real U8 defect is dimensional (+36 % pad length, −26 % EP).

---

## 7. Fix list (library/source changes — NOT applied; design files untouched)

1. `vitalq:TPS61240_YFF` — add `F.Paste` to all six pads (TI example: ~Ø0.25 apertures). **[release blocker]**
2. `vitalq:QFN40P400X400X100-33N-D` — pads 0.2×0.55, EP 3.85×3.85 per RSM0032B.
3. `vitalq:TCA6408A_RSV` — pads ~0.2×0.6–0.7; review the two ~1.2 mm centre pads.
4. `Package_DFN_QFN:Texas_RSW0010A_UQFN-10` — pads ~0.2×0.6.
5. `Package_SO:VSSOP-8_2.3x2mm_P0.5mm` — pads 0.3×0.85 per DCU0008A.
6. `vitalq:TMP117_DRV_NOPASTE` — signal pads 0.3×0.45 (keep no-paste EP).
7. SOT-23-5/6, SOT-363 — trim toward ~1.1 mm heel (marginal).
8. U6/U7/U22 footprints — unify ball-pad Ø and add explicit `solder_mask_margin` ≈0.04–0.05 for true NSMD.
9. `vitalq:MLX90632SLDDCB100SP` — reduce stencil apertures toward 0.25 mm squares.
10. `Package_SON` U2/U3 pads — fine geometrically; confirm TI asymmetric DLA/DSG detail if exact-match is wanted.

*Tools:* `reports/tools/extract_fp_geometry.py` (S-expr pad geometry) + gerber aperture/flash analysis. kicad-cli/pcbnew unavailable. Design files unmodified; outputs confined to `hardware/pcb/reports/`.
