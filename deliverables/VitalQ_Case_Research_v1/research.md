# VitalQ enclosure research — existing chest/patch wearables

Scope: how shipping chest/patch biosensors solve attachment, sealing,
charging, skin contact and comfort — and what that implies for the VitalQ
case (`case/case.py`: 50.3×74.3 mm two-part shell, ~11.5 mm apex, R150
chest curvature, M2 screw ears + strap loops, FDM PETG today).

Devices are grouped by archetype: **disposable adhesive patches**
(Zio XT, VitalPatch, BX100, BioButton, CAM), **reusable strap pods**
(Polar H10, Garmin HRM-Pro, Movesense MD), **garment-integrated**
(Hexoskin, Whoop Body), and **adjacent references** (Withings BPM Core,
Biostrap, Corsano bracelet).

---

## Disposable adhesive patches

### iRhythm Zio XT — single-lead ECG patch
- **Form:** elongated lozenge, **132 × 51 × 14 mm, 24.5 g**. Worn on the
  upper-left chest. Continuous 1-ch ECG up to **14 days**, single-patient
  use, returned by mail for analysis.
- **Attachment:** integrated adhesive wings (skin adhesive); one embedded
  electrode acquires the ECG — the "electrode" is part of the disposable
  assembly, not a user-replaceable snap.
- **Materials:** medical-grade polymer housing, latex-free.
- **UI:** one patient event **button**, green/orange **LED** visible only
  at activation.
- **Water:** shower-tolerant per IFU (wear through sleep and showering;
  face away from direct water); not rated for submersion.
- **Lesson:** electronics pod sits *inside* a much larger adhesive footprint —
  the rigid core (~55 mm) floats on a flexible adhesive skirt that does the
  actual skin conforming. We can copy that trick without being disposable.
- Sources:
  https://www.irhythmtech.com/content/dam/irhythm/united-states/ifus/2025/october/NLB0020.10%20(N100A4010.10)%20-%20ZIO%20XT%20CLINICAL%20REFERENCE%20MANUAL.pdf
  https://fccid.io/m/db1a1f5788c3216c26b731ac28d955a40d180ee6c060213dbe8aaf54e1193ab9.pdf

### VitalConnect VitalPatch — multi-vital patch (hospital RPM)
- **Form:** **120 × 41 × 9.5 mm, 13 g**; worn on upper chest, fully
  disposable. Measures ECG, HR/HRV, respiration, skin temp, posture/fall,
  steps.
- **Attachment/battery:** medical adhesive; **zinc-air 1.4 V** primary
  cell, 120 or 168 h — no charging at all.
- **Sealing:** **IP24** (splashing) / **IP27** claim (temporary immersion,
  though radio won't work submerged).
- **Materials:** soft thermoplastic patch; single-patient use.
- **Lesson:** 9.5 mm thick and 13 g is the comfort bar for adhesive wear;
  achieved by ditching recharge (primary cell) and any connector.
- Sources:
  https://vitalconnect.com/docs/ifu006/RevAB/IFU-06_RevAB_VitalPatch.pdf
  https://www.accessdata.fda.gov/cdrh_docs/pdf14/K141167.pdf
  https://vitalconnect.com/docs/mkt084/revD/MKT-084_RevD.pdf

### Philips Biosensor BX100 — COVID-era disposable patch
- **Form:** **96 × 61 × 6.2 mm, ~10 g** — the flattest device surveyed.
- **Battery:** **CR2032 coin cell**, ~5 days, disposable (no charging, no
  cleaning).
- **Sealing:** **IP27**; survives 1 m drop (IEC 60068-2-27).
- **Regulatory:** **defib-proof CF applied part** — the strictest
  patient-contact class; applies 16 µA / 40 kHz patient auxiliary current.
- **Lesson:** a CR2032 class battery halves thickness vs a pouch Li-ion;
  if VitalQ ever makes a truly disposable variant, coin-cell +
  weld-shut shell is the route.
- Sources:
  https://fcc.report/FCC-ID/2AOGX-617PCBJ12Z/4410213.pdf
  https://tadviser.com/index.php/Product:Philips_Biosensor_BX100

### BioIntelliSense BioButton / BioSticker — coin-cell patch
- **Form:** BioButton **41.2 × 37.5 × 5.7 mm, 9.4 g** (later rev
  42.4 × 40 × 8.5 mm, 11.6 g); BioSticker **81.8 × 37.8 × 8.2 mm, 23 g**.
- **Battery:** **CR1632 140 mAh** coin cell → up to 60 days; a
  rechargeable Li-ion variant exists (7–30 d, 300 cycles) charged on a
  dock.
- **Attachment:** double-sided fabric adhesive ring on medical-grade
  **silicone** housing.
- **Sealing:** **IP47**.
- **Regulatory:** ISO 10993-1 biocompatibility, **IEC 60601-1,
  60601-1-11 (home use), 60601-1-2 (EMC)**, ASTM E1112 (temperature).
- **Lesson:** silicone skin contact + adhesive ring is the standard
  medical-patch material stack; coin cell makes 5.7 mm possible but caps
  the sensor payload (accelerometer-derived HR only — no real ECG front
  end at that thickness).
- Sources:
  https://fda.innolitics.com/submissions/CV/subpart-c%E2%80%94cardiovascular-monitoring-devices/DRG/K212957
  https://www.biointellisense.com/wp-content/uploads/2026/05/IFU-BBR-2022-Ver.6.pdf
  https://fda.innolitics.com/device/K241101

### Bardy CAM patch — P-wave-optimised ECG
- **Form:** **178 × 38 × 14 mm, <25 g**, distinctive slim **hourglass**
  shape worn **vertically along the sternum** — placement over the heart
  maximises P-wave amplitude (their core clinical claim vs Zio).
- **Attachment:** long-term adhesive, 2 gel electrodes with internal lead
  wires (11.6 cm), disposable.
- **Materials:** medical-grade thermoplastic polymer, UL-HB.
- **Sealing:** only **IP23** — spray at 60° from vertical. **BF** applied
  part. Coin cell (lithium primary, <1 g), 2/7/14-day variants.
- **Lesson:** electrode spacing drives outline — the hourglass is a
  consequence of maximising electrode baseline while minimising width
  (compliance under breasts/pecs). Our ECG electrode positions should
  decide the case's long axis, not the board.
- Sources:
  https://www.bardydx.com/wp-content/uploads/2025/01/DWG000781B-CAM-Instructions-for-Use.pdf
  https://www.bardydx.com/wp-content/uploads/2025/01/DN000601A-14Day-Half-fold-CAM-Brochure.pdf

---

## Reusable strap pods

### Polar H10 — chest-strap reference
- **Form:** sensor pod **~65 × 34 × 10 mm, 21 g** (60 g with strap).
- **Attachment:** pod **snaps onto** the elastic strap; the strap fabric
  *is* the electrode carrier — textile electrode pads inside the strap,
  silicone print prevents slip. Strap is machine-washable; pod is not.
- **Materials:** ABS / ABS+GF / PC pod, stainless snaps; PA/PU/elastane
  strap.
- **Sealing/power:** **WR30**; user-replaceable **CR2025** (400 h) behind
  an **O-ring sealed** battery door (silicone 20.0 × 0.9 O-ring) — the
  door is the only opening, and it's gasketed.
- **Lesson:** *the strap carries the electrodes*, so the pod stays
  completely sealed except the coin door. Separating "dirty/elastic" and
  "electronic" parts makes cleaning trivial — the strap goes in the wash.
- Sources:
  https://support.polar.com/e_manuals/h10-heart-rate-sensor/polar-h10-user-manual-english/technical-specifications.htm
  https://www.polar.com/en/sensors/h10-heart-rate-sensor?sku=92075957

### Garmin HRM-Pro / Pro+ — same pattern
- **Form:** module **29.6 × 53.7 × 8.6 mm**; ~52 g with strap.
- **Attachment:** snap-on to elastic strap with in-strap electrodes.
- **Sealing/power:** **5 ATM**; CR2032 ~12 months, **tool-free battery
  door**.
- **Lesson:** same snap-module architecture as Polar; adds running
  dynamics via accelerometer. Confirms 8–10 mm is the accepted pod
  thickness *when the electrodes live in the strap*.
- Source: https://thewearify.com/polar-h10-vs-garmin-hrm-pro-plus/

### Movesense MD — medicalised pod (closest analogue to VitalQ)
- **Form:** **Ø36.6 mm, 7.8–10.6 mm thick, 9.4 g** — a small puck that
  clips into **adapters**: chest-strap holder, adhesive patch frame, etc.
- **Sealing/power:** **IP68** (1 m/1 h; marketing sheet says 30 m);
  user-replaceable CR2025 under a twist-lock back.
- **Regulatory:** **EU MDR Class IIa**, Type **BF** applied part,
  IEC 60601-1, ISO 13485 manufacturing; explicitly approved for home +
  professional environments and oxygen-rich use.
- **Lesson:** the *sensor is a sealed puck*; attachment is a replaceable
  mechanical interface. For VitalQ this suggests: seal our shell properly
  and let straps/adhesive frames be cheap interchangeable parts.
- Sources:
  https://www.movesense.com/product/movesense-medical-mdr/
  https://www.movesense.com/wp-content/uploads/2024/05/Movesense-MD-sensor-IFU-3_V6.0.pdf
  https://www.movesense.com/wp-content/uploads/2023/04/Movesense-Medical-Spec-Sheet-2.0-03-2023.pdf

---

## Garment-integrated & adjacent

### Hexoskin — sensors woven into a shirt
- **Form:** electronics pod **13 × 42 × 72 mm, 40 g** pockets into the
  shirt side; shirt carries **3 silver-plated nylon textile electrodes**
  (Lead I / CC5) and 2 RIP respiration bands; conductive cream recommended
  on the electrodes.
- **Power/UI:** rechargeable via USB cable, 36 h battery, 30-day
  store-and-forward memory, one button + 3 LEDs (battery/rec/BLE).
- **Lesson:** textiles-as-electrodes need skin moisture/cream; the rigid
  module stays *off* the sternum entirely (side pocket) — comfort via
  relocation rather than miniaturisation.
- Sources:
  https://hexoskin.com/products/hexoskin-smart-device
  https://support.hexoskin.com/placement-of-sensors-hexoskin-smart-shirts-proshirts
  https://support.hexoskin.com/hubfs/Hexoskin%20User%20Guide.pdf

### Whoop 4.0 + Whoop Body — pod relocates into garments
- **Form:** sensor **36 × 25 × 10.1 mm, 11.3 g** (28.5 g with band);
  Any-Wear garments have pockets/pod mounts at torso positions.
- **Sealing/power:** **IP68** (10 m/2 h); 192 mAh ~5 days;
  **slide-on waterproof battery pack** charges on-body — no port, no
  removal.
- **Lesson:** charging-while-wearing eliminates the "off-body charging
  gap" in continuous monitoring; a sealed sensor with a dock/pack is the
  only architecture that gets both IP68 and rechargeability.
- Sources:
  https://www.whoop.com/us/en/press-center/introducing-4-0-whoop-body-any-wear-technology/
  https://gadgetsandwearables.com/technical-specs/whoop-band-4-0/

### Withings BPM Core — dry stainless electrodes (different use-case)
- **Form:** cuff monitor, 430 g; not chest-worn but the best dry-electrode
  reference: **3 stainless-steel electrodes** (2 inside the cuff + 1 on
  the tube the user grips) record a spot-check Lead-I-style ECG; silicone
  membrane + stainless support for the stethoscope; micro-USB, ~6-month
  battery.
- **Lesson:** stainless dry electrodes work for *spot* checks where the
  user can maintain pressure; for continuous chest wear, dry metal on
  moving skin = motion artefact — hence every continuous monitor uses gel
  hydrogel or textile.
- Sources:
  https://www.withings.com/en-eu/products/bpm-core
  https://support.withings.com/hc/article_attachments/360018608677/BPM_Core_User_Guide_EN.pdf

### Biostrap EVO / Kairos — optical wrist reference
- **Form:** wrist sensor ~**19 × 51 × 13 mm**; food-grade silicone strap;
  shoe-pod accessory 35 × 23 × 15 mm, 8 g.
- **Sealing/power:** **IP68** / 5 ATM claims; **wireless inductive
  charging** puck (no contacts at all); 2–5 days battery.
- **Lesson:** Qi-class charging removes contacts entirely — possible for
  VitalQ but power budget and coil area on a curved case make pogo pins
  more realistic.
- Sources:
  https://outliyr.com/biostrap-evo-review
  https://www.alexfergus.com/blog/biostrap-evo

### Corsano CardioWatch 287-2 — regulatory template
- **Form:** wrist bracelet **~42 × 25 × 10 mm, 19 g**.
- **Sealing/power:** **IP66** (shower-proof, not swim); **magnetic
  pogo-pin USB cable** — polarised magnets snap the head onto contacts;
  LiPo 140 mAh, ~1 week.
- **Regulatory (the full stack we should target):** CE MDR Class IIa;
  IEC 60601-1, -1-2, **-1-11 (home use)**, -2-47 (ambulatory ECG);
  ISO 10993-1; ISO 14971 risk; IEC 62304 software; ISO 80601-2-56/-2-61;
  BF applied part.
- **Lesson:** magnetic pogo charging is the field-proven middle path
  between open USB-C and Qi; the magnet itself provides alignment and the
  contacts stay on the *inside* of the seal.
- Sources:
  https://corsano.com/wp-content/uploads/2025/10/EN-IFU-Corsano-CardioWatch-287-2-Bracelet-v17.pdf
  https://corsano.com/wp-content/uploads/2024/01/Corsano-287-2-Leaflet.pdf
  https://corsano.com/wp-content/uploads/2024/01/Corsano-287-2-Specsheet-v1.2.pdf

---

## Cross-cutting findings

**Two architectures dominate:**
1. *Disposable adhesive patch* (Zio, VitalPatch, BX100, CAM): primary
   battery, zero ports, gel electrodes on the adhesive layer, foam/film
   laminate construction, wear 5–14 days, mail-back. Thin (5–10 mm) and
   light (10–25 g) but not reusable.
2. *Reusable sealed pod + strap* (H10, HRM-Pro, Movesense, Whoop): the
   pod is sealed (coin door O-ring or fully potted), electrodes live in
   the strap or snap onto the pod; coin cells dominate because
   rechargeable + sealed is hard without pogo/Qi.

VitalQ sits between these: rechargeable multi-sensor pod → closest
living relatives are **Movesense MD** (puck + attachment interface) and
**Corsano** (magnetic pogo charging, full 60601 stack).

**Sealing ladder observed:** IP23 (CAM) < IP24/27 (VitalPatch) < IP47
(BioButton) < IP66 (Corsano) < IP68 (Movesense/Whoop/Biostrap). Anything
with an exposed connector tops out ~IPx4; sealed pods with gasketed doors
or contact charging reach IP6x.

**Electrodes:** continuous-wear devices → **hydrogel gel electrodes**
(integrated into adhesive, or snap studs accepting standard ECG snaps)
or **textile electrodes + moisture** (Hexoskin, Polar). Dry stainless
appears only in spot-check devices (BPM Core).

**Skin-contact materials:** medical-grade silicone, hydrocolloid/acrylic
adhesives, or textile — all qualified under **ISO 10993-1/-5/-10**
(cytotoxicity, sensitisation, irritation). FDM PETG skin contact is a
prototyping convenience, not a shipping material.

**Standards to design toward:** IEC 60601-1 (basic safety), -1-2 (EMC),
**-1-11 (home-use environment)** — the one that drives enclosure
ingress/mechanical specs — and -2-47 (ambulatory ECG). ISO 10993 for
skin contact, ISO 14971 risk file, BF applied part is the realistic class
(CF only if defib-protected like BX100).

## Comparison table

| Device | Size (mm) | Thick | Weight | Attachment | Electrodes | Sealing | Battery / charging | Class |
|---|---|---|---|---|---|---|---|---|
| **Zio XT** | 132×51 | 14 | 24.5 g | adhesive wings | 1 integrated gel | shower-OK | zinc primary, none (14 d disposable) | Rx patch, 510(k) |
| **VitalPatch** | 120×41 | 9.5 | 13 g | adhesive | gel, integrated | IP24/27 | zinc-air, none (5–7 d) | Rx disposable |
| **Philips BX100** | 96×61 | 6.2 | 10 g | adhesive | integrated | IP27 | CR2032, none (5 d) | **CF** applied part |
| **BioButton** | 41×38 | 5.7 | 9.4 g | adhesive ring | none (accel+thermistor) | IP47 | CR1632 / Li-ion dock | IEC 60601 stack |
| **Bardy CAM** | 178×38 | 14 | <25 g | adhesive | 2 gel, wired | IP23 | coin cell (2/7/14 d) | BF, Rx |
| **Polar H10** | 65×34 pod | 10 | 21 g pod | strap snaps | textile in strap | WR30 | CR2025, O-ring door | consumer sport |
| **Garmin HRM-Pro+** | 30×54 pod | 8.6 | ~52 g w/strap | strap snaps | textile in strap | 5 ATM | CR2032, tool-free door | consumer sport |
| **Movesense MD** | Ø36.6 | 8–10.6 | 9.4 g | holder/adapters | via strap/snap adapter | **IP68** | CR2025 twist door | **MDR IIa, BF** |
| **Hexoskin** | 13×42×72 pod | 13 | 40 g | shirt pocket | 3 textile + cream | — | Li-ion, USB cable | research/clinical |
| **Whoop 4.0** | 36×25 | 10.1 | 11.3 g | band/garment pod | PPG (optical) | **IP68** | 192 mAh, slide-on pack | consumer |
| **Biostrap EVO** | 19×51 | 13 | ~10 g | wrist band | PPG | IP68/5ATM | Li-ion, Qi puck | consumer |
| **Corsano 287-2** | 42×25 | 10 | 19 g | wrist band | PPG | IP66 | LiPo 140 mAh, **mag pogo** | **MDR IIa, BF** |
| **VitalQ today** | 55.5×78.4 | ~11.5 | — | strap loops | via end-notch tails | **none** (open USB + notch) | 452535 LiPo, USB-C | prototype |

## What this means for VitalQ

1. **We're port-exposed.** Every device above IPx4 either has no connector
   or a gasketed/contact charging scheme. Our open USB-C slot is the
   sealing ceiling — see IDEAS.md.
2. **Pods get thin by moving electrodes off the pod.** If our gel
   electrodes live on an adhesive skirt/strap (wired via the existing
   tail pads), the shell only needs the optical window — matching the
   sealed-pod pattern.
3. **The adhesive skirt is the universal comfort trick.** Zio/CAM/VitalPatch
   all conform via a large thin flexible flange, not by curving the rigid
   pod. Our R150 curved rigid body + a TPU film skirt ≈ same solution.
4. **Strap loops are right for a strap form factor** but nothing surveyed
   uses pass-through loops on a hard shell — pods snap into holders so
   the strap can swap/wash independently.
5. **Coin-cell disposable vs LiPo rechargeable is the fork**: our board
   is rechargeable-first, so target the sealed-pod pattern (pogo dock)
   rather than trying to out-thin VitalPatch.
