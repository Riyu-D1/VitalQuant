# VitalQ logo v2 — recommendation

## Top 1: **C2 — Hidden-pulse Q** (`c2_qcounter_*`)

**Hidden message:** the Q's tail is the *same trace* that spikes inside the
bowl — a heartbeat hidden in the counter, and the letter's tail is
literally its own pulse. Primary read (a clean Q) survives at 16 px; the
second reading appears on inspection — the exact FedEx-arrow dynamic
asked for.

Why it wins:
- Q is the brand's anchor letter; this mark needs no wordmark to be
  identifiable.
- Two strokes, two colours, big counter — the simplest geometry of the
  set after C5, and the most embossable (ring + diagonal trace survive
  a 0.4 mm relief cleanly).
- Fixes round-1 feedback: the ECG element is now main-scale, not a
  footnote tail.
- Reads in mono and reversed identically; teal accent only rewards the
  colour version.

## Top 2: **C1 — R-wave V** (`c1_rwave_v_*`)

**Hidden message:** the letter V's right stroke overshoots into a sharp
QRS spike — *the name starts with a heartbeat*. It's the boldest option:
one asymmetric V, two strokes, instantly readable at 16 px, and it makes
the wordmark itself carry the gimmick (the V glyph *is* the mark, echoed
in the lockup).

Trade-off: the double-read is more "styled V" than true negative space —
but it's the most distinctive silhouette in the set and the most
device-side-embossable mark (two strokes only).

## The field

| # | Concept | Verdict |
|---|---|---|
| 1 | c1_rwave_v — R-wave V | **Top 2.** Boldest silhouette; name-starts-with-heartbeat story. |
| 2 | c2_qcounter — Hidden-pulse Q | **Top 1.** True negative-space payoff, ownable Q, most versatile. |
| 3 | c3_qdots — Quantised beat | Honest "life, measured" idea; dots get fragile below ~24 px and read busier than C2. |
| 4 | c4_breath — Breath + beat | Smart RSA story (one stroke, two vitals) but reads as an abstract wave first; weakest standalone mark. |
| 5 | c5_vcg — VCG loop Q | Elegant insider cardiology reference; teardrop loop is a real QRS-loop silhouette. Solid, slightly abstract. |
| 6 | c6_hub — Body to hub | Tells the monitoring pipeline story; arcs read sideways-"broadcast" and weaken at favicon sizes. |
| 7 | c7_tittle — Chest tittle | Cleverest trick (sternum dot = the i's tittle) but the bare mark is the least self-explanatory; works only paired with the wordmark. |

## Suggested pairing

`c2_qcounter` as the primary mark + `VitalQuant`/`VitalQ` Space Grotesk
620 lockups as shipped; `c1_rwave_v` held as the app-icon / favicon
candidate where the "V" shorthand is more useful.

## Delivered files (all in this folder)

- `cN_*_mark_{color,mono,reversed}.{svg,png}` — 21 mark files
- `cN_*_lockup_{vitalquant,vitalq}_{color,mono,reversed}.{svg,png}` — 42 lockups
- `*_mark_{v}_{16,32,64}.png` — favicon tests at all three sizes
- `*_mark_hero.png` — ~600 px hero renders
- `comparison_sheet.png` — full grid
- `make_logos2.py`, `sheet2.py` — reproducible generators (no AI imagery,
  text as Space Grotesk glyph outlines, qlmanage renders)
