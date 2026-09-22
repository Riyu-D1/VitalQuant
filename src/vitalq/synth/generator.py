"""Synthetic sensor-stream generator.

Produces `BatchIngest` objects shaped exactly like real ESP32 output — the same
schema the API validates — from a HardwareProfile. Corruptions are injected
deterministically so the processing pipeline can be tested against them.

PPG model: stylised beat = systolic Gaussian + dicrotic Gaussian, respiration
amplitude modulation, baseline wander; motion bursts inject broadband noise and
can clip the ADC — intentionally "realistic-ish", not physiological truth.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from uuid import NAMESPACE_URL, UUID, uuid4, uuid5

import numpy as np

from vitalq.core.config import HardwareProfile
from vitalq.core.types import (
    BatchIngest,
    ClockAnchorIn,
    DeviceEventIn,
    ScalarSampleIn,
    SignalWindowIn,
    SpectralFrameIn,
)

# AS7341-ish base spectrum shape (counts, emitter_on) — illustrative, smooth in λ
_BASE_SPECTRUM = dict(
    f1=220, f2=380, f3=520, f4=700, f5=820, f6=760, f7=560, f8=340,
    clear=1600, nir=180, flicker=0,
)


@dataclass
class CorruptionSpec:
    """Probabilities per emitted batch for the §28 corruption battery."""

    dropout_channel_p: float = 0.0      # whole channel window dropped
    ntp_jump_p: float = 0.0             # wall clock jumps ± seconds between anchors
    ntp_jump_s: float = 2.0
    clipping_p: float = 0.0             # PPG window clips at ADC rail
    packet_loss_p: float = 0.0          # batch never delivered (caller skips it)
    sensor_fault_p: float = 0.0         # emits sensor_fault event, channel goes flat
    duplicate_p: float = 0.0            # same batch_id emitted twice (replay)


@dataclass
class SynthConfig:
    session_id: UUID = field(default_factory=uuid4)
    device_label: str = "synth-0"
    firmware_version: str = "synth-0.1.0"
    duration_s: float = 300.0
    batch_s: float = 30.0
    hr_bpm: float = 65.0
    resp_hz: float = 0.22
    seed: int = 42
    corruptions: CorruptionSpec = field(default_factory=CorruptionSpec)


class SyntheticDevice:
    """Iterates ingest batches for a profile; also emits session metadata."""

    def __init__(self, profile: HardwareProfile, cfg: SynthConfig,
                 start: datetime | None = None):
        self.profile = profile
        self.cfg = cfg
        self.rng = np.random.default_rng(cfg.seed)
        self.start = start or datetime.now(timezone.utc).replace(microsecond=0)
        self._mono0 = 5_000_000  # boot counter origin, µs
        self._corrupt_state: dict[str, bool] = {}

    # -- waveform synthesis ------------------------------------------------

    def _ppg_window(self, n: int, fs: float, t0_s: float, motion: np.ndarray) -> np.ndarray:
        t = t0_s + np.arange(n) / fs
        ibi_s = 60.0 / self.cfg.hr_bpm
        phase = (t % ibi_s) / ibi_s
        systolic = np.exp(-0.5 * ((phase - 0.10) / 0.045) ** 2)
        dicrotic = 0.35 * np.exp(-0.5 * ((phase - 0.38) / 0.07) ** 2)
        resp = 1 + 0.08 * np.sin(2 * np.pi * self.cfg.resp_hz * t)
        wander = 300 * np.sin(2 * np.pi * 0.1 * t)
        noise = self.rng.normal(0, 25, n)
        burst = motion * self.rng.normal(0, 1200, n)   # broadband artifact bursts
        return 40_000 + wander + (systolic + dicrotic) * 2_400 * resp + noise + burst

    def _motion_trace(self, n: int, fs: float, active: np.ndarray) -> np.ndarray:
        mask = np.repeat(active, max(1, n // max(len(active), 1)))[:n]
        if len(mask) < n:
            mask = np.pad(mask, (0, n - len(mask)), mode="edge")
        base = self.rng.normal(0, 0.02, n)
        burst = mask * (self.rng.normal(0, 0.35, n)
                        + 0.4 * np.sin(2 * np.pi * 2.5 * np.arange(n) / fs))
        return base + burst

    def _activity_mask(self, duration_s: float) -> np.ndarray:
        """Per-second motion-activity mask — bursts with some probability."""
        sec = np.arange(int(duration_s) + 2)
        active = self.rng.random(len(sec)) < 0.12
        return np.convolve(active.astype(float), np.ones(3), "same") > 0

    # -- scalar synthesis ----------------------------------------------------

    def _scalar_series(self, channel: str, times_s: np.ndarray) -> np.ndarray:
        r = self.rng
        if channel == "temp.object":
            return 33.2 + 0.15 * np.sin(2 * np.pi * times_s / 900) + r.normal(0, 0.05, len(times_s))
        if channel == "temp.ambient":
            return 24.5 + r.normal(0, 0.08, len(times_s))
        if channel == "env.temperature":
            return 24.8 + r.normal(0, 0.05, len(times_s))
        if channel == "env.humidity":
            return 45 + 2 * np.sin(2 * np.pi * times_s / 1800) + r.normal(0, 0.3, len(times_s))
        if channel == "env.pressure":
            return 1013.2 + r.normal(0, 0.1, len(times_s))
        if channel == "env.gas_resistance":
            return 48_000 + r.normal(0, 900, len(times_s))
        if channel == "contact.level":
            # seated band ~1500 ± noise; periodic dropouts to 0 (device lifted)
            drop = r.random(len(times_s)) < 0.03
            return np.where(drop, 0.0, 1500 + r.normal(0, 120, len(times_s))).clip(0, 4095)
        if channel == "sys.battery_v":
            return np.clip(4.05 - 0.00005 * times_s + r.normal(0, 0.005, len(times_s)), 3.0, 4.4)
        return r.normal(0, 1, len(times_s))

    def _spectral_channels(self, illumination: str) -> dict[str, float]:
        r = self.rng
        if illumination == "dark_frame":
            return {k: float(abs(r.normal(3, 2))) for k in _BASE_SPECTRUM}
        scale = 1.0 if illumination == "emitter_on" else 0.35
        return {k: float(max(0.0, v * scale + r.normal(0, v * 0.01 + 1)))
                for k, v in _BASE_SPECTRUM.items()}

    # -- batch iteration -----------------------------------------------------

    def batches(self):
        """Yield BatchIngest objects covering cfg.duration_s, batch_s each."""
        cfg, rng, corr = self.cfg, self.rng, self.cfg.corruptions
        prof = self.profile
        n_batches = max(1, math.ceil(cfg.duration_s / cfg.batch_s))
        activity = self._activity_mask(cfg.duration_s)

        ppg = prof.sensors.get("ppg")
        mot = prof.sensors.get("motion")
        spec = prof.sensors.get("spectral")
        env = prof.sensors.get("environment")
        tmp = prof.sensors.get("temperature")
        con = prof.sensors.get("contact")
        ppg_rate = (ppg.sample_rate_hz or 200) if ppg and ppg.enabled else None
        mot_rate = (mot.rate_hz or mot.sample_rate_hz or 52) if mot and mot.enabled else None

        wall_offset_s = 0.0  # injected NTP drift accumulates here

        for i in range(n_batches):
            t0 = i * cfg.batch_s
            t1 = min(t0 + cfg.batch_s, cfg.duration_s)
            mono_start = self._mono0 + int(t0 * 1e6)
            scalars: list[ScalarSampleIn] = []
            windows: list[SignalWindowIn] = []
            frames: list[SpectralFrameIn] = []
            events: list[DeviceEventIn] = []

            # injected NTP jump *between* anchors → tests jump detection
            if i > 0 and rng.random() < corr.ntp_jump_p:
                wall_offset_s += float(rng.choice([-1, 1])) * corr.ntp_jump_s
                events.append(DeviceEventIn(
                    t_us=mono_start, kind="ntp_sync",
                    detail={"offset_ms": wall_offset_s * 1000}))
            clock = ClockAnchorIn(
                wall_time=self.start + timedelta(seconds=t0 + wall_offset_s),
                monotonic_us=mono_start,
            )

            act = activity[int(t0):int(t1) + 1]

            # waveform channels
            def maybe_drop(ch: str) -> bool:
                if rng.random() < corr.dropout_channel_p:
                    self._corrupt_state[ch] = True
                    return True
                self._corrupt_state[ch] = False
                return False

            if ppg_rate and ppg.enabled:
                n = int((t1 - t0) * ppg_rate)
                for chdef in ppg.channels:
                    cid = chdef.id if hasattr(chdef, "id") else chdef
                    if maybe_drop(cid):
                        continue
                    motion_burst = np.repeat(act.astype(float), int(ppg_rate))[:n]
                    w = self._ppg_window(n, ppg_rate, t0, motion_burst)
                    if rng.random() < corr.clipping_p:
                        w = np.clip(w, None, 45_000)  # ADC rail
                    if rng.random() < corr.sensor_fault_p:
                        w = np.zeros(n)             # dead sensor: flat line
                        events.append(DeviceEventIn(
                            t_us=mono_start, kind="sensor_fault", detail={"channel": cid}))
                    windows.append(SignalWindowIn(
                        channel=cid, rate_hz=ppg_rate,
                        t_start_us=mono_start, samples=w.round(1).tolist()))

            if mot_rate and mot.enabled:
                n = int((t1 - t0) * mot_rate)
                for axis, base in (("accel_x", 0.0), ("accel_y", 0.0), ("accel_z", 1.0)):
                    cid = f"motion.{axis}"
                    if maybe_drop(cid):
                        continue
                    w = base + self._motion_trace(n, mot_rate, act)
                    windows.append(SignalWindowIn(
                        channel=cid, rate_hz=mot_rate,
                        t_start_us=mono_start, samples=w.round(4).tolist()))

            # scalar channels at ~1 Hz (contact) / 0.5 Hz (env/temp)
            for group, rate in ((tmp, 1.0), (env, 0.5), (con, 1.0)):
                if not (group and group.enabled):
                    continue
                times = np.arange(t0, t1, 1.0 / rate)
                for ch in group.outputs:
                    vals = self._scalar_series(ch, times)
                    for ts, v in zip(times, vals, strict=False):
                        scalars.append(ScalarSampleIn(
                            channel=ch, t_us=self._mono0 + int(ts * 1e6), v=round(float(v), 4)))

            # spectral frames: 2 read groups per tick (audit C2), dark every ~5th
            if spec and spec.enabled:
                fs_hz = spec.frame_rate_hz or 2
                times = np.arange(t0, t1, 1.0 / fs_hz)
                for j, ts in enumerate(times):
                    dark = (j % 5) == 4
                    illum = "dark_frame" if dark else (spec.illumination or "emitter_on")
                    for grp in range(spec.parallel_groups or 1):
                        frames.append(SpectralFrameIn(
                            t_us=self._mono0 + int(ts * 1e6) + grp * 30_000,
                            read_group=grp, gain_x=spec.gain or 64,
                            integ_ms=spec.integ_time_ms or 27.8,
                            illumination=illum,
                            channels=self._spectral_channels(illum),
                            sensor_temp_c=round(float(25 + rng.normal(0, 0.2)), 2)))

            batch = BatchIngest(
                # deterministic per (session, index): re-emission is a true replay
                batch_id=uuid5(NAMESPACE_URL, f"vitalq-batch:{cfg.session_id}:{i}"),
                session_id=cfg.session_id,
                firmware_version=cfg.firmware_version,
                profile_hash=prof.profile_hash,
                clock=clock,
                scalars=scalars, windows=windows, frames=frames, events=events,
            )
            yield i, batch

    def describe(self) -> dict:
        return {"session_id": str(self.cfg.session_id), "profile": self.profile.hardware_revision,
                "firmware_version": self.cfg.firmware_version, "started_at": self.start.isoformat(),
                "data_class": "synthetic", "device_label": self.cfg.device_label}
