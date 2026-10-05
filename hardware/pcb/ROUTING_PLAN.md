# VitalQ hw_v2 — routing plan

Board: `vitalq_hw_v1.kicad_pcb` (regenerated hw_v2), **40.0 × 62.0 mm**
(grew east from 32.0 to host the J11/J13 HV-boundary protection row —
J10/J11 moved to the new east edge), **6 layers** since 2026-10-03
(F.Cu / In1.Cu GND / In2.Cu power+signal / In3.Cu signal / In4.Cu GND /
B.Cu), double-sided assembly, **292 footprints**,
~1,025 pads.

**Status: PARTIALLY ROUTED — NOT FAB-READY.** As of 2026-10-03 the
board carries 1,727 track segments + 226 vias from the parallel
grid-router effort described in VERIFICATION.md ("2026-10-03 —
six-layer routing effort"). ~450 unconnected items remain per
kicad-cli DRC, concentrated in the U6/U7/U8 dense center, the I2C/SPI
buses, HV tails, and power nets lacking pour islands. Do not send it
out until every net is routed and DRC is clean except agreed waivers.

Generated 2026-09-30 with KiCad 10.0.4 pcbnew + `kicad-cli pcb drc`.

The post-snapshot additions are now **placed and verified**: the
J11/J13 0 Ω cut-point chains (R118–R122, R_2512_6332Metric FP_HV parts
in the east protection column) with the D26–D30 DNP clamps,
R114–R117, C84 and TP28, plus the L2 span fix, the J1 pad joins and
the ADS1292 CLK tie (see VERIFICATION.md "Recent schematic
additions"). Net-class tallies and per-net link counts in §1 remain
the earlier 276-part census — directionally right, pending a fresh
tally on the 291-part board.

## 0. Current DRC snapshot (pre-route)

| Type | Severity | Count | Note |
| --- | --- | --- | --- |
| unconnected_items | error | 499 | the whole point of routing |
| clearance | error | 9 | Q1/Q2/Q4 CSD13380F3 intra-package pad gaps (0.100 vs 0.127 mm — footprint-inherent, same as hw_v1); **waived-by-name in `check.sh`** — a package-level rule area remains an option for fab |
| via_dangling | warning | 49 | expected — the pre-placed VIP/GND vias have no tracks yet |
| silk_* | warning | 35 | `silk_edge_clearance` 15 + `silk_over_copper` 11 + `silk_overlap` 9 — **report-only in `check.sh`** since this round |
| text_height / text_thickness | warning | 29 | 15 + 14; cosmetic |
| lib_footprint_mismatch | warning | 2 | U6/U7 local BGA overrides; benign |
| schematic_parity | items | 31 | reported alongside (TP/J8/fiducial BOM-exclude diffs + H1/H2 pin-less holes) |

Resolved since the first snapshot: `courtyards_overlap` 55 → **0**
(`B.CrtYd` removed from the nine custom footprints), `solder_mask_bridge`
5 → **0** and the 5 U1-adjacent clearance errors → **0** (a layerless
rect defaulting to F.Cu was deleted from the S3-MINI land),
`copper_edge_clearance` 14 → **0** (TMP117 moat redrawn as a ∩-channel —
paddle x18.53–21.87 covers all U11/U20 pads with ~0.32 mm margin). The
only remaining error class is the nine waived intra-package clearances —
**zero other error classes** — and `check.sh` passes end-to-end.

Silk warnings (35) no longer gate the script but should still be
cleaned **before or during** routing for fab.

## 1. Net census

**284 net names** on the current board (280 netcodes at the earlier
census, +J11/J13 chain nets): ~190 multi-pin nets + 94 single-pin
`unconnected-(…)` no-connects (incl. 30 U22 RSV balls, U7 DNC/spare-GPIO
balls, U5 NC pins, unused module pins, FID/H pads). Single-pin nets
need no copper. Per-net link counts below are the earlier census —
treat them as direction, not checklist.

### Per-net missing links

Two honest numbers: **kicad-cli DRC reports 499 `unconnected_items`
violations**, while pcbnew connectivity reports **678 unconnected items**
and the minimum spanning links (Σ pads−1 per net) is **~672**. The gap is
a DRC quirk: ~110 fully-isolated nets (e.g. USB_DP/USB_DM with 6 islands
each, +1V8, IN1N, all 2-pad LV nets) produce **zero** reported violations.
**Do not rely on the 499 list as the routing checklist** — use the
ratsnest in pcbnew (it shows all of them).

Top nets by missing links (Σ pads−1):

| Net | Pads | Min links | Class |
| --- | ---: | ---: | --- |
| GND | 210 | ~209 | ground |
| +3V3 | 63 | ~62 | power |
| +3V3_ANA | 24 | ~23 | power (analog) |
| I2C_SDA / I2C_SCL | 14+14 | ~26 | I2C 3V3 |
| VBAT / VBUS | 13+13 | ~24 | power |
| VDD_CP2102 / TX_5V | 9+9 | ~16 | power |
| +1V8 | 8 | ~7 | power (1V8 island) |
| ESP_EN | 8 | ~7 | digital |
| SPI_MOSI / SCK / MISO | 7 each | ~18 | SPI |
| ECG_P / ECG_N / RLDOUT | 6 each | ~15 | electrode LV |
| USB_DP / USB_DM (conn side) | 6 each | ~10 | USB diff |
| VBAT_SYS | 6 | ~5 | power |
| I2C_SDA_1V8 / SCL_1V8 | 5+5 | ~8 | I2C 1V8 island |
| IN1P / IN1N | 5 each | ~8 | electrode LV |
| 5× CS_* + USB strap legs + MISO_* | 2-4 each | ~24 | SPI/USB |
| UART/console (ESP_TX/RX, CP_*) | 2-4 | ~15 | UART |
| ~150 small analog/misc/TP nets | 2-3 each | ~200 | misc |

### Net-class roll-up

| Class | Nets | Pads | Where they live |
| --- | ---: | ---: | --- |
| power-gnd | 1 | 210 | everywhere; pours planned B.Cu + In1.Cu |
| power-rail | 18 | 172 | mostly top (power block y 14–32) + islands |
| analog-electrode | 86 | 216 | bottom side; J5/J6/J7 pads y≈60.4; ladder y≈28/36 |
| misc-digital | 149 | 279 | everywhere; ~half are single-pin NC nets |
| i2c | 4 | 38 | both buses; see §3.7 |
| spi | 8 | 38 | module → 5 devices, mostly bottom |
| uart | 8 | 23 | U1 ↔ U5 CP2102, top area |
| usb-diff | 6 | 20 | J1 → U21 → strap Rs → U1 / U5 |

## 2. What is already on the board

- **49 vias, 0 track segments.** 29 are 0.30 mm pad / 0.20 mm drill
  via-in-pad escapes pre-placed under U7 (AD5940 — now including the
  SWEAT_WE ball) and U6 (AFE4900); 20 are 0.60/0.30 mm GND perimeter
  stitches, including a new column along the east edge (x 38.8). Keep
  them; finish connecting them.
- **6 copper zones, all unfilled** (deliberate — fills are deferred):
  GND on B.Cu (full board), GND on In1.Cu (full board),
  +3V3 on In2.Cu (bulk fill), TX_5V island In2 (x 23–30.6, y 0.4–6.5),
  VBAT island In2 (x 11–20.5, y 10–17), +1V8 island In2 (x 21–28.6, y 9–15.5).
- **80 rule areas**: 3× `bga_fanout` (U6 region 24.0,46.3 3.4×4.0; U7
  region 6.3,15.7 6.5×7.0; **U22 now has its own** — the constraint-#2
  action is done), 36× `hv_inner` + 36× `hv_ownlayer` pad-shadow
  creepage keepouts (electrode pad row, ladder rows, **plus the
  J11/J13-chain pad shadows in the east protection column**),
  2× `hole_keepout`,
  2× `sensor_nopower` on In2 only (TMP117/ZHF islands), 1× `ufl_dress`
  (top-left coax lane x≈7.6, y 0–2.5).
- Design rules (`vitalq_hw_v1.kicad_dru`): 0.127 mm default clearance;
  0.09 mm clearance/track inside `bga_fanout`; HV_ELECTRODE class =
  1.5 mm to any other class + 0.25 mm min width; ELECTRODE_LV = 0.20 mm
  clearance / 0.15 mm width. Net classes exist; **no differential-pair
  class is defined yet** (needed for USB, constraint #3).

## 3. Top-10 routing constraints (priority order)

1. **HV electrode corridor — route first.** 18 HV_ELECTRODE pads:
   J5 (ECG1/ECG2/RLD/AFE_P/AFE_N_PAD, x 15–22, y 60.4), J6 (EDA CE/RE/SE/DE,
   x 5.7–12.3, y 60.4), J7 (BIOZ FP/FN/SP/SN, x 11–18, y 60.35, F side),
   **plus the J11/J13 chain pads** (J11_WE/RE/CE on the east edge at
   (38.8, 55.0); J13_INP/INM on the bottom edge at (29.6, 60.4)).
   They feed the defib ladder R32–R36 (B, y 28.4) + R76–R79 (F, y 28.4) +
   R80–R83 (B, y 36.3) — i.e. **~30 mm runs** up the board under a 1.5 mm
   clearance-to-everything rule plus the `hv_inner` keepouts and 1.0 mm
   creepage slots at both ladder rows and the pad row. This is a dedicated
   corridor; nothing else may share it. Route on whichever face the keepout
   stack leaves open at each segment (keepouts differ F vs B — the pad-row
   keepouts cover F/In1/In2, leaving B.Cu as the HV lane near y 58–62).
   0.25 mm tracks. After the ladder, the LV side (*_SURGE, ECG_P/N,
   RLD_CLAMP, *_ISO nets) relaxes to 0.20 mm clearance / 0.15 mm tracks.
   **Do not route anything through this region early — it is the single
   biggest scheduling constraint.**

   The class now also carries the J11/J13 tail chains: connector pads
   J11_WE/RE/CE and J13_INP/INM → 0 Ω cut-points R120–R122 / R118–R119 →
   SWEAT_*/MX_ECG_* toward the AD5940 (A1/A2/C5) and U22 (A6/A7), with
   DNP TPD1E10B06 footprints D26–D30 to GND on the IC side of each 0 Ω.
   The cut-points are **R_2512_6332Metric (FP_HV)** parts — same
   HV-boundary footprint pattern as R76–R83 — placed in the east
   protection column (R118 F 35.7,44.3; R119 F 35.7,56.5; R120 B
   35.0,48.5; R121 B 34.3,39.7; R122 B 34.3,58.0). The 0 Ω parts are
   deliberate cut points for optional population — dropping one
   isolates its tail (J13 and its clamps ship DNP anyway).
   Route the chains so either fit works: clamp pads get real copper with
   the class clearance honoured, and the connector-side pads carry the
   same pad-shadow `hv_inner`/`hv_ownlayer` keepouts as the electrode
   field. Do not shortcut a cut-point with pour or a track jumper.
2. **U22 MAX86178 WLP-49 escape.** 0.35 mm pitch, 0.20 mm pads — tighter
   than the 0.4 mm BGAs. Live balls are only the perimeter ring: column A
   (PD_A, PD_K, LED1/2/3_K, MX_ECG_INP/INM → J10/skin cluster, escape on
   B.Cu), column G (SPI_MOSI/SCK, CS, INT, MISO_MX → digital, escape on
   B.Cu), and **interior column F** (+3V3_ANA, +3V3, VBAT, MX_VREF, 3×GND,
   pads y≈45.75) which cannot surface-route (pad gap 0.15 mm < 0.27 mm
   needed at 0.09 rules) → **needs 0.20/0.30 via-in-pad with POFV**, 7 vias,
   escaping onto In2/F.Cu. The other 30 balls are RSV (no-connect) — do not
   use their pads as a routing channel. ~~Action needed: add a `bga_fanout`
   rule area over U22~~ — **done**: a third `bga_fanout` rule area now
   covers U22, so the 7 F-column VIPs can be placed (the 29 pre-placed
   VIP vias are all under U6/U7 — U22's are still to come). Confirm JLC
   accepts 0.30 mm via pads under POFV (already the U6/U7 assumption);
   otherwise this part forces an HDI/microvia quote.
3. **USB D+/D− 90 Ω differential pair.** Path: J1 (F, top edge x≈26,
   y≈6.75) → U21 USBLC6 (B) → strap resistors R102/R103 (B, fitted) →
   USB_DP_S3/USB_DM_S3 → U1 pins 23/24 (F, x≈12.4–13.25, y≈17). Also route
   the DNP alternate leg R104/R105 → USB_DP_CP/DM_CP → U5 pins 4/5 as far
   as the strap pads (the CP leg terminates at DNP parts — route it fully
   anyway so either assembly works). ~15–20 mm. **Create a `USB` diff-pair
   net class first** (KiCad 10: Board Setup → Net Classes → differential
   pair defaults; none exist now). Target geometry on this stackup
   (JLC04161H-7628, outer Cu 1 oz, ~0.2 mm prepreg to In1 GND): start at
   0.20 mm width / 0.20 mm gap, verify with JLC's impedance tool — USB FS
   tolerates ±15 %. Length-match P/N to ≤1 mm (meander inside the pair,
   keep symmetric through the strap pads; the 0 Ω links are part of the
   transmission line). Keep over GND reference, no plane splits, no stubs
   except the strap pads. **Caveat:** DRC reports 0 unconnected violations
   for USB_DP/USB_DM even though all six pads per net are isolated
   (verified via connectivity API) — the pcbnew ratsnest still shows them.
   Route them anyway; don't trust the 499 list.
4. **SPI fanout + MISO star.** MOSI/SCK broadcast from U1 (top, x≈12.4,
   y≈15–17) to five targets on B: U18 flash (4.6,46.1), U7 AD5940 (9.5,19.2),
   U8 ADS1292 (24.5,16.5), U6 AFE4900 (25.7,48.3), U22 (26.1,45) — plus 5 CS
   nets (CS_ADS1292/CS_AD5940/CS_FLASH/CS_AFE4900/CS_MAX86178) with pull-ups
   R98/R7/R52 at the top edge plus post-snapshot R116 (CS_AFE4900→+3V3_ANA)
   and R117 (CS_MAX86178→+3V3). Device MISOs are **separate nets**
   (MISO_FL/AD/ADS/AFE/MX) each joined by a series resistor
   (R92/R91/R89/R90/R106) into SPI_MISO → U1.17. Route SCK/MOSI as a clean
   tree (daisy or star), keep ≥0.5 mm from the analog cluster and the HV
   corridor; these are 5–20 MHz edges. Total ~10 nets.
5. **Power star links R93–R97 — never bridge the islands.**
   VBAT→(R93)→VBAT_SYS→U3/U15 inputs; +3V3→(R94)→+3V3_ESP (module pin 2 +
   C7/C8) and →(R95)→+3V3_ANA (analog AFEs); TX_5V_RAW→(R96)→TX_5V
   (AFE4900 TX + SFH7072/satellite anodes); +1V8_LDO→(R97)→+1V8
   (AS7341/PCA9306/MLX pull-ups). Pouring load-side nets onto source-side
   islands (or vice versa) silently bypasses the 0 Ω sense links — the
   current-flow checks in VERIFICATION depend on them.
6. **Analog front-end cluster (bottom).** U6 AFE4900 BGA-30 (0.4 mm),
   U7 AD5940 BGA-56 (0.4 mm), U8 ADS1292R QFN-33, U22 WLP-49 — 4 BGA/QFN
   escapes; VIP vias already sit at U6/U7/U22 (the earlier 5
   via-vs-U1-segment clearance errors are resolved — the layerless rect
   that caused them was removed from the S3-MINI land). Sensitive nets:
   IN1P/IN1N, RLDOUT/RLDREF/RLDINV,
   *_AC series nodes, VCAP1/VCAP2, VREF_2V5, VREF_1V82, AFE_BG, MX_VREF —
   short, direct, on B.Cu/In1-adjacent layers, decoupling caps within ~1 mm
   of each pin, no routing under the switcher column. Guard/hatch ECG/bioZ
   inputs with GND where possible.
7. **I2C buses.** 3.3 V trunk (I2C_SDA/SCL, 14 pads each): U1 → U9 HV side,
   U11+U20 TMP117s, U13 BME280, U14 LSM6, U17 gauge, U19 expander, U23 SHT45,
   U24 RTC(DNP), J9 tail, J12 FFC, R19/R20 pull-ups — a long spine across
   both faces; keep off the HV corridor, expect tail-cable capacitance
   (≤400 kHz, strong pull-ups already 4.7 kΩ). 1.8 V island
   (I2C_SDA/SCL_1V8, 5 pads each): U9 LV side → U10 AS7341 + U12 MLX90632 +
   R21/R22 — all bottom; keep inside the LV pocket, never bridge the
   PCA9306 boundary except through U9.
8. **Skin-cluster optics/thermal.** Bottom side x≈8–26, y≈44–60:
   U16 SFH7072, U10 AS7341, U12 MLX90632, U11 TMP117 (thermal moat),
   U23 SHT45 (≥5 mm from the temp island), D10/D11/D12 LEDs.
   LED1/2/3_K, LED_AN, PD_A/PD_K between U22 (26.1,45) ↔ J10 (38.8,43.4
   on the new east edge)
   and SFH7072: keep <15 mm, wide enough for LED pulse current (≥0.3 mm,
   LED_AN is the shared anode now fed from TX_5V through R114 — moved
   off VBAT post-snapshot). The two
   `sensor_nopower` In2 keepouts (TMP117 islands) must stay free of power
   pour — verify after filling.
9. **Switcher hot loops.** U3 TPS63802 (29.5,32 F) + L1 (30.1,28.8):
   SW_L1/SW_L2 to inductor and C44/C45 — minimum-area loops, solid GND
   return, shielded from the analog cluster on B (which is ~15 mm below —
   rely on the In1 GND wall). U15 TPS61240 (26,19.9) + L2 (29,16.4):
   same discipline — L2 now spans VBAT_SYS→TX_SW (corrected post-snapshot;
   it had been drawn TX_SW→TX_5V) and TX_5V_RAW feeds R96→TX_5V to the
   LED path.
10. **Module + miscellaneous.** U1 ESP32-S3-MINI-1U: +3V3_ESP decoupling
    (C7 currently at (2.17,10.3) B — ~10 mm from the power pin; consider
    adding a via pair or moving), EN/IO0 strap area, GPIO strap-link
    resistors R106–R109/R112–R114 parked at (4–15, 8–11) B under the module
    footprint — that area is inside the module body shadow, watch the
    U1-copper-segment clearance errors already flagged. U.FL: no board
    copper needed (jack is on-module) — only respect the `ufl_dress` lane
    (x≈7.6, y 0–2.5) + no tall parts under the pigtail bend. J8 TC2030 +
    TP1–TP28: route pads last, keep the J8 pogo field clear of vias on the
    contact side.

## 4. Recommended layer assignment

| Layer | Role | Notes |
| --- | --- | --- |
| In1.Cu | **Solid GND** (pour exists, unfilled) | No signal tracks. This is the return/reference plane. |
| In2.Cu | **Power pours** (zones exist) | +3V3 bulk + TX_5V/VBAT/+1V8 islands; keep the `sensor_nopower` and `hv_inner` exclusions; avoid signal tracks entirely if possible — every F↔B via transition needs a nearby GND via to keep the return on In1. |
| F.Cu | Primary signal | USB pair, SPI trunk starts here (module), power-IC routing, most top-side interconnects; optional GND pour fill at the end. |
| B.Cu | Signal + GND pour | Analog cluster, electrode runs, tail fan-out; the existing GND zone should be kept and stitched — it doubles as the analog guard plane. |

HV runs: bottom-edge pad row is F/In1/In2 keepout-blocked → use B.Cu lane;
at the ladder rows obey each `hv_inner` layer set (some cover all 4 layers,
the B-column set excludes F — route those segments on F if forced, but
prefer keeping everything on B for a single clean corridor).

## 5. Via strategy

| Via | Where | Notes |
| --- | --- | --- |
| 0.60 pad / 0.30 drill through | general signals | Default class; ~150–250 expected incl. GND stitching. |
| 0.30 pad / 0.20 drill, VIP/POFV | inside `bga_fanout` only (U6, U7 — **add U22**) | JLC minimum via; in-pad plated over. ~30 already placed + ~7 needed for U22 F column. Confirm pad-size exception with JLC (0.30 mm pad is below their standard 0.45 mm annular floor — the repo already assumes POFV for the 0.4 mm BGAs). |
| Stitching | board edges, around HV keepouts, pour ties | 0.60/0.30, ≥0.5 mm from HV keepout edges. |

## 6. Suggested routing order

1. Fix remaining placement nits first: J9 courtyard vs U1/H2, R59/R60
   vs U1 (the 5 via-vs-U1-segment errors are resolved; the Q1/Q2/Q4
   footprint-inherent 0.10 mm gaps are now waived-by-name in `check.sh`
   — a scoped rule area stays an option for fab), silk cleanup (35).
2. ~~Add `bga_fanout` rule area over U22~~ (done); create the `USB`
   diff-pair class.
3. HV electrode corridor: 18 pad nets (incl. the J11/J13 chains) →
   ladder → LV-side *_SURGE/ECG nets to the AFEs. Lock it.
4. BGA escapes: finish U6/U7 VIP tails, escape U22 (A/G columns on B,
   F column VIP → In2/F).
5. USB D+/D− diff pair, both strap legs.
6. Power: VBAT/VBAT_SYS/+3V3/+3V3_ESP/+3V3_ANA/TX_5V(+RAW)/+1V8(+LDO)/
   VBUS/VDD_CP2102 — via-drops into the In2 pours, switcher loops.
7. Analog LV cluster: ADS1292 input/RLD network, AD5940 loops, AFE4900
   PD/LED nets, decoupling.
8. SPI + CS + MISO star, UART/console, GPIO strap area, buttons.
9. I2C trunk + 1V8 island, skin-cluster locals, tail connectors
   (J9–J13 — including the J11/J13 cut-point + DNP-clamp chains in
   constraint #1), TP taps.
10. Remaining misc digital, then: fill all pours → GND stitching →
    full DRC → length-check USB pair → re-run `check.sh`.

Estimated primitive count: ~672+ net links (earlier census; the
J11/J13 chains add HV links) ⇒ roughly **1,000–1,400 track
segments + 250–400 vias** on a 2,480 mm² board with ~60 % pad coverage.

## 7. Tooling options (ranked)

1. **Hybrid manual-first (recommended).** pcbnew InteractiveRouter for
   constraints 1–6 (~40 % of links, all the risky ones), then either keep
   hand-routing or hand the remainder to an autorouter and clean up.
   Highest success probability on this density and the custom HV rules,
   which Freerouting will not fully honour (DSN carries class clearances
   but not the `intersectsArea`/creepage semantics — verify after export).
2. **Freerouting + Java.** Not installed; user needs
   `brew install --cask temurin` (or any JDK 17+) then the
   `freerouting-*-executable.jar` from GitHub releases. Usage:
   `java -jar freerouting.jar -de vitalq_hw_v1.dsn -do vitalq_hw_v1.ses -mp 100`,
   then File → Import → Specctra Session in pcbnew
   (`pcbnew.ImportSpecctraSES` also exists). A clean DSN has been exported
   to `hardware/pcb/vitalq_hw_v1.dsn` (169 KB, all layers/keepouts/planes).
   **Caveats from hw_v1 history:** strip the `(plane …)` polygons from the
   DSN before routing — plane shapes stall the router; expect ~60–80 %
   completion on this board; do not let it touch the HV corridor, USB pair,
   or BGA escapes (pre-route + fix those first). A past maze/short incident
   (+3V3↔GND) means any SES import must be followed by full DRC.
3. **Pure manual.** ~2–4 days of focused work for an experienced layout
   person; safest option, the only one that fully honours the HV/creepage
   intent without babysitting an autorouter.
4. **Other routers (TopoR/DeepPCB/freerouting-ng)** — possible but same
   caveats as Freerouting plus extra conversion friction; not worth it
   unless option 2 stalls again.

## 8. Honest effort assessment

- Criticals (constraints 1–6): ~1–1.5 days manual.
- Bulk digital + pours + stitching + DRC cleanup: ~0.5–1 day manual, or
  Freerouting + a few hours of review.
- DRC closure beyond routing: courtyard overlap is 0 and the nine
  intra-package clearances are waived-by-name, so remaining cleanup is
  the 35 silk warnings plus a deliberate review of the waiver — budget
  half a day.
- Total realistic: **2–4 days to fab-ready**, assuming no placement rework.
  If placement must move (J9/U1 overlap, module-edge VIP crowding), add
  re-verification time.

## 9. Data appendix

- Board origin lower-left in file coords; sizes above in mm.
- Component map (for corridor planning): module U1 (12.4,10) F; USB J1
  (26,2.7) F; CP2102 U5 (24.5,13.4) F; power block y 14–32 F; AFEs on B:
  U7 (9.5,19.2), U8 (24.5,16.5), U6 (25.7,48.3), U22 (26.1,45); skin
  cluster x 8–26 y 44–60 B; electrode pads y≈60.4; ladder rows y≈28.4/36.3;
  tails: J9 (8,1.5)B top-left, **J10 (38.8,43.4)B and J11 (38.8,55.0)B
  on the new east edge**, J12 (3.3,50)B, J13 (29.6,60.4)B;
  J11/J13 cut-points R118–R122 (R_2512 FP_HV) in the east protection
  column x 34.3–35.7 (R118/R119 F, R120–R122 B).
- DSN: `vitalq_hw_v1.dsn` in this directory, regenerated via
  `pcbnew.ExportSpecctraDSN` (KiCad 10 API; `kicad-cli pcb export` has no
  dsn subcommand).
- Do not edit the board file by hand — it is regenerated by
  `build_pcb.py`/`design.py`; route it in pcbnew and keep the generated
  files as the routing input snapshot.
