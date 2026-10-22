# Connectivity sweep — shipped fix_v19 gerbers

Method: parse the 8 copper gerbers with their X2 object attributes (`%TO.P` ref.pad, `%TO.N` net per object), build exact copper geometry (flashes, track capsules, zone regions), union everything that physically touches on a layer, and union plated barrels (flashes at the same drill position on different layers). Touch tolerance 0.002 mm.

Expected nets cross-checked vs `reports/01_netlist_pinmap.csv`.

## Verdict summary

| class | count |
|---|---|
| OPEN (isolated or dead-end pad on live net) | 0 |
| SHORT (pad bridges foreign net / cross-net pad merge) | 0 |
| BENIGN same-net merges | 8 |
| isolated NC/unconnected pads (benign) | 66 |
| connected clean | 896 |
| advisory: pads whose component contains unattributed copper | 217 |
| missing pad objects in gerber | 0 |
| net attr vs pinmap mismatch | 0 |

## Known-suspect validation

- **U20.4** (+3V3): `CONNECTED`. pad directly touches {region(+3V3)x1}; comp 1429 objects; 
  - claimed open / floating pin. Shipped gerber: pad is fully enclosed by a +3V3 pour island that also carries the +3V3 via-in-pad at U20.5 — pin is connected, claim does not reproduce in v19 copper.
- **U19.12** (I2C_SCL): `CONNECTED`. pad directly touches {track(I2C_SCL)x1}; comp 458 objects; 
  - claimed I2C_SCL stub ending ~0.37 mm short. Shipped gerber: an I2C_SCL track reaches the pad (edge overlap); no merge with U19.11/EXP_INT.
- **U11.2** (GND): `CONNECTED`. pad directly touches {track(GND)x1}; comp 4239 objects; 
  - claimed GND stub ending ~0.06 mm short of the resized pad. Shipped gerber: a GND track overlaps the pad — connected to the main GND net.

## OPEN — isolated pads on live nets


## SHORT — pads bridged to foreign nets / cross-net pad merges


## BENIGN — same-net pad merges (direct pad-shape contact)

- `J1.A1` (GND) pad shape touches `J1.B12`(GND)
- `J1.A12` (GND) pad shape touches `J1.B1`(GND)
- `J1.A4` (VBUS) pad shape touches `J1.B9`(VBUS)
- `J1.A9` (VBUS) pad shape touches `J1.B4`(VBUS)
- `J1.B1` (GND) pad shape touches `J1.A12`(GND)
- `J1.B12` (GND) pad shape touches `J1.A1`(GND)
- `J1.B4` (VBUS) pad shape touches `J1.A9`(VBUS)
- `J1.B9` (VBUS) pad shape touches `J1.A4`(VBUS)

## BENIGN/INFO — isolated unconnected pads

- `FID1.1` (unconnected-(FID1-FID-Pad1))
- `FID2.1` (unconnected-(FID2-FID-Pad1))
- `FID3.1` (unconnected-(FID3-FID-Pad1))
- `FID4.1` (unconnected-(FID4-FID-Pad1))
- `FID5.1` (unconnected-(FID5-FID-Pad1))
- `FID6.1` (unconnected-(FID6-FID-Pad1))
- `J1.A8` (unconnected-(J1-SBU1-PadA8))
- `J1.B8` (unconnected-(J1-SBU2-PadB8))
- `J12.14` (unconnected-(J12-Pin_14-Pad14))
- `U1.26` (unconnected-(U1-IO26-Pad26))
- `U1.27` (unconnected-(U1-IO47-Pad27))
- `U1.28` (unconnected-(U1-IO33-Pad28))
- `U1.29` (unconnected-(U1-IO34-Pad29))
- `U1.30` (unconnected-(U1-IO48-Pad30))
- `U1.31` (unconnected-(U1-IO35-Pad31))
- `U1.32` (unconnected-(U1-IO36-Pad32))
- `U1.33` (unconnected-(U1-IO37-Pad33))
- `U1.41` (unconnected-(U1-IO45-Pad41))
- `U1.44` (unconnected-(U1-IO46-Pad44))
- `U10.6` (unconnected-(U10-GPIO-Pad6))
- `U10.7` (unconnected-(U10-INT-Pad7))
- `U14.10` (unconnected-(U14-Pad10))
- `U14.11` (unconnected-(U14-Pad11))
- `U14.9` (unconnected-(U14-Pad9))
- `U20.3` (unconnected-(U20-ALERT-Pad3))
- `U22.B3` (unconnected-(U22-GPIO1-PadB3))
- `U22.B4` (unconnected-(U22-GPIO2-PadB4))
- `U22.D4` (unconnected-(U22-PD2_IN-PadD4))
- `U24.1` (unconnected-(U24-CLKOUT-Pad1))
- `U26.9` (unconnected-(U26-A2-Pad9))
- `U3.5` (unconnected-(U3-PG-Pad5))
- `U4.4` (unconnected-(U4-NC-Pad4))
- `U5.10` (unconnected-(U5-NC-Pad10))
- `U5.11` (unconnected-(U5-~{SUSPEND}-Pad11))
- `U5.12` (unconnected-(U5-SUSPEND-Pad12))
- `U5.13` (unconnected-(U5-CHREN-Pad13))
- `U5.14` (unconnected-(U5-CHR1-Pad14))
- `U5.15` (unconnected-(U5-CHR0-Pad15))
- `U5.16` (unconnected-(U5-~{WAKEUP}{slash}GPIO.3-Pad16))
- `U5.17` (unconnected-(U5-RS485{slash}GPIO.2-Pad17))
- `U5.18` (unconnected-(U5-~{RXT}{slash}GPIO.1-Pad18))
- `U5.19` (unconnected-(U5-~{TXT}{slash}GPIO.0-Pad19))
- `U5.20` (unconnected-(U5-GPIO.6-Pad20))
- `U5.21` (unconnected-(U5-GPIO.5-Pad21))
- `U5.22` (unconnected-(U5-GPIO.4-Pad22))
- `U6.A5` (unconnected-(U6-INM3-PadA5))
- `U6.B5` (unconnected-(U6-INP3-PadB5))
- `U6.C2` (unconnected-(U6-RLD_OUT-PadC2))
- `U6.C3` (unconnected-(U6-DNC-PadC3))
- `U6.D3` (unconnected-(U6-PROG_OUT1-PadD3))
- `U7.B2` (unconnected-(U7-AFE1-PadB2))
- `U7.C2` (unconnected-(U7-AFE2-PadC2))
- `U7.C3` (unconnected-(U7-DNC_1-PadC3))
- `U7.C6` (unconnected-(U7-RC0_2-PadC6))
- `U7.D3` (unconnected-(U7-DNC_2-PadD3))
- `U7.D4` (unconnected-(U7-DNC_3-PadD4))
- `U7.D6` (unconnected-(U7-GPIO1-PadD6))
- `U7.E2` (unconnected-(U7-GPIO3-PadE2))
- `U7.F4` (unconnected-(U7-GPIO6-PadF4))
- `U7.F6` (unconnected-(U7-GPIO5-PadF6))
- `U7.G1` (unconnected-(U7-DNC_4-PadG1))
- `U7.G4` (unconnected-(U7-GPIO7-PadG4))
- `U7.G5` (unconnected-(U7-XTALI-PadG5))
- `U7.G6` (unconnected-(U7-XTALO-PadG6))
- `U7.G7` (unconnected-(U7-GPIO4-PadG7))
- `U7.G8` (unconnected-(U7-DNC_5-PadG8))

## Advisory — unattributed copper inside net components

2 copper objects carry no `%TO.N` attribute. They are electrically absorbed into an attributed net component where they overlap, but have no declared net of their own (all affected pads remain CONNECTED):

- `F.Cu` flash at (28.936, 36.154), 0.35x0.35 mm — NPTH mounting-hole annular ring
- `L7` region at (2.150, 63.372), 0.50x1.65 mm

Affected pads (217): `C1.2`, `C10.2`, `C11.2`, `C13.2`, `C14.2`, `C15.2`, `C16.2`, `C18.2`, `C19.2`, `C2.2`, `C20.2`, `C21.2`, `C22.2`, `C23.2`, `C24.2`, `C25.2`, `C26.2`, `C28.2`, `C29.2`, `C3.2`, `C30.2`, `C32.2`, `C33.2`, `C36.2`, `C37.2`, `C38.2`, `C39.2`, `C4.2`, `C40.2`, `C41.2`, `C42.2`, `C43.2`, `C44.2`, `C45.2`, `C46.2`, `C47.2`, `C48.2`, `C49.2`, `C52.2`, `C53.2`, `C58.2`, `C59.2`, `C6.2`, `C60.2`, `C61.2`, `C62.2`, `C63.2`, `C64.2`, `C66.2`, `C7.2`, `C76.2`, `C77.2`, `C78.2`, `C79.2`, `C8.2`, `C80.2`, `C81.2`, `C82.2`, `C83.2`, `C84.2`, `C85.2`, `C86.2`, `C87.2`, `C88.2`, `C89.2`, `C9.2`, `C90.2`, `C91.2`, `D1.2`, `D2.2`, `D21.2`, `D22.2`, `D23.2`, `D24.2`, `D25.2`, `D26.2`, `D27.2`, `D28.2`, `D29.2`, `D3.2`, `D30.2`, `D4.2`, `D5.2`, `D6.2`, `D7.2`, `D8.2`, `D9.2`, `J1.A1`, `J1.A12`, `J1.B1`, `J1.B12`, `J1.SH`, `J12.4`, `J2.2`, `J8.2`, `J9.4`, `Q1.2`, `Q2.2`, `Q4.2`, `R1.2`, `R10.2`, `R100.2`, `R101.2`, `R108.2`, `R11.2`, `R110.2`, `R115.2`, `R123.1`, `R14.2`, `R16.2`, `R2.2`, `R24.2`, `R25.2`, `R27.2`, `R29.2`, `R3.2`, `R48.2`, `R51.2`, `R59.2`, `R61.2`, `R64.2`, `R66.2`, `R75.2`, `R8.2`, `R9.2`, `SW1.2`, `SW2.2`, `TP7.1`, `TP8.1`, `U1.1`, `U1.2`, `U1.42`, `U1.43`, `U1.46`, `U1.47`, `U1.48`, `U1.49`, `U1.50`, `U1.51`, `U1.52`, `U1.53`, `U1.54`, `U1.55`, `U1.56`, `U1.57`, `U1.58`, `U1.59`, `U1.60`, `U1.61`, `U1.62`, `U1.63`, `U1.64`, `U1.65`, `U10.3`, `U10.5`, `U11.2`, `U11.4`, `U11.7`, `U12.3`, `U12.5`, `U12.6`, `U13.1`, `U13.5`, `U13.7`, `U14.1`, `U14.2`, `U14.3`, `U14.6`, `U14.7`, `U15.A2`, `U17.1`, `U17.4`, `U17.6`, `U17.9`, `U18.4`, `U18.9`, `U19.16`, `U19.6`, `U2.4`, `U2.9`, `U20.2`, `U20.7`, `U21.2`, `U22.C3`, `U22.C4`, `U22.D3`, `U23.4`, `U24.5`, `U24.8`, `U25.4`, `U25.5`, `U26.1`, `U26.3`, `U26.4`, `U3.11`, `U3.2`, `U3.3`, `U3.8`, `U4.2`, `U5.29`, `U5.3`, `U6.B4`, `U6.D1`, `U6.D5`, `U7.C4`, `U7.D5`, `U7.E3`, `U7.E4`, `U7.E5`, `U7.E6`, `U8.10`, `U8.13`, `U8.16`, `U8.17`, `U8.24`, `U8.33`, `U9.1`

## Missing gerber pad objects


## Net attribute vs pinmap mismatches


## Extra gerber pads not in pinmap


## Conclusion

The shipped fix_v19 copper matches the pinmap netlist on all 970 pads: no opens on live nets, no cross-net shorts, no missing pad copper, and every Gerber `%TO.N` attribution agrees with `01_netlist_pinmap.csv`. The only pad-to-pad merges are 4 same-net pad-shape overlaps (J1.A1/J1.B12, J1.A12/J1.B1, J1.A4/J1.B9, J1.A9/J1.B4).
