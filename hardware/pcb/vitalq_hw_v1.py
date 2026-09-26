#!/usr/bin/env python3
"""VitalQ hw_v1 schematic netlist.

Research prototype wiring only. This is not a medical device and it is not a
finished board: footprints are dropped on an import grid so the ratsnest can
be reviewed. Place and route in KiCad.

  python3 hardware/pcb/lib/make_libs.py
  python3 hardware/pcb/vitalq_hw_v1.py

Set VITALQ_SKIP_SCH=1 to stop after the SKiDL ERC and the netlist.
"""

import builtins
import json
import os
import subprocess
import sys
from pathlib import Path

import skidl
from skidl import ERC, Net, generate_netlist, generate_schematic
from skidl.logger import erc_logger
from skidl.net import NCNet
from skidl.part import default_empty_footprint_handler

from common import HERE, power_net, pwr_flag
from contact import contact
from i2c_sensors import i2c_sensors
from mcu import mcu
from power import power
from spi_afes import spi_afes
from usb import usb


def build():
    gnd = power_net("GND")
    vbus = power_net("VBUS")
    vbat = power_net("VBAT")
    vsys = power_net("VSYS")
    v3d = power_net("+3V3")
    v3a = power_net("+3.3VA")
    v3s = power_net("3V3_S")
    v1v8 = power_net("+1V8")
    # GND and VBUS have no power-output pin of their own.
    pwr_flag(gnd)
    pwr_flag(vbus)

    en = Net("ESP_EN")
    io0 = Net("ESP_IO0")
    tx = Net("UART_TX")
    rx = Net("UART_RX")
    sck = Net("SPI_SCK")
    mosi = Net("SPI_MOSI")
    miso = Net("SPI_MISO")
    cs_ad = Net("CS_AD5940")
    cs_ads = Net("CS_ADS1292R")
    cs_afe = Net("CS_AFE4900")
    afe_rst = Net("AFE4900_RESETZ")
    afe_rdy = Net("AFE4900_ADC_RDY")
    ad_rst = Net("AD5940_RESET")
    ad_gpio0 = Net("AD5940_GPIO0")
    ads_pwdn = Net("ADS1292_PWDN")
    ads_start = Net("ADS1292_START")
    ads_drdy = Net("ADS1292_DRDY")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    lsm_int = Net("LSM6_INT1")
    tmp_alert = Net("TMP117_ALERT")
    as_int = Net("AS7341_INT")
    fsr = Net("FSR_SENSE")
    vbat_adc = Net("VBAT_SENSE")

    vbat_sw = power(gnd, vbus, vbat, vsys, v3d, v3a, v3s, v1v8, tag="power")
    usb(gnd, vbus, v3d, en, io0, tx, rx, tag="usb")
    mcu(
        gnd, v3d, en, io0, tx, rx, sck, mosi, miso,
        cs_ad, cs_ads, cs_afe, afe_rst, afe_rdy, ad_rst, ad_gpio0,
        ads_pwdn, ads_start, ads_drdy, sda, scl, lsm_int, tmp_alert,
        as_int, fsr, vbat_adc,
        tag="mcu",
    )
    spi_afes(
        gnd, v3d, v3a, vsys, sck, mosi, miso,
        cs_ad, cs_ads, cs_afe, afe_rst, afe_rdy, ad_rst, ad_gpio0,
        ads_pwdn, ads_start, ads_drdy,
        tag="spi_afes",
    )
    i2c_sensors(gnd, v3s, v1v8, sda, scl, lsm_int, tmp_alert, as_int, tag="i2c_sensors")
    contact(gnd, v3s, vbat_sw, fsr, vbat_adc, tag="contact")


def _ignore_schematic_only_footprints(part):
    if part.name == "PWR_FLAG" or str(part.ref).startswith("PFLG"):
        return
    default_empty_footprint_handler(part)


def _erc_counts():
    errors = erc_logger.error.count + erc_logger.bare_error.count
    warnings = erc_logger.warning.count + erc_logger.bare_warning.count
    return errors, warnings


def _bom(path):
    rows = {}
    for part in builtins.default_circuit.parts:
        if part.name == "PWR_FLAG" or str(part.ref).startswith("PFLG"):
            continue
        mpn = part.fields.get("MPN", "") if getattr(part, "fields", None) else ""
        dnp = "yes" if getattr(part, "dnp", False) else ""
        key = (part.value, part.footprint, mpn, dnp, part.description or "")
        rows.setdefault(key, []).append(part.ref)
    lines = ["Qty,Value,Footprint,MPN,DNP,Refs,Description"]
    for (value, fp, mpn, dnp, desc), refs in sorted(rows.items(), key=lambda kv: kv[1][0]):
        ref_list = " ".join(sorted(refs, key=lambda r: (r[0], int("".join(ch for ch in r if ch.isdigit()) or "0"))))
        def q(text):
            text = str(text).replace('"', '""')
            return f'"{text}"'
        lines.append(",".join([
            str(len(refs)), q(value), q(fp), q(mpn), q(dnp), q(ref_list), q(desc),
        ]))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _board_json(path):
    """Physical parts and nets, without schematic-only power flags."""
    parts = []
    skip = set()
    for part in builtins.default_circuit.parts:
        if part.name == "PWR_FLAG" or str(part.ref).startswith("PFLG"):
            skip.add(part.ref)
            continue
        pins = []
        nc_pins = []
        for pin in part.pins:
            num = str(pin.num)
            pins.append(num)
            net = pin.net
            if net is None or isinstance(net, NCNet):
                nc_pins.append(num)
        parts.append({
            "ref": part.ref,
            "value": str(part.value),
            "footprint": part.footprint,
            "pins": pins,
            "nc_pins": nc_pins,
            "dnp": bool(getattr(part, "dnp", False)),
        })
    nets = []
    for net in builtins.default_circuit.get_nets():
        if isinstance(net, NCNet):
            continue
        pads = []
        for pin in net.pins:
            if pin.part.ref in skip:
                continue
            pads.append({"ref": pin.part.ref, "pin": str(pin.num)})
        if pads:
            nets.append({"name": net.name, "pads": pads})
    path.write_text(json.dumps({"parts": parts, "nets": nets}, indent=2), encoding="utf-8")


def _strip_power_flags(netlist_path):
    """Drop PWR_FLAG components from the KiCad netlist so pcbnew has nothing to place for them."""
    from simp_sexp import Sexp

    tree = Sexp(netlist_path.read_text(encoding="utf-8"))
    comps = tree.search("/export/components", ignore_case=True)
    nets = tree.search("/export/nets", ignore_case=True)
    if not comps or not nets:
        raise SystemExit("netlist is missing components or nets")
    comp_node = comps[0]
    net_node = nets[0]
    kept = [comp_node[0]]
    for comp in comp_node[1:]:
        ref = None
        for item in comp:
            if isinstance(item, list) and item and item[0] == "ref":
                ref = item[1].strip('"')
        if ref and ref.startswith("PFLG"):
            continue
        kept.append(comp)
    comp_node[:] = kept

    new_nets = [net_node[0]]
    for net in net_node[1:]:
        if not isinstance(net, list):
            new_nets.append(net)
            continue
        pruned = [net[0]]
        for item in net[1:]:
            if isinstance(item, list) and item and item[0] == "node":
                ref = None
                for sub in item:
                    if isinstance(sub, list) and sub and sub[0] == "ref":
                        ref = sub[1].strip('"')
                if ref and ref.startswith("PFLG"):
                    continue
            pruned.append(item)
        # Drop a net that lost every pad.
        if any(isinstance(item, list) and item and item[0] == "node" for item in pruned):
            new_nets.append(pruned)
    net_node[:] = new_nets
    netlist_path.write_text(tree.to_str() + "\n", encoding="utf-8")


def main():
    root = Path(HERE)
    os.chdir(root)
    build()
    ERC()
    errors, warnings = _erc_counts()
    summary = root / "erc_skidl.txt"
    summary.write_text(
        f"errors {errors}\nwarnings {warnings}\n",
        encoding="utf-8",
    )
    print(f"SKiDL ERC: {errors} errors, {warnings} warnings")
    if errors:
        sys.exit(1)

    skidl.empty_footprint_handler = _ignore_schematic_only_footprints
    net_path = root / "vitalq_hw_v1.net"
    generate_netlist(file_=str(net_path), track_abs_path=False)
    _strip_power_flags(net_path)
    _bom(root / "vitalq_hw_v1_bom.csv")
    build_dir = root / "build"
    build_dir.mkdir(exist_ok=True)
    _board_json(build_dir / "board.json")
    subprocess.check_call(["/usr/bin/python3", str(root / "make_pcb.py")], cwd=root)

    if os.environ.get("VITALQ_SKIP_SCH") == "1":
        print("skipped schematic (VITALQ_SKIP_SCH=1)")
        return

    # The wire router does not finish on this netlist. Explicit stubs are the
    # same labels-only schematic auto_stub falls back to, and they still ERC.
    # Power names become KiCad power symbols. NC pins stay no-connect flags.
    for net in builtins.default_circuit.nets:
        if net.valid:
            net.stub = True

    # auto_stub stays off. With every net already stubbed it would still run
    # the two-pin snap pass, which stacks passives and power symbols and
    # shorts unrelated nets in the schematic. Labels are emitted because
    # net.stub is set above.
    generate_schematic(
        filepath=str(root),
        top_name="vitalq_hw_v1",
        title="VitalQ hw_v1 research prototype — wiring only, not placed",
        flatness=0.0,
        retries=1,
        auto_stub=False,
    )
    print("schematic written")


if __name__ == "__main__":
    main()
