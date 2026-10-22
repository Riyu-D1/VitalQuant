/* soc/gpio_sig_map.h — host-sim GPIO-matrix signal indices (ESP32-S3).
 *
 * Values are the real ESP32-S3 signal-index assignments.  Only the
 * groups the firmware (or sibling shims) reference are populated:
 * SPI2/FSPI (the vq_spi bus), SPI3, I2S (mic shim), UART, plus
 * SIG_GPIO_OUT_IDX — add more rows from the TRM map as needed.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- UART -------------------------------------------------- */
#define U0RXD_IN_IDX                  14
#define U0TXD_OUT_IDX                 14
#define U1RXD_IN_IDX                  17
#define U1TXD_OUT_IDX                 17

/* ---------- I2S ---------------------------------------------------- */
#define I2S1_MCLK_IN_IDX              21
#define I2S1_MCLK_OUT_IDX             21
#define I2S0O_BCK_IN_IDX              22
#define I2S0O_BCK_OUT_IDX             22
#define I2S0_MCLK_IN_IDX              23
#define I2S0_MCLK_OUT_IDX             23
#define I2S0O_WS_IN_IDX               24
#define I2S0O_WS_OUT_IDX              24
#define I2S0I_SD_IN_IDX               25
#define I2S0O_SD_OUT_IDX              25
#define I2S0I_BCK_IN_IDX              26
#define I2S0I_BCK_OUT_IDX             26
#define I2S0I_WS_IN_IDX               27
#define I2S0I_WS_OUT_IDX              27
#define I2S1O_BCK_IN_IDX              28
#define I2S1O_BCK_OUT_IDX             28
#define I2S1O_WS_IN_IDX               29
#define I2S1O_WS_OUT_IDX              29
#define I2S1I_SD_IN_IDX               30
#define I2S1O_SD_OUT_IDX              30
#define I2S1I_BCK_IN_IDX              31
#define I2S1I_BCK_OUT_IDX             31
#define I2S1I_WS_IN_IDX               32
#define I2S1I_WS_OUT_IDX              32

/* ---------- SPI3 (GPSPI3, second general-purpose host) ------------- */
#define SPI3_CLK_IN_IDX               66
#define SPI3_CLK_OUT_IDX              66
#define SPI3_Q_IN_IDX                 67
#define SPI3_Q_OUT_IDX                67
#define SPI3_D_IN_IDX                 68
#define SPI3_D_OUT_IDX                68
#define SPI3_HD_IN_IDX                69
#define SPI3_HD_OUT_IDX               69
#define SPI3_WP_IN_IDX                70
#define SPI3_WP_OUT_IDX               70
#define SPI3_CS0_IN_IDX               71
#define SPI3_CS0_OUT_IDX              71
#define SPI3_CS1_OUT_IDX              72
#define SPI3_CS2_OUT_IDX              127

/* ---------- SPI2 / FSPI (the vq_spi bus host) ----------------------- */
#define FSPICLK_IN_IDX                101
#define FSPICLK_OUT_IDX               101
#define FSPIQ_IN_IDX                  102
#define FSPIQ_OUT_IDX                 102
#define FSPID_IN_IDX                  103
#define FSPID_OUT_IDX                 103
#define FSPIHD_IN_IDX                 104
#define FSPIHD_OUT_IDX                104
#define FSPIWP_IN_IDX                 105
#define FSPIWP_OUT_IDX                105
#define FSPIIO4_IN_IDX                106
#define FSPIIO4_OUT_IDX               106
#define FSPIIO5_IN_IDX                107
#define FSPIIO5_OUT_IDX               107
#define FSPIIO6_IN_IDX                108
#define FSPIIO6_OUT_IDX               108
#define FSPIIO7_IN_IDX                109
#define FSPIIO7_OUT_IDX               109
#define FSPICS0_IN_IDX                110
#define FSPICS0_OUT_IDX               110
#define FSPICS1_OUT_IDX               111
#define FSPICS2_OUT_IDX               112
#define FSPICS3_OUT_IDX               113
#define FSPICS4_OUT_IDX               114
#define FSPICS5_OUT_IDX               115
#define FSPIDQS_OUT_IDX               126

/* ---------- constant / plain-GPIO routing --------------------------- */
/* Route a GPIO output through the plain output register (disconnects
 * every peripheral signal — used to park MIC_CLK low etc.).          */
#define SIG_GPIO_OUT_IDX              256
/* Function-208..212 inputs (generic peripheral inputs). */
#define SIG_IN_FUNC_208_IDX           208
#define SIG_IN_FUNC208_IDX            208
#define SIG_IN_FUNC_209_IDX           209
#define SIG_IN_FUNC209_IDX            209
#define SIG_IN_FUNC_210_IDX           210
#define SIG_IN_FUNC210_IDX            210
#define SIG_IN_FUNC_211_IDX           211
#define SIG_IN_FUNC211_IDX            211
#define SIG_IN_FUNC_212_IDX           212
#define SIG_IN_FUNC212_IDX            212

#ifdef __cplusplus
}
#endif
