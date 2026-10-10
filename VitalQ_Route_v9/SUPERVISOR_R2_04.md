# Supervisor instructions R2 #04 — U6 (AFE4900, B side, 0.4 mm WLP) escape plan
(from renders of candidates/R2-7 + copper query; use after the U22 batch)

## Grid (B side balls; x by column, y by row)
cols: 1=26.5, 2=26.1, 3=25.7, 4=25.3, 5=24.9 ; rows: A=49.8, B=50.2, C=50.6, D=51.0, E=51.4, F=51.8
Opens on U6: A1 AFE_INM, A2 PD_INP, A3 PD_INM, B2 PD2_INP, B3 PD2_INM, C4 +3V3_ANA, D2 AFE_BG, D4 TX1, E2 MISO_AFE, E3 CS_AFE4900, E4 TX3, E5 TX2, F2 SPI_MOSI, F3 SPI_SCK.
Existing GND via-in-pads (keep): B4 (25.3,50.2), D1 (26.5,51.0), D5 (24.9,51.0).

## Layer situation under U6 (rendered)
- In3 (Signal Layer 4): EMPTY under and around U6/U22. Use it first.
- F.Cu above U6 (x 24.4–27.0, y 49.4–51.9): EMPTY except the GND via tops. Usable as an escape layer (U6 is on B). TX_5V F diagonal (27.13,49.27)->(29.03,51.17) is the first obstacle east.
- In2: two foreign tracks run THROUGH the array and must be ripped inside x 24.0–27.0, y 49.0–52.2 (authorised, log uuids) and re-routed afterwards outside the array:
  - ESP_TX: (24.84,49.93)->(24.84,50.25)->(25.26,50.70)->(25.26,51.15)->(24.84,51.45) (sits on C4/D4/E4 sites).
  - AFE_INP: (26.97,49.71 via)->(26.95,50.0)->(26.95,51.2)->(26.36,51.4)->(26.36,54.6) (sits on the E1/F1 east channel and between col1/col2). Re-route AFE_INP from its B1 pad stub/via (26.97,49.71) on In2 EAST of x 27.3 or on F.
- B.Cu: ring of TX4 / TX_5V / +1V8 / +3V3_ANA / AFE_CLK / ADC_RDY / RESETZ / AFE_INP hugging the array — leave it; all new escapes go through via-in-pad to F/In2/In3.

## Via-in-pad set (0.25/0.15 POFV at ball centre)
A1, A2, A3, B2, B3, C4, D2, D4, E2, E3, E4, E5, F2, F3 (+ existing B4, D1, D5).
Free (no-via) cells that form the channels: A4, A5, B1, B5, C1, C2, C3, C5, D3, E1, F1, F4, F5.
Rule reminder: a track on a ball-centre line is 0.40 from the neighbouring via centres (legal, >= 0.32 needed); a track can NOT pass between two adjacent vias or diagonally past one (0.28).

## Escape topology (forced by the grid)
- Row A (A1, A2, A3): boundary — escape NORTH (y < 49.6) on any layer.
- E5, F2, F3: boundary — escape west (E5) / south (F2, F3).
- E4 TX3: E4 -> F4 (25.3,51.8) -> south.
- C4 +3V3_ANA: C4 -> C5 (24.9,50.6) -> west.
- E2 MISO_AFE: E2 -> E1 (26.5,51.4) -> east.
- B2 PD2_INP: B2 -> B1 (26.5,50.2) -> east.
- B3 PD2_INM, D2 AFE_BG, D4 TX1, E3 CS_AFE4900 all MUST use the C2 -> C1 -> east exit (B3 via C3; D4 and E3 via D3 -> C3; D2 directly via C2). That is 4 nets through one cell sequence, so they need 4 different layers: assign D2 AFE_BG = F.Cu (target C15.1 @ 28.776,50.042 is on F; cross the TX_5V F diagonal by dropping to In3 for 0.5 mm or go around its north end at (27.13,49.27)), B3 PD2_INM = In3, D4 TX1 = In2 (after the ESP_TX/AFE_INP rip), E3 CS_AFE4900 = In4 (Power Layer 5: a short local track is allowed; the +3V3 pour refills around it and must stay connected — check) OR move CS_AFE4900 onto F and AFE_BG onto In4. If you find a 3-layer solution, better.
- After exiting east (x >= 26.9), PD2_INP / PD2_INM / TX1 must loop to U16 (B side, locked, 13.4,56.0; pads PD_INP 10.4,57.25 / PD_INM 11.6,57.25 / PD2_INP 12.8,57.25 / TX1 16.4,57.25 / TX3 15.2,54.75 / PD2_INM 12.8,54.75 / TX2 10.4,54.75): go south on In3 east of the array (x ~27.0–27.3, In3 is empty there), then west along y ~52.5–53.5 on In3 under U6's south side, then to U16 with vias next to its pads (outside any hv_inner area). PD_INP / PD_INM (A2/A3) go north 0.3–0.8 mm, then west on In3 along y ~49.0/49.4, then south-west to U16.
- A1 AFE_INM joins its existing F.Cu track (8.39 mm piece) — via A1 to F, then F.
- MISO_AFE (E2) target R90.1 (44.897,49.062, F) — east on In3/In2, via near R90.
- SPI_MOSI / SPI_SCK (F2/F3): south via F row, then to the SPI bus (SPI_SCK must also reach U22 A2 @26.5,46.95).

Do this as one or two batches (row-A/E5/F-row/E4/C4/E2/B2 first; then the 4-layer C-channel group). Print the 3-line summary after each.
