/* board_pins.h — fix_v22 / hw_v3 GPIO and board truth table.
 *
 * Source of truth: fix_v22 schematic netlist + fab/BOM.csv + hw brief §2/§3.
 * EVERY firmware-visible pin lives here; drivers must not hard-code GPIOs.
 *
 * FORBIDDEN GPIO (never configure): 26-32 (internal flash/PSRAM SPI),
 * 35/36/37 (octal PSRAM), 45 (VDD_SPI strap — keep reading 0), 46, 47, 48,
 * 19/20 as GPIO (they are native USB D-/D+; USB-Serial/JTAG owns them).
 * IO0 is a strap (BOOT); only read it, never drive it.
 */
#pragma once
#include "driver/gpio.h"
#include "sdkconfig.h"

/* ---------------- I2C : one bus, 100 kHz MAX (2.2k pulls, ~503 ns rise) --- */
#define VQ_I2C_PORT        I2C_NUM_0
#define VQ_I2C_SDA_GPIO    GPIO_NUM_8
#define VQ_I2C_SCL_GPIO    GPIO_NUM_9
#define VQ_I2C_FREQ_HZ     100000      /* hard limit — do not raise */

/* I2C addresses — 3.3 V side */
#define VQ_ADDR_TCA6408    0x20
#define VQ_ADDR_MAX17048   0x36
#define VQ_ADDR_SHT45      0x44
#define VQ_ADDR_TMP117_SKIN   0x48     /* U11 — skin contact */
#define VQ_ADDR_TMP117_DOME   0x49     /* U20 — under insulation dome */
#define VQ_ADDR_TMP117_TAIL   0x4A     /* J9 tail, optional — probe */
#define VQ_ADDR_RV3028     0x52
#define VQ_ADDR_LSM6DSV    0x6A
#define VQ_ADDR_BME280     0x76
/* 1.8 V island behind PCA9306 (same logical bus) */
#define VQ_ADDR_AS7341     0x39
#define VQ_ADDR_MLX90632   0x3A        /* strap ADDR=GND — hw_v1's 0x3B is wrong */

/* ---------------- SPI2/FSPI host ---------------------------------------- */
#define VQ_SPI_HOST        SPI2_HOST
#define VQ_SPI_MOSI_GPIO   GPIO_NUM_11
#define VQ_SPI_SCLK_GPIO   GPIO_NUM_12
#define VQ_SPI_MISO_GPIO   GPIO_NUM_13

/* Chip-selects — all active-low, external 10k pull-ups; idle HIGH always.
 * Only ONE CS may be asserted at a time (shared MISO). */
#define VQ_CS_ADS1292_GPIO   GPIO_NUM_10   /* SPI mode 1, <= 4 MHz            */
#define VQ_CS_AD5940_GPIO    GPIO_NUM_15   /* SPI mode 0, <= 8 MHz            */
#define VQ_CS_FLASH_GPIO     GPIO_NUM_16   /* W25Q512, mode 0/3, <= 40 MHz    */
#define VQ_CS_AFE4900_GPIO   GPIO_NUM_18   /* SPI mode 0, <= 8 MHz            */
#define VQ_CS_MAX86141_GPIO  GPIO_NUM_38   /* SPI mode 0, <= 8 MHz — ALSO the
                                            * U26 MISO level-shifter OE.      */

#define VQ_SPI_ADS1292_HZ    (4*1000*1000)
#define VQ_SPI_AD5940_HZ     (8*1000*1000)
#define VQ_SPI_FLASH_HZ      (40*1000*1000)
#define VQ_SPI_AFE4900_HZ    (8*1000*1000)
#define VQ_SPI_MAX86141_HZ   (8*1000*1000)
#define VQ_SPI_PROBE_HZ      (1*1000*1000) /* self-test probe clock           */

/* ---------------- Interrupt / timestamped inputs ------------------------ */
#define VQ_ADS1292_DRDY_GPIO   GPIO_NUM_2    /* active-low   */
#define VQ_AFE4900_RDY_GPIO    GPIO_NUM_21   /* active-high pulse */
#define VQ_AD5940_GPIO0_GPIO   GPIO_NUM_14   /* polarity set in AD5940        */
#define VQ_MAX86141_INT_GPIO   GPIO_NUM_39   /* open-drain, active-low        */
#define VQ_LSM6_INT1_GPIO      GPIO_NUM_4    /* push-pull, active-high        */
#define VQ_TMP117_ALERT_GPIO   GPIO_NUM_5    /* open-drain, active-low        */
#define VQ_GAUGE_ALRT_GPIO     GPIO_NUM_6    /* open-drain, active-low        */
#define VQ_CHG_PG_GPIO         GPIO_NUM_7    /* BQ25170 ~PG, low = VBUS good  */
#define VQ_EXP_INT_GPIO        GPIO_NUM_3    /* TCA6408 ~INT, open-drain low.
                                              * STRAP pin — input only.       */

/* ---------------- Misc outputs ------------------------------------------- */
#define VQ_STATUS_LED_GPIO     GPIO_NUM_17   /* active-high via R88 150R      */
#define VQ_MIC_CLK_GPIO        GPIO_NUM_40   /* PDM clock out                 */
#define VQ_MIC_DAT_GPIO        GPIO_NUM_41   /* PDM data in                   */
#define VQ_SPARE_IN_GPIO       GPIO_NUM_42   /* floating (R107/108 DNP) —
                                              * internal pull-down or unused. */
#define VQ_FSR_ADC_GPIO        GPIO_NUM_1    /* ADC1_CH0, 12-bit, 11 dB atten */

/* ---------------- TCA6408 port map (0x20) --------------------------------
 * All pins are INPUTS after any ESP reset (expander ~RESET = ESP_EN).
 * External pulls keep power domains off in that state — safe default. */
typedef enum {
    VQ_EXP_AFE_RESETZ = 0,   /* P0 out — 0 = AFE4900 in reset (dflt)         */
    VQ_EXP_ADS_PWDN   = 1,   /* P1 out — 0 = ADS1292 powered down (dflt)     */
    VQ_EXP_AD5940_RST = 2,   /* P2 out — 0 = AD5940 in reset (dflt)          */
    VQ_EXP_TX5_EN     = 3,   /* P3 out — TX_5V LED boost enable (dflt off)   */
    VQ_EXP_IR_GATE    = 4,   /* P4 out — Q1 low-side IR LED switch (dflt off)*/
    VQ_EXP_CHG_STAT   = 5,   /* P5 in  — BQ25170 STAT, low = charging        */
    VQ_EXP_VBUS_DET   = 6,   /* P6 in  — VBUS divider, high = USB present    */
    VQ_EXP_CHG_DIS    = 7,   /* P7 out — high = charge inhibit (dflt low)    */
} vq_exp_pin_t;

/* ---------------- Strap / forbidden summary ------------------------------ */
/* IO45: must read 0 at boot (3.3 V flash). board_init logs it; never drive. */
#define VQ_STRAP_VDD_SPI_GPIO   GPIO_NUM_45  /* read-only check            */
#define VQ_STRAP_JTAG_GPIO      GPIO_NUM_3   /* same as EXP_INT — input    */
#define VQ_BOOT_STRAP_GPIO      GPIO_NUM_0   /* BOOT strap — read only     */

/* ---------------- Rails (constants for power logic) ---------------------- */
#define VQ_BATT_LOW_MV        3200   /* VALRT threshold: stop loads, deep sleep */
#define VQ_BATT_EMERG_MV      3000   /* emergency shutdown                      */
#define VQ_BATT_BOOST_MIN_MV  3300   /* never run TX_5V below this              */
#define VQ_SOC_LOW_PCT        5      /* MAX17048 SOC alert                      */
#define VQ_CHG_CURRENT_MA     200    /* BQ25170 R_ISET = 1.5k                   */
#define VQ_USB_BUDGET_MA      500    /* default USB 2.0 budget                  */

/* ---------------- Mic ---------------------------------------------------- */
#define VQ_MIC_PDM_CLK_HZ     2048000 /* sits in the IM69D130 mode-band gap
                                      * (bands: .4-.95/1.05-1.9/2.1-2.65/
                                      * 2.9-3.3 MHz); interface accepts
                                      * 0.4-3.3 MHz — specs unguaranteed,
                                      * 24kHz×128→3.072MHz is the DS pick  */
