"""vitalq.core — shared schema, channel registry, clock model, hardware profiles."""

from vitalq.core.channels import CHANNEL_IDS, CHANNELS, SPECTRAL_CHANNELS, Channel
from vitalq.core.clock import ClockAnchor, ClockJumpError, ClockModel
from vitalq.core.config import HardwareProfile, load_profile
from vitalq.core.types import BatchIngest, SessionCreate, SessionOut

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
