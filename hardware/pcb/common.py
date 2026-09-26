"""Shared SKiDL setup for the VitalQ hw_v1 netlist.

Import this module before creating parts so the KiCad 9 libraries and the
project-local vitalq symbol library are on the search path.
"""

import os

import builtins

from skidl import KICAD9, Net, Part, POWER, lib_search_paths, set_default_tool

HERE = os.path.dirname(os.path.abspath(__file__))

# SKiDL warns, and skips the official libs, when these are unset.
for _ver in ("", "6", "7", "8", "9", "10"):
    _key = f"KICAD{_ver}_SYMBOL_DIR" if _ver else "KICAD_SYMBOL_DIR"
    os.environ.setdefault(_key, "/usr/share/kicad/symbols")

set_default_tool(KICAD9)
_lib = os.path.join(HERE, "lib")
if _lib not in lib_search_paths[KICAD9]:
    lib_search_paths[KICAD9].append(_lib)

FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_C_BULK = "Capacitor_SMD:C_0805_2012Metric"

_seq = {}


def ref(prefix):
    """Next reference designator. IC and connector refs are assigned by hand."""
    _seq[prefix] = _seq.get(prefix, 0) + 1
    return f"{prefix}{_seq[prefix]}"


def R(value):
    designator = ref("R")
    return Part("Device", "R", value=value, ref=designator, tag=designator, footprint=FP_R)


def C(value, bulk=False):
    designator = ref("C")
    fp = FP_C_BULK if bulk else FP_C
    return Part("Device", "C", value=value, ref=designator, tag=designator, footprint=fp)


def shunt(net, gnd, value, bulk=False):
    """Capacitor from net to gnd."""
    c = C(value, bulk=bulk)
    c[1] += net
    c[2] += gnd
    return c


def series(a, b, value):
    """Resistor between two nets. Returns the resistor."""
    r = R(value)
    r[1] += a
    r[2] += b
    return r


def pwr_flag(net):
    """Schematic-only power-output flag. Stripped from the PCB netlist later."""
    designator = ref("PFLG")
    flag = Part("power", "PWR_FLAG", value="PWR_FLAG", ref=designator, tag=designator)
    flag.exclude_from_bom = True
    flag[1] += net
    return flag


def power_net(name):
    net = Net(name)
    net.drive = POWER
    return net


def nc(part, *pins):
    """Mark pins as intentionally unconnected (default NC net, omitted from the PCB)."""
    for pin in pins:
        builtins.NC += part[pin]


def local_part(symbol, value, designator, footprint, mpn, description):
    part = Part("vitalq", symbol, value=value, ref=designator, tag=designator, footprint=footprint)
    part.fields["MPN"] = mpn
    part.description = description
    return part


def stock(lib, name, value, designator, footprint, mpn, description):
    part = Part(lib, name, value=value, ref=designator, tag=designator, footprint=footprint)
    part.fields["MPN"] = mpn
    part.description = description
    return part
