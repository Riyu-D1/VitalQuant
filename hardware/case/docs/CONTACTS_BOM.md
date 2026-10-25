> **v22/case_v10 note:** this document predates the pogo-electrode
> restoration — the 'wired leads / no skin contacts' design it
> describes is superseded. The land↔net table below is still the
> board-truth reference; in case_v10 every listed land is reached
> by a spring pogo through its floor aperture (see ASSEMBLY.md and
> FIT_REPORT.md), and J3×2 brings the count to 20.

# VitalQuant case_v8 — wired electrode BOM

The current enclosure has **no conductive skin pads or electrode guide holes**. It routes 18 distinct B.Cu lands (J5/J6/J7/J11/J13) to an 8 × 2.9 mm +y edge exit. This file lists suggested external consumables; the 18-wire harness is a prototype assembly, not included in the printed case.

## External electrodes and cable options

| Qty | Item | Manufacturer / part | Source / status | Notes |
|---:|---|---|---|---|
| As needed | Disposable snap ECG electrode, 32 mm | 3M Red Dot `2268-3`; product ID `7100212948`; 3 per bag | https://www.solventum.com/en-us/home/f/b00037611/ ; distributor example https://www.graylinemedical.com/products/3m-healthcare-red-dot-ecg-monitoring-electrodes-ecg-electrode-red-dot-pediatric-3bag-2268-3 | Verify availability, snap size and intended application. Disposable electrode only; not supplied with a lead. |
| 10 leads/pack | Snap-on electrode cable | OpenBCI Snap Electrode Cables, 26 AWG, 1.45 mm OD; 0.5/1.0/1.5 m | https://shop.openbci.com/products/snap-electrode-cables-emg-ecg-eeg (sold out when checked) | Pack has female header ends and no electrodes; not bare-wire leads. A compatible header-to-wire transition or a purpose-made snap-to-bare-wire cable is needed. |
| 18 leads (if separately fabricated) | Flexible insulated stranded wire | 30 AWG nominal; select insulation with measured OD ≤0.8 mm | Prototype consumable, source to be selected | One separate, labeled conductor per populated signal; inspect solderability and insulation. |
| 2 | Cable-tie strain relief | Small nylon cable tie to suit integral posts | General assembly consumable | Tie the harness at the two modeled posts; avoid crushing wire insulation. |
| 1 | Skin-zone gasket | TPU 95A printed sheet, 0.40 mm | Printed `gasket.stl` | Existing optical/thermal/mic openings only; no electrode holes. |
| 1 | Thermal slug | C110 copper, plated, per `thermal_slug.stl` | Custom machining | Face z=−0.60 mm (0.60 proud); separate from electrode contacts. |

**Compatibility caveat:** the listed 3M snap electrode and cable are candidate parts, not a verified mating pair. Confirm snap dimensions and connector sex before purchase. Do not connect the snap electrodes directly to board lands or tie signal nets together. Circuit topology and patient protection require independent review.

## 18-land wiring map

Each row is electrically distinct. The chest/palm site is an engineering starting point only; use the application-specific placement after circuit and safety review.

| PCB land | Net | Suggested use | Initial ECG-only build |
|---|---|---|---|
| J5.1 | `ECG1_PAD` | ECG input; upper-right chest | Separate electrode |
| J5.2 | `ECG2_PAD` | ECG input; lower-left torso, several cm from ECG1 | Separate electrode |
| J5.3 | `RLD_PAD` | RLD/reference; lower-right torso | Separate electrode; review protection |
| J5.4 | `AFE_P_PAD` | Auxiliary AFE positive | Insulate / leave open |
| J5.5 | `AFE_N_PAD` | Auxiliary AFE negative | Insulate / leave open |
| J6.1 | `EDA_CE_PAD` | EDA counter; palm/thenar | Insulate / leave open |
| J6.2 | `EDA_RE_PAD` | EDA reference; nearby palm site | Insulate / leave open |
| J6.3 | `EDA_SE_PAD` | EDA sense | Insulate / leave open |
| J6.4 | `EDA_DE_PAD` | EDA drive | Insulate / leave open |
| J7.1 | `BIOZ_FP_PAD` | BioZ outer drive+ | Insulate / leave open |
| J7.2 | `BIOZ_FN_PAD` | BioZ outer drive− | Insulate / leave open |
| J7.3 | `BIOZ_SP_PAD` | BioZ inner sense+ | Insulate / leave open |
| J7.4 | `BIOZ_SN_PAD` | BioZ inner sense− | Insulate / leave open |
| J11.1 | `J11_WE` | Sweat working electrode | Insulate / leave open |
| J11.2 | `J11_RE` | Sweat reference electrode | Insulate / leave open |
| J11.3 | `J11_CE` | Sweat counter electrode | Insulate / leave open |
| J13.1 | `J13_INP` | Auxiliary input positive | Insulate / leave open |
| J13.2 | `J13_INM` | Auxiliary input negative | Insulate / leave open |

For an ECG-only prototype, connect J5.1/J5.2/J5.3 to three separate electrodes. Cap each unused lead independently; never short two unconnected nets together. Other functions may require their entire paired/grouped topology and firmware configuration.

## Assembly and electrical acceptance

- Use magnification to solder each wire to its named land. Keep solder and stripped conductor within the land; do not span neighboring lands. Use flexible wire and strain relief at the enclosure exit so cable flex is not carried by the solder joints.
- With the PCB unpowered, test continuity end-to-end for every connected net, then test pairwise isolation between adjacent lands/leads and record results. Verify there are no solder bridges. After shell closure and gentle cable flex, repeat continuity and isolation checks.
- Confirm the 18-wire lead bundle fits the 8 × 2.9 mm modeled edge exit and route without pinching; actual harness fit is a physical check.
- A CAD fit pass does not validate electrode placement, analog functionality, patient protection, skin suitability or clinical use. Conduct circuit/safety review and bench validation before any human-wear test.
