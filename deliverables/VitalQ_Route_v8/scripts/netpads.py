import pcbnew, json, re, math
from collections import defaultdict
board=pcbnew.LoadBoard('vitalq_v2.kicad_pcb')
NM=1e6
nets=['GND','+3V3','+3V3_ANA','SPI_MOSI','SPI_SCK','CS_MAX86178','CS_AD5940','CP_RX','CP_TX','CS_AFE4900','VREF_2V5','TMP117_ALERT','AD5940_GPIO0','MAX86178_INT','MISO_FL','AIN4_LPF0','SWEAT_WE','AFE_INM','BIOZ_SP_PAD','ADS1292_PWDN','AD5940_RESET','IR_GATE','EDA_RE_PAD','SE_SURGE','MISO_AFE','VBIAS0','BIOZ_FP','BIOZ_SP','DE0','VZERO0','BIOZ_FN','J11_RE','PD_INP','PD_INM','PD2_INP','TX1','TX3','PD2_INM','TX2','LED1_K','LED2_K','LED3_K','AFE_N_PAD','AFE_BG','RLD_PAD','unconnected-(J12-PadMP)']
for net in nets:
    pads=[]
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname()==net:
                pp=p.GetPosition()
                pads.append(f"{fp.GetReference()}.{p.GetNumber()}@({pp.x/NM:.2f},{pp.y/NM:.2f}){'B' if fp.GetSide() else 'F'}")
    trks=[t for t in board.GetTracks() if t.GetNetname()==net and t.Type()!=pcbnew.PCB_VIA_T]
    vias=[t for t in board.GetTracks() if t.GetNetname()==net and t.Type()==pcbnew.PCB_VIA_T]
    print(f"{net:<26} pads({len(pads)}): {' '.join(sorted(pads))}  tracks={len(trks)} vias={len(vias)}")
