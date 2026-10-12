# Fit issue to fix (owner priority)
The skin-side B.Cu surface sits at z=3.4 above the skin plane, so the electrodes (J5/J6/J11/J13) and the TMP117 contact are recessed ~3.4 mm inside their apertures and will NOT touch the skin.
1. Bring every skin contact to the skin plane +0.2 mm. Use conductive contact inserts per electrode (e.g. stainless or gold-plated spring/pogo contacts, or conductive-silicone studs, soldered or pressed onto each pad; pick real MPNs and heights), or lower the base floor under the electrode zone while still clearing every bottom-side part.
2. Give the TMP117 (U11) a thermal pad or metal insert to the skin.
3. J7 BioZ pads are on F.Cu (top). Confirm whether they are meant for skin contact. If so, bring them to the skin with inserts; otherwise flag it.
4. Add a FIT check that every skin contact reaches within 0.2 mm of the skin plane.
5. Re-render properly shaded 3D views (mesh faces, not boxes): the skin side with the contacts visible, a section through an electrode, and an assembled view.
Update FIT_REPORT.md, ASSEMBLY.md and a contacts BOM. End with CASE COMPLETE. Do not stop to ask.
