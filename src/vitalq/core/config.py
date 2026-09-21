"""Hardware profile schema + loader (spec §34).

One YAML file per hardware revision; sensors, buses, rates and optics are
configuration, never compiled code. `profile_hash` lets the backend detect
firmware/config drift on ingest.
"""

from __future__ import annotations

import hashlib
from typing import Literal

import yaml
from pydantic import BaseModel, Field, field_validator, model_validator

from vitalq.core.channels import (
    CHANNEL_IDS,
    SPECTRAL_CHANNELS,
    get_channel,
)

# Sensors the codebase ships drivers/abstractions for. A model name outside this
# set fails validation loudly rather than being silently ignored by firmware.
KNOWN_SENSOR_MODELS = {
    "MAX30102", "MAX86140", "MAX86141",
    "AS7341", "AS7341-DLGM",
    "MLX90614", "MLX90632", "MLX90637",
    "BME280", "BME680",
    "MPU6050", "ICM-42670-P", "LSM6DS3", "BMI270",
    "FSR402",
}

SENSOR_KINDS = ("ppg", "spectral", "temperature", "environment", "motion", "contact")


class BusI2C(BaseModel):
    sda: int
    scl: int
    hz: int = 400000


class BusSPI(BaseModel):
    miso: int
    mosi: int
    sclk: int


class Compute(BaseModel):
    soc: str = "esp32s3"
    buses: dict[str, BusI2C | BusSPI] = Field(default_factory=dict)


class ClockConfig(BaseModel):
    ntp_sync_interval_s: int = 3600
    drift_estimate_window_batches: int = 20
    max_unsynced_s: int = 7200


class PpgChannelDef(BaseModel):
    id: str
    wavelength_nm: int
    led_driver: int | None = None
    pd: int | None = None

    @field_validator("id")
    @classmethod
    def _known(cls, v: str) -> str:
        if get_channel(v).sample_role.value != "waveform":
            raise ValueError(f"{v!r} is not a waveform channel")
        return v


class SensorDef(BaseModel):
    """Generic sensor entry; `kind` is set by the parent mapping key."""

    enabled: bool = True
    model: str
    bus: str | None = None
    address: str | None = None
    input: str | None = None                      # analog pin e.g. adc1_ch0 (FSR)
    outputs: list[str] = Field(default_factory=list)
    rate_hz: float | None = None
    sample_rate_hz: float | None = None
    channels: list[str | PpgChannelDef] = Field(default_factory=list)
    # optical / spectral extras
    led_current_ma: list[float] | None = None
    ambient_cancellation: bool | None = None
    parallel_groups: int | None = None
    gain: float | None = None
    integ_time_ms: float | None = None
    illumination: Literal["ambient", "emitter_on", "dark_frame"] | None = None
    frame_rate_hz: float | None = None

    @field_validator("model")
    @classmethod
    def _known_model(cls, v: str) -> str:
        if v not in KNOWN_SENSOR_MODELS:
            raise ValueError(f"unknown sensor model {v!r} — add it to KNOWN_SENSOR_MODELS")
        return v

    @field_validator("outputs")
    @classmethod
    def _known_channels(cls, v: list[str]) -> list[str]:
        for ch in v:
            get_channel(ch)
        return v

    @field_validator("address")
    @classmethod
    def _addr(cls, v: str | None) -> str | None:
        if v is not None:
            int(v, 16)  # raises ValueError if not hex — fail at config load
        return v


class StorageConfig(BaseModel):
    offline_buffer: Literal["flash_ring", "sdcard"] = "flash_ring"
    buffer_capacity_s: int = 1800


class HardwareProfile(BaseModel):
    hardware_revision: str
    description: str = ""
    compute: Compute = Field(default_factory=Compute)
    clock: ClockConfig = Field(default_factory=ClockConfig)
    sensors: dict[str, SensorDef] = Field(default_factory=dict)
    power_profile: Literal["battery", "mains"] = "battery"
    storage: StorageConfig = Field(default_factory=StorageConfig)

    @model_validator(mode="after")
    def _check(self) -> HardwareProfile:
        bus_names = set(self.compute.buses)
        for kind, s in self.sensors.items():
            if kind not in SENSOR_KINDS:
                raise ValueError(f"unknown sensor kind {kind!r}")
            if not s.enabled:
                continue
            if s.bus is not None and s.bus not in bus_names:
                raise ValueError(f"{kind}: bus {s.bus!r} not defined in compute.buses")
            if s.bus is None and s.input is None:
                raise ValueError(f"{kind}: enabled sensor needs a bus or an analog input")
            if kind == "spectral":
                for ch in s.channels:
                    name = ch if isinstance(ch, str) else ch.id
                    if name not in SPECTRAL_CHANNELS:
                        raise ValueError(f"spectral channel {name!r} not in {SPECTRAL_CHANNELS}")
            if kind == "ppg":
                for ch in s.channels:
                    if isinstance(ch, str):
                        raise ValueError("ppg.channels entries must be {id, wavelength_nm, ...}")
            for out in s.outputs:
                if out not in CHANNEL_IDS:
                    raise ValueError(f"{kind}: unknown output channel {out!r}")
        return self

    @property
    def profile_hash(self) -> str:
        canonical = self.model_dump_json(exclude_none=True)
        return "sha256:" + hashlib.sha256(canonical.encode()).hexdigest()[:16]

    def enabled_sensors(self) -> dict[str, SensorDef]:
        return {k: v for k, v in self.sensors.items() if v.enabled}

    def declared_channels(self) -> list[str]:
        """All channel ids the profile can emit."""
        out: list[str] = []
        for s in self.enabled_sensors().values():
            out.extend(s.outputs)
            for ch in s.channels:
                out.append(ch.id if isinstance(ch, PpgChannelDef) else f"spectral.{ch}")
        return sorted(set(out))


def load_profile(path: str) -> HardwareProfile:
    with open(path) as f:
        return HardwareProfile(**yaml.safe_load(f))
