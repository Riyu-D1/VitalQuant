# Task: replace U22 (MAX86178) with MAX86141ENP+ in the VitalQ board

Work on macOS. kicad-cli: /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli (KiCad 10).
Source: ~/Downloads/VitalQ_Quilter_v4 (vitalq_v2.kicad_sch/.kicad_pcb/.kicad_pro + bypass_capacitors_fixed.csv). DO NOT modify or delete v4 until v5 fully verifies.
Output: NEW folder ~/Downloads/VitalQ_Quilter_v5 (copy of v4, then edit). Keep file names vitalq_v2.*, project name "vitalq_v2", board footprint (path "/<symbol uuid>"), (sheetname "/"), (sheetfile "vitalq_v2.kicad_sch"). Finish with `kicad-cli sch upgrade --force` and `pcb upgrade --force`.

## Datasheet (already researched)
- Full public datasheet, 89 pages: https://www.farnell.com/datasheets/2580845.pdf (analog.com and the Wayback copy are truncated/blocked; don't use ~/Downloads/max86141.pdf, it is truncated — delete it).
- Package N201A2+1, outline 21-100134, 20-bump WLP 2.048 x 1.848 mm, 5 cols x 4 rows, 0.4 mm pitch. Land pattern per Maxim AN1891 (use ~0.22-0.25 mm round NSMD pads). Pads: col 1..5 at x = -0.8..+0.8, row A..D at y = -0.6..+0.6 (bumps-down view, A1 top-left).
- Bump map (MAX86141, bumps facing down):
  - A1 VLED, A2 SCLK, A3 SDO, A4 SDI, A5 CSB
  - B1 LED3_DRV, B2 INT (open-drain, active low), B3 GPIO1, B4 GPIO2, B5 VREF
  - C1 LED2_DRV, C2 VDD_DIG, C3 GND_DIG, C4 GND_ANA, C5 PD_GND (photodiode anode)
  - D1 LED1_DRV, D2 VDD_ANA, D3 PGND, D4 PD2_IN, D5 PD1_IN (photodiode cathode)
- Supplies: VDD_ANA/VDD_DIG 1.7-2.0 V (1.8 typ). VLED 3.1-5.5 V. LED_DRVx abs max = VLED+0.3 V. "Connect LED cathode to LED_DRVx and its anode to the VLED supply."
- Decoupling from the pin table and app circuit: VDD_ANA 0.1 uF as close as possible + 10 uF; VDD_DIG bypass to GND_DIG (use 0.1 uF); VLED 10 uF to PGND; VREF 1 uF to GND_ANA.
- Logic: SCLK/SDI/CSB/INT are 6 V tolerant, VIH 1.4 V, so 3.3 V drive from the ESP32 is fine. SDO VOH = VDD-0.4 (about 1.4 V), which is below the ESP32-S3 VIH (about 2.5 V), so SDO NEEDS a translator. INT is open-drain and already has an R113 pull-up to +3V3, so no translator is needed on INT.

## Existing nets (from the v4 netlist)
- LED1_K/LED2_K/LED3_K: J10/J12 LED cathodes. LED_AN (anode) = TX_5V via R114.
- PD_K (cathode) and PD_A (anode) on J10/J12.
- SPI_SCK, SPI_MOSI, CS_MAX86178 (R117 pull-up to +3V3, ESP GPIO38), MAX86178_INT (R113 pull-up, GPIO39), MISO_MX (goes through R106 to shared SPI_MISO; other devices share MISO, so the MAX86141 MISO must be tri-stated when CS is high).
- MX_VREF already has C83 1 uF to GND.
- +1V8 (from +1V8_LDO via R97; U9 and U10 small loads), TX_5V (5 V, 500 mA budget), VBAT, +3V3.

## Required wiring for U22 (keep refdes U22, keep the SAME symbol uuid 29d3af96-bc5d-4a32-8c0b-3b3f396edf9a and footprint uuid, locked, B.Cu at (26.1, 45.05) rot 180; note that on B.Cu the board stores pad y mirrored, e.g. a lib (x,y) pad appears as (x,-y))
| Pin | Net |
|---|---|
| A1 VLED | TX_5V (the LED anodes are on TX_5V; VBAT would violate LED_DRV <= VLED+0.3 V) |
| A2 SCLK | SPI_SCK |
| A3 SDO | new net MX_SDO_1V8 -> translator A1 |
| A4 SDI | SPI_MOSI |
| A5 CSB | CS_MAX86178 |
| B1 LED3_DRV | LED3_K |
| B2 INT | MAX86178_INT |
| B3/B4 GPIO1/2 | no connect |
| B5 VREF | MX_VREF (existing C83 1 uF) |
| C1 LED2_DRV | LED2_K |
| C2 VDD_DIG | +1V8 |
| C3 GND_DIG, C4 GND_ANA, D3 PGND | GND |
| C5 PD_GND | PD_A |
| D1 LED1_DRV | LED1_K |
| D2 VDD_ANA | +1V8 |
| D4 PD2_IN | no connect |
| D5 PD1_IN | PD_K |
Leave MX_ECG_INP / MX_ECG_INM unconnected at U22. Do NOT remove D29/D30/R118/R119; report them as now unused. C29 (+3V3_ANA, 100 nF) no longer bypasses a U22 pin: keep the part and remove its U22 row from the CSV.

## New parts (all unlocked, placed off-board for Quilter; 0402 unless noted)
- U26 SN74AXC2T245RSWR (LCSC C1882550), UQFN-10 1.4x1.8. Datasheet https://www.ti.com/lit/ds/symlink/sn74axc2t245.pdf. KiCad footprint Package_DFN_QFN:Texas_RSW0010A_UQFN-10_1.4x1.8mm_P0.4mm. Pins: 1 DIR2=GND, 2 OE (active-low, referenced to VCCA, input up to 3.6 V OK) = CS_MAX86178, 3 GND, 4 B2=GND (unused input tied), 5 B1=MISO_MX, 6 VCCB=+3V3, 7 VCCA=+1V8, 8 A1=MX_SDO_1V8, 9 A2=NC, 10 DIR1=+1V8 (A->B). OE high = all Hi-Z, which keeps the shared MISO free.
- C86 100nF 0402 (C1525) on +1V8/GND at U22 D2
- C87 10uF 0402 6.3V (C15525) on +1V8/GND at U22 D2
- C88 100nF 0402 (C1525) on +1V8/GND at U22 C2
- C89 10uF 0603 10V (C19702) on TX_5V/GND at U22 A1 (VLED)
- C90 100nF 0402 (C1525) on +1V8/GND at U26 pin 7
- C91 100nF 0402 (C1525) on +3V3/GND at U26 pin 6
(Check that C86-C91 and U26 are unused refdes. The netlist has up to C85 and U25.)

## Schematic implementation notes
Single flat sheet, connections via global labels + wires. The U22 instance is at (1174.75, 963.93), lib_id vitalq:MAX86178, with an embedded lib symbol. Approach: add new embedded lib symbols vitalq:MAX86141 and vitalq:SN74AXC2T245, point U22 at vitalq:MAX86141 (value MAX86141ENP+, footprint vitalq:MAX86141_WLP20, LCSC C5328762), delete the wires/labels attached to old U22 pins, and put global labels (GND, +1V8, TX_5V, etc. connect fine through global labels in this design) or no_connects at the new pin endpoints. New caps use lib_id vitalq:C (pins at (0,3.81) and (0,-3.81) relative), footprint Capacitor_SMD:C_0402_1005Metric. Instances block: (project "vitalq_v2" (path "/cbbdfd44-951c-4f0b-9fac-a13ab1eca057" (reference ..) (unit 1))). Free sheet space: x > 1400 or y > 1050 (A0 sheet, max used about 2010 x 1125).

## PCB
- Create the lib footprint ~/VitalQuant-place/hardware/pcb/lib/vitalq.pretty/MAX86141_WLP20.kicad_mod (F.Cu, 20 pads, courtyard about 2.3x2.1, pin-1 mark) and put it on U22 in the board (B.Cu flipped).
- Add footprints for U26 and C86-C91 with correct (path "/<new symbol uuid>"), sheetname/sheetfile, pad nets, off-board, unlocked.

## Verify (report all)
1. Footprint count = 292 + 7 = 299; U22 has 20 pads.
2. `kicad-cli pcb drc --schematic-parity`: no missing/extra footprints, no net conflicts except the known H1/H2 "no pad for pin 1" and the existing exclude-from-BOM flags on J8/TP1-28.
3. Every U22/U26/new-cap pad net == schematic netlist.
4. Netlist diff vs v4 (`kicad-cli sch export netlist`): only U22, U26, C86-C91 and the new net MX_SDO_1V8 differ. MX_ECG_INP/INM lose their U22 node. No other part's pins change net.
5. Update bypass_capacitors_fixed.csv (columns capacitor,bypassed_component,bypassed_pin,capacitance in nF): C83 -> U22,B5,1000; drop C29's U22 row; add C86 U22 D2 100, C87 U22 D2 10000, C88 U22 C2 100, C89 U22 A1 10000, C90 U26 7 100, C91 U26 6 100.
6. Only after all checks pass, delete ~/Downloads/VitalQ_Quilter_v4.
7. Apply the same change to the repo ~/VitalQuant-place (hardware/pcb/lib/vitalq.pretty, hardware/pcb/vitalq_flat.kicad_sch, hardware/pcb/vitalq_quilter_input.kicad_pcb, and update the docs: PINMAP.md, HW_V2_SPEC.md, FOOTPRINT_AUDIT.md). Commit locally. DO NOT push.

## Report
Pin-to-net table, new parts with values/LCSC numbers, verification output, the now-unused parts (D29, D30, R118, R119, C29's U22 role), and anything uncertain. Quilter advice: 0.4 mm-pitch WLP 5x4. Outer row/column balls escape on the surface; the inner balls (B2-B4, C2-C4) need a via, ideally a 0.15 mm drill / 0.25-0.3 mm pad via-in-pad, or route them out between pads with 0.1 mm traces if the rules allow. Mark U22 for BGA fanout in Quilter.
