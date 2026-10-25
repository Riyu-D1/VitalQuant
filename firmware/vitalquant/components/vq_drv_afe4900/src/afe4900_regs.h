/* afe4900_regs.h — AFE4900YZR register map + wire protocol (module-private).
 *
 * SOURCES / CONFIDENCE
 *   TI SBAS857 (full AFE4900 datasheet) is selective-disclosure; only the
 *   8-page brief (SBAS861B) is public. Addresses below marked [evm] are
 *   cross-checked against public AFE4900EVM/CCS register dumps; they match
 *   the AFE44xx-family map the AFE4900 extends. Everything else carries
 *   TODO(datasheet) and must be confirmed against SBAS857 before ship.
 *
 * WIRE PROTOCOL (SPI mode — requires I2C_SPI_SEL=1; pin audit U6.A4 says
 *   "1=SPI, 0=I2C" and the ball is tied to +3V3_ANA):
 *   Frame = 32 SCLKs, MSB first, SPI mode 0, <=8 MHz, SEN active low.
 *     WRITE: {8-bit reg addr}{24-bit data}; reg latches on the 32nd clock.
 *     READ:  set CONTROL0.SPI_READ once, then a write-shaped frame
 *            {addr}{24 dummy} returns the register on SDOUT during the
 *            24 data clocks (family "read-after-write" scheme). Clear
 *            SPI_READ before any write. Back-to-back 32-bit frames inside
 *            one SEN pulse are legal -> FIFO bursts are single-CS.
 *   TODO(datasheet): confirm no dedicated read opcode / alternate
 *   read-after-write sequence is required instead of SPI_READ.
 */
#pragma once
#include <stdint.h>

/* ---- Confirmed register addresses [evm] ----------------------------- */
#define AFE4900_REG_CONTROL0        0x00u   /* global control            */
#define AFE4900_REG_LED2STC         0x01u   /* LED2 sample window        */
#define AFE4900_REG_LED2ENDC        0x02u
#define AFE4900_REG_LED1LEDSTC      0x03u   /* LED1 fire window          */
#define AFE4900_REG_LED1LEDENDC     0x04u
#define AFE4900_REG_ALED2STC        0x05u   /* ALED2 window — doubles as */
#define AFE4900_REG_ALED2ENDC       0x06u   /* the LED3 sample window    */
#define AFE4900_REG_LED1STC         0x07u   /* LED1 sample window        */
#define AFE4900_REG_LED1ENDC        0x08u
#define AFE4900_REG_LED2LEDSTC      0x09u   /* LED2 fire window          */
#define AFE4900_REG_LED2LEDENDC     0x0Au
#define AFE4900_REG_ALED1STC        0x0Bu   /* ALED1 window — doubles as */
#define AFE4900_REG_ALED1ENDC       0x0Cu   /* the LED4 sample window    */
#define AFE4900_REG_LED2CONVST      0x0Du   /* conversion windows        */
#define AFE4900_REG_LED2CONVEND     0x0Eu
#define AFE4900_REG_ALED2CONVST     0x0Fu
#define AFE4900_REG_ALED2CONVEND    0x10u
#define AFE4900_REG_LED1CONVST      0x11u
#define AFE4900_REG_LED1CONVEND     0x12u
#define AFE4900_REG_ALED1CONVST     0x13u
#define AFE4900_REG_ALED1CONVEND    0x14u
#define AFE4900_REG_ADCRSTST0       0x15u   /* ADC reset windows 0..3    */
#define AFE4900_REG_ADCRSTEND0      0x16u
#define AFE4900_REG_ADCRSTST1       0x17u
#define AFE4900_REG_ADCRSTEND1      0x18u
#define AFE4900_REG_ADCRSTST2       0x19u
#define AFE4900_REG_ADCRSTEND2      0x1Au
#define AFE4900_REG_ADCRSTST3       0x1Bu
#define AFE4900_REG_ADCRSTEND3      0x1Cu
#define AFE4900_REG_PRPCOUNT        0x1Du   /* pulse-rep period count    */
#define AFE4900_REG_CONTROL1        0x1Eu   /* TIMEREN + NUMAV           */
#define AFE4900_REG_TIAGAIN_2_3     0x1Fu   /* TIA gain, PD2/PD3         */
#define AFE4900_REG_TIAGAIN         0x20u   /* TIA gain, PD1             */
#define AFE4900_REG_TIA_AMB_GAIN    0x21u   /* ambient-path TIA gain     */
#define AFE4900_REG_LEDCNTRL1       0x22u   /* LED current codes (map?)  */
#define AFE4900_REG_CONTROL2        0x23u   /* clock sel + LED fullscale */
#define AFE4900_REG_LEDCNTRL2       0x24u
#define AFE4900_REG_TOGGLE          0x28u
#define AFE4900_REG_CLKDIV1         0x29u
#define AFE4900_REG_LED2VAL         0x2Au   /* latest results            */
#define AFE4900_REG_ALED2VAL        0x2Bu
#define AFE4900_REG_LED1VAL         0x2Cu
#define AFE4900_REG_ALED1VAL        0x2Du
#define AFE4900_REG_LED2_ALED2VAL   0x2Eu   /* LED-minus-ambient         */
#define AFE4900_REG_LED1_ALED1VAL   0x2Fu
#define AFE4900_REG_CONTROL3        0x31u
#define AFE4900_REG_PROG_INT2_STC   0x34u
#define AFE4900_REG_PROG_INT2_ENDC  0x35u
#define AFE4900_REG_LED3LEDSTC      0x36u   /* LED3 fire window          */
#define AFE4900_REG_LED3LEDENDC     0x37u
#define AFE4900_REG_CLKDIV2         0x39u
#define AFE4900_REG_OFFDAC          0x3Au   /* TIA offset-DAC settings   */
#define AFE4900_REG_THRDETLOW       0x3Bu
#define AFE4900_REG_THRDETHIGH      0x3Cu
#define AFE4900_REG_THRDET          0x3Du
#define AFE4900_REG_I_OFFDAC        0x3Eu
#define AFE4900_REG_AVG_LED2_ALED2  0x3Fu   /* averaged differences      */
#define AFE4900_REG_AVG_LED1_ALED1  0x40u
#define AFE4900_REG_FIFO_DATA       0x42u   /* FIFO pop port             */
#define AFE4900_REG_LED4LEDSTC      0x43u   /* LED4 fire window          */
#define AFE4900_REG_LED4LEDENDC     0x44u
#define AFE4900_REG_TG_PD1STC       0x45u   /* PD-switch toggle window   */
#define AFE4900_REG_TG_PD1ENDC      0x46u

/* ---- Not-yet-confirmed addresses ------------------------------------ */
/* TODO(datasheet): all addresses below are placeholders pending SBAS857.
 * The AFE4900 register map continues past 0x46 with FIFO control/count,
 * phase-store control, TX mapping, PD-switch, ECG and sleep-control
 * registers. Best-guess placements are grouped here so the fix is a
 * one-line edit each. */
#define AFE4900_REG_FIFO_CTRL       0x48u   /* TODO(datasheet) FIFO cfg  */
#define AFE4900_REG_FIFO_COUNT      0x49u   /* TODO(datasheet) fill cnt  */
#define AFE4900_REG_FIFO_PHASE      0x4Au   /* TODO(datasheet) which of
                                             * the phase results the FIFO
                                             * stores (FIFO_NPHASE mask)  */
#define AFE4900_REG_TX_SEL          0x4Bu   /* TODO(datasheet) fire-win
                                             * -> TX pin map, IF any. If
                                             * binding is rigid LEDk->TXk,
                                             * delete this and fix the
                                             * fire regs in the phase
                                             * table instead.            */
#define AFE4900_REG_POWERDN         0x4Cu   /* TODO(datasheet) sleep ctl */
#define AFE4900_REG_VERSION         0x4Fu   /* TODO(datasheet) die/ver ID
                                             * (family had none — probe
                                             * does write/readback anyway)*/

/* ---- CONTROL0 (0x00) ------------------------------------------------ */
#define AFE4900_C0_SPI_READ         (1u<<0)   /* [family] 1=read mode    */
#define AFE4900_C0_TM_COUNT_RST     (1u<<1)   /* [family] timing-counter
                                               * reset after prog; TODO(ds)*/
#define AFE4900_C0_DIAG_EN          (1u<<2)   /* [family] TODO(datasheet) */
#define AFE4900_C0_SW_RESET         (1u<<3)   /* [evm] soft reset        */
#define AFE4900_C0_FIFO_EN          (1u<<4)   /* TODO(datasheet) bit pos */
#define AFE4900_C0_ULP_EN           (1u<<5)   /* [evm] ultra-low-power   */

/* ---- CONTROL1 (0x1E) ------------------------------------------------ */
#define AFE4900_C1_NUMAV(x)         ((x) & 0xFFu)         /* avg-1, [evm] */
#define AFE4900_C1_TIMEREN          (1u<<8)               /* [evm]        */

/* ---- CONTROL2 (0x23) ------------------------------------------------ */
/* EVM writes 0x104218 = "internal oscillator (128 kHz), ILED_FS = 50 mA".
 * Bit decode is not public -> keep the composite value. TODO(datasheet)
 * for field layout + other full-scale options (FS likely covers up to
 * the 200 mA driver max). CLK ball is strapped to GND on fix_v22 so the
 * internal 128 kHz osc is the ONLY legal clock source here.            */
#define AFE4900_C2_INT_OSC_FS50     0x104218u  /* [evm]                  */

/* ---- CONTROL3 (0x31) ------------------------------------------------ */
#define AFE4900_C3_EVM_DEFAULT      0x000020u  /* [evm] TODO(datasheet)  */

/* ---- TIA gain codes ------------------------------------------------- */
#define AFE4900_TIA_RF_500K         0x000003u  /* [evm] TODO(datasheet)
                                                * exact RF/CF code map   */

/* ---- LED current packing -------------------------------------------- */
/* TODO(datasheet): LEDCNTRL1/2 byte-field layout. EVM writes
 * LEDCNTRL1=0xF00000, LEDCNTRL2=0x002A00 — consistent with one 8-bit
 * current code per LED at [23:16]/[15:8]/[7:0]. Which field feeds which
 * fire window/TX is unconfirmed; the driver keeps the codes in named
 * shifts so a decode fix is a #define edit. With ILED_FS=50 mA a code of
 * N ~ N/255*50 mA. */
#define AFE4900_LEDC1_LED1_SHIFT    16u   /* TODO(datasheet) */
#define AFE4900_LEDC1_LED2_SHIFT    8u    /* TODO(datasheet) */
#define AFE4900_LEDC1_LED3_SHIFT    0u    /* TODO(datasheet) */
#define AFE4900_LEDC2_LED4_SHIFT    16u   /* TODO(datasheet) */
#define AFE4900_LEDC_TXSEL_SHIFT    0u    /* TODO(datasheet) if the TX
                                           * map lives in LEDCNTRL*     */

/* ---- Result decoding ------------------------------------------------ */
#define AFE4900_VAL_SIGN_BIT        (1u<<23)   /* 24-bit two's comp      */
#define AFE4900_FIFO_DEPTH          128u       /* [brief] words          */
#define AFE4900_FIFO_COUNT_MASK     0xFFu      /* TODO(datasheet) field  */

/* ---- Timing engine --------------------------------------------------- */
/* Counts are in internal-osc ticks: 1 count = 1/128 kHz = 7.8125 us.
 * PRPCT = PRP counts per pulse-repetition period; PRF = 128000/(PRPCT+1).
 * ADC_RDY frame rate = PRF/(NUMAV+1) (decimation).                       */
#define AFE4900_OSC_HZ              128000u
