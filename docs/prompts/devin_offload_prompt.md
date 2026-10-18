# Task: offload all VitalQ deliverables to a GitHub branch, then free local disk space

Riyansh is low on disk space. He wants everything saved to a separate branch of github.com/Riyu-D1/VitalQuant, pushed, verified, and only then the local copies deleted.
This OVERRIDES the earlier no-push rule, but ONLY for the branch `vitalq-deliverables`. Do NOT push to, force-push, rebase or otherwise touch any other branch on the remote (including devin/hw-v2-pcb and main).

## 1. Record disk space
Run `df -h ~` and save the output for the report.

## 2. Create the branch
In ~/VitalQuant-place, create branch `vitalq-deliverables` from the current `devin/hw-v2-pcb` HEAD, keeping all local commits.

## 3. Add the files
- Create `deliverables/` with copies of these from ~/Downloads:
  - VitalQ_Quilter_v7 (or the latest VitalQ_Quilter_v* folder, if v7 isn't there)
  - VitalQ_Case_v2
  - VitalQ_Case_Research_v1
  - VitalQ_Logo_v1
  - VitalQ_Logo_v2
  - Quilter_vitalq_v2.kicad_pcb_Candidate_4_1.zip
  - ul_MAX86178ENJ-T.zip
- Also commit:
  - All ~/VitalQuant-place/devin_*_prompt.md files.
  - hardware/pcb/swap_max86141.py.
  - The untracked vitalq_flat.* and vitalq_hw_v1.* files in the repo root, OR confirm with a diff that they are duplicates of files already committed.
- EXCLUDE case/.venv, __pycache__, .DS_Store, and any secrets or tokens. Grep for keys, tokens and .env files before committing.
- Check file sizes: list anything over 50 MB. GitHub rejects files over 100 MB. Use Git LFS only if a file actually needs it.

## 4. Commit and push
- Commit with a clear message.
- Push with `git push -u origin vitalq-deliverables`. Use NO force flags.

## 5. Verify before deleting anything
- `git fetch origin` then `git ls-remote origin vitalq-deliverables` must equal the local `git rev-parse HEAD`.
- `git ls-tree -r --name-only origin/vitalq-deliverables` must list every deliverable file.
- Compare the file count per deliverable (the source folder in Downloads vs the tree on the remote), and confirm the zips are present.
- If ANY check fails, STOP. Delete nothing and report.

## 6. Only after step 5 passes, delete these
In ~/Downloads:
- VitalQ_Case_v2, VitalQ_Case_Research_v1, VitalQ_Logo_v1 and VitalQ_Logo_v2.
- Quilter_vitalq_v2.kicad_pcb_Candidate_4_1.zip and ul_MAX86178ENJ-T.zip.
- ~/Downloads/max86141.pdf, if present.

EXCEPTION: KEEP ~/Downloads/VitalQ_Quilter_v7 (or the latest Quilter folder) until Riyansh has uploaded it to Quilter. It is backed up on the branch, but do not delete it locally yet.

In the repo:
- Delete the stray vitalq_flat.* / vitalq_hw_v1.* files from the repo root ONLY if they are committed on the branch or confirmed duplicates.
- Delete ~/VitalQuant-place/case/.venv (about 610 MB; it can be recreated with uv), then run `uv cache clean`.

KEEP the ~/VitalQuant-place clone itself, because Devin still works in it. Afterwards, switch back to devin/hw-v2-pcb if that makes sense for future work, and make sure the working tree is clean.

## 7. Report
- The branch URL: https://github.com/Riyu-D1/VitalQuant/tree/vitalq-deliverables
- The commit hash.
- The number of files pushed, per deliverable.
- What was deleted.
- That VitalQ_Quilter_v7 was kept until the Quilter upload.
- `df -h ~` before and after, and the disk space freed.
