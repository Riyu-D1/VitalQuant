"""Synthetic generator: determinism, schema shape, corruption behaviour."""

from datetime import datetime, timezone
from uuid import UUID

import numpy as np
from conftest import PROFILE

from vitalq.core.config import load_profile
from vitalq.synth.generator import CorruptionSpec, SynthConfig, SyntheticDevice


def _dev(seed=7, **kw):
    prof = load_profile(str(PROFILE))
    cfg = SynthConfig(session_id=UUID("00000000-0000-0000-0000-000000000d15"),
                      duration_s=60, batch_s=15, seed=seed, **kw)
    return SyntheticDevice(prof, cfg, start=datetime(2026, 1, 1, tzinfo=timezone.utc))


def test_deterministic():
    a = [b.model_dump() for _, b in _dev(seed=3).batches()]
    b = [x.model_dump() for _, x in _dev(seed=3).batches()]
    assert a == b


def test_batch_shape_and_channels():
    dev = _dev()
    batches = list(dev.batches())
    assert len(batches) == 4
    _, b = batches[0]
    assert b.session_id == dev.cfg.session_id
    ppg = [w for w in b.windows if w.channel.startswith("ppg.")]
    assert ppg and all(len(w.samples) == 3000 for w in ppg)   # 15 s @ 200 Hz
    assert {w.channel for w in ppg} == {"ppg.red", "ppg.ir"}
    assert any(s.channel == "contact.level" for s in b.scalars)
    assert b.frames and all(set(f.channels) for f in b.frames)


def test_anchors_monotonic():
    anchors = [b.clock.monotonic_us for _, b in _dev().batches()]
    assert anchors == sorted(anchors)


def test_dropout_removes_channel():
    cfg = CorruptionSpec(dropout_channel_p=1.0)
    b = next(b for _, b in _dev(corruptions=cfg).batches())
    assert not b.windows


def test_flat_fault_window():
    cfg = CorruptionSpec(sensor_fault_p=1.0)
    _, b = next(iter(_dev(corruptions=cfg).batches()))
    w = next(x for x in b.windows if x.channel == "ppg.red")
    assert np.allclose(w.samples, 0.0)


def test_ntp_jump_shifts_anchor_wall_time():
    cfg = CorruptionSpec(ntp_jump_p=1.0)
    bs = [b for _, b in _dev(corruptions=cfg).batches()]
    # monotonic advances ~15 s; wall_time advances ~15 s ± injected ±2 s jumps
    mono_gap = (bs[1].clock.monotonic_us - bs[0].clock.monotonic_us) / 1e6
    wall_gap = (bs[1].clock.wall_time - bs[0].clock.wall_time).total_seconds()
    assert abs(wall_gap - mono_gap) > 1.0
