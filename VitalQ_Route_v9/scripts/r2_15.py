"""R2-15 (approved): adopt R2-14 candidate (23 open); replace only the ESP_RX via at (23.75,52.35) that violates J8 NPTH hole-to-copper 0.2."""
from r2 import ROOT
from r2_batch import run
from r2_rip import rip, prune_orphans, reconnect
from r2route import SIGNAL_LAYERS
from session import summary

BASE = ROOT / 'candidates' / 'R2-14'
BAD = ['88621442-a3a2-4ce0-89af-5bb3db071f7e', 'bb47d9ec-983d-41a9-9f2d-6db95ec442c0', '3233c28b-ebea-4cd3-8a13-8d041156c128']
APP = 'R2-15 ESP_RX re-route avoiding J8 NPTH (via hole/copper to NPTH hole >= 0.2), A*'


def plan(b, ledger, opens):
    assert all(u in b.items and b.items[u]['net'] == 'ESP_RX' for u in BAD)
    rip(b, ledger, BAD, 'R2-15 approved: ESP_RX via 0.17 mm from J8 NPTH hole (JLC 0.2) and its two R2-14 segments')
    prune_orphans(b, ledger, {'ESP_RX'})
    reconnect(b, ledger, {'ESP_RX'}, [22.0, 50.0, 27.5, 54.0], [(APP + ', margin 3 mm', 3.0, SIGNAL_LAYERS), (APP + ', margin 6 mm', 6.0, SIGNAL_LAYERS)])


def extra(d):
    s = summary(d / 'drc.json')
    ok = s['unconnected'] <= 23 and s['real'] <= 235
    return {'pass': ok, 'text': f"approved gate unconnected {s['unconnected']}<=23, real {s['real']}<=235 -> {'PASS' if ok else 'FAIL'}"}


run('R2-15', 'adopt_R2-14_fix_ESP_RX_via', plan, base=BASE, extra_check=extra, next_step='R2-16 J8-covered U6 balls with per-via POFV notches')
