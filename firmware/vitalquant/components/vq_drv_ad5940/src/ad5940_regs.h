/* ad5940_regs.h — AD5940 register map + constants (module-private).
 *
 * Sources: AD5940 datasheet (Rev. 0) register summary and Analog Devices
 * ad5940lib (github.com/analogdevicesinc/ad5940lib) — names kept identical
 * to the vendor headers so the datasheet cross-references directly.
 * Anything not fully verified against the datasheet is marked
 * TODO(datasheet).
 */
#pragma once
#include <stdint.h>

/* ---------------------------------------------------------------
 * SPI protocol (datasheet "SPI Interface" + ad5940lib ad5940.c):
 *   write: CS↓ [0x20, addrH, addrL] CS↑  CS↓ [0x2d, data..] CS↑
 *   read : CS↓ [0x20, addrH, addrL] CS↑  CS↓ [0x6d, dummy, data..] CS↑
 *   data width = 32 bit for 0x1000..0x3014, else 16 bit (MSB first)
 *   fifo : CS↓ [0x5f, 6×dummy, then n words] CS↑  (last two words are
 *          clocked out while shifting 0x44444444 — vendor quirk)
 * --------------------------------------------------------------- */
#define AD5940_SPICMD_SETADDR   0x20
#define AD5940_SPICMD_READREG   0x6d
#define AD5940_SPICMD_WRITEREG  0x2d
#define AD5940_SPICMD_READFIFO  0x5f

#define AD5940_REG_WIDE(addr)   ((addr) >= 0x1000 && (addr) <= 0x3014)

/* --- IDs --- */
#define REG_ADIID               0x0400   /* fixed 0x4144 — wake/detect check */
#define REG_CHIPID              0x0404   /* 0x5500 S1, 0x5501/0x5502 S2     */
#define AD5940_ADIID_VAL        0x4144
#define AD5940_CHIPID_S1        0x5500
#define AD5940_CHIPID_S2        0x5501   /* 0x5502 == S3, same behaviour    */
#define AD5940_CHIPID_S3        0x5502

/* --- clocks / power (host-only regs) --- */
#define REG_CLKCON0             0x0408   /* SYSCLKDIV[5:0] ADCCLKDIV[9:6]   */
#define REG_CLKEN1              0x0410
#define REG_CLKSEL              0x0414   /* SYSCLKSEL[1:0] ADCCLKSEL[3:2]   */
#define REG_SWRSTCON            0x0424
#define REG_TRIGSEQ             0x0430   /* bit n = trigger sequence n      */
#define REG_PWRMOD              0x0A00   /* RAMRETEN=bit15 SEQSLPEN=bit3    */
#define REG_PWRKEY              0x0A04   /* write 0x4859 then 0xF27B        */
#define REG_OSCKEY              0x0A0C
#define REG_OSCCON              0x0A10
#define REG_EI2CON              0x0A28
#define AD5940_KEY_OSCCON       0xcb14
#define AD5940_PWRKEY_1         0x4859
#define AD5940_PWRKEY_2         0xF27B

#define OSCCON_LFOSCEN          0x0001
#define OSCCON_HFOSCEN          0x0002
#define OSCCON_LFOSCOK          0x0100
#define OSCCON_HFOSCOK          0x0200

/* CLKSEL: SYSCLKSRC_HFOSC=0, ADCCLKSRC_HFOSC=0 → value 0.            */
#define SYSCLKDIV_2             2        /* HFOSC 32 MHz /2 → 16 MHz sysclk */
#define ADCCLKDIV_2             2        /* 32 MHz /2 → 16 MHz ADC clock    */

/* --- AFE block (sequencer-writable window is 0x2000..0x21FF) --- */
#define REG_AFECON              0x2000
#define REG_SEQCON              0x2004   /* SEQEN=bit0                      */
#define REG_FIFOCON             0x2008   /* DATAFIFOSRCSEL[15:13] EN[11]    */
#define REG_SWCON               0x200C   /* SWSOURCESEL=bit16 (commit)      */
#define REG_HSDACCON            0x2010
#define REG_WGCON               0x2014
#define REG_WGFCW               0x2030
#define REG_WGPHASE             0x2034
#define REG_WGOFFSET            0x2038
#define REG_WGAMPLITUDE         0x203C
#define REG_ADCFILTERCON        0x2044
#define REG_LPREFBUFCON         0x2050
#define REG_SYNCEXTDEVICE       0x2054   /* SYNC[7:0] seq-driven GPIO data  */
#define REG_DATAFIFORD          0x206C
#define REG_CMDFIFOWRITE        0x2070
#define REG_DFTREAL             0x2078
#define REG_DFTIMAG             0x207C
#define REG_SINC2DAT            0x2080
#define REG_AFEGENINTSTA        0x209C   /* seq/host custom IRQ 0-3         */
#define REG_HPOSCCON            0x20BC   /* CLK32MHZEN=bit2                 */
#define REG_DFTCON              0x20D0   /* DFTINSEL[21:20] DFTNUM[7:4] HANN[0] */
#define REG_LPTIASW0            0x20E4
#define REG_LPTIACON0           0x20EC
#define REG_HSRTIACON           0x20F0   /* CTIA[13:5] SW6[4] RTIA[3:0]     */
#define REG_HSTIACON            0x20FC
#define REG_SEQSLPLOCK          0x2118   /* key 0xa47e5 unlocks sleep       */
#define REG_SEQTRGSLP           0x211C   /* write 0 then 1 → hibernate      */
#define REG_LPDACDAT0           0x2120
#define REG_LPDACSW0            0x2124
#define REG_LPDACCON0           0x2128
#define REG_DSWFULLCON          0x2150
#define REG_NSWFULLCON          0x2154
#define REG_PSWFULLCON          0x2158
#define REG_TSWFULLCON          0x215C
#define REG_ADCCON              0x21A8
#define REG_SEQ0INFO            0x21CC   /* LEN[26:16] ADDR[10:0]           */
#define REG_SEQ2INFO            0x21D0
#define REG_CMDFIFOWADDR        0x21D4
#define REG_CMDDATACON          0x21D8   /* seq/data SRAM split + modes     */
#define REG_DATAFIFOTHRES       0x21E0   /* HIGHTHRES[26:16]                */
#define REG_SEQ3INFO            0x21E4
#define REG_SEQ1INFO            0x21E8
#define REG_FIFOCNTSTA          0x2200   /* DATAFIFOCNTSTA[26:16]           */
#define REG_CALDATLOCK          0x2230
#define REG_PMBW                0x22F0   /* SYSBW[3:2] SYSHP[0]             */
#define REG_ADCBUFCON           0x238C

/* --- interrupt controller (host-only) --- */
#define REG_INTCPOL             0x3000   /* bit0: 0=asserts LOW (neg edge),
                                          *       1=asserts HIGH (pos edge) */
#define REG_INTCCLR             0x3004
#define REG_INTCSEL0            0x3008
#define REG_INTCSEL1            0x300C
#define REG_INTCFLAG0           0x3010
#define REG_INTCFLAG1           0x3014

/* --- AGPIO (host-only, <0x2000 so NOT sequencer-writable) --- */
#define REG_GP0CON              0x0000   /* 2-bit func per pin              */
#define REG_GP0OEN              0x0004
#define REG_GP0PE               0x0008
#define REG_GP0IEN              0x000C
#define REG_GP0IN               0x0010
#define REG_GP0OUT              0x0014
#define REG_GP0SET              0x0018
#define REG_GP0CLR              0x001C
#define REG_GP0TGL              0x0020

/* GP0CON pin function codes (2 bits each, pin n at bit 2n) */
#define AGPIO_FUNC_INT          0        /* pin = INTC0 output              */
#define AGPIO_FUNC_TRIG         1        /* pin = seq trigger input         */
#define AGPIO_FUNC_SYNC         2        /* pin = SYNCEXTDEVICE-driven out  */
#define AGPIO_FUNC_GPIO         3        /* pin = normal GPIO (NOT on GP2!) */
#define AGPIO_PIN0              0x01
#define AGPIO_PIN2              0x04

/* AFECON bits — note HPREFDIS/ALDOILIMITEN are inverted-semantics in the
 * vendor lib: leaving them 0 keeps the HP reference on / ALDO limit on. */
#define AFECON_DACBUFEN         (1u << 21)
#define AFECON_DACREFEN         (1u << 20)
#define AFECON_ALDOILIMITEN     (1u << 19)   /* 1 = limit DISABLED (lib quirk) */
#define AFECON_SINC2EN          (1u << 16)
#define AFECON_DFTEN            (1u << 15)
#define AFECON_WAVEGENEN        (1u << 14)
#define AFECON_TIAEN            (1u << 11)
#define AFECON_INAMPEN          (1u << 10)
#define AFECON_EXBUFEN          (1u << 9)
#define AFECON_ADCCONVEN        (1u << 8)
#define AFECON_ADCEN            (1u << 7)
#define AFECON_DACEN            (1u << 6)
#define AFECON_HPREFDIS         (1u << 5)    /* 1 = HP reference OFF         */

/* FIFOCON */
#define FIFOCON_DATAFIFOEN      (1u << 11)
#define FIFOSRC_SINC3           0
#define FIFOSRC_DFT             2        /* pushes {real, imag} per result  */
#define FIFOSRC_SINC2NOTCH      3
#define FIFOMODE_FIFO           2        /* CMDDATACON.DATAMEMMDE field     */

/* CMDDATACON: cmd seq memory 2kB (1), data fifo 4kB (2), fifo mode fifo(2) */
#define CMDDATACON_DEF          ((2u << 9) | (2u << 6) | (1u << 3) | (1u << 0))

/* INTC source masks (INTCSEL0/1, INTCFLAG0/1, INTCCLR) */
#define AFEINTSRC_ADCRDY        0x00000001u
#define AFEINTSRC_DFTRDY        0x00000002u
#define AFEINTSRC_SINC2RDY      0x00000004u
#define AFEINTSRC_CUSTOMINT0    0x00000200u  /* AFEGENINTSTA bit0           */
#define AFEINTSRC_ENDSEQ        0x00008000u
#define AFEINTSRC_SEQTIMEOUTERR 0x00020000u
#define AFEINTSRC_DATAFIFOTHRESH 0x02000000u
#define AFEINTSRC_DATAFIFOOF    0x04000000u
#define AFEINTSRC_DATAFIFOUF    0x08000000u
#define AFEINTSRC_ALLINT        0xffffffffu

/* Switch matrix selects (DSWFULLCON/PSWFULLCON/NSWFULLCON/TSWFULLCON) */
#define SWD_OPEN        0
#define SWD_RCAL0       (1u << 0)
#define SWD_AIN1        (1u << 1)
#define SWD_CE0         (1u << 4)
#define SWP_OPEN        0
#define SWP_RCAL0       (1u << 0)
#define SWP_AIN3        (1u << 3)
#define SWP_RE0         (1u << 4)
#define SWP_CE0         (1u << 10)
#define SWP_PL          (1u << 13)
#define SWP_PL2         (1u << 14)
#define SWN_OPEN        0
#define SWN_AIN0        (1u << 0)
#define SWN_DE0LOAD     (1u << 5)
#define SWN_SE0LOAD     (1u << 4)
#define SWN_SE0         (1u << 8)
#define SWN_RCAL1       (1u << 9)
#define SWN_NL          (1u << 10)
#define SWN_NL2         (1u << 11)
#define SWT_OPEN        0
#define SWT_AIN2        (1u << 2)
#define SWT_SE0LOAD     (1u << 4)
#define SWT_TRTIA       (1u << 8)      /* T9: close TIA feedback path     */
#define SWT_RCAL1       (1u << 11)
#define SWCON_SWSOURCESEL (1u << 16)

/* HS loop */
#define HSTIARTIA_1K    1
#define HSTIARTIA_5K    2
#define HSTIARTIA_10K   3
#define HSTIARTIA_40K   5
#define HSTIARTIA_160K  7
#define HSTIABIAS_1P1   0              /* HSTIACON VBIASSEL                */
#define WGTYPE_SIN      2              /* WGCON TYPESEL                    */
#define AD5940_SIN_FCW_BITS_S2 30       /* FCW width, silicon S2/S3         */
#define AD5940_SIN_FCW_BITS_S1 26
#define WGAMP_800MVPP   800.0f         /* WGAMPLITUDE full-scale ref       */

/* DSP */
#define ADCPGA_1        0
#define ADCPGA_4        3
#define ADCMUXP_HSTIA_P 0x01
#define ADCMUXP_LPTIA0_P 0x21
#define ADCMUXN_HSTIA_N 0x01
#define ADCMUXN_LPTIA0_N 0x02
#define ADCRATE_800KHZ  1              /* ADC clk 16 MHz → 800 kSPS        */
#define ADCSINC3OSR_2   2
#define ADCSINC2OSR_533 5
#define DFTSRC_SINC3    1
#define DFTNUM_4096     10             /* idx → points = 4 << idx          */
#define DFTNUM_2048     9
#define DFT_HANNING     1

/* LP loop (sweat amperometric leg) */
#define LPDACCON0_WAVETYPE_MMR  0
#define LPDACCON0_PWDEN_ON      0       /* bit1=0 → powered                */
#define LPDACCON0_RSTEN         0x01    /* bit0: allow LPDACDAT0 writes    */
#define LPDACSW_VBIAS2LPPA      0x10
#define LPDACSW_VZERO2LPTIA     0x04
#define LPDACSW_VZERO2PIN       0x02
#define LPTIASW_PABIASSEL       (1u << 12)
#define LPTIASW_TIABIASSEL      (1u << 13)
#define LPTIASW_NORM            0x2C    /* vendor "normal work mode" set   */
#define LPTIACON0_RF_100K       (3u << 13)
#define LPTIACON0_RL_SHORT      (0u << 10)
#define LPTIACON0_RTIA_10K      (8u << 5)
#define DAC12BIT_1LSB_MV        (2200.0f / 4095.0f)   /* 2.2 V range       */
#define DAC6BIT_1LSB_MV         (DAC12BIT_1LSB_MV * 64.0f)

/* Sequencer commands — SEQ_WR is only legal for 0x2000..0x21FF regs and
 * 24-bit data (datasheet sequencer section + ad5940lib).                */
#define SEQ_WAIT(n)     (0x00000000u | ((uint32_t)(n) & 0x3fffffffu))
#define SEQ_TOUT(n)     (0x40000000u | ((uint32_t)(n) & 0x3fffffffu))
#define SEQ_WR(a, d)    (0x80000000u | (((((uint32_t)(a)) >> 2) & 0x7f) << 24) \
                                     | (((uint32_t)(d)) & 0xffffffu))
#define SEQ_STOP()      SEQ_WR(REG_SEQCON, 0x00)
#define SEQ_SLP()       SEQ_WR(REG_SEQTRGSLP, 1)
#define SEQ_INT0()      SEQ_WR(REG_AFEGENINTSTA, 1u)

#define SEQ_ID_0        0
#define SEQ_ID_1        1
#define SEQ_ID_2        2
#define SEQ_ID_3        3
#define SLPKEY_UNLOCK   0xa47e5u
#define LPMODEKEY       0xc59d6u

/* AD5940_Initialize() verbatim table — REQUIRED after every reset
 * (vendor ad5940lib; several entries are undocumented/OTP-related).     */
typedef struct { uint16_t addr; uint32_t data; } ad5940_regval_t;
static const ad5940_regval_t AD5940_INIT_TABLE[] = {
    {0x0908, 0x02c9},        /* watchdog config                          */
    {0x0c08, 0x206C},        /* timer/undocumented — vendor value        */
    {0x21F0, 0x0010},        /* REPEATADCCNV                             */
    {0x0410, 0x02c9},        /* CLKEN1                                   */
    {0x0A28, 0x0009},        /* EI2CON — external interrupt config       */
    {0x238c, 0x0104},        /* ADCBUFCON                                */
    {0x0a04, 0x4859},        /* PWRKEY                                   */
    {0x0a04, 0xF27B},        /* PWRKEY                                   */
    {0x0a00, 0x8009},        /* PWRMOD: RAMRETEN | SEQSLPEN | pwr 1      */
    {0x22F0, 0x0000},        /* PMBW: LP power, auto bandwidth           */
    {0x2230, 0xDE87A5AF},    /* CALDATLOCK unlock                        */
    {0x2250, 0x103F},        /* factory cal reg (undocumented)           */
    {0x22B0, 0x203C},        /* factory cal reg (undocumented)           */
    {0x2230, 0xDE87A5A0},    /* CALDATLOCK lock                          */
};
#define AD5940_INIT_TABLE_LEN  (sizeof(AD5940_INIT_TABLE) / sizeof(AD5940_INIT_TABLE[0]))
