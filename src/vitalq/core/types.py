"""Wire + storage types shared by ingest API, synth generator and worker.

The batch envelope is THE device contract (docs/05-api-spec.md). Keep it strict:
unknown channels, missing fields and bad rates fail validation here, on-device
in tests and in the API at the edge.
"""

from __future__ import annotations

from datetime import datetime
from typing import Literal
from uuid import UUID, uuid4

from pydantic import BaseModel, Field, field_validator, model_validator

from vitalq.core.channels import (
    ILLUMINATION_STATES,
    SPECTRAL_CHANNELS,
    get_channel,
)

DataClass = Literal["real", "synthetic", "simulated"]

MAX_BATCH_BYTES = 2_000_000  # API rejects above this (413)
MAX_WINDOW_SAMPLES = 20_000  # e.g. 200 Hz × 100 s — keeps one row per window sane


def _aware(v: datetime) -> datetime:
    if v.tzinfo is None:
        raise ValueError("timestamp must be timezone-aware (timezone.utc)")
    return v


class ClockAnchorIn(BaseModel):
    wall_time: datetime
    monotonic_us: int

    @field_validator("wall_time")
    @classmethod
    def _tz(cls, v: datetime) -> datetime:
        return _aware(v)


class ScalarSampleIn(BaseModel):
    channel: str
    t_us: int                    # device monotonic µs
    v: float

    @field_validator("channel")
    @classmethod
    def _ch(cls, v: str) -> str:
        ch = get_channel(v)
        if ch.sample_role != "scalar":
            raise ValueError(f"{v!r} is not a scalar channel")
        return v


class SignalWindowIn(BaseModel):
    channel: str
    rate_hz: float
    t_start_us: int
    samples: list[float]

    @field_validator("channel")
    @classmethod
    def _ch(cls, v: str) -> str:
        ch = get_channel(v)
        if ch.sample_role != "waveform":
            raise ValueError(f"{v!r} is not a waveform channel")
        return v

    @field_validator("rate_hz")
    @classmethod
    def _rate(cls, v: float) -> float:
        if not (0.01 <= v <= 10_000):
            raise ValueError("rate_hz out of range")
        return v

    @field_validator("samples")
    @classmethod
    def _samples(cls, v: list[float]) -> list[float]:
        if not (1 <= len(v) <= MAX_WINDOW_SAMPLES):
            raise ValueError("empty or oversized window")
        return v


class SpectralFrameIn(BaseModel):
    t_us: int
    read_group: int = 0
    channels: dict[str, float]
    gain_x: float | None = None
    integ_ms: float | None = None
    illumination: str | None = None
    sensor_temp_c: float | None = None

    @field_validator("channels")
    @classmethod
    def _chs(cls, v: dict[str, float]) -> dict[str, float]:
        unknown = set(v) - set(SPECTRAL_CHANNELS)
        if unknown:
            raise ValueError(f"unknown spectral channels: {sorted(unknown)}")
        return v

    @field_validator("illumination")
    @classmethod
    def _illum(cls, v: str | None) -> str | None:
        if v is not None and v not in ILLUMINATION_STATES:
            raise ValueError(f"illumination must be one of {ILLUMINATION_STATES}")
        return v


class DeviceEventIn(BaseModel):
    t_us: int
    kind: str
    detail: dict = Field(default_factory=dict)


class BatchIngest(BaseModel):
    """POST /v1/ingest/batch body."""

    batch_id: UUID = Field(default_factory=uuid4)
    session_id: UUID
    firmware_version: str
    profile_hash: str | None = None
    clock: ClockAnchorIn
    scalars: list[ScalarSampleIn] = Field(default_factory=list)
    windows: list[SignalWindowIn] = Field(default_factory=list)
    frames: list[SpectralFrameIn] = Field(default_factory=list)
    events: list[DeviceEventIn] = Field(default_factory=list)

    @model_validator(mode="after")
    def _nonempty(self) -> BatchIngest:
        if not (self.scalars or self.windows or self.frames or self.events):
            raise ValueError("empty batch")
        return self


class SessionCreate(BaseModel):
    session_id: UUID = Field(default_factory=uuid4)
    subject_id: UUID | None = None
    firmware_version: str
    hardware_revision: str
    data_class: DataClass = "real"
    body_site: str | None = None
    started_at: datetime
    notes: str | None = None

    @field_validator("started_at")
    @classmethod
    def _tz(cls, v: datetime) -> datetime:
        return _aware(v)


class SessionOut(BaseModel):
    session_id: UUID
    device_id: UUID
    subject_id: UUID | None
    firmware_version: str
    hardware_revision: str
    data_class: str
    body_site: str | None
    started_at: datetime
    ended_at: datetime | None
    notes: str | None
