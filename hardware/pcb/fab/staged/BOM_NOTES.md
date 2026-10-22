# BOM_NOTES.md — fab-gate BOM fixes (applied to standalone fab CSVs, not the zip)

Source files edited in place: `fab/BOM.csv`, `fab/BOM_JLC.csv`, `fab/CPL.csv`,
`fab/CPL_JLC.csv` (byte-identical copies exist inside
`VitalQ_v2_fixv19_fabrication.zip`; repack must refresh the zip + SHA256SUMS).

No comment rows were added to any CSV. Nothing else changed.

---

## FIX 1 — R61 DNP (charger TS pin)

R61 = "NTC 10k B3380" (Murata NCP15XH103F03RC, LCSC C77131) on the BQ25170 `TS`
pin, parallel to the J2 pack NTC. Parallel 10k||10k = 5k → ~190 mV at TS ≈ the
188 mV hot-suspend threshold → permanent HOT fault. Marked DNP everywhere,
mirroring the established conventions:

- `BOM.csv` convention: literal `DNP` in the DNP column (as R93/R104/R105/
  R107/R108/J13).
- `BOM_JLC.csv`, `CPL_JLC.csv` convention: DNP refs are entirely absent
  (verified: J13, R93, R104, R105, R107, R108 appear in neither file).
- `CPL.csv` convention — two existed: legacy DNP refs (J13/R104/R105/R107/R108)
  remain listed, but the most recent and authoritative precedent is R93, which
  per `FAB_VERIFICATION.json` ("dnp": "R93 now DNP (removed from CPL; flagged
  DNP in BOM.csv; absent from BOM_JLC/CPL_JLC)") was REMOVED from CPL.csv.
  Task specified "mirror R93's treatment" → R61 removed from CPL.csv.

Verbatim diffs (vs zip-extracted originals, `diff orig new`):

### BOM.csv (LF endings preserved)
```
177c177
< "R61","NTC 10k B3380","Resistor_SMD:R_0402_1005Metric","1",""
---
> "R61","NTC 10k B3380","Resistor_SMD:R_0402_1005Metric","1","DNP"
```

### BOM_JLC.csv (CRLF endings preserved — row deleted)
```
36d35
< "NTC 10k B3380","R61","R_0402_1005Metric","C77131","1","Murata","NCP15XH103F03RC"
```

### CPL.csv (LF — row deleted)
```
197d196
< R61,10k,R_0402_1005Metric,31.154740,-7.091400,90.000000,top
```
(pre-existing quirk: CPL.csv carried Val "10k" vs BOM "NTC 10k B3380" — moot
now that the row is removed.)

### CPL_JLC.csv (CRLF — row deleted)
```
168d167
< R61,31.154740mm,-7.091400mm,Top,90.000000
```

## FIX 2 — U15 MPN swap TPS61240YFFR → TPS61241YFFR

### (a) Pin-compatibility evidence — VERIFIED drop-in

Single datasheet SLVS806D (local copy `reports/datasheets/TPS61240YFFR.txt`,
also TI "TPS6124x" product pages) covers TPS61240, TPS61241 and TPS61242 with
ONE shared §6 pin map — identical pinout by construction:

- Package: same **DSBGA-6 (YFF)**, 1.25 mm × 0.86 mm, 0.4 mm pitch; same
  tape/reel (YFF | 6 | 3000 | LARGE T&R, identical reel dims A0/B0/K0/P1/W,
  Pin1 quadrant Q1 — datasheet tape-and-reel table lists TPS61240YFFR and
  TPS61241YFFR with byte-identical dimensions).
- Pin map (DSBGA balls): **A1=VIN, A2=GND, B1=L (switch/inductor), B2=VOUT,
  C1=EN, C2=FB** — matches the U15 pin audit exactly
  (`pin_audit_r7/U15_TPS61240.md`: A1 VIN→VBAT_SYS, A2 GND, B1 L→TX_SW via
  L2 1.0 µH, B2 VOUT→TX_5V_RAW, C1 EN→TX5_EN, C2 FB tied to TX_5V_RAW).
- Output: fixed **5.0 V ±2 %** for both (device-options table §5: both "5V";
  spec table VOUT 4.9/5.0/5.1 V). VIN range 2.3–5.5 V both.
- ONLY electrical difference = current capability, which is the reason for
  the swap: datasheet text + device-options table — "The TPS61240 has an
  input valley current limit of 500 mA, and the TPS61241 has an input valley
  current of 600 mA"; TPS61240 "Supports 5 V, up to 250 mA loading down to
  3.2 V input", TPS61241 "Supports 5 V, up to **300 mA** loading down to
  3.2 V input" (TI.com parametric: switch current limit typ 0.7 A, output
  up to 450 mA). External passives unchanged (L 0.5–1.5 µH, CIN/COUT ≥4.7 µF
  — already met by L2/C44/C47).
- Conclusion: same footprint, same pin map, same fixed rail, higher current
  headroom → true drop-in upgrade for the LED/TX_5V load.

### (b) LCSC / JLC part number — C544431

- JLCPCB: https://jlcpcb.com/partdetail/TexasInstruments-TPS61241YFFR/C544431
  — TI, MPN TPS61241YFFR, DSBGA-6, "4.9V~5V 450mA Boost", JLCPCB Part #
  **C544431**, SMT assembly, MSL 1.
- LCSC: https://www.lcsc.com/product-detail/C544431.html — same code C544431.
- STOCK RISK: LCSC listing showed only ~67–68 pcs in stock at audit time.
  Enough for a prototype build, but treat as low-stock. Fallback if it goes
  to zero: revert to TPS61240YFFR / **C2070904** (still in the same row's
  history; fully pin-compatible, only lower valley current limit — the very
  limitation this fix removes).

### (c) Verbatim diffs

#### BOM_JLC.csv (CRLF preserved)
```
73c72
< "TPS61240YFFR","U15","TPS61240_YFF","C2070904","1","Texas Instruments","TPS61240YFFR"
---
> "TPS61241YFFR","U15","TPS61240_YFF","C544431","1","Texas Instruments","TPS61241YFFR"
```

#### BOM.csv (Value field = the part description)
```
255c255
< "U15","TPS61240YFFR","vitalq:TPS61240_YFF","1",""
---
> "U15","TPS61241YFFR","vitalq:TPS61240_YFF","1",""
```

#### CPL.csv (Val column names the part — kept consistent with BOM.csv)
```
263c263
< U15,TPS61240YFFR,TPS61240_YFF,40.797369,-38.824994,0.000000,top
---
> U15,TPS61241YFFR,TPS61240_YFF,40.797369,-38.824994,0.000000,top
```

`CPL_JLC.csv` U15 row needs NO change (no Val column — designator/coords only).

Intentionally unchanged: the Footprint fields still read `TPS61240_YFF` /
`vitalq:TPS61240_YFF` — that is the physical land pattern name in the board's
`vitalq.pretty` library and is the correct footprint for both MPNs (same YFF
package); renaming it would break the library link.

---

## Rows/artifacts that could NOT be updated here (flagged for repack stage)

1. `SHA256SUMS.txt` / `SHA256SUMS.json` — hashes of all four edited CSVs are
   now stale; regenerate during rezip.
2. `ASSEMBLY_CHECKS.json` — `"dnp_not_placed": ["R93"]` should gain `"R61"`,
   and the `fix_v19_changes.R61` note ("fitted (was DNP)… board-side TS
   sensing for 2-wire packs") is now contradicted by the fab-gate finding
   (pack NTC present on J2 → parallel NTC → TS ≈190 mV ≈ 188 mV hot-suspend).
   Left untouched per scope (verification artifact).
3. `FAB_VERIFICATION.json` — `"dnp"` / `"reconcile"` strings mention only
   R93; the DNP set is now {J13, R61, R93, R104, R105, R107, R108}. Stale.
4. `FAB_NOTES.md` — fix_v19 bullet says "R61 fitted"; the BOM-level revert
   to DNP is this change. Not edited (historical log).
5. `positions_kicad.csv` — raw KiCad placement export still lists R61
   (Val "10k") and U15 as TPS61240YFFR. Expected: it mirrors the physical
   board (the R61 pads and U15 site still exist in copper); regenerates from
   the .kicad_pcb, not a fab-order input. Not edited.
6. The standalone CSVs were edited; the copies inside the zip are now stale
   until repack rezips.
