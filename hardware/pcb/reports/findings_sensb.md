# 03 — SENSOR group B findings (pin-by-pin fab-gate audit)

Revision: **fix_v19** @ commit `b9002188` — `hardware/pcb/vitalq_v2.kicad_pcb`
(sha256 `78441506…a69d0`, verified unchanged at HEAD), fab authority
`fab/VitalQ_v2_fixv19_fabrication.zip` (sha256 `5f976845…befd52`).
Pinmap source: `reports/06_tool_output/01_netlist_pinmap_fixed.csv`
(`reports/01_netlist_pinmap.csv` has an **empty net column** — use the fixed
copy for any further work).

Parts: U14 LSM6DSV80XTR, U18 W25Q512JVEIQ, U23 SHT45-AD1B-R3,
U24 RV-3028-C7, U25 IM69D130V01. Matrix: `03_matrix_sensb.csv`.
Docs: `datasheets/manifest_sensb.csv`.

---

## F-01 — CRITICAL (source artifact only): U14 LGA-14 pad W/H transposed in pcb source; all 14 pads overlap neighbours

`vitalq_v2.kicad_pcb` embeds `QFN_LSM6DSV80XTR_STM` with every pad
`(size 0.254 0.7874)` — pads 1–4/8–11 (rot 0) land the **0.7874 mm side along
the 0.5 mm pitch axis**, pads 5–7/12–14 (rot 270) the same. Every adjacent
pair overlaps **0.287 mm** (computed from `vitalq_v2.kicad_pcb` lines
3824–3926). Consequences if gerbers were regenerated from this source:

| Overlap | Nets | Effect |
|---|---|---|
| pad5 × pad6 | +3V3 × GND | **rail dead short** |
| pad12 × pad13 | +3V3 × I2C_SCL | SCL tied to rail — I2C dead |
| pad13 × pad14 | I2C_SCL × I2C_SDA | bus dead |
| pad3 × pad4 | GND × LSM6_INT1 | INT1 grounded |
| pad8 × pad9 | +3V3 × NC(INT2 pin) | INT2 soldered to +3V3 |
| pad1×2, 2×3, 6×7, 9×10, 10×11 | same-net pairs | benign |

The same transposed geometry exists in the footprint library copies
`vitalq.pretty/QFN_LSM6DSV80XTR_STM.kicad_mod` and
`snap.pretty/QFN_LSM6DSV80XTR_STM.kicad_mod` (lines ~1023–1096).

**Fab zip is clean:** `vitalq_v2-Top Layer.gbr` flashes aperture
`R 0.254000X0.787400` for the pad-1–4 row at y=−38.1744 mm (pitch dir = board
x, so 0.254 is the pitch-direction width — correct) and `R 0.787400X0.254000`
for the pad-12–14 column at x=19.0517 — i.e. the manufactured copper is the
correct orientation (narrow in pitch, long radially). Per `00_target.md`, the
zip is the fab authority → **the shipped board is not affected; the pcb/lib
source is.** Any re-export of gerbers from the current source reproduces the
shorts; fix the footprint before the next spin. UNVERIFIABLE: which artifact
the zip was generated from (zip has no source hash — see 00_target).

## F-02 — MEDIUM: copper + GND via under U23 SHT45 (Sensirion keepout violated)

SHT4x DS §5.3/Fig.13 (p.13): *"There shall be no copper under the sensor
other than at the pin pads"*; die pad must not be soldered (heat-sink effect
defeats the on-package heater — §5.5 Table 8). Board: no EP pad (good), but
GND via `0.4/0.2 mm` at **(7.7146, 46.4611)** sits inside the 1.5×1.5 mm
package outline (U23 centre 7.6/47), plus GND routing at y≈46.6 under the die
area (Bottom Layer gerber draws at x 7.443–7.576, y −46.6). Sensor still
functions; heater mode performance degrades. Reroute the via outside the
keepout at next spin.

## F-03 — MEDIUM: U25 SELECT strapped to GND = RIGHT channel; firmware decodes LEFT

IM69D130 Table 7 (p.12): pin 4 = SELECT. Board ties it to GND. Infineon
convention (DS Fig.10 stereo circuit p.10 + Infineon AudioHub/support
statement): LR→VDD = left device, **LR→GND = right device**.
`components/vq_drv_mic/vq_drv_mic.c:5` assumes *"SELECT low → LEFT-slot →
I2S_PDM_SLOT_LEFT"*. The mic therefore drives DATA on the edge the ESP32 does
not sample; during the sampled half-cycle DATA is high-Z (no pull R fitted —
MIC_DAT is U25-1↔U1-37 only). Likely still decodes (line holds last level →
½-clock sample shift) but it is off-strap: either move SELECT to VDD (left)
or read the right slot. Bench-verify.

## F-04 — WARN (firmware-documented): 2.048 MHz PDM clock sits in IM69D130 operating-mode band gap

DS Table 4 (p.7): operating-mode bands 400–950 kHz, 1.05–1.9 MHz, 2.1–2.65
MHz, 2.9–3.3 MHz; PDM interface accepts 0.4–3.3 MHz. Firmware produces
16 kHz × 128 = **2.048 MHz** (`board_pins.h:106`) — inside the 1.9–2.1 MHz
gap: the mic will stream but no acoustic spec is guaranteed. Firmware already
documents this and proposes 24 kHz × 128 → 3.072 MHz (the DS headline test
clock). No board change possible; resolve in firmware before relying on
mic amplitude specs.

## F-05 — MINOR: RTC_INT has no MCU route; CLKOUT NC

RTC_INT (U24-2) → R112 10 kΩ pull-up (required, open-drain — AppMan §2.2) but
the net terminates at **TP28 only** — the RTC cannot interrupt/wake the host;
firmware polls by design (`vq_rv3028.c:5`). CLKOUT (pin 1) is NC; default
CLKOE=1 outputs 32.768 kHz into nothing — firmware parks it via EEPROM
FD=111 (`vq_rv3028.c`). Both OK by design; flagged for completeness.

## F-06 — MINOR: tented vias under U14 LGA body

(20.2, 36.8) `CHG_STAT` and (19.75, 36.05) `AD5940_RESET` sit under the LGA
outline; Top Solder gerber shows no mask openings → tented. Acceptable DFM;
note per TN0018-style guidance to avoid vias under LGA.

## F-07 — MINOR: W25Q512 local bypass is far

VCC pad 8 at (28.805, 48.117); nearest 100 nF on +3V3: C78 (22.96, 40.32)
≈9.7 mm, C91 ≈8.6 mm. Power-layer plane mitigates; recommend a closer cap.

## PASS items (verified, no action)

- **U14 pin functions all correct** (DS14764 §3 Table 2 p.11): CS=+3V3 → I2C
  mode; SDO/TA0=GND → addr 0x6A = firmware; SDx/SCx→GND per DS; NC pins
  10/11 left unconnected per note 2; INT2 NC (output, safe); VDD/VDDIO 3.3 V
  in range (1.71–3.6 / 1.62–3.6); LGA pad *positions* match the 4-3-4-3 grid,
  0.5 mm pitch (gerber-verified).
- **U18**: /CS w/ R52 10 kΩ pull-up; **/WP and /HOLD hard-tied +3V3 — not
  floating** (firmware confirms, `w25q512.c:4`); EP=GND allowed by DS §9.1
  p.86; no vias under EP (GND via at 24.732,44.570 is 0.018 mm outside pad
  edge — trivial); 4 unnumbered pads = F.Paste stencil splitting, correct;
  pinout 1-8 matches DS §3.1/3.2 exactly; standard-SPI firmware consistent
  with IO2/IO3 tied high (Quad mode impossible by design — intended).
- **U23**: pinout 1=SDA/2=SCL/3=VDD/4=VSS matches DS §5.4; 0.8 mm pad pitch,
  0.5×0.3 lands; C77 100 nF ≈1.6 mm; VDD 1.08–3.6 V; addr 0x44 = firmware;
  bus pull-ups R19/R20 2.2 kΩ > DS min 390 Ω (§3).
- **U24**: all 8 pins match DS/AppMan: VBACKUP→**10 kΩ to VSS exactly per
  AppMan §2.2 p.9** (R115); EVI→GND per "should not be left floating";
  INT open-drain pulled; VDD 1.2–5.5 V ✓; land pattern is exactly the DS
  "recommended solder pad" (0.5×0.8 mm @ 0.9 mm pitch, 3.2×1.5 pkg);
  I2C addr 0x52 fixed = firmware.
- **U25**: footprint is byte-identical pad geometry to KiCad official
  `Infineon_PG-LLGA-5-1` (verified vs gitlab master); pins 1=DATA/2=VDD/
  3=CLOCK/4=SELECT/5=GND per Table 7; acoustic port NPTH **0.8 mm = DS §7
  recommendation exactly**; VDD 1.62–3.6 V; C39 100 nF ≈2.9 mm; DATA line
  single-mic — no contention; custom F.Paste pads supply stencil apertures
  over pads 1–4 and the GND ring (pads deliberately have no own paste).
- No I2C address conflicts on the +3V3 bus (0x20,36,39,3A,44,48,49,4A,52,6A,
  76 all distinct per `board_pins.h:22-33`).

## UNVERIFIABLE / attempted

- ST **TN0018** LGA land-pattern doc (referenced by DS14764 p.159) not
  fetched — st.com 403s curl; verified pad geometry against package pad dims
  (0.25×0.475, p.159 fig.25) and the shipped gerber instead.
- SHT4x Fig. 13 land-pattern numbers are image-only in the PDF text layer;
  pin pitch 0.8 mm / pin size 0.3×0.3 confirmed via Sensirion SHT3x→SHT4x
  transition guide; board pads consistent.
- microcrystal.com refused curl twice (exit 92) → RV-3028-C7 datasheet via
  download.mikroe.com, App-Manual via cdn.sparkfun.com (sha256 in manifest).
- IM69D130 L/R edge timing lives in DS Fig. 9 (image); L/R↔SELECT polarity
  established from DS Fig. 10 + Infineon official support statement, not from
  the timing figure itself.
