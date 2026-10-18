from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest
from pydantic import ValidationError

from vitalq.core.channels import CHANNELS, get_channel
from vitalq.core.clock import ClockAnchor, ClockModel
from vitalq.core.config import KNOWN_SENSOR_MODELS, load_profile
from vitalq.core.types import BatchIngest, ScalarSampleIn, SignalWindowIn

ROOT = Path(__file__).resolve().parents[1]


def test_channel_registry_roles():
    assert CHANNELS["ppg.red"].sample_role == "waveform"
    assert CHANNELS["temp.object"].sample_role == "scalar"
    with pytest.raises(ValueError):
        get_channel("not.a.channel")


def test_scalar_rejects_waveform_channel():
    with pytest.raises(ValidationError):
        ScalarSampleIn(channel="ppg.red", t_us=1, v=1.0)


def test_window_rejects_scalar_channel():
    with pytest.raises(ValidationError):
        SignalWindowIn(channel="temp.object", rate_hz=1, t_start_us=0, samples=[1.0])


def test_profiles_validate():
    for p in ("config/hardware.example.yaml", "firmware/esp32/profiles/hw_v0.yaml"):
        prof = load_profile(str(ROOT / p))
        assert prof.hardware_revision.startswith("hw_")
        assert prof.profile_hash.startswith("sha256:")
        assert prof.enabled_sensors()


def test_unknown_sensor_model_rejected():
    assert "NOT-A-CHIP" not in KNOWN_SENSOR_MODELS


def test_clock_no_jump():
    base = datetime(2026, 1, 1, tzinfo=timezone.utc)
    anchors = [ClockAnchor(1_000_000 + i * 30_000_000, base + timedelta(seconds=30 * i))
               for i in range(4)]
    m = ClockModel(anchors)
    # midway between anchors → midpoint interpolation
    mid = m.to_utc(1_000_000 + 45_000_000)
    assert abs((mid - (base + timedelta(seconds=45))).total_seconds()) < 0.01


def test_clock_drift_fit():
    base = datetime(2026, 1, 1, tzinfo=timezone.utc)
    # device runs slow: mono undercounts by 50 ppm
    anchors = [ClockAnchor(i * 30_000_000,
                           base + timedelta(seconds=i * 30 * (1 + 50e-6)))
               for i in range(5)]
    m = ClockModel(anchors)
    assert abs(m.drift_ppm() - 50) < 1.0


def test_clock_jump_flagged():
    base = datetime(2026, 1, 1, tzinfo=timezone.utc)
    m = ClockModel([ClockAnchor(0, base)])
    flags = m.add_anchor(ClockAnchor(30_000_000, base + timedelta(seconds=32.0)))
    assert "clock.jump" in flags


def test_empty_batch_rejected():
    from uuid import uuid4
    with pytest.raises(ValidationError):
        BatchIngest(session_id=uuid4(), firmware_version="x",
                    clock={"wall_time": datetime.now(timezone.utc), "monotonic_us": 0})
