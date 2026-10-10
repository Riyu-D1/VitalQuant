from r2route import SIGNAL_LAYERS

MOVES = [
    {'ref': 'U19', 'from': [17.087664, 43.435463], 'to': [17.087664, 44.435463], 'detach': True, 'reason': 'SUPERVISOR_R2_01: dy=+1.00 moves U19 signal pins out of hv_inner so inner-layer escapes/vias can be legal'},
    {'ref': 'C59', 'from': [15.647664, 40.509838], 'to': [15.897664, 39.509838], 'detach': True, 'reason': 'SUPERVISOR_R2_01: (+0.25,-1.00), 1.03 mm authorised passive exception; C59.1 out of hv_inner for a legal +3V3 plane via'},
]
TARGET_NETS = {'ADS1292_PWDN', 'IR_GATE', 'AD5940_RESET'}
REGION = [12.0, 37.0, 22.0, 48.0]
APPROACH = [('R2-4 after U19/C59 moves + authorised outer-layer rip-up of copper conflicting with moved pads; A* F/B/In2/In3 (inner only outside hv_inner), margin 3 mm', 3.0, SIGNAL_LAYERS),
            ('R2-4 same after rip-up, wide detour margin 8 mm', 8.0, SIGNAL_LAYERS)]

