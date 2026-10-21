# U21 — USBLC6-2SC6 (USB ESD array, SOT23-6) — Round 6 full audit

Datasheet: `verify/datasheets_r6/usblc6_st_real.pdf` — ST USBLC6-2, DocID11265 Rev 5, 14 pp, real PDF via LCSC C7519.
Reference: DS §1 fig 1 pinout (1/6=I/O1, 3/4=I/O2, 2=GND, 5=VBUS), Table 1–2 ratings p2–3 (VRM 5.25 V, VBR 6 V, IEC61000-4-2 L4).

## Pin-by-pin audit

| Pin | Pad | DS requirement | Actual net | Parts | Decap | Verdict |
|-----|-----|----------------|-----------|-------|-------|---------|
| 1 | I/O1 | D+ through-pin (flow-thru to 6) | USB_DP | J1.6, R102 0Ω→U1 USB D+ | — | OK |
| 2 | GND | gnd | GND | — | — | OK |
| 3 | I/O2 | D− through-pin | USB_DM | J1.5, R103 0Ω→U1 | — | OK |
| 4 | I/O2 | D− through-pin | USB_DM | — | — | OK |
| 5 | VBUS | VBUS clamp ≤5.25 V working | VBUS | J1.1, U2/U5 VBUS, C1/C10 | C10 1µ/C1 4.7µ nearby | OK |
| 6 | I/O1 | D+ through-pin | USB_DP | — | — | OK |

## Absolute-max vs power states
VRM 5.25 V; VBUS 5.0/5.25 V ✓; clamp VCL 12–17 V @1–5 A keeps J1-side ESP32 USB pins within their 5 V-tolerant/ESD spec. No supply pins — passive device. All power states trivially bounded. Skin-fault n/a (connector-side device, which is exactly its purpose).

## Sequencing / passives
None required. I/O capacitance 2.5–3.5 pF, ΔC 0.015 pF — USB2 FS/HS compatible; ESP32-S3 native USB FS unaffected. Leakage ≤150 nA — negligible on battery.

## Findings
None. 6/6 pins verified against real ST datasheet (previously only TPD1E10B06 D* diodes were documented).
