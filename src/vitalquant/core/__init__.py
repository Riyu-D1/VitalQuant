"""vitalquant.core — shared schema, channel registry, clock model, hardware profiles."""

from vitalquant.core.channels import CHANNEL_IDS, CHANNELS, SPECTRAL_CHANNELS, Channel
from vitalquant.core.clock import ClockAnchor, ClockJumpError, ClockModel
from vitalquant.core.config import HardwareProfile, load_profile
from vitalquant.core.types import BatchIngest, SessionCreate, SessionOut

__all__ = [
    "CHANNELS",
    "CHANNEL_IDS",
    "SPECTRAL_CHANNELS",
    "BatchIngest",
    "Channel",
    "ClockAnchor",
    "ClockJumpError",
    "ClockModel",
    "HardwareProfile",
    "SessionCreate",
    "SessionOut",
    "load_profile",
]
