# Devin prompt — VitalQ v2 fix_v19 pre-fab datasheet verification

Copy everything below the line into Devin as the first message of a new session.

---

## Mission

You are the final electrical gate before an 8-layer PCB goes to fabrication. This board is being ordered today. A missed pin error costs a full re-spin (8 layers, 1.6 mm, ENIG, 302 placements, POFV via-in-pad) and weeks of schedule. A false "all clear" is the worst possible outcome you can produce — worse than reporting twenty real problems.

Your job: independently verify the board against the manufacturer datasheet of every component on it, then report. You are not reviewing the design's style, layout aesthetics, or architecture. You are answering one question: **when this board comes back from the fab and is powered up, will it work, and will anything be damaged?**

## Repo and review target

Repo: `github.com/Riyu-D1/VitalQuant`, branch `feature/vitalq-firmware-v2`.
Root of interest: `hardware/pcb/` (KiCad project `vitalq_v2`, KiCad 10.0.4).

Board under review: **VitalQ v2, revision fix_v19** — 8 copper layers (F.Cu, In1–In6, B.Cu), 55.6 × 75.0 mm, FR4/ENIG, nominal 1.6 mm, 871 vias, 8,016 track segments, 107 zones, 302 footprints, 1,027 pads, 300 component rows, 26 ICs + 13 connectors.

Before doing anything else, pin the review target in a file `reports/00_target.md`:

1. `git rev-parse HEAD` and `git status --porcelain`. If the working tree is dirty, stop and say so.
2. `sha256sum` of `hardware/pcb/vitalq_v2.kicad_pcb`, `vitalq_v2.kicad_sch`, and `fab/VitalQ_v2_fixv19_fabrication.zip`.
3. Confirm the `.kicad_pcb` you are auditing is the one the shipped zip was produced from. If you cannot prove that, say so explicitly and treat the zip as the authority for anything fab-facing (gerbers, drill, BOM, CPL).

Do not review "the latest board". Review one pinned revision and name it in every finding.

## Prior work — read it, then distrust it

The repo already contains a substantial audit trail: `pin_audit/`, `pin_audit_r7/`, `pin_audit_r9/`, `pinmap.json`, `audit_components.csv`, `audit_nets.csv`, `audit_footprints.csv`, `DFM_FAULT_R7.md`, `FIX_V19.md`, `fab/FAB_NOTES.md`, `fab/FAB_VERIFICATION.json`.

Use these only as a *hypothesis list*, never as evidence. Every claim you inherit must be re-derived from the datasheet and the netlist with your own citation. In `reports/08_disagreements.md`, diff your independent pin map against `pinmap.json` and the r7/r9 conclusions and list every disagreement, with your citation and their claim side by side. Disagreements are the highest-value output of this task — prior audit rounds are exactly where a systematic error survives.

Known internal inconsistencies you must resolve rather than assume away:

- `fab/FAB_VERIFICATION.json` inside the fix_v19 zip still names the **fix_v18** zip in its `"zip"` field, and its DRC numbers (61 violations) do not match `pin_audit_r9/ROUND9_REVIEWS.md` (48 warnings).
- `FAB_NOTES.md` says "Source board: `fix_v9/vitalq_v2.kicad_pcb`" while `README.md` calls fix_v19 order-ready.
- 29 BOM-exclusion warnings are described as "owner accepted". Do not accept them; verify they are genuinely limited to J8 and TP1–TP28 and have no electrical consequence.

## Scope — every pin, not a sample

Audit **all** of the following, every pin of every one, no sampling:

ESP32-S3-MINI-1-N4R2 (U1), BQ25170DSGR (U2), TPS63802DLAR (U3), TPS7A2018PDBVR (U4), CP2102N-A02-GQFN28R (U5), AFE4900YZR (U6), AD5940BCBZ-RL7 (U7), ADS1292RIRSMT (U8), PCA9306DCUR (U9), AS7341-DLGM (U10), TMP117AIDRVR (U11, U20), MLX90632SLD-DCB-100-SP (U12), BME280 (U13), LSM6DSV80XTR (U14), TPS61240YFFR (U15), SFH 7072 (U16), MAX17048G+T10 (U17), W25Q512JVEIQ (U18), TCA6408ARSVR (U19), USBLC6-2SC6 (U21), MAX86141ENP+ (U22), SHT45-AD1B (U23), RV-3028-C7 (U24), IM69D130 (U25), SN74AXC2T245RSWR (U26), plus discrete semiconductors (D1/D2/D8/D12/D21–D25/D33, Q1–Q6, L1–L2) and every connector (J1 USB-C HRO TYPE-C-31-M-12, J2 LiPo, J3 FSR, J5/J6/J7/J11/J13 electrode pads, J8 TC2030, J9 I2C tail, J10 optical tail, J12 FH12-14S FFC).

## Method

### 1. Extract mechanically, do not eyeball

Build the ground truth from files, not from reading schematics with your eyes:

- `kicad-cli sch export netlist` for the logical connection set.
- Parse `vitalq_v2.kicad_pcb` (s-expression or `pcbnew` python) for pad-number → net → position, per footprint.
- Produce `reports/01_netlist_pinmap.csv` with one row per **pad** (not per net): `ref, footprint, pad_number, pad_type, net, position_mm, side`.

Completeness gate: the row count must equal the sum of pads across all footprints in the board file (expected ≈1,027, including mechanical pads — state your count and your derivation). If your CSV has fewer rows than the board has pads, you sampled. Regenerate.

### 2. Acquire and fingerprint every datasheet

For each distinct MPN, download the manufacturer datasheet PDF. Record in `reports/02_datasheet_manifest.csv`: `ref, mpn, manufacturer, doc_number, revision, date, page_count, url, sha256_of_pdf, retrieved_at`.

Fingerprinting matters: a pin table is only as good as the revision it came from, and several of these parts have multiple silicon revisions with different errata.

If a datasheet cannot be obtained from the manufacturer or an authorised mirror, mark that part `UNVERIFIABLE` and list it in `reports/09_unverifiable.md` with what you tried. Do **not** substitute a distributor page, a forum post, a SnapEDA symbol, or your memory.

### 3. Pin-by-pin matrix

`reports/03_pin_matrix.csv`, one row per pin of every part above:

`ref, pin_number, pin_name, datasheet_function, datasheet_i/o_type, datasheet_section_page, board_net, connected_to, net_intent_ok, verdict, severity, note`

Verdict is exactly one of `PASS`, `FAIL`, `UNVERIFIABLE`. Every `PASS` must carry a datasheet citation of the form `DOCNUMBER rev §section p.N table/figure`, and a statement of *what it was compared against* (a net name alone is not a comparison — state the other end of the connection or the rail voltage).

Checks per pin, at minimum:

- **Identity**: pad number in the footprint maps to the datasheet pin number for the actual package variant ordered. BGA/WLP ball maps are the highest-risk item on this board (U6, U7, U22 are 0.4 mm pitch WLP; U8, U5, U26 are fine-pitch QFN/UQFN). Verify ball-to-pad mapping against the datasheet ball map *and* the footprint's pad numbering, as two separate facts.
- **Function**: the net's purpose is legal for that pin (a GPIO driving an analogue input, a 1.8 V signal landing on a 3.3 V-only pin, an output tied to an output).
- **Direction/level**: driver vs receiver, and the logic level of every net against the pin's VDD domain. The board has both 3.3 V and 1.8 V domains bridged by U9 and U26 — enumerate every net that crosses a domain boundary and prove the translator is in the path with correct VREF/OE/direction strapping.
- **Unused pins**: genuinely NC per datasheet, or required to be tied (many of these parts require unused inputs tied, not floated). Flag every floating pin whose datasheet says "do not leave floating" or "connect to GND".
- **Exposed pad**: EP electrical role (GND, thermal-only, or a real net), and that the footprint's EP pads carry the right net.

### 4. Electrical adequacy, not just topology

A correct netlist can still be a dead board. Derive the rail map first (`reports/04_rails.csv`: rail name, nominal V, source component, abs-max of each load pin, margin), then prove numerically:

- Every pin's applied voltage and expected transient against that pin's absolute maximum and recommended operating range.
- Required external components per pin from the datasheet's typical application circuit vs what the BOM actually fits: resistor and capacitor **values, tolerances, and dielectric** (X5R vs X7R DC-bias derating on the 0402 4.7 µF/10 µF parts), and inductor saturation rating for L1/L2.
- Local decoupling: for each power pin, compute from the board file the distance to the nearest capacitor on that net. Report the actual millimetres. Compare against the datasheet's requirement where stated; where the datasheet is silent, say so rather than inventing a rule.
- I²C: pull-up values vs bus speed, bus capacitance and the parts' V_IL/V_IH; every I²C device's address pin strapping vs any address conflict on the same bus (compute the full address map per bus and check for collisions).
- Charger path: BQ25170 TS/NTC window, ISET/charge current vs the cell, termination, and the load-sharing path (Q6/D33/R123) behaviour with and without VBUS — including what happens to VBAT_SYS when both sources are present and when only the cell is present.
- Fuel gauge: MAX17048 sense resistor / model configuration vs the cell.
- Optical chain: SFH 7072 LED forward voltage and current vs the TX_5V boost capability and the AFE4900/MAX86141 sink compliance. Check worst-case sum of LED currents against L2's rating.
- AFE inputs: electrode path series R/C vs each AFE's input bias current, input range, and lead-off behaviour; ADC full-scale vs the divider ratios on the FSR and EDA channels.
- ESP32-S3 specifically: strapping pins at reset (GPIO0, GPIO3, GPIO45, GPIO46), EN/RC delay, native USB D+/D− (GPIO19/20) vs the U5 CP2102N path and the R104/R105 DNP disconnect, module antenna keepout copper on all 8 layers, and flash/PSRAM pin usage vs the -N4R2 variant's internal flash.
- Sequencing and POR: any part that requires a rail to be up before another, or a reset held through power-up.
- ESD and exposure: every externally exposed connector and every skin-contact pad vs its protection device's working voltage and clamp direction.
- Thermal: total dissipation per regulator vs package thermal resistance at the actual copper area.

### 5. Footprint geometry against the recommended land pattern

For every footprint with a manufacturer land pattern (all ICs, the USB-C receptacle J1, J12 FFC, the Tag-Connect J8, the LEDs and the MOSFETs), compare against the datasheet's recommended land dimensions: pad length/width, pitch, EP size, solder-mask expansion, paste aperture ratio. Give numbers in millimetres with a tolerance. Record in `reports/05_footprints.csv`.

`audit_footprints.csv` exists; verify it, do not copy it.

### 6. Eight-layer and fab-process specifics

- Verify every via that intersects a solderable land is called out for POFV (filled, capped, planarised). Enumerate them from the board file with coordinates — `FAB_NOTES.md` names two explicitly (U6.E2 under the J8 keepout; J11.3 at x = 46.8, y = 55.3) — and check that list is complete rather than exactly two.
- Confirm the drill set (0.150 mm and 0.200 mm predominant, 875 PTH / 8 NPTH) is inside the fab's minimum drill, annular ring, and aspect-ratio capability, and that 0.150 mm holes are priced and offered by the specific process being quoted.
- Stackup: the notes admit the detailed stackup sums to 1.4776 mm / 1.5386 mm against a 1.6 mm nominal. Quantify the discrepancy and state what the fab must confirm in writing.
- Signal integrity sanity only where it could stop the board working: USB 2.0 differential pair continuity and length match, crystal-free parts, and the 2.2 kΩ / 5.1 kΩ resistors around the USB-C CC pins vs the receptacle's role.
- Creepage claims between electrode nets and other copper: verify from the board file that the `hv_inner`-scoped rule areas actually cover the nets they claim on In5/In6, and state plainly that this is a design review, **not** a medical-safety certification.

### 7. Re-run the tools yourself

Run `kicad-cli` ERC and DRC (with zone refill) on the pinned revision and attach the raw output to `reports/06_tool_output/`. Report the exact counts. If your numbers differ from `FIX_V19.md` or `FAB_VERIFICATION.json`, that is a finding, not a rounding difference — investigate until you can explain the delta.

### 8. Cross-document reconciliation

Checksum and compare: schematic ↔ board ↔ netlist ↔ `BOM.csv` ↔ `BOM_JLC.csv` ↔ `CPL.csv` ↔ `CPL_JLC.csv` ↔ `positions_kicad.csv` ↔ gerbers ↔ drill files. Verify:

- Ref-des parity across all four assembly files, and that every DNP part (R61 in, R93/R104/R105/R107/R108 out — check this yourself) is consistently excluded or included.
- The two consignments (D10 Nichia NF2W757G-F1, D12 XL0603NRC730) are genuinely unorderable at the named distributors today — check stock, do not trust the note.
- CPL rotation and side for every polarised part, and that no part is placed on a pad that belongs to another net.
- Gerber copper count per layer vs the board file's object count. `ROUND9_REVIEWS.md` claims 863 via sites covered on all 8 layers while the board has 871 vias — explain the 8.

## Outputs

Write these files; do not just describe them:

- `reports/00_target.md` — pinned revision, hashes, dirty-tree status.
- `reports/01_netlist_pinmap.csv` — every pad, with the completeness arithmetic.
- `reports/02_datasheet_manifest.csv` — every datasheet with sha256 and revision.
- `reports/03_pin_matrix.csv` — every pin, verdict, citation.
- `reports/04_rails.csv` — rail map with voltage margins.
- `reports/05_footprints.csv` — land-pattern comparisons in millimetres.
- `reports/06_tool_output/` — raw ERC/DRC/drill/gerber-parse output.
- `reports/07_findings.md` — findings by severity, each with: what is wrong, the datasheet citation proving it, the consequence at bring-up, and the smallest possible fix (state whether it needs a re-spin, a rework, a BOM swap, or a firmware constraint).
- `reports/08_disagreements.md` — where you differ from the prior audit rounds, with both sides cited.
- `reports/09_unverifiable.md` — everything you could not prove, and why.
- `reports/10_summary.md` — the one page the owner reads: severity counts, the top five risks ranked by (probability × cost of a re-spin), and a single explicit recommendation: **SHIP** or **HOLD**.

Commit incrementally — one commit per report — so a timeout never loses the whole pass.

## Severity

- **BLOCKER** — board will not function, or will be damaged, or is unsafe to power. Must be fixed before ordering.
- **HIGH** — a function fails or a spec is violated in a plausible operating case; may be fixable in firmware or BOM.
- **MEDIUM** — degraded performance, marginal margin, or a spec violation only at temperature/extremes.
- **COSMETIC** — documentation or silkscreen only.

## Rules of engagement

1. **Read-only.** Do not edit `vitalq_v2.kicad_pcb`, the schematic, the fab zip, or any BOM/CPL. This board is being ordered. Report; do not "helpfully" fix.
2. **No uncited claims.** Every technical statement carries a datasheet citation or a computed number from a repo file. If you cannot cite it, it goes in `09_unverifiable.md`. Do not write from memory of what a part "typically" does, and do not paraphrase a pin table you did not open.
3. **Numeric over qualitative.** "Decoupling is adequate" is not acceptable. "C63 is 1.2 mm from U2.1; datasheet requires ≥1 µF within 2 mm (§6.3 p.11)" is.
4. **Absence of evidence is a finding.** If a net has no other end, a pin has no driver, or a datasheet requirement has no corresponding part on the BOM, that is a FAIL, not a pass by default.
5. **Resolve, do not defer.** If two documents disagree, decide which is right using the datasheet and the board file, and say how you decided.
6. **Stop conditions.** If more than five parts are UNVERIFIABLE, stop the pin-by-pin pass and report the blocker list first — a partial audit presented as complete is worse than an honest partial one.

## Definition of done

- Every one of the 26 ICs and 13 connectors has a datasheet in the manifest with a sha256.
- `03_pin_matrix.csv` row count equals the total pin count derived from the board file, and contains zero blank verdict cells.
- Every FAIL and every BLOCKER has a citation, a stated consequence, and a proposed fix classified as re-spin / rework / BOM / firmware.
- ERC and DRC have been re-run by you, with raw output attached, and any delta from the documented numbers explained.
- `10_summary.md` ends with a single word verdict: SHIP or HOLD.

Begin with `00_target.md` and the datasheet manifest. Report the manifest back to me before starting the pin matrix, so I can confirm you have the right parts.