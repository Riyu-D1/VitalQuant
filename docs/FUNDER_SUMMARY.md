# VitalQ — status summary for reviewers

VitalQ is a chest-worn, multi-sensor **early-warning research prototype**. It
combines several physiological sensing front-ends on one small board so we can
study whether richer measurement surfaces subtle biological changes earlier
than conventional approaches. Work on sepsis warning signs so far is a
**feasibility simulation only** — no patient data, no clinical claims.

This document states facts only; it is not a medical, safety or regulatory
statement.

## The board (fix_v23)

- 8-layer PCB, 55.65 × 81.05 mm outline (50 × 75 mm main body plus antenna
  and connector tabs), ENIG finish, 1.6 mm nominal thickness.
- Main controller: **ESP32-S3-WROOM-1-N16R8** (16 MB flash, 8 MB octal PSRAM,
  Wi-Fi/BLE).
- Sensors and front-ends (from the current BOM):
  - Optical/PPG: TI AFE4900 and ADI/Maxim MAX86141 AFEs; SFH7072 multi-LED,
    SFH4053 IR, JB3030UWT and XL0603NRC730 (730 nm, consigned) emitters;
    AS7341 multi-channel spectral sensor.
  - Physiological: ADS1292R ECG/respiration AFE, AD5940 electrochemical /
    bioimpedance AFE, skin-contact electrode sites, force-sensor contact.
  - Environmental/motion: SHT45 temperature/humidity, BME280
    pressure/humidity, MLX90632 IR temperature, LSM6DSV80X IMU.
- Power: USB-C input, BQ25170 charger, LiPo battery, TPS63802 buck-boost,
  MAX17048 fuel gauge.
- W25Q512 flash, CP2102N USB-UART bridge, Tag-Connect programming land
  pattern.
- Three parts are do-not-populate on this build: J12 (FPC connector),
  U24 (RTC), U25 (MEMS microphone).

## Where each piece stands

- **Board:** design complete. KiCad 10.0.4 measured: DRC 0 errors,
  0 unconnected, 0 shorts, 0 schematic-parity issues; ERC 0 errors.
  Full fabrication package (gerbers, drill, BOM, placement) regenerated and
  cross-checked. **Not yet fabricated** — still needs a fab engineering
  review and bench bring-up before ordering.
- **Case:** enclosure CAD complete; all 141 fit checks pass in simulation.
  Nothing printed; no physical fit test yet.
- **Firmware:** driver contract and three hardware profiles (hw_v0, hw_v1,
  hw_v2) are in the repo. The next profile (hw_v3, WROOM module) and
  firmware v2 sit in an open PR pending bench bring-up.
- **Software:** Python ingest/processing/ML platform with synthetic-data
  generator, Postgres schema, dashboard, and CI tests — runs locally without
  hardware.
- **Web:** marketing/documentation site builds cleanly.

## Open items

- 900 mA worst-case LED pulse needs firmware interleaving (5 V boost
  sustains ~400 mA continuous).
- USB can draw over 500 mA while charging on current-limited hosts.
- No physical prototype; RF/antenna performance and case fit untested on
  hardware.
- One sensor (MLX90632, 1.8 V I²C variant) and one LED are consigned parts.

## What funding would unlock

- First PCB fabrication and assembly run.
- Bench bring-up: power, clocks, programming, sensor-by-sensor validation.
- Case printing and a physical fit test.
- RF/antenna characterisation on the finished board.
- Firmware v2 bring-up against real hardware.
- Design of the first data-collection study.

VitalQ is a research prototype. It is **not a medical device**, and no
safety, sterility or regulatory compliance is claimed.
