# VitalQ hw_v1: pre-fabrication design review (commit 87e94a46)

Reviewed: 27 Sep 2026 (BST). Inputs: VERIFICATION.md, PINMAP.md, vitalq_hw_v1_bom.csv, vitalq_hw_v1.pdf (all sheets rendered), pcb-*.png, earlier review notes, and datasheets in /workspace/vitalq-hw/ds and ds2.

Board: 4-layer, 36.5 × 53.4 mm, 191 parts. Status: unrouted, ERC 0.

## How the claims are tagged
- **[checked]**: verified against a cited datasheet page or section, or a URL.
- **[inferred]**: engineering reasoning from checked facts, not directly confirmed.
- **[guess]**: plausible but unverified. The CAD agent must confirm before relying on it.

LCSC stock figures come from JLC parts search on 27 Sep 2026 and will change.

## Already fixed since earlier reviews (no action; checked in schematic/BOM)
- **ESP32 reset and programming:**
  - EN RC is 10k + C64 1 µF.
  - Auto-program uses Q3 BC847BS.
  - R72/R73 1k series resistors on UART.
  - CP2102N has RSTb pull-up R62 and 4.7 µ + 100 n decoupling.
- **Charger and battery:**
  - BQ25170 has cell NTC on TS and Q2 TS-disable.
  - R75 27k gives VREG 4.20 V (SLUSDJ8A Table 7-1).
  - R3 3.0k gives ICHG = 300/3000 = 100 mA (KISET 300 AΩ).
  - R18 200k / R66 301k bleed.
- **Front-end protection and ECG:**
  - Every electrode line is protected by 51k DPCR2512 → TPD1E10B06 → series R.
  - RLD integrator C65 1.5 n; RLDREF bypass C66 1 µ.
  - AFE4900 ECG bias comes from ADS RLDOUT via 5.11 M.
- TCA6408A /RESET is on ESP_EN.
- **Pin maps verified against datasheets [checked]:**
  - TPS63802 (DS Table 7-1): R23/R24 560k/100k gives 3.3 V.
  - BQ25170 (Table 5-1).
  - TPS61240 YFF balls.
  - AD5940 WLCSP ball map (Rev C Fig 5 / Table 7).
  - AFE4900 supplies: RX_SUP = IO_SUP = 3V3 in LDO mode, TX_SUP = 5 V.
  - SFH7072 12-pin map (DS p.2).
  - ADS1292R pins 1–16 and 25–33 (SBAS502C). CLK is open, which is allowed with CLKSEL = 1.
  - TCA6408A RSV, PCA9306 DCU, TMP117 DRV, MLX90632, BME280.
  - LSM6DSV80X (DS14764 Table 2: pins 10/11 NC).
- **I2C addresses:** no conflicts [checked].
- **ESP32 straps [checked, WROOM-32E DS v2.1 Table 4]:**
  - GPIO0 has an external pull-up.
  - GPIO2 and GPIO12 (MTDI) are open with internal pull-down, which gives 3.3 V flash. That is correct.
  - GPIO15 (MTDO) has a pull-up.
  - GPIO5 has an internal pull-up and serves as CS_AFE.
  - IO16 is reserved for PSRAM on N8R2 and is correctly unused.
- **ADC pins:** no analog function is placed on ADC2 pins. GPIO34–39 are used only as inputs. Pins without internal pulls have external ones where needed (e.g. EXP_INT on GPIO35) [checked PINMAP vs ESP32 TRM].
- **Power budget [inferred from checked ratings]:**
  - TPS63802 is rated 2 A, well above the ESP32 Wi-Fi peak of about 0.5 A.
  - TPS61240 is rated 200 mA at VIN 2.3–5.5 V (SLVS806D §7). It only feeds the pulsed PPG LED drive (TX_5V), which draws mA-level average current with µs pulses buffered by bulk capacitance. OK if ≥10 µF stays on TX_5V (C44 10 µ 0603 is present).
  - TPS7A20 is rated 300 mA; the 1.8 V loads are in the mA range. OK.
- **Running while charging [checked, BQ25170 DS]:**
  - The BQ25170 has no power path: the system is on OUT/BAT in parallel ("system load may be connected in parallel with the battery").
  - The board runs while charging, but the charge current is shared with the load. See F1.

---

## A) Must-fix errors (the CAD agent must implement all of these)

**A1. MAX17048 symbol and footprint do not match the MPN [checked].**
- The BOM MPN MAX17048G+T10 is the 8-TDFN-EP 2 × 2 mm package (Maxim package code T822+3, outline 21-0168). The "X+T10" suffix is the WLP.
- The symbol uses WLP ball names (A1–B4) and the footprint is `vitalq:MAX17048_WLP`.
- Fix:
  - Keep MPN MAX17048G+T10 (LCSC C2682616, in stock).
  - Renumber the symbol to TDFN: 1 CTG, 2 CELL, 3 VDD, 4 GND, 5 ALRT, 6 QSTRT, 7 SCL, 8 SDA, 9 = EP → GND.
  - Use footprint `Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm` (see E2).
  - Keep the existing nets: CTG → GND, QSTRT → GND, CELL → VBAT, VDD → VBAT, ALRT → GPIO25 with pull-up. Check each against the net names after renumbering.
- Sources: MAX17048 DS Rev 7 pin description and ordering information; 21-0168 package drawing.

**A2. ADS1292R GPIO1 (pin 26) and GPIO2 (pin 25) are floating [checked, SBAS502C, GPIO section].**
- The datasheet requires unused GPIO pins to be driven, or shorted to DGND through a series resistor.
- Fix: add 2 × 10 kΩ 0402 (C25744, basic) from each pin to GND.

**A3. The TMP117 thermal pad is soldered with paste [checked, TMP117 DS SNOSD82 §9/§10.1 layout guidelines].**
- For best accuracy on a rigid PCB, TI recommends not soldering the thermal pad (it is not required for electrical function).
- Fix: in a local copy of the TMP117 DRV footprint (U-ref of TMP117), remove paste from the centre pad, or delete the EP pad entirely.
- Keep the pull-up resistors, U2, the switchers and the ESP32 away from it (see C4).

**A4. VBUS input capacitor C1 is 4.7 µF 6.3 V 0402 (GRM155R60J475ME87) [inferred].**
- It sits directly on USB VBUS / BQ25170 IN. Hot-plug ringing on a USB cable can overshoot well above 5 V.
- At 5 V DC bias, a 6.3 V 0402 X5R retains only a fraction of its capacitance.
- Fix: change C1 to 4.7 µF 25 V X5R 0603, Samsung CL10A475KA8NQNC (LCSC C69335).
- Also check that the VREGIN/VBUS decoupling at the CP2102N is not a 6.3 V part on the VBUS net. If it is, apply the same change.

**A5. The L1 symbol overlaps U4 on schematic sheet 1 [checked visually on the rendered PDF].**
- Electrically it is probably not shorted: the SW_L2 wire runs between the IN and EN wires [inferred], but this cannot be proven without the netlist.
- Fix: move L1 clear of U4 and redraw its wires. Then confirm in the exported netlist that:
  - SW_L1 contains only U3 pin 9 and L1 pin 1;
  - SW_L2 contains only U3 pin 7 and L1 pin 2;
  - neither net touches +3V3 or U4.

**A6. Schematic parts sit on the title block or border [checked visually].**
- Sheet 4 (AFE4900): R43, R45, C56, C57 and the AFE_INP/INM labels.
- Sheet 6 (ADS1292R): D3, C66, D4, D5.
- Fix: move them inside the drawing area.
- Afterwards, re-run ERC and confirm that no label now dangles or has become merged.

**A7. Inner-layer copper under defibrillation-exposed nodes [inferred].**
- The JLC 4-layer 1.6 mm stack has about 0.2 mm 7628 prepreg between L1 and L2. A solid L2 GND under the electrode pads, the electrode-side pad of each 51k 2512 resistor and the traces between them therefore leaves only about 0.2 mm dielectric to kV-level pulses.
- Fix: add rule-area keep-outs (no copper on L2, L3, L4, and no vias) under J5, J6 and J7 pads and all electrode-side nets up to the 51k resistor. Extend each keep-out ≥1.0 mm beyond the copper.
- Surface clearance from those nets to any other net: ≥2.5 mm target, ≥1.5 mm absolute minimum.
- This is prototype-level practice, not a certified IEC 60601-1 creepage analysis [guess on the exact distances].

**A8. Parts JLC cannot currently supply, or supply in too-low quantity [checked, JLC/LCSC search 27 Sep 2026].**
- The build must not proceed with these unresolved. Assign the LCSC field as follows and put the alternatives in F.

| Part | Status | Action in BOM |
|---|---|---|
| DPCR2512-51KJT18 (all 51k defib R) | not listed | Leave LCSC blank and flag "consign/global sourcing" (F5) |
| MLX90632SLD-DCB-100-SP | 0 stock | Flag (F2); in-stock alternative -000-SP C5240460 |
| LSM6DSV80XTR (C46898862) | 0 | Flag (F4); LSM6DSV16XTR C5267406 same LGA-14 2.5×3 [inferred pin-compatible] |
| W25Q512JVEIQ (C2986165) | 0 | Flag (F3); W25Q256JVEIQ C97522 same WSON-8 8×6 |
| NF2W757G-F1 | 0 | Consign / global sourcing |
| CSD13380F3 | 0 | Use CSD13380F3T C2871092 (same part, small reel) [checked] |
| AD5940 C650308 (26), ADS1292R C882777 (16), TCA6408A C2649390 (17), SFH7072 C2655172 (47) | low | OK for ≤5 boards; order promptly or pre-order to the JLC parts library |
| 10 M 0402 (UniOhm stock 2), 301k (stock 6) | low | Pick any in-stock 1% 0402 of the same value |

**A9. JLC assembly tier and panel [checked, jlcpcb.com/capabilities/pcb-assembly-capabilities].**
- The design needs **Standard PCBA**, for three reasons:
  - it has 0.4 mm-pitch BGAs (Economic requires ≥0.5 mm);
  - it has parts on both sides;
  - it has LGA/QFN parts that need X-ray.
- Standard PCBA needs a minimum single board of about 70 × 70 mm plus rails and fiducials, so the 36.5 × 53.4 mm board must be panelised.
- Fix: build a 2 × 2 panel (≈75 × 119 mm including rails) or 2 × 1 with 5 mm rails on the long edges, using mouse bites.
  - Rails: 2.0 mm NPTH tooling holes and 1.0 mm fiducials placed 3.85 mm from the rail edge (JLC guideline).
  - Keep tabs off the antenna edge and off the electrode-pad edges.
  - Alternative: order "Panel by JLCPCB" and state 2 × 2 with rails.

**A10. Via-in-pad is mandatory for AFE4900 (DSBGA-30, 0.4 mm pitch, 0.23 mm balls) and AD5940 (WLCSP-56, 0.4 mm pitch) [checked + inferred].**
- The gap between 0.214–0.23 mm pads at 0.4 mm pitch is about 0.17–0.19 mm. That is not enough for a 0.09 mm track with 0.09 mm clearance each side, so inner balls cannot escape on L1.
- Inner-ring balls needing escape:
  - AFE4900: about 9 signal balls (INP2, INM2, CONTROL1, IO_SUP, BG, TX1, SDOUT, SEN, TX3).
  - AD5940: the second ring carries about 12 signals; the centre balls (C4, D5, E3–E6) are grounds.
- Fix:
  - Use epoxy-filled, copper-capped via-in-pad (JLC "POFV") only on those balls.
  - Via size: 0.15 mm hole / 0.25 mm diameter if the current capability page accepts POFV at that size; otherwise 0.2 / 0.30 mm.
  - BGA pads: enlarge to 0.25 mm NSMD for via-in-pad balls, keep 0.23 mm elsewhere.
  - Surface finish: ENIG (required for fine BGA flatness).
- Source for POFV sizes: https://jlcpcb.com/news/free-via-in-pad-6-20-layer-pcbs-pofv. That page lists min 0.2 hole / 0.3 diameter and a 0.45 mm spacing to PTH. The newer capability page lists smaller sizes; the CAD agent must use whichever is valid at order time.
- POFV is a paid option on 4-layer and free on 6–20 layer (same URL). See F6.

---

## B) Recommended additions (implement all unless marked optional)

| # | Addition | Part / LCSC | Package | Why | Cost |
|---|---|---|---|---|---|
| B1 | USB ESD on D+, D−, VBUS, close to J1 | USBLC6-2SC6 C7519 (or smaller USBLC6-2P6 SOT-666 C15999, or TPD2E2U06DRLR C1972959 for data only) | SOT-23-6 / SOT-666 | USB is the only user-touched port; CP2102N ESD is limited; the wearable is plugged in daily [inferred] | low |
| B2 | 22 µF bulk at the ESP32 3V3 pin: change C7 10 µ 0402 6.3 V to 22 µF 0603 10 V | CL10A226MQ8NRNC C59461 (basic) | 0603 | Espressif WROOM-32E peripheral schematic uses 22 µF + 0.1 µF on 3V3 [checked, WROOM-32E DS §peripheral schematic]; the 0402 10 µ derates heavily | low |
| B3 | Reset button EN → GND | TS-1088-AR02016 C720477 (basic) | 4 × 3 mm SMD | Recover a crashed or locked board without unplugging; first prototype essential | low |
| B4 | Boot button IO0 → GND | TS-1088-AR02016 C720477 | 4 × 3 mm | Manual download mode if the auto-program circuit misbehaves | low |
| B5 | Status LED: GPIO17 → 1 kΩ → green LED → GND (GPIO17 is free on N8R2, no strap) [checked PINMAP + DS note 3] | KT-0603G C12624 + 1k C11702 | 0603 / 0402 | Boot/firmware heartbeat without USB; no strap interaction | low |
| B6 | 10 kΩ pull-up on CS_ADS1292 (ESP32 GPIO4) | 10k C25744 | 0402 | GPIO4 has a default internal pull-down during reset/boot, which would select the ADS1292R and let it drive the shared MISO while other devices are accessed [inferred] | low |
| B7 | 100 kΩ pull-up on ADS1292_DRDY (GPIO34, no internal pull) | 100k C25741 | 0402 | Defined level when the ADS1292R is in PWDN/RESET, avoiding spurious interrupts [inferred] | low |
| B8 | 0 Ω series footprint on each device MISO (fit 0 Ω) | 0 Ω C17168 | 0402 | Isolate a device that fails to tri-state SDOUT. AFE4900 tri-state behaviour is not in the public DS [guess] | low |
| B9 | Current-measurement links: 0 Ω in series at (a) VBAT → system (TPS63802 + TPS61240 inputs), (b) +3V3 → ESP32, (c) +3V3 → analog front ends group, (d) TX_5V output, (e) +1V8 output | (b)(a): 0603 0 Ω, agent to pick a basic LCSC part, e.g. 0603WAF0000T5E [guess on LCSC#]; (c)(d)(e): 0402 0 Ω C17168 | 0603 / 0402 | Lets Riyansh measure per-rail current for the battery-life claim in the funding application | low |
| B10 | Test pads 1.0 mm round, no paste, top side where possible, labelled: VBUS, VBAT, +3V3, +1V8, TX_5V, VDD_CP2102, GND ×2, ESP_EN, ESP_IO0, ESP_TX, ESP_RX, I2C_SDA/SCL (both 3V3 and 1V8 sides), SPI_SCK/MOSI/MISO, each CS, ADS1292_DRDY, AFE_ADC_RDY, EXP_INT, CHG_STAT | copper only | 1.0 mm pad | Probe access; no BOM cost | low |
| B11 | UART/boot programming footprint: Tag-Connect TC2030-NL footprint (or 6-pad 1.27 mm row) carrying 3V3, GND, TX, RX, EN, IO0 | copper only | TC2030 | Flash and debug when USB/CP2102N is suspect or the enclosure blocks USB | low |
| B12 | Fiducials: 3 per populated side (top and bottom), 1.0 mm copper, 2.0 mm mask opening, asymmetric placement | copper only | – | Required/strongly recommended by JLC for Standard PCBA [checked JLC assembly capabilities] | low |
| B13 | Two mounting/strap-anchor holes: NPTH 2.2 mm (M2), diagonal corners, 1.0 mm copper keep-out; if space prevents it, 1.6 mm NPTH for enclosure pegs | – | – | Mechanical retention in the chest housing; stops strain on the electrode pads [inferred] | low |
| B14 | Silkscreen: "VitalQ hw_v1 rev A 2026-09", a "JLCJLCJLCJLC" order-number placeholder on the bottom in a non-critical area, pin-1 dots on every IC/LED/SFH7072, "+" / "−" on J2 (BAT+ / BAT−, larger +), function labels on every J pad (J2 BAT+/BAT−/NTC; J3 FSR; J5 per electrode; J6 EDA; J7 F+ F− S+ S−). J5 labels are not visible in pcb-bottom.png [inferred] | – | – | Assembly, debug and safe battery connection. There is no reverse-polarity protection (see F1) | low |
| B15 | Optional: bottom-side thermal-isolation slots (NPTH routed, 1.0 mm wide) partly around TMP117 and MLX90632, leaving bridges for traces | – | – | Decouples the skin-temperature sensor from board heat. TI recommends thermal isolation for body-temperature sensing [inferred from TMP117 DS §10] | low |

---

## C) Layout and routing rules for the CAD agent

**C1. Stackup [checked JLC capability; stack choice inferred]:**
- JLC standard 4-layer 1.6 mm (JLC04161H-7628), 1 oz outer / 0.5 oz inner, ENIG, green or black mask.
- Layers:
  - L1: signals + components.
  - L2: solid GND, no splits.
  - L3: power pours (3V3, VBAT, TX_5V, 1V8) + low-speed routing.
  - L4: signals + skin-side components + GND pour.
- Stitch GND vias every ≤3 mm along the edges and around the RF/switcher areas.

**C2. Ground:**
- One ground; no split analog/digital planes.
- Partition by placement instead:
  - Analog zone: AFE4900 + SFH7072, ADS1292R, AD5940, and their passives near J5/J6/J7.
  - Digital zone: ESP32, flash, CP2102N, expander.
  - Power zone: U2 charger, U3/L1 TPS63802, U15/L2 TPS61240.
- Digital return currents must not pass under the front ends.
- Each switcher's input cap, output cap and GND pin form a tight loop on L1, with multiple vias to L2.

**C3. ESP32-WROOM-32E antenna [checked, Espressif hardware design guidelines]:**
- Place the antenna at the board edge, with the module antenna overhanging or flush with the edge.
- Keep-out: no copper on any layer, no traces and no parts under or around the antenna. Espressif's guideline is ≥15 mm clearance area where possible; at minimum, a copper-free area on all layers under the antenna.
- No batteries, electrodes or metal within that zone.

**C4. Thermal [inferred]:**
- Place U2 (BQ25170, dissipates while charging), U3/L1, U15/L2 and the ESP32 as far as possible from U11 (TMP117), U20 (MLX90632) and U12, ideally at the opposite end of the board.
- No power pours on L3/L4 directly under TMP117 and MLX90632. Use a small local GND island under TMP117 tied by a narrow neck (TI: good thermal coupling to skin, isolation from board heat).

**C5. Skin side (bottom, L4):**
- Parts: SFH7072, AS7341 + Nichia LED D10, MLX90632, TMP117.
- Nothing taller than the optical parts should sit near the optical windows.
- Keep D10 ≥ ~5 mm from the AS7341 aperture, or plan an enclosure optical barrier [inferred].
- Leave the SFH7072 window clear of silkscreen.

**C6. Electrode nets (J5, J6, J7):**
- Create netclass `HV_ELECTRODE` (all nets from pad to the 51k resistor): track 0.25 mm, clearance ≥1.5 mm to any other net. Apply the A7 keep-outs.
- After the 51k resistor, netclass `ELECTRODE_LV`: 0.15 mm track, 0.2 mm clearance, as short as possible.
- Differential ECG/BioZ pairs are routed symmetrically, side by side, with equal length.
- Place TPD1E10B06 right after the 51k with the shortest possible GND via.

**C7. PPG photodiode (SFH7072 PD → AFE4900 INP/INM):**
- Shortest possible, routed as a pair.
- GND guard traces either side on the same layer, stitched to L2.
- No digital nets on adjacent layers under them.

**C8. USB:**
- Full-speed (12 Mb/s), so JLC controlled impedance is not required [inferred].
- Route D+/D− as a tight pair (about 90 Ω; for JLC04161H-7628 L1 over L2, roughly 0.20 mm width / 0.15 mm gap, verify with the JLC impedance calculator [guess]).
- Length-match within 1 mm, with no stubs.
- ESD part (B1) placed at J1.

**C9. SPI:**
- Put series termination footprints (B8) close to the driver.
- SCK on L1/L4 away from the analog zone. Do not route SCK under AFE4900, ADS1292R or AD5940 inputs.

**C10. I2C:** separate 3V3 and 1V8 segments across PCA9306. Keep the 1V8 segment (MLX90632) short.

**C11. W25Q512 WSON:** no exposed vias in the EP pad (datasheet: "avoid exposed vias under EP" [checked W25Q512JV Rev G §9.1]). The EP may connect to GND through tented vias outside the paste area.

**C12. Via sizes:**
- Standard vias: 0.3 mm hole / 0.5–0.6 mm diameter (no JLC surcharge).
- Small vias (0.2/0.3–0.4) and POFV only where BGA fan-out needs them (A10).
- 0.15/0.25 is allowed but costs extra [checked JLC PCB capabilities].

**C13. JLC DRC rule set (4-layer) [checked jlcpcb.com/capabilities/pcb-capabilities]:**
- Track/space minimum 0.09/0.09 mm, but use 0.127/0.127 everywhere except BGA fan-out.
- Copper to board edge ≥0.3 mm (minimum 0.2).
- Annular ring ≥0.075 mm (POFV 0.05 minimum).
- Solder-mask bridge ≥0.10 mm; pad to silkscreen ≥0.15 mm.
- Silk line ≥0.15 mm, text height ≥1.0 mm.
- Hole to hole ≥0.5 mm.
- NPTH slot ≥1.0 mm.

**C14. Decoupling:** every 100 nF within 1 mm of its pin, with a via to L2 at the pad. Bulk caps near the regulator outputs.

---

## D) Manufacturability notes (JLCPCB)

- **D1. Fine-pitch parts [checked JLC assembly capabilities: Standard supports 0.35 mm pitch, 0.3 mm BGA pitch, 0201, X-ray for BGA/QFN/LGA]:**
  - AFE4900 (DSBGA 0.4), AD5940 (WLCSP 0.4) and TPS61240 (YFF WCSP 0.4) are assemblable on Standard PCBA with ENIG.
  - AS7341 (OLGA-8 3.1 × 2), MLX90632 (SFN 3 × 3), LSM6DSV (LGA-14) and BME280 (LGA-8) are assemblable, and X-ray inspection is automatic.
  - No 0201 parts are present [checked BOM].
- **D2. HDI:** not required. No blind/buried or microvias are needed if POFV through-vias are used. That keeps the design at 4-layer through-hole-via cost plus the POFV option [inferred].
- **D3. Cheapest full-function option:**
  - Keep the AD5940 WLCSP: via-in-pad is already needed for the AFE4900, so AD5941 LFCSP-48 saves nothing (and has 0 stock at LCSC C503573).
  - Compare 4-layer + paid POFV against 6-layer with free POFV at quote time (F6).
- **D4. Double-sided assembly is unavoidable** because the skin-side sensors are on the bottom. Put all fine-pitch BGAs on the top if possible, so the bottom reflow has fewer critical parts [inferred]. Cost: medium/high setup.
- **D5. Hand-assembly note:** even for a "hand-assembled" prototype, the AFE4900, AD5940 and TPS61240 need stencil + reflow, so JLC assembly of at least those is strongly advised [inferred].
- **D6. MSL:** SFH7072 is MSL4 and Nichia is MSL3 [checked datasheets]. JLC bakes parts per standard. For consigned parts, send them in dry packs.
- **D7. Cost flags (flag only; nothing is removed):**
  - The legacy ADS1292R respiration network duplicates the new AD5940 4-wire chest BioZ.
  - The AFE4900 2nd ECG duplicates the ADS1292R ECG.
  - BME280/680 adds value only for ambient compensation.
  - Several extended-library parts each add a JLC loading fee.
  - All are owner decisions; there is no reliability issue.
- **D8. Cheap-part reliability risks:**
  - The 6.3 V 0402 caps on VBUS/VBAT (A4).
  - A 2512 51k that is not pulse-rated if DPCR2512 is substituted (F5).
  - The unprotected battery pads (F1).

---

## E) Pin-1 footprint data for hand-drawn footprints

**E1. ams-OSRAM SFH 7072 [checked, SFH 7072 DS v1.2 p.2 pin table, p.19–20 package and recommended solder pad]:**
- Body 7.5 × 3.9 × 0.9 mm. 12 pads, each 0.9 mm (along the long axis) × 1.0 mm.
  - Pitch 1.2 mm along the long axis.
  - Pad centres: x = ±0.6, ±1.8, ±3.0 mm; y = ±1.25 mm (row pitch 2.5 mm).
- Solder resist: one common window of 7.8 × 4.2 mm. Stencil apertures about 0.8 mm wide.
- Numbering (datasheet bottom view): pins 1–6 run left→right on the top row and pins 12–7 on the bottom row.
  - In TOP view (component side, long axis horizontal, row with 1–6 at the top): pin 1 is the top-RIGHT pad and numbering runs counter-clockwise (1→6 right→left along the top, 7→12 left→right along the bottom).
  - The CAD agent must compare this against the existing footprint. The mirror error between bottom and top view is the classic mistake.
- Pin functions: 1 BPC, 2 BPA, 3 IPC, 4 IA, 5 G1A, 6 G1C, 7 RA, 8 RC, 9 IC, 10 IPA, 11 G2A, 12 G2C [checked p.2].
- The package has no dedicated pin-1 mark [inferred]. Add a silk dot outside the pad 1 corner. MSL4.

**E2. Analog Devices/Maxim MAX17048G+T10, 8-TDFN-EP 2 × 2 [checked, 21-0168 rev M, variant T822-3]:**
- D = E = 2.0 mm, A 0.75 max, e 0.50 mm, b 0.25 ± 0.05, L 0.30 (0.20–0.40). Exposed pad D2 0.80 × E2 1.20 mm; k ≥ 0.25. Leads on two opposite edges.
- Top view: pin 1 is bottom-left (index area). Pins 1–4 run left→right on the bottom row and 5–8 right→left on the top row.
- The EP has a C0.25 chamfer on the pin-1 corner. The package top has a pin-1 dot.
- Land pattern [inferred from IPC nominal; matches KiCad `TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm`]: pads 0.30 × 0.65 mm at 0.5 mm pitch, EP land 0.8 × 1.2 mm with 2–4 paste windows (about 50 %).
- Pins: 1 CTG, 2 CELL, 3 VDD, 4 GND, 5 ALRT, 6 QSTRT, 7 SCL, 8 SDA, EP GND [checked MAX17048 DS Rev 7].

**E3. Winbond W25Q512JVEIQ, WSON-8 8 × 6 mm [checked, W25Q512JV DS Rev G §9.1 p.88]:**
- D 8.00, E 6.00, e 1.27, b 0.40 (0.35–0.48), L 0.50 mm. EP D2 3.40 × E2 4.30 mm.
- Four pads on each 6 mm edge.
- Top view: pin 1 top-left (package indent / dot), numbering counter-clockwise. The EP has a C0.4 chamfer as pin-1 ID.
- Pins: 1 /CS, 2 DO (IO1), 3 /WP (IO2), 4 GND, 5 DI (IO0), 6 CLK, 7 /HOLD (IO3), 8 VCC.
- The EP is not internally connected; tie it to GND with no exposed vias.
- "IQ" has QE = 1 fixed, so /WP and /HOLD act as IO2/IO3. Pulling them to 3V3 is fine for SPI use [checked DS ordering info].
- Footprint: KiCad `Package_SON:WSON-8-1EP_8x6mm_P1.27mm_EP3.4x4.3mm` [inferred to exist].
- W25Q256JVEIQ uses the same footprint.

**E4. Nichia NF2W757G-F1 (3.0 × 3.0 × 0.65 mm) [inferred; the text extraction of the drawing was garbled]:**
- Ratings [checked]: IF abs max 100 mA, IFP 130 mA; VF 2.4–3.3 V at 65 mA (rank dependent); MSL3.
- Cathode identification [checked text]: "the side with the larger distance a > b is the cathode". The cathode is also identified by the asymmetric electrode on the bottom.
- Recommended pads extracted as: anode pad ≈0.6 × 2.3, cathode pad ≈1.45 × 2.3, gap ≈0.95, overall 3.15 mm. Electrodes 1.42 / 0.48 / 2.2 / 2.6.
- The CAD agent MUST verify these against the drawing in the Nichia spec STS-DA7-7098: https://led-ld.nichia.co.jp/api/data/spec/led/NF2W757GT-F1-E(5056)Rfa00%20Rfc00.pdf
- Put a "K" or bar silk mark on the cathode side.
- Firmware note: the AS7341 LED_DRIVE can reach 258 mA, but the LED is rated 100 mA. The anode is on 3V3 with VF up to 3.3 V, so there is almost no headroom. Firmware must cap LED_DRIVE at ≤40 mA [checked limits, inferred setting].

---

## F) Decisions for Riyansh (only real choices)

1. **Charge current vs cell capacity.**
   - R3 = 3.0k gives only 100 mA [checked]. With a system load of about 50–95 mA sharing the charger, charging is very slow. Charging may also not terminate (ITERM is 10 mA), so the 10 h safety timer can fault [checked BQ25170 DS; load figure inferred].
   - Options: R3 = 1.5k (200 mA) or 1.0k (300 mA), keeping ≤1C for the cell.
   - Also confirm the cell has a 10k NTC and a protection PCM. If there is no cell NTC, fit R61 (currently DNP), otherwise TS floats and charging is blocked [inferred].
2. **MLX90632 variant.** -100 (1.8 V I2C, 0 stock) → consign it; or -000 (3.3 V I2C, C5240460 in stock) moved onto the 3V3 I2C bus.
3. **Flash.** Consign the W25Q512JVEIQ; or use W25Q256JVEIQ (C97522, 32 MB, same footprint); or W25Q512JVFIQ SOIC-16 (C2962011, bigger footprint).
4. **IMU.** Consign the LSM6DSV80X; or LSM6DSV16XTR (C5267406, same package [inferred pin-compatible], 16 g range instead of 80 g).
5. **Defib resistors.** Consign the DPCR2512-51KJT18 (the pulse-rated part). No LCSC substitute is verified as defib-pulse-rated; Ever Ohms CRH2512 51k (C175367) is high-voltage but not verified for defib pulses.
6. **PCB layer count.** 4-layer + paid POFV vs 6-layer with free POFV (and a better ground). Pick whichever is cheaper in the JLC quote; the routing rules are unchanged.

---

## G) Ready-to-paste implementation prompt for the CAD agent

```
You are implementing the VitalQ hw_v1 pre-fab changes in KiCad (project at commit 87e94a46). Do ALL of the following in one run. Do not remove any part except where a footprint/symbol is replaced as stated. Do not change MPNs except as stated. Where a step says "verify", actually check and report the result.

DEFAULT DECISIONS (use unless the prompt header says otherwise): keep MLX90632-100 on 1.8V bus, keep W25Q512JVEIQ, keep LSM6DSV80X, keep DPCR2512 (LCSC blank = consigned), 4-layer, R3 = 1.5k (200 mA charge), R61 stays DNP.

A. MUST-FIX
A1 MAX17048 (MAX17048G+T10, TDFN-8 2x2): renumber symbol pins 1 CTG,2 CELL,3 VDD,4 GND,5 ALRT,6 QSTRT,7 SCL,8 SDA,9 EP=GND; footprint Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm; keep existing nets (CTG,QSTRT->GND; CELL,VDD->VBAT; ALRT->GPIO25 net with pull-up; SCL/SDA 3V3 I2C). Verify pin 1 bottom-left top view, EP chamfer at pin 1.
A2 ADS1292R pin 25 (GPIO2) and pin 26 (GPIO1): each to GND through new 10k 0402 (LCSC C25744).
A3 TMP117: local footprint copy with NO paste on the exposed pad (or delete EP pad).
A4 C1 (VBUS/BQ25170 IN) -> 4.7uF 25V X5R 0603 CL10A475KA8NQNC (C69335). Any other cap on VBUS net rated <16V -> same.
A5 Sheet 1: move L1 off U4, redraw wires; verify via netlist SW_L1 = {U3.9, L1.1}, SW_L2 = {U3.7, L1.2}, neither touches +3V3/U4.
A6 Move R43,R45,C56,C57 and AFE_INP/INM labels (sheet 4) and D3,C66,D4,D5 (sheet 6) inside the drawing frame; re-check no label dangles.
A7 Rule areas: no copper on L2/L3/L4 and no vias under J5, J6, J7 pads and all electrode-side copper up to each 51k 2512 resistor (+1.0 mm margin). Netclass HV_ELECTRODE (pad->51k): 0.25 mm track, 1.5 mm clearance (target 2.5 mm to unrelated nets).
A8 BOM LCSC field: fill from the list below; CSD13380F3 -> CSD13380F3T C2871092; leave LCSC blank and set a "Consign" field for DPCR2512-51KJT18, NF2W757G-F1, MLX90632SLD-DCB-100-SP, LSM6DSV80XTR, W25Q512JVEIQ. Replace 10M and 301k 0402 with in-stock 1% equivalents.
A9 Create a JLC assembly panel 2x2 (or 2x1 if >=70x70 mm achieved) with 5 mm rails, mouse bites (not on antenna edge or electrode pad edges), 2.0 mm NPTH tooling holes and 1.0 mm fiducials 3.85 mm from rail edges (use KiKit if available; else document "Panel by JLCPCB 2x2" in the order notes).
A10 AFE4900 and AD5940: via-in-pad (filled+capped, JLC POFV) only on inner-ring balls needing escape; via 0.2/0.30 mm (or 0.15/0.25 if JLC capability page allows POFV there); via-in-pad BGA pads 0.25 mm NSMD, others 0.23 mm; surface finish ENIG. Document "POFV + ENIG" in fab notes.

B. ADDITIONS
B1 USBLC6-2SC6 (C7519) at J1 on D+, D-, VBUS (GND to plane with 2 vias).
B2 C7 -> 22uF 10V 0603 CL10A226MQ8NRNC (C59461) at ESP32 3V3 pin, keep 100nF next to it.
B3 Reset button TS-1088-AR02016 (C720477) EN->GND. B4 Boot button same part IO0->GND. Place at an accessible edge (top side).
B5 GPIO17 -> 1k (C11702) -> green LED KT-0603G (C12624) -> GND.
B6 10k pull-up (C25744) CS_ADS1292 (GPIO4) to 3V3. B7 100k pull-up (C25741) ADS1292_DRDY (GPIO34) to 3V3.
B8 0R 0402 (C17168) series on each device MISO near the device.
B9 0R links for current measurement: VBAT->system (TPS63802+TPS61240 inputs) 0603 0R; +3V3->ESP32 0603 0R; +3V3->analog front ends 0402 0R; TX_5V out 0402 0R; +1V8 out 0402 0R. Pick basic LCSC 0603 0R (verify number).
B10 1.0 mm test pads (no paste), labelled, top side where possible: VBUS, VBAT, +3V3, +1V8, TX_5V, VDD_CP2102, GND x2, ESP_EN, ESP_IO0, ESP_TX, ESP_RX, SDA/SCL (3V3 and 1V8), SCK, MOSI, MISO, all CS, ADS1292_DRDY, AFE_ADC_RDY, EXP_INT, CHG_STAT.
B11 Tag-Connect TC2030-NL footprint: 3V3, GND, TX, RX, EN, IO0.
B12 3 fiducials per side (1.0 mm Cu, 2.0 mm mask opening, asymmetric).
B13 2x NPTH 2.2 mm mounting holes at diagonal corners (1.6 mm if space is short), 1 mm Cu keep-out.
B14 Silk: "VitalQ hw_v1 rev A 2026-09"; "JLCJLCJLCJLC" on bottom in a clear area; pin-1 dots on all ICs/LEDs/SFH7072; J2 "BAT+ / BAT- / NTC" with large "+"; labels on every J5/J6/J7/J3 pad; asymmetric J2 pads (bigger +).
B15 (optional) 1.0 mm NPTH isolation slots partly around TMP117 and MLX90632 on bottom, leaving trace bridges.

C. PLACEMENT AND ROUTING RULES
- Stackup JLC04161H-7628 1.6 mm: L1 signal, L2 solid GND (no splits), L3 power pours + slow signals, L4 signal + GND pour. GND stitching vias <=3 mm at edges, RF and switcher areas.
- Zones: analog (AFE4900+SFH7072, ADS1292R, AD5940 near J5/J6/J7), digital (ESP32, flash, CP2102N, TCA6408A), power (U2, U3/L1, U15/L2). No digital return under the front ends. Switcher hot loops tight on L1 with multiple GND vias.
- ESP32 antenna at the board edge; no copper on any layer, no traces/parts under or near the antenna (Espressif keep-out); no electrodes/battery near it.
- Thermal: U2, U3/L1, U15/L2 and ESP32 far from TMP117, MLX90632, U12; no power pours under TMP117/MLX; small GND island under TMP117 with a narrow neck.
- Skin side (bottom): SFH7072, AS7341+D10, MLX90632, TMP117; keep windows clear of silk; D10 >= 5 mm from AS7341 aperture.
- Electrodes: HV_ELECTRODE as A7; after the 51k use ELECTRODE_LV (0.15 mm, short, pairs symmetric); TPD1E10B06 right after each 51k, shortest GND via.
- PPG PD -> AFE4900 INP/INM: shortest, pair, GND guard both sides stitched to L2, no digital nets on adjacent layers.
- USB D+/D-: pair, ~90 ohm diff (approx 0.20/0.15 mm on L1 over L2; verify with JLC calculator), length match <1 mm, no stubs; no impedance-control order option needed (FS 12 Mb/s).
- SPI SCK away from analog inputs; I2C 1V8 segment short.
- W25Q EP: no exposed vias in the pad.
- Vias: standard 0.3/0.5-0.6 mm; small/POFV only for BGA fan-out.
- Decoupling caps within 1 mm of pins, via to GND at the pad.
- Check the hand-drawn footprints against the pin-1 data in section E of /workspace/vitalq-hw/VitalQ_prefab_review.md (SFH7072 top-view pin 1 top-right counter-clockwise; MAX17048 TDFN; W25Q WSON 8x6 pin 1 top-left; Nichia cathode on larger-gap side per spec STS-DA7-7098). Fix any mismatch and report.

D. FINISH
1. Fully route the board (no unconnected items). Fill all zones.
2. Set DRC to JLC 4-layer rules: track/space 0.127/0.127 (0.09/0.09 only in BGA fan-out rule area), copper-edge 0.3, annular 0.075 (0.05 for POFV), hole-hole 0.5, mask bridge 0.1, silk-pad 0.15, silk width 0.15/text 1.0 mm, plus the HV_ELECTRODE netclass.
3. Run: kicad-cli sch erc --severity-all --exit-code-violations; kicad-cli pcb drc --severity-all --schematic-parity --refill-zones --exit-code-violations. Reach 0 errors and 0 unconnected; list any accepted warnings with reasons.
4. Export for JLCPCB: Gerbers (all copper, mask, paste, silk, Edge.Cuts, protel extensions) + Excellon drill (PTH/NPTH separate, mm) zipped; BOM CSV columns "Comment,Designator,Footprint,LCSC Part #" (DNP excluded; consigned parts listed with blank LCSC); CPL CSV "Designator,Mid X,Mid Y,Layer,Rotation" (top and bottom, bottom not mirrored in rotation beyond JLC convention); apply JLC rotation corrections (use the JLC rotation database from Bouni kicad-jlcpcb-tools or check each IC/polarised part against the JLC preview), and list every correction applied.
5. Render top, bottom and iso PNGs of the routed board and panel; export the schematic PDF.
6. Report: changes made per item (A1..A10, B1..B15), ERC/DRC results, output file paths, and anything not done.
```
