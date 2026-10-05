#!/usr/bin/env python3
"""Emit the 16 HV/electrode nets via deterministic BFS flood routing.

Suppresses lane-squatter nets (obs.dead) so the reserved corridors are
usable; victims are re-routed afterwards by worker_route.py.
Round 0 emits at HW width; rounds 1-2 retry stragglers at 0.12mm.

usage: emit_hv.py <board> <pro_file>
"""
import sys
import time
import shutil
sys.path.insert(0, '.')
import pcbnew
import route_all as ra
import route_hv as rh
import route_hv_all as rha
from route_flood import pad_cells, bfs

IN = sys.argv[1]
PRO = sys.argv[2] if len(sys.argv) > 2 else IN.replace('.kicad_pcb', '.kicad_pro')

# nets whose lane-region copper was ripped / whose remaining copper must
# not block the corridors; all are re-routed afterwards.
DEAD = {
    '+1V8', '+3V3', '+3V3_ANA', 'AFE4900_ADC_RDY', 'AFE_CLK', 'AFE_N_AC',
    'AFE_P_AC', 'CE_SURGE', 'CHG_STAT', 'CS_AFE4900', 'CS_FLASH',
    'DE_SURGE', 'ECG_N', 'ECG_P', 'EXP_INT', 'FN_ISO', 'FN_SURGE',
    'FSR_ADC', 'GND', 'I2C_SDA', 'LED2_K', 'LED3_K', 'LED_A', 'MISO_AD',
    'MISO_FL', 'MX_ECG_INM', 'MX_ECG_INP', 'PD_A', 'PD_K', 'RE_SURGE',
    'RLDREF', 'RLD_CLAMP', 'RTC_INT', 'SE_SURGE', 'SWEAT_CE', 'SWEAT_RE',
    'VBUS',
    # second wave — non-HV nets whose vias/segs seal the corridor field
    'Q3_B1', 'CS_ADS1292', 'RC0_0', 'RC0_1', '+1V8_LDO', 'FP_ISO',
    'FN_ISO2', 'ESP_TX', 'ESP_RX', 'CP_RX', 'CP_TX', 'VBAT_SYS', 'VBAT',
    'FB_3V3', 'TX_SW', 'TX_5V', 'RX_5V', 'ADS_GPIO1', 'ADS_GPIO2',
    'AIN4_LPF0', 'AIN4_LPF1', 'LED1_K', 'LED4_K', 'FP_SURGE', 'SP_SURGE',
    'SN_SURGE', 'IN1P', 'IN1N', 'SPI_MISO', 'SPI_MOSI', 'SPI_SCK',
    'SCK_FL', 'MOSI_FL', 'CS_AD5940', 'Q3_B2', 'GAUGE_ALRT', 'AFE_INM',
    'AFE_INP', 'RLDOUT', 'MX_ADS_DRDY', 'I2C_SCL', 'CHG_PG', 'TS_EN',
    'BAT_NTC', 'MIC_B', 'MIC_A', 'HUM_SDA', 'HUM_SCL', 'TP_VBAT',
    'SW_CLK', 'SW_DIO', 'ESP_EN', 'ESP_IO0', 'U1_RX', 'U1_TX',
}

b = pcbnew.LoadBoard(IN)
assert ra.class_of(b, 'ECG1_PAD') == 'HV_ELECTRODE', 'netclasses unresolved!'
obs = ra.Obstacles(b)
guard = rh.SlotGuard(obs, b)
guard.wrap(obs)
rha.sibling_wrap(b, obs, guard)
obs.dead |= DEAD

todo = list(rha.PLANS)
passed = []
for rnd in range(3):
    still = []
    for net, (sref, sname), planf in todo:
        cls = ra.class_of(b, net)
        hw = rha.HW if rnd == 0 else 0.06
        _, (gref, gname) = planf()
        seed = pad_cells(b, sref, sname, net, cls, obs, hw)
        goals = pad_cells(b, gref, gname, net, cls, obs, hw)
        t0 = time.time()
        path, n = bfs(obs, seed, goals, net, cls, hw, cap=1500000)
        stat = 'UNREACHABLE'
        if path:
            sp = ra.simplify(path)
            pc = ra.path_clear(obs, sp, net, cls, hw)
            wv = rh.path_wall_violation(guard, sp, hw)
            if pc and not wv:
                ra.emit(b, obs, sp, net, hw * 2)
                stat = f'EMITTED {len(sp)}segs'
            else:
                stat = f'unsafe(c={pc},w={wv})'
        print(f'[r{rnd}] {net}: {stat} ({n}c,{time.time()-t0:.0f}s)',
              flush=True)
        pcbnew.SaveBoard(IN, b)
        dst = IN.replace('.kicad_pcb', '.kicad_pro')
        if dst != PRO:
            shutil.copy(PRO, dst)
        if stat.startswith('EMITTED'):
            passed.append(net)
        else:
            still.append((net, (sref, sname), planf))
    todo = still
    if not still:
        break
    print(f'== round {rnd} done, {len(still)} left ==', flush=True)
print(f'== FINISHED: {len(passed)} routed: {passed}', flush=True)
print(f'== open: {[n for n, _, _ in todo]}', flush=True)
