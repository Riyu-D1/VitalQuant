# Devin prompt — VitalQ fixv19c: last BOM pass, then freeze

Copy everything below the line into Devin.

---

This is the final BOM pass. After this, the package is frozen and shipped to the fab. Do not touch copper.

## Correction to fixv19b

You were right and I was wrong about C1525. I read the LCSC HTML product page, which carries an "Out of Stock / Notify Me" block that is not live state. The wmsc API returns **stockNumber 7,145,500** for C1525. It was never out of stock.

So the C1525 → C71629 swap fixed nothing. Revert it: restore `fab/BOM_JLC.csv` to C1525 / Samsung Electro-Mechanics / CL05B104KO5NNNC, and note the revert in `FAB_NOTES.md` so the record shows why it moved and came back.

## The three genuine zeros

I verified these against the wmsc detail API (`https://wmsc.lcsc.com/ftps/wm/product/detail?productCode=<CODE>`, field `stockNumber`) today:

- **C25744 — 10k 0402 ×28** (R5 R6 R7 R8 R9 R11 R14 R49 R52 R53 R60 R65 R67 R68 R69 R70 R71 R72 R73 R74 R98 R100 R101 R112 R113 R115 R116 R117) — stock **0**.
- **C86295 — 22 µF 0603 X5R ×2** (C7 ESP32 VDD33 bulk, C45 U3 VOUT bulk) — stock **0**.
- **C5378715 — FH12-14S-0.5SH(55) FFC connector** (J12) — stock **0**.

Also confirm and leave alone unless they fail: C5349491 MLX90632 = 3 pieces (need 1), C544431 TPS61241YFFR = 67 pieces (need 1). Both adequate.

## Substitution rules

**10k 0402 ×28 — substitute freely.** These are pull-ups and pull-downs, not precision values (the precision resistors on this board are the odd values: 40.2k, 5.11M, 51k, 200k, 5.1k). Find an in-stock 10k, 0402, tolerance equal or better than ±1%, power ≥62.5 mW, and prove it: same value, same package, stock ≥ 5,000, verified via the API above. Record the new code, MPN, manufacturer and stock in `FAB_NOTES.md` and in the BOM.

**22 µF 0603 ×2 — substitute, but pick carefully.** C45 is the U3 (TPS63802) VOUT bulk capacitor, which is already the weakest decoupling on the board at 13.5 mm from the pin. Do not pick a part by headline value alone. Requirements: 22 µF, 0603, X5R or X7R, rated voltage **≥ 10 V**, in stock ≥ 1,000. From the candidates that pass, choose the one with the best typical DC-bias derating at 3.3 V — pull the manufacturer's characteristic curves and state the effective capacitance at 3.3 V for the part you pick. If every in-stock candidate derates badly, say so and stop; I would rather know than get a quiet downgrade on the rail that is already marginal.

**J12 FH12-14S — do NOT substitute.** It is a mating connector; the footprint and the physical mate are the product. Set J12 to consigned in the JLC BOM (blank LCSC cell, `FH12-14S-0.5SH(55) (consigned)` in the MPN field, same pattern as D10 and D12), and add it to the consigned list in `ASSEMBLY_CHECKS.json`. State in `FAB_NOTES.md` that source is Mouser or DigiKey and that J12 will not be fitted by the assembler.

**No other line changes.** If any other row is out of stock, report it and stop.

## Package

Produce `fab/VitalQ_v2_fixv19c_fabrication.zip`. It supersedes fixv19a and fixv19b; say so in one line at the top of `FAB_NOTES.md`. Preserve both earlier zips untouched.

Hard constraints, same as before:

- Do not modify `vitalq_v2.kicad_pcb`, the schematic, any gerber, any drill file, any CPL, or `positions_kicad.csv`. This is BOM-only.
- Prove it: every gerber and drill member in the new zip must be **byte-identical** to fixv19a's. Put the sha256 comparison in the commit message. If anything differs, stop and tell me first.
- Connectivity is unchanged because copper is unchanged. Say so explicitly.

## Also fix the record

`reports/11_stock_sweep.csv` — add a `stock_source` column stating the endpoint and the value returned, because the HTML page and the API disagree and the next person needs to know which one you used. Note the C1525 discrepancy explicitly at the top of `reports/11_stock_sweep.md` as a caution about scraping the HTML.

## Done when

- `BOM_JLC.csv` contains C1525 (reverted), an in-stock 10k for the 28-resistor line, an in-stock 22 µF with a stated DC-bias number, and J12 consigned. Nothing else changed.
- `fixv19c` zip exists with gerbers and drills byte-identical to fixv19a, proven by hash.
- One paragraph telling me: what to order, the two new part numbers, and every line that still needs a human decision.