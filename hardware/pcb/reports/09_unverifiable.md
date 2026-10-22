# 09 — Unverifiable items, fix_v19 audit

Everything we could not prove, and why. Absence here would itself be a red flag.

## Datasheet/doc gaps
- **U6 AFE4900YZR** — full parametric datasheet SBAS857 is TI selective-disclosure (NDA-ish).
  Pin map + functions verified via public mirror text and prior audit material (quoted in
  03_matrix_afe.csv), but abs-max/timing tables could not be re-verified line-by-line.
  → Residual risk on AFE4900 supply limits; mitigate at bench with rail measurements.
- **D12 XL0603NRC730** (consigned 730nm IR LED, Shenzhen Starsealand) — no public PDF exists;
  manufacturer page is JS-walled. Spec table viewed only (Vf/λ range). Consigned anyway.
- **AD5940 Rev G** — analog.com unreachable (all manufacturer domains blocked: analog, st,
  silabs, microcrystal, mouser-botwall). PDF via cdn.sprinkle mirror, content-verified
  (title/rev/producer/142pp) but unofficial chain-of-custody.
- **RV-3028-C7** — via Wayback (DS v1.3) + texduino mirror (AppMan Rev 1.4). microcrystal.com
  refused connections.
- **TYPE-C-31-M-12.pdf** — vector drawing, no text layer; pin numbering verified from drawing
  geometry, not a text table.
- **SFH7072 / NF2W757G land patterns** — recommended-pad figures are raster-only; mm dims
  not extractable. One-time overlay check recommended at stencil review.

## Tool gaps
- **ERC/DRC re-run** — no `kicad-cli`, no Python `pcbnew` in this environment. Raw v19 checker
  output exists in-repo (`verify/drc_v19.rpt`: 61 warnings, 0 clearance violations — class
  breakdown reconciled vs ROUND9's 48 for v16 in 08_disagreements.md). Independent re-run
  could not be performed; flagged as residual coverage gap.
- **Schematic↔PCB parity check** — same tool limitation; `drc_v19.rpt` lacked
  `--schematic-parity`, which is how the U14/U15 source-vs-gerber divergence survived.
- **J1 USB-C shield/mechanical land** — HRO drawing is the only doc; no full 3D land spec.

## Coverage boundaries (honest scope)
- **466 passive pads are UNVERIFIABLE at per-pad datasheet level** (03_pin_matrix.csv) —
  values verified at BOM level and circuit roles checked where findings cite them; per-pad
  datasheet inspection was scoped to the named ICs, discretes, and connectors.
- **Live distributor stock** — consignment rationale (D10/D12) verified against distributor
  catalog presence, but real-time stock quantities could not be confirmed (digikey/mouser
  bot-walls). Owner should re-check LCSC stock the day of ordering.
- **Analog-domain performance** — SNR, optical crosstalk, electrode input noise are outside
  a pin/netlist audit; register/protocol correctness was verified, physics was not.
- **Medical-safety** — creepage/isolation claims reviewed for net coverage only; this is a
  design review, NOT a safety certification.
- **Gerber↔source divergence scope** — proven: U14 source footprint broken while gerbers are
  correct → the shipped zip cannot be proven to derive from the audited .kicad_pcb (no source
  hash in zip). Object counts per layer are consistent (no staleness signature), but the two
  artifacts may carry other divergences not yet found. Any regenerated zip invalidates this
  audit's gerber-level confirmations — re-verify if the fab package is re-exported.
- **U19 TCA6408 missing centre pads** — flagged UNVERIFIABLE (likely NC thermal pads; pin map
  shows no net requirement).
