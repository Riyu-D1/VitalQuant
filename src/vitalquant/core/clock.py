"""Device→timezone.utc clock model (spec §10, audit G3, API §clock-sync).

The ESP32 stamps samples with a monotonic `esp_timer` µs counter. Each ingest
batch anchors it: `clock.wall_time` (SNTP-disciplined timezone.utc at anchor time) and
`clock.monotonic_us`. This module converts any device timestamp to corrected
timezone.utc without the backend guessing.

Model: piecewise-linear between anchors. Offset jumps (an NTP resync mid-session)
are detected and flagged rather than silently absorbed.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta, timezone

# A jump larger than this between consecutive anchors is an NTP resync event,
# not drift. 500 ms default; typical RTC drift is ms-per-hour.
JUMP_THRESHOLD_US = 500_000


@dataclass(frozen=True)
class ClockAnchor:
    monotonic_us: int
    wall_time: datetime          # timezone.utc


class ClockJumpError(ValueError):
    """Raised when a batch anchor is inconsistent with the session's clock line."""


class ClockModel:
    """Fits device→timezone.utc conversion from a series of anchors (one per batch)."""

    def __init__(self, anchors: list[ClockAnchor] | None = None):
        self._anchors: list[ClockAnchor] = sorted(
            anchors or [], key=lambda a: a.monotonic_us
        )

    @property
    def anchors(self) -> tuple[ClockAnchor, ...]:
        return tuple(self._anchors)

    def add_anchor(self, anchor: ClockAnchor) -> list[str]:
        """Register a batch anchor; returns flag strings for anomalies found."""
        flags: list[str] = []
        prev = self.last()
        if prev is not None and anchor.monotonic_us > prev.monotonic_us:
            fitted = self._predict_wall_time(anchor.monotonic_us)
            delta_us = (anchor.wall_time - fitted).total_seconds() * 1e6
            if abs(delta_us) > JUMP_THRESHOLD_US:
                flags.append("clock.jump")
        self._anchors.append(anchor)
        self._anchors.sort(key=lambda a: a.monotonic_us)
        return flags

    def last(self) -> ClockAnchor | None:
        return self._anchors[-1] if self._anchors else None

    def _predict_wall_time(self, monotonic_us: int) -> datetime:
        return self.to_utc(monotonic_us)

    def offset_us(self, monotonic_us: int | None = None) -> int:
        """Device→timezone.utc offset near the given monotonic time (defaults: latest anchor)."""
        if not self._anchors:
            raise ValueError("no anchors")
        if monotonic_us is None:
            a = self._anchors[-1]
        else:
            a = min(self._anchors, key=lambda x: abs(x.monotonic_us - monotonic_us))
        return int(a.wall_time.timestamp() * 1e6) - a.monotonic_us

    def drift_ppm(self) -> float:
        """Mean fitted drift across anchors (0.0 with <2 anchors)."""
        if len(self._anchors) < 2:
            return 0.0
        a, b = self._anchors[0], self._anchors[-1]
        dmono = b.monotonic_us - a.monotonic_us
        if dmono <= 0:
            return 0.0
        dwall = (b.wall_time - a.wall_time).total_seconds() * 1e6
        return (dwall - dmono) / dmono * 1e6

    def to_utc(self, monotonic_us: int) -> datetime:
        """Convert a device timestamp to corrected timezone.utc.

        Between anchors: linear interpolation (captures drift).
        Beyond the last anchor: extrapolate with the last fitted offset+drift.
        """
        if not self._anchors:
            raise ValueError("no anchors — cannot convert device time")
        anchors = self._anchors

        if monotonic_us <= anchors[0].monotonic_us:
            a = anchors[0]
            return a.wall_time + timedelta(microseconds=monotonic_us - a.monotonic_us)

        for lo, hi in zip(anchors, anchors[1:], strict=False):
            if lo.monotonic_us <= monotonic_us <= hi.monotonic_us:
                span = hi.monotonic_us - lo.monotonic_us
                frac = 0.0 if span == 0 else (monotonic_us - lo.monotonic_us) / span
                wall_span_us = (hi.wall_time - lo.wall_time).total_seconds() * 1e6
                offset = wall_span_us * frac
                return lo.wall_time + timedelta(microseconds=offset)

        # beyond last anchor: extrapolate using global drift fit
        a = anchors[-1]
        drift = self.drift_ppm()
        ahead_us = monotonic_us - a.monotonic_us
        return a.wall_time + timedelta(microseconds=ahead_us * (1 + drift / 1e6))


def utcnow() -> datetime:
    return datetime.now(timezone.utc)
