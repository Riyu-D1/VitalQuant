#!/usr/bin/env python3
"""toolfind.py - locate kicad-cli / ngspice without machine-specific paths.

Search order: $PATH (shutil.which), then per-platform standard install
locations. No user-home paths are ever consulted.
"""
import os, shutil, sys

_FALLBACKS = {
    "darwin": [
        "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli",
        "/usr/local/bin/ngspice",
        "/opt/homebrew/bin/ngspice",
    ],
    "linux": [
        "/usr/bin/kicad-cli",
        "/usr/local/bin/kicad-cli",
        "/usr/bin/ngspice",
        "/usr/local/bin/ngspice",
    ],
}


def find_tool(name):
    """Return the path to `name` or None. PATH first, then standard dirs."""
    p = shutil.which(name)
    if p:
        return p
    plat = "darwin" if sys.platform == "darwin" else "linux"
    for cand in _FALLBACKS.get(plat, []):
        if os.path.basename(cand) == name and os.path.exists(cand):
            return cand
    return None


def require_tool(name):
    p = find_tool(name)
    if not p:
        sys.exit(f"error: '{name}' not found on PATH or standard install dirs")
    return p
