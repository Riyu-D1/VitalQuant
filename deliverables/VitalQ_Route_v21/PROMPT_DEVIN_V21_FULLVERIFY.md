# VitalQ fix_v21: FULL single-pass verification and simulation (REPORT-ONLY)

Routing is done. Now verify EVERYTHING on the fix_v21 board in **ONE run**. The owner gets one funded build. Earlier rounds kept finding new problems because each was narrow or stopped early. This run must find **all** problems in a single pass.

## Hard rules
- **REPORT-ONLY.** Do NOT edit `fix_v21/vitalq_v2.kicad_pcb`, `.kicad_sch`, `.kicad_pro`, `fab/` or anything in `case_*`. Work on COPIES in `fix_v21/verify21/` (create it). Write only inside `fix_v21/verify21/` and `fix_v21/VERIFY_REPORT.md`.
- **Never stop at the first failure.** Every check below MUST be run even if earlier ones fail. A check that crashes gets one retry with a different method. If it still fails, record it as `UNVERIFIED` with the reason, and carry on.
- Never touch `fix_v19` (the order-ready fallback) or the `fix_v20*` folders. Delete nothing (stale files go to `_stale/`). No commit/push. No firmware edits. No ordering, no pricing, no contacting anyone.
- Never claim medical safety, regulatory compliance or clinical validity. Use wording like "within the datasheet limit" or "meets the IEC 60601-1 numeric BF limit in this model", never "safe" or "compliant".
- **No time caps.** No templated rows: if two different part types have identical "checks performed" text, that counts as a failure of this run. Cite datasheet page/table numbers. Give the file and line, or the coordinates, as evidence for every finding.
- Keep installs light (pip in a venv inside `verify21/`; ngspice is already used in `sims/`).
- Reuse the earlier scripts wherever they work, and adapt them for the WROOM:
  - `../verify/` (`parse_net.py`, `dump_nets.py`, `pinmap.py`, `power_budget.py`, `crosstalk.py`, `ic_tables.py`, `final9/`, `deep/`, `stress/`, `loop/`, datasheet caches)
  - `../sims/fullsim_v20/` (`run_all.py`, `decks/`, `models/`, `build_audit.py`, the gerber_check, the 20 blocks b01…b20)
  - `../sepsis_v11/`
  - `../case_v8/verify_fit.py`
  - `../verify/final9/FINAL9_REPORT.md` (the 19 checks)
  - `../case_v3/PROMPT_DEVIN_FULLSIM_V19.md` (the Part A/B/C standards)
  - `../verify/PROMPT_DEEP.md` (per-part and per-net method)

## Context
- fix_v21 = fix_v19 with U1 changed from ESP32-S3-MINI-1-N4R2 to the **ESP32-S3-WROOM-1-N16R8** at (11.5, −6.5) rot 0, and 41 new U1 connections hand-routed (see `V21_PROGRESS.md`, `WROOM_REPORT.md`, and `FIRMWARE_NOTES.md` §13).
- Reviewer pre-check (20 Oct, ~20:30 BST, on a temp copy with the v19 `.kicad_pro`/`.kicad_dru`): DRC error-severity 0 violations / 0 unconnected / 0 parity, and ERC errors 0. **Re-run these yourself; do not trust them.**
- Known points to scrutinise:
  - Devin reported the new routes run "mostly on In1.Cu". **In1.Cu is the L2 GND plane** and In6.Cu is the L7 GND plane. Check plane integrity, slots, splits and return paths.
  - C64 and C8 moved −0.15 mm in X.
  - CS_MAX86178 takes a long detour (In1 → x 21.5 → In6 → B.Cu).
  - The STATUS_LED via is at (5.5, 18.46).
  - USB_DP has an In5.Cu hop around the ESP_EN horseshoe.

## Checks (one numbered section each in VERIFY_REPORT.md; every check row = PASS / FAIL / WARN / UNVERIFIED + evidence + file/line or xy)

### 1. DRC / ERC / parity
Work on a copy with the fix_v19 `.kicad_pro` + `.kicad_dru`:
- (a) DRC `--severity-error` without refill;
- (b) DRC after a zone refill;
- (c) `--severity-all`: list every warning class with counts and decide if each one is acceptable;
- (d) `--schematic-parity`;
- (e) ERC errors and warnings.
Report the counts.

### 2. Netlist pad-to-pad (schematic vs PCB vs Gerber)
- Export the netlist: `kicad-cli sch export netlist` (kicadsexpr and kicadxml).
- Parse every net from the PCB with the pcbnew Python API at `/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3`.
- Compare **every pad of every net**: schematic vs PCB. Write `verify21/NETS.csv` (net, schematic pads, PCB pads, match, driver(s), receivers, voltage domain, pull-up/down, floating?, single-pin?).
- Do a copper-connectivity check per net: is each net one connected copper island? Do it from the board, and separately **from the Gerbers + drill only**, by re-parsing `fab/` (reuse `sims/fullsim_v20/gerber_check`).
- Report the number of nets checked out of the total.

### 3. ESP32-S3-WROOM-1-N16R8 specific
Cite the Espressif WROOM-1 datasheet and the ESP32-S3 datasheet with page numbers:
- Pin table: every U1 pad 1–41 → module pin name → GPIO → net, compared three ways (`pinmap.json` vs schematic vs PCB). Every GPIO must have exactly one function, and its direction and voltage domain must match the peer IC.
- **Octal PSRAM pins IO35, IO36, IO37 (pads 28–30) must be unconnected.** On the N16R8 they are used internally by the octal PSRAM. Also check that no internal flash/PSRAM SPI pins (GPIO26–32) are exposed or used.
- Strapping pins:
  - IO0: boot button and pull-up, default high.
  - IO3: JTAG source.
  - IO45: VDD_SPI voltage. **It MUST be low/floating for the N16R8's 3.3 V flash.** A pull-up would brick the flash.
  - IO46: ROM log / download.
  For each, give the level at reset and what is attached to it (including any peer IC that could drive it during boot).
- EN RC: the R and C values, the rise time against the datasheet requirement (EN must go high after 3V3 is stable; Espressif recommends 10 kΩ / 1 µF). Check the reset button/auto-reset if present.
- USB D−/D+ on IO19/IO20:
  - which series resistors, ESD part and connector are used;
  - differential impedance target 90 Ω ±10% from the stack-up (compute it from the board's layer stack in `.kicad_pcb`/`.kicad_pro`);
  - intra-pair length mismatch (aim ≤0.15 mm; compute it from the tracks);
  - vias and layer changes counted (including the new In5.Cu hop);
  - stubs.
  Full Speed USB is tolerant, so grade it WARN versus FAIL accordingly.
- UART0 TX/RX (IO43/IO44) to CP2102 or the header. **R72/R73 10k and leakage into the CP2102 when it is unpowered** (back-powering through the TX/RX pins). Compute the µA and compare with the CP2102 datasheet.
- Power:
  - 3V3 rail headroom for WROOM Wi-Fi TX peaks of about 500 mA (datasheet ≥500 mA supply);
  - the regulator (TPS63802) current limit and transient at a 500 mA step;
  - bulk capacitance at the module (≥10 µF + 0.1 µF close, measured pad-to-pin distance in mm);
  - droop simulation in ngspice and brown-out margin (BOD ~2.7 V).
- Antenna:
  - the keep-out is empty on **all** layers, including zones, vias and silk-on-copper;
  - the antenna sits at or overhangs the board edge per the Espressif layout guide (give the distances);
  - there is no GND pour under the antenna on any layer;
  - case material and the clearance over the antenna (case_v8).
- Thermal/EP pad 41: the GND via count and grid, the paste aperture, and solder voiding risk.

### 4. Every part (all refs, no skipping)
Use `verify21/PARTS.csv`, one row per refdes. For each part:
- footprint vs datasheet land pattern;
- pin-1/polarity marking vs the footprint vs the CPL rotation (JLC rotation convention);
- value, tolerance and voltage vs the circuit need;
- capacitor DC-bias derating at the real rail, and the effective µF vs the IC minimum;
- resistor power, including the single-fault case (5 V across the skin-path 51k);
- inductor Isat/Irms vs the peak;
- for ICs, abs-max vs worst case at all corners, including back-power when unpowered;
- MPN and LCSC number present and matching (package/value/rating);
- top or bottom side vs function.
Give the total checked out of the total refs.

### 5. Decoupling and pull-ups per IC
For every IC:
- each supply pin's required decoupling (from the datasheet) vs fitted, with the measured distance in mm;
- the I2C pull-up value per bus and the rise time at 100 kHz (and 400 kHz if used), with the bus capacitance estimated from the track lengths;
- the address map (no clashes);
- the PCA9306 levels.

### 6. Power, budget, thermal
- The power tree; current-carrying track widths vs current (IPC-2152, internal vs external layers) for VBUS, VBAT, VBAT_SYS, 3V3, TX_5V and GND returns.
- The **power budget and battery life** with the WROOM, which draws more than the MINI with PSRAM. Cover idle, BLE, Wi-Fi bursts and the full sensor duty cycle.
- LDO, charger and buck-boost thermal: Tj at 25 and 45 °C ambient (BQ25170, TPS63802, TPS7A20, TPS61240).
- Power sequencing, back-powering, plug/unplug transients and the Q6/D33 power path.

### 7. Signal integrity
- **SPI clock ≤8 MHz to the MAX86141/MAX86178**, and the SN74AXC2T245 direction/OE.
- Length, stubs and crosstalk (reuse `crosstalk.py`) for **every new WROOM route**, especially routes on In1/In6 next to analogue nets (ECG/EDA/BioZ/PPG).
- SPI CS lines tri-state, and no shared-MISO conflict.
- The I2C 100 kHz timing.
- The USB from §3.

### 8. Analogue front-ends end-to-end
- ECG ADS1292R: CMRR Monte Carlo (≥500 runs), RLD, lead-off, input range, HP corner.
- AFE4900 and MAX86141/MAX86178 + SFH7072: LED compliance and current, **LED pulse staggering/interleave on TX_5V**.
- AD5940 EDA/BioZ: CE compliance with the 51k.
- TMP117 slug and offset, IMU, humidity, FSR, mic.
- **Check whether any new In1/In6 route passes under or near the electrode or analogue areas.**

### 9. Protection, ESD, patient-current (numbers only)
- ESD on USB and on every exposed skin/pogo pad.
- Single-fault DC patient current on all skin lands, compared with the IEC 60601-1 numeric BF limits (report the numbers only; no compliance claim).
- **R72 leakage path into the CP2102.**

### 10. The 20 simulation blocks
Re-run b01…b20 from `sims/fullsim_v20/run_all.py`, adapted to the WROOM (b09 ESP32 straps/EN/antenna; b08 sequencing; b20 budget). Use real decks and results on disk in `verify21/sims/`, with corners 0/25/45 °C, battery 3.0/3.7/4.2 V and USB 4.75/5.0/5.5 V. Report a margin % for every block. Anything under 20% is a FAIL or WARN, with the physical reason.

### 11. The 19 FINAL9 checks
Re-run all 19 rows of `../verify/final9/FINAL9_REPORT.md` §10 on fix_v21, one row each:
DRC, parity, Gerbers, drill, BOM, CPL, zip, netlist, IC pins, earlier fixes, power, USB/ESD/leakage, thermal, sensor chains, SI/crosstalk, antenna, mechanical, JLC DFM, sepsis.

For the earlier fixes row, confirm each one is still present:
- R61 is the 10k NTC (fitted);
- R107/R108 DNP;
- R88 150R;
- R19–22 2.2k;
- R51 150k;
- R72/R73 10k;
- R104/R105 DNP;
- D10/D12 consigned;
- the U12/U23 parts;
- J3/J7 positions.

### 12. Sepsis simulation (238 patients)
Run `cd ../sepsis_v11 && python3 run.py && python3 score_v11.py` on a copy inside `verify21/sepsis/`, with the pinned detector trend_ews_v11 and seed 7. The expected result is sensitivity 0.98 and specificity 0.853. Report any difference. It is a feasibility simulation only.

### 13. Mechanical / case
- Run `../case_v8/verify_fit.py` on a copy, with the geometry regenerated from fix_v21.
- Check the board outline, the north tab with the WROOM (the WROOM is longer and taller than the MINI: 18 × 25.5 × 3.1 mm vs 15.4 × 20.5 × 2.4 mm) against the case cavity, lid clearance and antenna window.
- Check every pogo-pin/contact pad position against the case_v8 pins (the C64/C8 moves should be irrelevant, but prove it).
- Check connector access.
- **Flag the case changes needed. Do NOT edit any case file.**

### 14. Fab outputs
In `verify21/fab_regen/`:
- Regenerate the Gerbers, drill, BOM and CPL from the board copy, and diff them against `fix_v21/fab/` (layer count, aperture/hit counts, SHA-256).
- Check that every BOM line has an LCSC part number, or is marked consigned/DNP with a reason.
- Check that CPL XY/rotation/side exactly match the board, with JLC rotation offsets for the WROOM (and for SOT-23, QFN and similar packages).
- Check JLC 8-layer capabilities: min track/space, via, annular ring, POFV, mask web, silk, board thickness/stack-up, impedance-control availability.
- Check zip integrity.

### 15. Renders
Produce new 3D renders (top, bottom and an isometric view) in `verify21/render/`, plus the U1 close-ups. Look at them and note anything visually wrong (missing 3D models, wrong side, wrong rotation).

### 16. Adversarial final pass
- "What would make the first board not boot at all? Not enumerate on USB? Not charge? Leave one sensor dead?"
- List anything not covered and cover it.

## Output
`fix_v21/VERIFY_REPORT.md`:
1. Executive summary, with counts of PASS/FAIL/WARN/UNVERIFIED and BLOCKER/MAJOR/MINOR.
2. A table of BLOCKER and MAJOR issues first. Each one gets a concrete suggested fix (do not apply it) and says whether it needs a board change.
3. Sections 1–16 in full.
4. Coverage: parts N/N, nets N/N, U1 pads 41/41, sim blocks 20/20, FINAL9 checks 19/19.
5. What cannot be verified without hardware.
6. A bench bring-up test list.

Also keep `verify21/PROGRESS.md` updated, so a new window could resume.

End by printing exactly:
`VITALQ V21 VERIFY COMPLETE  PASS=<n> FAIL=<n> WARN=<n> UNVERIFIED=<n>  BLOCKER=<n> MAJOR=<n> MINOR=<n>`
