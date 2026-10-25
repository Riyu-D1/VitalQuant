/* max86141_regs.h — private register map for MAX86140/MAX86141 (SPI variant).
 *
 * SPI framing (datasheet "SPI Interface", verified against two reference
 * drivers): every transaction is 3 bytes under one CS assertion —
 *   [reg_addr][0x00 = write-enable | 0xFF = read-enable][data].
 * FIFO burst read: [0x08][0xFF] then N*3 bytes with CS held low; each FIFO
 * word is 24 bits = {tag[4:0] in byte0[7:3], 19-bit ADC data in bits 18:0}.
 * Tag value = the LEDCn exposure code that produced the word (PD1 channel);
 * on the MAX86141 second PD channel the tag is code+6 (unused here — PD2_IN
 * is unconnected and we run single-channel mode).
 *
 * Bit positions verified against MAX86140/MAX86141 datasheet rev5 register
 * tables; fields marked TODO(datasheet) are confirmed present but the exact
 * bitfield semantics worth a re-check on the bench.
 */
#pragma once
#include <stdint.h>

#define MX_SPI_WRITE_EN        0x00    /* byte 2 of frame: write */
#define MX_SPI_READ_EN         0xFF    /* byte 2 of frame: read  */

/* ---- Status / interrupt ------------------------------------------------- */
#define MX_REG_INT_STAT_1      0x00    /* read clears */
#define MX_INT1_A_FULL         0x80    /* FIFO crossed (128 - FIFO_A_FULL)   */
#define MX_INT1_DATA_RDY       0x40    /* new word pushed to FIFO            */
#define MX_INT1_ALC_OVF        0x20    /* ambient-cancel loop saturated      */
#define MX_INT1_PROX_INT       0x10    /* LEDC1 reading crossed prox thresh  */
#define MX_INT1_LED_COMPB      0x08    /* LED driver out of compliance       */
#define MX_INT1_DIE_TEMP_RDY   0x04
#define MX_INT1_VDD_OOR        0x02    /* VDD_ANA <1.65V or >2.05V           */
#define MX_INT1_PWR_RDY        0x01    /* VDD dipped below UVLO              */

#define MX_REG_INT_STAT_2      0x01    /* [0] SHA_DONE only                  */

#define MX_REG_INT_EN_1        0x02    /* same bit positions as INT_STAT_1   */
#define MX_INT1_EN_ALL         0xFE
#define MX_REG_INT_EN_2        0x03

/* ---- FIFO --------------------------------------------------------------- */
#define MX_REG_FIFO_WR_PTR     0x04    /* [6:0] */
#define MX_REG_FIFO_RD_PTR     0x05    /* [6:0] */
#define MX_REG_OVF_COUNTER     0x06    /* [6:0] */
#define MX_REG_FIFO_DATA_COUNT 0x07    /* 0..128 */
#define MX_REG_FIFO_DATA       0x08    /* burst-read, 3 B/word               */
#define MX_FIFO_DEPTH          128

#define MX_REG_FIFO_CONFIG_1   0x09    /* [6:0] FIFO_A_FULL: empty-space watermark —
                                        * INT asserts when count >= 128 - value */
#define MX_REG_FIFO_CONFIG_2   0x0A
#define MX_FIFO2_FLUSH_FIFO    0x10    /* strobe: clear FIFO contents        */
#define MX_FIFO2_STAT_CLR      0x08    /* FIFO_DATA read clears status bits  */
#define MX_FIFO2_A_FULL_TYPE   0x04    /* 1 = assert only on NEW condition   */
#define MX_FIFO2_FIFO_RO       0x02    /* 1 = roll over (drop oldest)        */

/* ---- System control ----------------------------------------------------- */
#define MX_REG_MODE_CONFIG     0x0D
#define MX_MODE_RESET          0x01    /* soft reset, self-clearing          */
#define MX_MODE_SHDN           0x02    /* shutdown                           */
#define MX_MODE_LP_MODE        0x04    /* low-power mode                     */
#define MX_MODE_SINGLE_PPG     0x08    /* MAX86141: one optical channel (PD1)*/

/* ---- PPG engine --------------------------------------------------------- */
#define MX_REG_PPG_SYNC_CTRL   0x10    /* [7]TIME_STAMP_EN [4]SW_FORCE_SYNC
                                        * [3:0]GPIO_CTRL (0000 = internal osc) */
#define MX_REG_PPG_CONFIG_1    0x11    /* [1:0]PPG_TINT [3:2]PPG1_ADC_RGE
                                        * [5:4]PPG2_ADC_RGE */
#define MX_REG_PPG_CONFIG_2    0x12    /* [7:3]PPG_SR [2:0]SMP_AVE           */
#define MX_REG_PPG_CONFIG_3    0x13    /* [7:6]LED_SETLNG; DIG_FILT_SEL /
                                        * BURST_EN positions TODO(datasheet)  */
#define MX_REG_PROX_INT_THRESH 0x14
#define MX_REG_PD_BIAS         0x15    /* [2:0]PD1 bias, [6:4]PD2 bias       */
#define MX_REG_PICKET_FENCE    0x16

/* LED exposure sequence — each reg packs two 4-bit LEDC codes:
 * [3:0] = odd-numbered slot, [7:4] = even-numbered slot. */
#define MX_REG_LED_SEQ_1       0x20    /* LEDC2 | LEDC1                      */
#define MX_REG_LED_SEQ_2       0x21    /* LEDC4 | LEDC3                      */
#define MX_REG_LED_SEQ_3       0x22    /* LEDC6 | LEDC5                      */

/* LEDCn exposure codes */
#define MX_LEDC_NONE           0x0
#define MX_LEDC_LED1           0x1     /* J10 SFH7050A 530 nm (green)        */
#define MX_LEDC_LED2           0x2     /* 660 nm (red)                       */
#define MX_LEDC_LED3           0x3     /* 940 nm (IR)                        */
#define MX_LEDC_LED1_LED2      0x4
#define MX_LEDC_LED1_LED3      0x5
#define MX_LEDC_LED2_LED3      0x6
#define MX_LEDC_LED123         0x7
#define MX_LEDC_PILOT_LED1     0x8
#define MX_LEDC_AMBIENT        0x9     /* direct ambient (no LED)            */
#define MX_LEDC_LED4           0xA     /* external mux only — not wired      */

#define MX_REG_LED1_PA         0x23
#define MX_REG_LED2_PA         0x24
#define MX_REG_LED3_PA         0x25
#define MX_REG_LED4_PA         0x26
#define MX_REG_LED5_PA         0x27
#define MX_REG_LED6_PA         0x28
#define MX_REG_LED_PILOT_PA    0x29
#define MX_REG_LED_RANGE_1     0x2A    /* [1:0]LED1_RGE [3:2]LED2 [5:4]LED3  */
#define MX_REG_LED_RANGE_2     0x2B    /* LED4..6 ranges (unused)            */

/* LED full-scale ranges: 0=31mA 1=62mA 2=93mA 3=124mA */
#define MX_LED_RGE_124MA       0x3
/* LEDx_PA LSB = range/255 → 124 mA range ≈ 0.486 mA/count */

/* ---- Die temperature (optional) ----------------------------------------- */
#define MX_REG_TEMP_CONFIG     0x40    /* [0] TEMP_EN */
#define MX_REG_TEMP_INT        0x41
#define MX_REG_TEMP_FRAC       0x42    /* [3:0] */

/* ---- Identity ------------------------------------------------------------ */
#define MX_REG_PART_ID         0xFF
#define MX_PART_ID_MAX86140    0x24    /* 1 PPG channel  */
#define MX_PART_ID_MAX86141    0x25    /* 2 PPG channels — expected part     */

/* ---- Enumerated field values --------------------------------------------- */
/* PPG_TINT (integration = LED on-time window): 0=14.8 1=29.4 2=58.7 3=117.3 µs */
#define MX_TINT_117US          0x3
/* PPGx_ADC_RGE: 0=4µA 1=8µA 2=16µA 3=32µA */
#define MX_ADC_RGE_16UA        0x2
/* PPG_SR (samples per second @32768 Hz osc, 1 pulse/sample):
 * 0x00=24.995 0x01=50.027 0x02=84.021 0x03=99.902 0x04=199.805 0x05=399.610 */
#define MX_SR_25SPS            0x00
#define MX_SR_100SPS           0x03
#define MX_SR_200SPS           0x04
#define MX_SR_400SPS           0x05
/* SMP_AVE: 0..7 = average of 1,2,4,8,16,32,64,128 */
#define MX_SMP_AVE_1           0x0
/* LED_SETLNG: 0=4µs 1=6µs 2=8µs 3=12µs */
#define MX_LED_SETLNG_12US     0x3
