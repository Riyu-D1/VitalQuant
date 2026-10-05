# hw_v2 — New-parts BOM (delta vs `pcb` branch)

All parts added in hw_v2 that did not exist on the `pcb` branch board.
Per board. DNP = designed in, not populated at assembly (order only if
you want them on hand for experimentation).

## ICs & modules

| Ref | Value | Orderable MPN | Manufacturer | LCSC | Qty | Status |
|---|---|---|---|---|---|---|
| U1 | ESP32-S3-MINI-1-N4R2 | ESP32-S3-MINI-1-N4R2 | Espressif | C3013941 | 1 | **CHANGED** — replaces WROOM-32E-N8R2 |
| U22 | MAX86178ENJ+ | MAX86178ENJ+ | Analog Devices | — | 1 | Fitted (NDA part; order via ADI) |
| U23 | SHT45-AD1B | SHT45-AD1B-R2 | Sensirion | — | 1 | Fitted |
| U24 | RV-3028-C7 | RV-3028-C7 | Micro Crystal | — | 1 | DNP |
| U25 | IM69D130 | IM69D130V01XTSA1 | Infineon | — | 1 | DNP |

## Optical

| Ref | Value | Orderable MPN | Manufacturer | LCSC | Qty | Status |
|---|---|---|---|---|---|---|
| D12 | 730 nm NIR emitter | SFH 4735 | ams OSRAM | — | 1 | Fitted (emitter choice VERIFY in design) |
| Q4 | N-FET, D12 gate | CSD13380F3T | Texas Instruments | C2871092 | 1 | Fitted |

## Connectors

| Ref | Value | Orderable MPN | Manufacturer | Qty | Status |
|---|---|---|---|---|---|
| J12 | 14-pos FFC 0.5 mm | FH12-14S-0.5SH(55) | Hirose | 1 | DNP |
| J9, J10, J11, J13 | Tail pads | — (bare pads, no part) | — | — | Nothing to order |

## ESD protection

| Ref | Value | Orderable MPN | LCSC | Qty | Status |
|---|---|---|---|---|---|
| D26–D30 | TPD1E10B06 | TPD1E10B06DPYR | — | 5 | **DNP** (fit for human-subject use) |

## Capacitors

| MPN | Value | Pkg | Refs | Qty fitted | DNP | LCSC |
|---|---|---|---|---|---|---|
| CL10A476MQ8NRNC | 47 µF 6.3V X5R | 0603 | C85 | 1 | 0 | C19702 |
| GRM155R60J106ME05 | 10 µF | 0402 | C30 | 1 | 0 | — |
| GRM155R61A105KE15 | 1 µF | 0402 | C80, C82, C83, C84 | 4 | 0 | — |
| GRM155R71C104KA88 | 100 nF | 0402 | C76, C79, C81 + C77, C78 | 3 | 2 | — |

## Resistors

| MPN | Value | Pkg | Refs | Qty fitted | DNP | LCSC |
|---|---|---|---|---|---|---|
| 25121WJ0000T4E | 0 Ω (HV cut-points) | 2512 | R118–R122 | 5 | 0 | C2908946 |
| 0402WGF0000TCE | 0 Ω strap links | 0402 | R102, R103, R106, R114 + R104, R105 | 4 | 2 | C17168 |
| RC0402FR-07100RL | 100 Ω | 0402 | R109 | 1 | 0 | — |
| RC0402FR-07100KL | 100 kΩ | 0402 | R110 + R107, R108 | 1 | 2 | C25741 |
| RC0402FR-0710KL | 10 kΩ | 0402 | R113, R116, R117 + R112, R115 | 3 | 2 | C25744 |

## Other

| Ref | Item | Qty | Notes |
|---|---|---|---|
| TP28 | Test point (RTC_INT) | 1 | Board feature, no orderable part |

## Order summary for CFO

**Fitted — buy now (per board):**
- 1× ESP32-S3-MINI-1-N4R2 (C3013941, ~US$5)
- 1× MAX86178ENJ+ (ADI — **NDA-restricted, longest lead item**)
- 1× SHT45-AD1B-R2
- 1× SFH 4735
- 1× CSD13380F3T (C2871092)
- Caps: 1× 47µF 0603 (C19702), 1× 10µF 0402, 4× 1µF 0402, 3× 100nF 0402
- Resistors: 5× 0Ω 2512 (C2908946), 4× 0Ω 0402 (C17168), 1× 100Ω, 1× 100k, 3× 10k 0402

**DNP — optional spares:**
- 1× RV-3028-C7, 1× IM69D130V01XTSA1, 1× FH12-14S-0.5SH(55)
- 5× TPD1E10B06DPYR (recommend fitting if worn on skin)
- 2× 100nF, 2× 0Ω 0402, 2× 100k, 2× 10k 0402
