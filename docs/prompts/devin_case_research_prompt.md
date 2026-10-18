# Task: research existing chest/patch wearables and ideate improvements to the VitalQ case

Work in ~/VitalQuant-place (branch devin/hw-v2-pcb). A local commit is fine. Do NOT push.
Put outputs in a NEW folder: ~/Downloads/VitalQ_Case_Research_v1/

## 1. Deep research: research.md, with source URLs
Research existing chest-worn and patch biotech wearables and their enclosures, for example:
- Zio XT/AT (iRhythm)
- VitalConnect VitalPatch
- Philips Biosensor BX100
- BioIntelliSense BioButton
- Bardy CAM
- Chest straps: Polar H10, Garmin HRM-Pro, Movesense
- Hexoskin
- Withings BPM Core
- Biostrap
- Corsano
- Whoop (body garments)

For each device, cover:
- Form factor, dimensions and thickness, and weight.
- Attachment (adhesive, strap or clip), and how it handles electrodes and sensor windows.
- Materials (PC/ABS, TPU or silicone overmould, medical adhesive).
- Sealing and IP rating.
- Charging method (USB-C, pogo pins, Qi), plus buttons and LEDs.
- Skin comfort, cleaning and disinfection.
- Relevant standards: IEC 60601-1, IEC 60601-1-11 (home use), ISO 10993 (biocompatibility).

Finish with a comparison table.

## 2. Ideas: IDEAS.md
Ideate concrete improvements to OUR case (~/VitalQuant-place/case/case.py, the latest version with the emblem). Do NOT depend on the PCB layout, which is not final (Quilter is laying it out). Cover:
- Ergonomics and chest-curvature fit, and a thinner or tapered profile.
- Strap attachment and quick-release.
- Skin-side contact and electrode strategy (snap electrodes, dry electrodes, a flush optical window).
- Waterproofing and IP rating (gaskets, ultrasonic welding vs screws).
- Charging (pogo-pin cradle vs USB-C, and how to seal it).
- An antenna/RF keep-out for the ESP32, and thermal behaviour.
- Materials, and printability now vs injection moulding later.
- A light pipe for the status LED, a button, aesthetics and the emblem.

Rank every idea by impact vs effort, in a table. Clearly flag which ideas would need PCB changes.

## 3. Optional concept variants
Optionally build 2–3 quick concept variants in build123d as SEPARATE scripts (e.g. case/concepts/variant_a.py). Do NOT overwrite case.py. Include iso, top and bottom renders of each in the output folder.

## Report
When done, report the folder contents, the top 5 ideas, and which of them need PCB changes.
