# VitalQuant case_v10 - assembly + functional simulation (V22 board)

**[checked]** = mesh boolean/ray (same probes as verify_fit);
**[inferred]** = analytic model, assumptions stated.

| # | check | value | basis | result |
|---|---|---|---|---|
| **FIT** | | | | |
| 1 | board seated: edge->wall | grown+0.05 hit 0.000 mm3 (plan gap >=0.30) | checked | PASS |
| 2 | tolerance stack +0.30 | hit -0.000 mm3 | checked | PASS |
| 3 | board insertion sweep | 0 inter @dz 0.05..3 vs base | checked | PASS |
| 4 | lid closure over board | 0 inter @dz 0.5..3 | checked | PASS |
| 5 | H1/H2 locating pins | pin top z0.00; pin top z0.00 | checked | PASS |
| 6 | lid clamp coverage | 13/13 edge pts under ledge | checked | PASS |
| 7 | USB-C overmould corridor | 0.000 mm3 | checked | PASS |
| 8 | USB-C plug tongue | 0.000 mm3 to full depth | checked | PASS |
| **WIRE** | | | | |
| 9 | J2->floor groove | groove bottom z1.00 (wires sit at 1.0..1.62 under board) | checked | PASS |
| 10 | west wall portal | base hits [], top hits [] (want 0) | checked | PASS |
| 11 | lid notch re-entry | base [], top [] (want 0) | checked | PASS |
| 12 | strain-relief posts | two ceiling posts at y9.0 present | checked | PASS |
| 13 | O3.2 lid hole | 0 hits (want 0) | checked | PASS |
| 14 | cup wire notch over hole | 0 hits (want 0) | checked | PASS |
| 15 | hole inside bay footprint | (6.6,8.2) -> True | checked | PASS |
| 16 | harness seam pinch | 0.000 mm3 in z4.35 band | checked | PASS |
| 17 | antenna keep-out avoided | entire J2 route at y3.4..9.0 (keep-out y<-10) -> zero crossing | checked | PASS |
| 18 | ECG contact scheme | 20 spring pogos, no leads; pogos vs case 0.000 mm3 | checked | PASS |
| 19 | contact separation | min copper gap 0.50 mm (J7.1-J7.3) | checked | PASS |
| 20 | pogo tip reaches skin | J5.1 tip z-0.60 (mouth 0.36, +0.6 proud) | checked | PASS |
| **THERM** | | | | |
| 21 | interior air idle 0.35 W | +11 C over skin 37 C (R~31 K/W; comp limit +48) | inferred | PASS |
| 22 | interior air sustained 0.7 W | +21 C over skin 37 C (R~31 K/W; comp limit +48) | inferred | PASS |
| 23 | interior air burst 1.2 W | +37 C over skin 37 C (R~31 K/W; comp limit +48) | inferred | PASS |
| 24 | skin-face surface @0.7 W | ~44 C vs <43 C touch target - marginal at sustained max | inferred | FLAG |
| 25 | dedicated dissipation path | none for U1/U2 (antenna keep-out forbids metal near U1); U11 slug is a skin-temp sensor. Mitigate via firmware power budget + board-edge seat-ring conduction (171/196 edge pts) | inferred | FLAG |
| **OPTICS** | | | | |
| 26 | U16 SFH7072 PPG AFE | ap 8.6x5.4 mm, FOV ~107 deg, centre open=True - 3xLED+PD PPG front-end | checked | PASS |
| 27 | U10 AS7341 spectral | ap 4.5x5.8 mm, FOV ~97 deg, centre open=True - reflectance spectral; reads D10-lit skin | checked | PASS |
| 28 | D10 white LED illum | ap 4.3x5.1 mm, FOV ~94 deg, centre open=True - AS7341 illuminator | checked | PASS |
| 29 | D11 IR emitter | ap 4.2x3.6 mm, FOV ~84 deg, centre open=True - PPG IR beside U16 | checked | PASS |
| 30 | D12 red emitter | ap 4.2x3.6 mm, FOV ~84 deg, centre open=True - PPG red beside U16 | checked | PASS |
| 31 | U12 MLX90632 IR temp | ap 5.8x5.8 mm, FOV ~111 deg, centre open=True - FOV ~50 deg | checked | PASS |
| 32 | U23 SHT45 vent | ap 2.4x2.4 mm, FOV ~62 deg, centre open=True - humidity membrane hole | checked | PASS |
| 33 | white->spectral pairing | aperture centres 4.5 mm | checked | PASS |
| 34 | IR/red->PPG pairing | aperture centres 6.4 mm | checked | PASS |
| 35 | standoff to skin | 2.0 mm (floor+gasket) - PPG contact range | checked | PASS |
| 36 | channel isolation | zero open chords between optical apertures (FIT_REPORT leakage check) - no direct emitter->detector bleed | checked | PASS |

**34/36 PASS, 0 FAIL, 2 FLAG**

## Wiring organisation

**ECG/skin contacts**: no wires at all - 20 spring pogos drop
through apertures straight onto the skin lands (J5 x5, J6 x4, J7 x4,
J11 x3, J13 x2, J3 x2). Self-centring 45 deg funnels guide them;
min copper gap 0.50 mm keeps contacts isolated; tips stand 0.6 mm
proud at working compression.

**Battery**: the only harness. J2 B-pads feed a floor groove under
the board -> west-wall portal -> exterior up-run -> lid notch ->
two strain-relief ceiling posts -> O3.2 hole into the bay (inside
the cup footprint). Every leg probed open; the whole route stays
y3.4..9.0, clear of the antenna keep-out and the seam pinch band.

## Thermal

Lumped model: interior-air -> wall (R~21 K/W) +
skin-contact exterior (R~10 K/W) ~= 31 K/W total.
At the 0.7 W sustained estimate interior air rises ~+21 C over skin;
component ratings (85 C) hold with margin even at 1.2 W bursts.
**Flags**: the skin-facing surface could reach ~44 C vs the ~43 C
touch-comfort target at sustained max load, and no metal heatsink
path exists for U1/U2 (the antenna keep-out forbids metal).
Mitigations: firmware power budget (<0.5 W avg), board-edge
seat-ring conduction (unmodelled parallel path lowers real R),
and the skin itself is a 37 C perfused sink.

## Optics

Every optical pad sits inside its aperture (+0.25/side margin);
usable FOV through the 1.6 mm floor + 0.4 mm gasket (2.0 mm
standoff) is 84..111 deg - ample vs the 50 deg MLX90632 FOV.
Emitter/detector pairs are adjacent (D10->U10 ~5.6 mm,
D11/D12->U16 ~4.4 mm) matching the reflection-mode sensing
geometry; the zero-open-chord leakage check proves walls isolate
each channel from direct bleed. Deep slots + the TPU gasket limit
ambient leakage; a matte/dark interior would cut noise further.
