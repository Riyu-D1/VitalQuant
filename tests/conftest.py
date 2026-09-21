import os
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

PROFILE = ROOT / "config" / "hardware.example.yaml"
DATABASE_URL = os.environ.get("DATABASE_URL")

requires_db = pytest.mark.skipif(not DATABASE_URL, reason="DATABASE_URL not set")
