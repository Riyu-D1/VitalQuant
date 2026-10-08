"""Channel registry — the single source of truth for measurement channel ids.

Every channel id used in ingest envelopes, hardware profiles and DB rows must be
registered here. Unknown channels are rejected at ingest (API spec, error
`schema.channel_unknown`).
"""

from __future__ import annotations

from enum import Enum

from pydantic import BaseModel


class SensorType(str, Enum):
    OPTICAL = "optical"
    SPECTRAL = "spectral"
    TEMPERATURE = "temperature"
    ENVIRONMENT = "environment"
    MOTION = "motion"
    CONTACT = "contact"
    SYSTEM = "system"
    EXTERNAL = "external"   # imported recordings (WFDB etc.) or derived streams


class SampleRole(str, Enum):
    WAVEFORM = "waveform"   # high-rate → raw.signal_windows
    SCALAR = "scalar"       # row-per-sample → raw.measurements_scalar
    FRAME = "frame"         # multi-channel frame → raw.spectral_frames


class Channel(BaseModel, frozen=True):
    channel_id: str
    sensor_type: SensorType
    unit: str | None  # SI only; None = unitless/counts
    sample_role: SampleRole
    # plausible physical range for validation (None = unchecked)
    plausible_min: float | None = None
    plausible_max: float | None = None


_CHANNELS: list[Channel] = [
    # Optical / PPG — wavelengths are hardware-profile properties, not channel names
    Channel(channel_id="ppg.red", sensor_type=SensorType.OPTICAL, unit="count",
            sample_role=SampleRole.WAVEFORM, plausible_min=0),
    Channel(channel_id="ppg.ir", sensor_type=SensorType.OPTICAL, unit="count",
            sample_role=SampleRole.WAVEFORM, plausible_min=0),
    Channel(channel_id="ppg.ch3", sensor_type=SensorType.OPTICAL, unit="count",
            sample_role=SampleRole.WAVEFORM, plausible_min=0),  # MAX86141 extra PD
    Channel(channel_id="ppg.ch4", sensor_type=SensorType.OPTICAL, unit="count",
            sample_role=SampleRole.WAVEFORM, plausible_min=0),
    # Temperature (non-contact IR thermopile; ambient is the sensor die temperature)
    Channel(channel_id="temp.object", sensor_type=SensorType.TEMPERATURE, unit="degC",
            sample_role=SampleRole.SCALAR, plausible_min=-40, plausible_max=125),
    Channel(channel_id="temp.ambient", sensor_type=SensorType.TEMPERATURE, unit="degC",
            sample_role=SampleRole.SCALAR, plausible_min=-40, plausible_max=125),
    # Environment
    Channel(channel_id="env.temperature", sensor_type=SensorType.ENVIRONMENT, unit="degC",
            sample_role=SampleRole.SCALAR, plausible_min=-40, plausible_max=85),
    Channel(channel_id="env.humidity", sensor_type=SensorType.ENVIRONMENT, unit="pctRH",
            sample_role=SampleRole.SCALAR, plausible_min=0, plausible_max=100),
    Channel(channel_id="env.pressure", sensor_type=SensorType.ENVIRONMENT, unit="hPa",
            sample_role=SampleRole.SCALAR, plausible_min=300, plausible_max=1100),
    Channel(channel_id="env.gas_resistance", sensor_type=SensorType.ENVIRONMENT, unit="ohm",
            sample_role=SampleRole.SCALAR, plausible_min=0),
    # Motion
    Channel(channel_id="motion.accel_x", sensor_type=SensorType.MOTION, unit="g",
            sample_role=SampleRole.WAVEFORM, plausible_min=-20, plausible_max=20),
    Channel(channel_id="motion.accel_y", sensor_type=SensorType.MOTION, unit="g",
            sample_role=SampleRole.WAVEFORM, plausible_min=-20, plausible_max=20),
    Channel(channel_id="motion.accel_z", sensor_type=SensorType.MOTION, unit="g",
            sample_role=SampleRole.WAVEFORM, plausible_min=-20, plausible_max=20),
    Channel(channel_id="motion.gyro_x", sensor_type=SensorType.MOTION, unit="dps",
            sample_role=SampleRole.WAVEFORM, plausible_min=-2000, plausible_max=2000),
    Channel(channel_id="motion.gyro_y", sensor_type=SensorType.MOTION, unit="dps",
            sample_role=SampleRole.WAVEFORM, plausible_min=-2000, plausible_max=2000),
    Channel(channel_id="motion.gyro_z", sensor_type=SensorType.MOTION, unit="dps",
            sample_role=SampleRole.WAVEFORM, plausible_min=-2000, plausible_max=2000),
    # Contact (FSR — ordinal level only, audit C6: never a pressure in units)
    Channel(channel_id="contact.level", sensor_type=SensorType.CONTACT, unit=None,
            sample_role=SampleRole.SCALAR, plausible_min=0, plausible_max=4095),
    # System housekeeping
    Channel(channel_id="sys.battery_v", sensor_type=SensorType.SYSTEM, unit="V",
            sample_role=SampleRole.SCALAR, plausible_min=2.5, plausible_max=5.5),
    # External/imported waveforms (public datasets, auxiliary leads). No plausible
    # range — units vary by source and are recorded in session notes.
    Channel(channel_id="resp.waveform", sensor_type=SensorType.EXTERNAL,
            unit=None, sample_role=SampleRole.WAVEFORM),
    Channel(channel_id="ecg.ii", sensor_type=SensorType.EXTERNAL,
            unit=None, sample_role=SampleRole.WAVEFORM),
    Channel(channel_id="ecg.v", sensor_type=SensorType.EXTERNAL,
            unit=None, sample_role=SampleRole.WAVEFORM),
    Channel(channel_id="ecg.avr", sensor_type=SensorType.EXTERNAL,
            unit=None, sample_role=SampleRole.WAVEFORM),
]

# Spectral frame channel names (AS7341 family) — validated separately because they are
# subfields of raw.spectral_frames.channels, not standalone channel rows.
SPECTRAL_CHANNELS = ("f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "clear", "nir", "flicker")
ILLUMINATION_STATES = ("ambient", "emitter_on", "dark_frame")

CHANNELS: dict[str, Channel] = {c.channel_id: c for c in _CHANNELS}
CHANNEL_IDS: frozenset[str] = frozenset(CHANNELS)


def get_channel(channel_id: str) -> Channel:
    try:
        return CHANNELS[channel_id]
    except KeyError:
        raise ValueError(f"unknown channel: {channel_id!r}") from None
