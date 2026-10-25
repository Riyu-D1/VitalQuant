> **v22/case_v10 note:** this document predates the pogo-electrode
> restoration — the 'wired leads / no skin contacts' design it
> describes is superseded. The land↔net table below is still the
> board-truth reference; in case_v10 every listed land is reached
> by a spring pogo through its floor aperture (see ASSEMBLY.md and
> FIT_REPORT.md), and J3×2 brings the count to 20.

# case_v8 — wired disposable-electrode prototype

## Design status

The case has **no conductive skin contacts**. All 18 separate B.Cu signal lands on J5/J6/J7/J11/J13 remain inside the enclosure and are soldered individually to insulated leads. Leads run through recessed floor raceways to an 8 × 2.9 mm opening in the +y wall, below the shell seam; two integral posts provide cable-tie strain relief. J3 is not part of this wired site list. The existing optical, thermal-slug, humidity and microphone openings remain. PCB files are unchanged.

The current model routes nominal 30-AWG lead envelopes; actual solder joints, insulation diameter, routing order and an 18-wire harness must be checked in a physical prototype. Route and insulate one net at a time; do not bridge adjacent lands. Fit, continuity and isolation checks are required before closing the case.

## External electrodes and leads

| Item | Suggested prototype part | Source / notes |
|---|---|---|
| Disposable snap electrode | 3M Red Dot ECG Monitoring Electrode `2268-3`, product ID `7100212948`; 32 mm diameter, 3 per bag | Manufacturer details: https://www.solventum.com/en-us/home/f/b00037611/ ; distributor example: https://www.graylinemedical.com/products/3m-healthcare-red-dot-ecg-monitoring-electrodes-ecg-electrode-red-dot-pediatric-3bag-2268-3 (confirm availability, lead/snap compatibility and intended use before ordering) |
| Snap lead cable | OpenBCI Snap Electrode Cables, 10 color-coded leads, 26 AWG, 1.45 mm OD; select 0.5, 1.0 or 1.5 m | https://shop.openbci.com/products/snap-electrode-cables-emg-ecg-eeg — sold out at time checked; wires terminate in single female headers and do not include electrodes. This is not a bare-wire pigtail; use a verified snap-to-bare-lead equivalent or adapt each lead without altering its electrode connector. |
| Bare lead wire (alternative) | 30-AWG insulated stranded wire, each signal individually identified | Prototype consumable; select flexible insulated wire with measured outside diameter ≤0.8 mm to match nominal raceways. |

3M 2268-3 is a disposable clinical monitoring electrode, not a guaranteed fit for every snap cable. Verify mechanical snap compatibility and electrical hookup before use. This enclosure/prototype is not a validated medical device; this note is not a recommendation for clinical use.

## Land-to-electrode wiring map

Each signal below is a separate connection. The wiring map is a functional prototype suggestion, not a validated clinical placement or circuit prescription. Use electrodes several centimeters apart where the application requires it; the same electrode cannot serve as a shared short between nets.

| PCB land / net | Suggested electrode use / location | Prototype note |
|---|---|---|
| J5.1 `ECG1_PAD` | ECG input, upper-right chest (V1-like) | Active |
| J5.2 `ECG2_PAD` | ECG input, lower-left torso (V5-like) | Active; keep well separated from ECG1 |
| J5.3 `RLD_PAD` | Right-leg drive/reference electrode, lower-right torso | Active; follow AFE safety/current-limit design |
| J5.4 `AFE_P_PAD` | Auxiliary AFE positive input | Leave insulated/unconnected unless its measurement is intentionally configured |
| J5.5 `AFE_N_PAD` | Auxiliary AFE negative input | Leave insulated/unconnected unless paired measurement is configured |
| J6.1 `EDA_CE_PAD` | EDA counter electrode, palm/thenar region | Active for EDA |
| J6.2 `EDA_RE_PAD` | EDA reference electrode, nearby palm site | Active for EDA; keep close to CE as appropriate |
| J6.3 `EDA_SE_PAD` | EDA sense electrode, nearby palm site | Optional; configure the analog front end first |
| J6.4 `EDA_DE_PAD` | EDA drive electrode, nearby palm site | Optional; configure the analog front end first |
| J7.1 `BIOZ_FP_PAD` | BioZ drive+ outer position | Active only for 4-electrode BioZ |
| J7.2 `BIOZ_FN_PAD` | BioZ drive− outer position | Active only for 4-electrode BioZ |
| J7.3 `BIOZ_SP_PAD` | BioZ sense+ inner position | Active only for 4-electrode BioZ |
| J7.4 `BIOZ_SN_PAD` | BioZ sense− inner position | Active only for 4-electrode BioZ |
| J11.1 `J11_WE` | Sweat/working electrode | Optional; verify J11 front-end topology |
| J11.2 `J11_RE` | Sweat/reference electrode | Optional; verify J11 front-end topology |
| J11.3 `J11_CE` | Sweat/counter electrode | Optional; verify J11 front-end topology |
| J13.1 `J13_INP` | Auxiliary input positive | Leave unconnected unless explicitly configured |
| J13.2 `J13_INM` | Auxiliary input negative | Leave unconnected unless paired input is configured |

For an initial ECG-only checkout, wire J5.1–J5.3 with separate electrodes and leave every other lead individually capped/insulated. Add EDA, BioZ and other inputs only after confirming the circuit topology, analog configuration, patient-protection paths and isolation. Do not use the provisional map for human wear until reviewed and tested by qualified engineering/clinical personnel.

## Required build and verification

1. Solder each chosen insulated wire to its specific land; use magnification and minimal solder. Inspect for bridges and strain-relieve the harness without loading the PCB lands.
2. With the board unpowered, verify continuity end-to-end for each intended net and record resistance. Test isolation between every pair of adjacent signal lands and leads; investigate any unexpected path rather than relying on the case model.
3. Route the bundle through the +y wall slot, secure with a suitable cable tie at the two posts, and confirm no pinching at the seam, strap lugs or battery tray. Confirm the cable bundle actually fits the slot and wall/board clearance.
4. Recheck continuity/isolation after closing and after gentle cable flex. Validate front-end function and safety with appropriate test equipment before any wearer evaluation.
5. Run `python3 build_case.py`, `python3 verify_fit.py`, `python3 export_step.py` and `python3 render_views.py` after geometry edits. `CASE8 WIRED COMPLETE` indicates CAD checks only; it is not electrical, clinical or skin-contact approval.
