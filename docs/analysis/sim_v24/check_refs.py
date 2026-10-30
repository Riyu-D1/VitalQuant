#!/usr/bin/env python3
"""check_refs.py — verify every file/dir path referenced by the docs exists
in git.

Scans the markdown docs in this folder (REPORT.md, SIM_README.md,
PROGRESS.md, CHANGES_FIXES.md) for backtick-quoted and markdown-link paths
and checks each against `git ls-files`:

  * exact file            -> must be tracked
  * glob (results/*.csv)  -> >=1 tracked match
  * directory (decks/)    -> >=1 tracked file under it

Repo-root-relative refs (e.g. hardware/pcb/...) and refs relative to this
folder are both accepted. Exits non-zero if anything is missing.

Usage:  python3 check_refs.py            # report + exit code
        python3 check_refs.py -v         # list every OK hit too
"""
import os, re, sys, fnmatch, subprocess, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = subprocess.run(["git", "-C", HERE, "rev-parse", "--show-toplevel"],
                      capture_output=True, text=True, check=True).stdout.strip()
PREFIX = os.path.relpath(HERE, ROOT).replace(os.sep, "/")

LS = set(subprocess.run(["git", "-C", ROOT, "ls-files"],
                        capture_output=True, text=True, check=True)
         .stdout.split())

ap = argparse.ArgumentParser()
ap.add_argument("-v", "--verbose", action="store_true")
args = ap.parse_args()

DOCS = [f for f in ("REPORT.md", "SIM_README.md", "PROGRESS.md",
                    "CHANGES_FIXES.md")
        if os.path.exists(os.path.join(HERE, f))]

EXTS = r"(?:csv|json|txt|png|py|sp|inc|lib|net|md|sh|log|gbr|gbrjob|drl|" \
       r"gtl|gbl|g1|g2|g3|g4|g5|g6|kicad_pcb|kicad_sch|kicad_pro|kicad_dru|kicad_prl)"


def candidates(text):
    """Yield referenced-path tokens from markdown text."""
    # backtick spans and [links](targets)
    spans = re.findall(r"`([^`\n]+)`", text)
    spans += re.findall(r"\[[^\]]*\]\(([^)\s]+)\)", text)
    for sp in spans:
        sp = sp.strip()
        # alternation like results/*.csv|.log -> expand on |
        parts = [sp]
        if "|" in sp and not sp.startswith("|"):
            base = sp.split("|")[0]
            stem = base.rsplit("/", 1)
            parts = [base]
            for p in sp.split("|")[1:]:
                if p.startswith(".") and "/" not in p:
                    # extension swap: *.csv|.log -> *.log
                    root = stem[1].rsplit(".", 1)[0] if "." in stem[1] else stem[1]
                    p = root + p
                parts.append((stem[0] + "/" if len(stem) > 1 else "") + p)
        for part in parts:
            for tok in re.split(r"\s+", part):
                tok = tok.strip(".,;:()[]{}'\"")
                if not tok or tok.startswith(("-", "http", "git ", "$")):
                    continue
                if "/" not in tok and not re.search(r"\." + EXTS + r"$", tok):
                    continue
                yield tok


def expand_braces(tok):
    m = re.search(r"\{([^}]+)\}", tok)
    if not m:
        return [tok]
    return [tok[:m.start()] + alt + tok[m.end():]
            for alt in m.group(1).split(",")]


# paths that are referenced but intentionally untracked (git-ignored scratch)
SKIP_PREFIXES = ("_work/",)
# tokens that look like paths but are not file refs (git branch name)
SKIP_REFS = {"docs/sim-v24"}


def exists(ref):
    ref = ref.lstrip("./")
    if ref.startswith(SKIP_PREFIXES) or ref in SKIP_REFS:
        return True
    cands = []
    for t in expand_braces(ref):
        cands += [t, f"{PREFIX}/{t}"]
    for c in cands:
        if c in LS:
            return True
        if any(fnmatch.fnmatch(f, c) for f in LS):
            return True
        cdir = c.rstrip("/") + "/"
        if any(f.startswith(cdir) for f in LS):
            return True
    # bare filename -> resolve by basename anywhere under this folder
    if "/" not in ref:
        return any(f.startswith(PREFIX + "/") and
                   os.path.basename(f) == ref for f in LS)
    return False


missing = []
seen = set()
for doc in DOCS:
    text = open(os.path.join(HERE, doc)).read()
    for ref in candidates(text):
        key = (doc, ref)
        if key in seen:
            continue
        seen.add(key)
        ok = exists(ref)
        if args.verbose or not ok:
            print(f"{'OK     ' if ok else 'MISSING'} {doc}: {ref}")
        if not ok:
            missing.append((doc, ref))

print(f"\n{len(seen)} referenced paths checked, {len(missing)} missing")
sys.exit(1 if missing else 0)
