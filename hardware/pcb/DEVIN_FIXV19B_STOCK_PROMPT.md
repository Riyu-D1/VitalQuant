# Devin prompt — VitalQuant fixv19b: one BOM stock fix, copper frozen

Copy everything below the line into Devin.

---

Small, narrow task. Do not do anything else.

## The defect

`fab/BOM_JLC.csv`, the 100 nF 0402 line (32 pieces: C8, C13, C15, C18, C24, C25, C27, C28, C29, C36, C37, C38, C39, C40, C41, C50, C51, C56, C57, C58, C59, C60, C61, C62, C76, C77, C78, C81, C86, C88, C90, C91) points at **LCSC C1525 (Samsung CL05B104KO5NNNC)**. I checked the LCSC product page today: C1525 is **Out of Stock**, minimum order 100, "Notify Me". That is the decoupling capacitor for most of the board. The order will fail or the line will get flagged.

Drop-in replacement, verified on LCSC today: **C71629 = muRata GRM155R71C104KA88D, 100 nF, 16 V, X7R, 0402**, in stock (listed availability 953,900). Same value, same dielectric, same voltage, same package. Alternative if C71629 is out at order time: **C105883 = YAGEO CC0402KRX7R8BB104, 100 nF, 25 V, X7R, 0402** (in stock, higher voltage rating, harmless).

## What to do

1. Update that one line in `fab/BOM_JLC.csv`: LCSC part number to C71629, Manufacturer to muRata, Manufacturer Part Number to GRM155R71C104KA88D. Change nothing else in the file.
2. Record the substitution and the C105883 fallback in `fab/FAB_NOTES.md` under a new "fixv19b" section, with the date and the reason.
3. Update `fab/ASSEMBLY_CHECKS.json` if it records that code.
4. Repack the fabrication zip as `fab/VitalQuant_v2_fixv19b_fabrication.zip`. Preserve `fixv19a` untouched.

## Then the systematic version of the same check

The last audit verified that every LCSC code resolves to the right value, package and tolerance, but it did not verify stock. Do that now for **every** row in `BOM_JLC.csv`:

- Fetch each LCSC product page.
- Record part number, description, stock quantity, and minimum order quantity.
- Flag any line that is out of stock, below 100 pieces, or below 10x the placed quantity.

Write it to `reports/11_stock_sweep.csv` and list the failures in `reports/11_stock_sweep.md`. If any other line is unstocked, stop and report it rather than choosing a substitute yourself - I will decide.

## Hard constraints

- **Do not touch `vitalquant_v2.kicad_pcb`, the schematic, the gerbers, the drill files, the CPLs, or `positions_kicad.csv`.** Copper is frozen. This is a BOM-only change.
- Prove the copper is untouched: the new zip's gerber and drill members must be **byte-identical** to `fixv19a`'s. State the sha256 comparison in the commit message. If any gerber differs, stop and tell me before doing anything else.
- Connectivity does not need re-running, but say explicitly in the report that it still stands because copper is unchanged.
- Do not change any other LCSC code, value, or DNP status.
- `fixv19b` supersedes `fixv19a` as the file to order. Say that in one line at the top of `FAB_NOTES.md`.

## Done when

- One line changed in `BOM_JLC.csv`, C1525 gone, C71629 present.
- New zip exists, gerbers and drills byte-identical to fixv19a, proven by hash.
- `reports/11_stock_sweep.csv` covers every BOM_JLC row with a stock quantity.
- A one-paragraph summary telling me what to order and what, if anything, is still unstocked.