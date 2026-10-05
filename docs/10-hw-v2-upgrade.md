# 10 — hw_v2 Hardware Upgrade (design doc)

Scope: design record for the second VitalQ board revision, `hw_v2`, carried on
branch `hw_v2`. The authoritative coordination spec is
`hardware/pcb/HW_V2_SPEC.md`; where this doc and that file disagree, the spec
wins. Research prototype. This is not a medical device; nothing here is a
clinical claim (same posture as docs 01/08 and `hardware/pcb/README.md`).

**Status: specification only.** Schematic edits, placement, and routing are
pending on this branch; nothing described below has been placed, routed, or
DRC-checked. See the hw_v2 section of `hardware/pcb/VERIFICATION.md`.

## 1. Motivation — the early-sepsis feature set

hw_v1 already measures the core early-sepsis channels: ECG-derived HRV
(ADS1292R), respiration rate (AD5940 4-wire chest bioZ on J7), skin and IR
temperature (TMP117 ×2, MLX90632), perfusion (SFH 7072 + AFE4900 PPG), EDA,
motion, and ambient spectral sensing (AS7341). hw_v2 keeps every one of those
and adds the channels the sepsis literature points at next:

| New feature | Hardware added | Honest status |
|---|---|---|
| Core-temperature trend ("ZHF-style" pair) | Foam insulation dome over the U20 TMP117 (already fitted); U11 TMP117 is the skin-contact element | **Trend, not diagnostic.** Passive insulation, not a servo-controlled zero-heat-flux thermometer — reads a skin-under-insulation temperature that tracks core trends. No calibrated core-temp claim |
| Mottling / tissue-StO₂ index | AS7341 (existing) + new D12 730 nm LED filling the gap between F8 (680 nm) and NIR (~910 nm) | Exploratory diffuse-reflectance index only — same "illumination-defined channel" caveat as doc 02 §A. Not an oximetry number; per-device calibration required before any ratio is reported |
| PAT (pulse arrival time) | MAX86178 synchronised ECG/PPG/BioZ AFE — its own ECG channel and its PPG share one clock, and it drives the satellite-PPG tail natively | PAT is a sympathetic-tone/perfusion proxy, not a blood-pressure measurement |
| Peripheral perfusion gradient | J9 distal-temp tail (flex TMP117/TMP119 at 0x4A) + J10 satellite-PPG tail (off-board SFH7050A) | Core-vs-distal ΔT plus distal perfusion — the "cold mottled extremity" analog. Placement of the distal site is a harness decision |
| Sweat-lactate channel | J11 WE/RE/CE pads into a spare AD5940 mux channel | **RESEARCH-GRADE everywhere it appears.** Sweat↔blood lactate correlation is contested in the literature and the electrode needs functionalisation the board does not provide |
| Skin humidity | SHT45-AD1B at 0x44 on the bottom skin face | Sweat-rate context, EDA confound control, and microclimate reading inside the adhesive carrier |
| On-device inference headroom | U1 → ESP32-S3-MINI-1U-N4R2 (vector/SIMD, 2 MB PSRAM, 4 MB flash) | Enables TinyML-window scoring on-device; also removes the PCB antenna keep-out strip (U.FL) and gives native USB for fast waveform offload |

Why the S3 module earns the upgrade: it is the only part replaced, and it is
strictly better for this feature set — PSRAM for multi-channel synchronous
buffers, vector instructions for on-device inference, native USB so the
console path is no longer the data bottleneck, and a U.FL connector that
frees the x < 6.5 mm antenna strip and moves the antenna off the skin side.
The W25Q512 logger stays; nothing about the storage plan changes.

## 2. BOM delta

Full orderable detail is in `hardware/pcb/BOM_V2_ADDENDUM.md`. Delta summary:

| Ref | Action | Part | Package | Interface | Role |
|---|---|---|---|---|---|
| U1 | **Replace** | ESP32-WROOM-32E → ESP32-S3-MINI-1U-N4R2 | 15.4 × 15.4 mm module | Native USB on GPIO19/20 → J1; UART0 GPIO43/44 → CP2102 | MCU |
| U22 | Add | MAX86178 | WLP-49 | SPI CS=GPIO38, INT=GPIO39 | Sync ECG/PPG/BioZ + satellite-PPG drive |
| U23 | Add | SHT45-AD1B | DFN-4, 1.5 × 1.5 mm | I2C3V3 **0x44** | Skin RH + temp, bottom skin face |
| D12 | Add | 730 nm LED (emitter TBD) | LED_0402 | +3V3 via R109, Q4 sink | AS7341 NIR pair for StO₂ index |
| Q4 | Add | CSD13380F3T (Q1 clone) | 3-ball CSP-class | gate = NIR730_GATE (AD5940 GPIO2, ball E1) | 730 nm switch |
| R109, R110 | Add | 100 Ω series, 100 kΩ gate hold-down | 0402 | — | See `PASSIVES.md` |
| J9 | Add | 4 SMD pads (3V3/SDA/SCL/GND) | Pads_SMD | tail device at **0x4A** | Distal-temp tail |
| J10 | Add | 6 SMD pads | Pads_SMD | LED1/2/3_K cathodes, LED_AN anode (VBAT via R114), PD_K/PD_A | Satellite-PPG tail (SFH7050A off-board) |
| J11 | Add | 3 SMD pads WE/RE/CE | Pads_SMD | AD5940 AIN6(C5)/AFE3(A2)/AFE4(A1) — RESEARCH-GRADE | Sweat-lactate site |
| J12 | Add, **DNP** | FH33J-14S-class FFC, 14-position | TBD | J9+J10+J11 lines unified (13 nets; pin 14 open) | Assembly option vs discrete pads |
| J13 | Add, **DNP** | 2 SMD pads | Pads_SMD | MX_ECG_INP / MX_ECG_INM | Optional MAX86178 ECG input pair |
| U24 | Add, **DNP** | RV-3028-C7 or equiv | TBD (integrated 32.768 kHz — verify) | I2C3V3 **0x52** | Wall-clock RTC for multi-day logging |
| U25 | Add, **DNP** | IM69D130-class PDM mic | TBD | PDM CLK/DAT GPIO40/41 | Chest acoustics, footprint only |
| R102/R103 + R104/R105 | Add | 0 Ω USB strap links (two pairs) | 0402 | USB D± select | Default fitted R102/R103 → S3 native USB; DNP R104/R105 → CP2102 bridge. **Never fit both pairs** |
| — | Mech. note | ZHF foam dome over U20 | — | — | Assembly instruction, not a PCBA part |

New I2C addresses per spec: 0x44 (SHT45), 0x4A (distal TMP117 on the tail —
needs the tail sensor's ADD0 strapped to SDA), 0x52 (RTC, DNP). All verified
free against the hw_v1 map; nothing moved. Five SPI chip selects now share
the host: CS_ADS1292 (10), CS_AD5940 (15), CS_FLASH (16), CS_AFE4900 (18),
CS_MAX86178 (38).

## 3. What was kept, and why

Per the mandate — maximum functionality, zero compromise — nothing was
removed:

- **Defib ladder** (R32–R36, R76–R83 pulse resistors; D1–D9, D21–D24 TVS;
  all creepage slots): kept verbatim. Still a research-prototype attempt at
  the TI pulse guidance, not an IEC 60601 claim — same disclaimer as hw_v1.
- **TP1–TP27 and J8 (TC2030)**: kept; test/debug coverage is part of "no
  sensing function dropped".
- **CP2102N console path**: kept per user mandate, wired to UART0
  (GPIO43/44). The 0 Ω strap pairs (fitted R102/R103 → S3 native USB;
  DNP R104/R105 → CP2102) preserve it as an out-of-band
  console-recovery path independent of the S3 native USB. Never fit
  both pairs — two USB devices cannot share one D+/D− pair.
- **Power tree** (BQ25170 + MAX17048 + TPS63802 + TPS61240 + TPS7A2018) and
  all current-sense links R93–R97: no rail changes; new loads hang off +3V3.
- **All hw_v1 sensors**: AFE4900, ADS1292R, AD5940, AS7341, MLX90632,
  BME280, LSM6DSV80X, SFH 7072, both TMP117s, FSR pads, buttons.
  AD5940 keeps EDA and chest-bioZ duty; the sweat site only borrows a spare
  mux channel. ADS1292R remains the primary ECG; MAX86178's ECG is for
  synchronised PAT, not a replacement.
- **0402 passive convention** and the W25Q512 WSON-8 logger.

## 4. Interface summary

The authoritative pin/net map is `hardware/pcb/PINMAP.md` (hw_v2 update
owned by the pin/power agent — verify it has landed before schematic
review). Headline deltas from hw_v1:

- I2C moves to **GPIO8 SDA / GPIO9 SCL** — which matches the *existing*
  firmware profile's expectation (the profile already assumed the S3 bus
  map). SPI host is **GPIO11 MOSI / GPIO12 SCK / GPIO13 MISO**, likewise.
- USB D−/D+ are native on **GPIO19/20** through the fitted R103/R102
  0 Ω straps (nets USB_DM_S3/USB_DP_S3; J1 alternates are the DNP
  R104/R105 pair → CP2102).
- UART0 TX/RX on **GPIO43/44** → CP2102 (kept console).
- MAX86178: **CS GPIO38**, timestamped **INT GPIO39**.
- Mic (DNP): **GPIO40/41**. GPIO42 is **SPARE_IN — digital-only** (no
  SAR-ADC channel on the S3): the DNP R107/R108 divider is a
  logic-level battery-present detect option only, never an analog
  VBAT_SENSE. Battery voltage/SoC stays with MAX17048.
- Straps: GPIO0 boot (button + TP10; TP9 is ESP_EN — TP24 stays on
  ADS1292_DRDY), GPIO3 JTAG-select via EXP_INT
  (existing R53 pull-up is fine), GPIO45/46 left unstrapped.
- Expander map P0–P7 unchanged.
- NIR730_GATE is **AD5940 GPIO2, ball E1** — documented in PINMAP
  (GPIO14 ↔ AD5940_GPIO0 on ball F5 is a different sideband link).

Net effect on firmware drift: hw_v2 closes the bus-map gap that
`hw_v1.yaml` never matched, but the profile's device list (MAX86141,
MLX90637, ICM-42670-P) still describes neither board — firmware profile
update is its own work item.

## 5. Deployment notes

- **Electrodes**: Ag/AgCl snap electrodes on the J5 (ECG/RLD + AFE pair),
  J6 (EDA shoulder tail), and J7 (chest 4-wire bioZ) tails are **non-sterile
  single-use consumables**. Replace per session; do not use on broken skin;
  the board makes no biocompatibility claim. The J6 shoulder-tail rule from
  hw_v1 stands: EDA electrodes land ≥ 5 cm from the ECG electrodes — that
  distance lives on the harness, not on this board.
- **Adhesive carrier**: a user-supplied medical-grade adhesive carrier/patch
  holds the skin cluster. The optical aperture must stay aligned over the
  AS7341/SFH 7072 window; carrier choice is an enclosure decision.
- **ZHF dome**: fit the closed-cell foam dome over the U20 TMP117 at
  assembly so it insulates the sensor from the enclosure side while U11
  reads the skin spot at the same XY. The pair reports a **core-temperature
  trend**, not a calibrated core temperature — label any downstream feature
  accordingly.
- **Tails**: J9–J11 are bare SMD pads for soldered flex tails; J12 is the
  DNP 14-pos FFC alternative; J13 (MAX86178 ECG pair) is DNP bare pads.
  Do not hot-plug tails (see risk register). The
  J9-tail sensor must be strapped to 0x4A. SFH7050A on J10 is soldered to
  the tail, not assembled on the board.
- **USB strap**: ship with R102/R103 fitted (native USB). Move the links
  to R104/R105 only for CP2102 console recovery — never fit both pairs.

## 6. Size and stackup

Target outline **~32 × 62 mm (±10 %)** — the freed antenna keep-out is what
makes it reachable. If placement cannot close with all parts + the ladder +
27 test points, fall back to **36.5 × 70 mm** and document why (spec §Target
size). Still one rigid 4-layer board, JLC04161H-7628, ENIG; the MAX86178
WLP-49 will likely need POFV escapes — JLC assembly tier check is a fab
gate. No rigid-flex this rev; tails are pads or the FFC option.

## 7. Power / duty-cycle note

No new rails. Additions are +3V3 loads except LED drive currents. Rough
budget intent (real numbers owned by the pin/power agent):

- SHT45: µA-class idle, sub-mA while measuring — duty-cycle at ~1 Hz or
  slower; it is context, not waveform.
- D12 730 nm: ~15 mA class while gated on — **must be duty-cycled** and
  synchronised to AS7341 integration windows (also keeps heat off the
  sensor island).
- Satellite PPG (J10): MAX86178 LED pulses are the largest new transient
  load — firmware caps them like the AFE4900 budget; duty-cycle the tail.
- RTC (DNP): tens of nA class if fitted. Mic (DNP): ~1 mA class if fitted.
- S3 module: same order as the WROOM for Wi-Fi bursts; PSRAM and the second
  core are the new average-current terms. The buck-boost and the 200 mA
  charger are unchanged — multi-day logging depends on the usual
  sleep/duty-cycle discipline, which the RTC is there to anchor.

## 8. Risk register

| Risk | Why it matters | Mitigation / status |
|---|---|---|
| LED ↔ temperature adjacency | D10/D11/D12 + SFH 7072 sit beside the TMP117 island and the new SHT45; emitter self-heating biases skin temp/RH readings | Thermal island and slots kept; duty-cycle all emitters; take dark/temp readings before LED windows; thermal review is a fab gate |
| Tail hot-plug ESD | J9–J13 carry sensor/IC pins off-board with no added TVS in the spec | Handling protocol: connect tails unpowered only; ESD review is a fab gate; revisit if bench ESD events occur |
| Sweat-lactate validity | Sweat↔blood lactate correlation is contested; unfunctionalised electrodes drift and foul | RESEARCH-GRADE label mandatory (spec rule); features gated behind SQI; never surfaced as a clinical number |
| RTC / mic are DNP | Footprints and pinouts are unverified until the schematic lands; an unfitted DNP must not break the bus | Both excluded from JLC BOM by convention; verify land + address at schematic review |
| MAX86178 WLP-49 assembly | Fine-pitch WLP on JLC needs the right assembly tier and probably POFV escapes | JLC tier check is a fab gate; escape pattern pending layout |
| U.FL coax routing | The keep-out strip is gone; a loose coax over the sensor island or the ladder is an RF/optical problem | RF review gate; dress the coax off the skin cluster and off the HV ladder |
| NIR730_GATE source | Gating through an AD5940 GPIO couples LED timing to AD5940 firmware state; AS7341 sits on the 1.8 V bus | Ball assigned: GPIO2/E1, documented in PINMAP; firmware schedules LED-on inside AS7341 integration windows |
| J9 tail I2C length | 0x4A sensor rides the shared R19/R20 4.7 kΩ pull-ups; a long tail adds capacitance | Keep the tail a short flex; verify rise times on the bench |
| R102/R103 + R104/R105 both fitted | Would tie the CP2102 and S3 USB PHYs together | Assembly note: fit exactly one pair; R104/R105 ship DNP |
| Carried-over hw_v1 debt | All hw_v1 accepted warnings (C54 voltage, unrouted nets, firmware profile drift, …) still apply | See `VERIFICATION.md` — hw_v2 does not clear them |

## 9. Open items

Placement, routing, ERC/DRC against hw_v2 content, the regenerated
schematic and board, PINMAP/power budgets, and the firmware profile are all
owned by their respective agents and are **pending** at the time this doc
was written. The fab gate list lives in
`hardware/pcb/VERIFICATION.md` §hw_v2.
