/* i2s_types.h — host-sim stand-in merging driver/i2s_types.h and
 * hal/i2s_types.h (IDF v5.4.2).  The real tree splits "public driver"
 * types (port, comm mode, mclk multiple, handles, event payloads) from
 * "hal" types (slot/dir/role/bit-width/clock-src/PDM enums); the shim
 * keeps both in one file so firmware sees the same names with the same
 * values.  Only the ESP32-S3 surface the firmware uses is modelled —
 * PDM RX is supported on I2S_NUM_0 only, and there is no PDM RX HP
 * filter (SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER is P4-only).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "driver/gpio.h"          /* gpio_num_t / GPIO_NUM_NC */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------- hal/i2s_types.h portion --------------------------- */

/**
 * @brief I2S channel slot mode
 */
typedef enum {
    I2S_SLOT_MODE_MONO = 1,          /*!< I2S channel slot format mono, transmit same data in all slots for tx mode, only receive the data in the first slots for rx mode. */
    I2S_SLOT_MODE_STEREO = 2,        /*!< I2S channel slot format stereo, transmit different data in different slots for tx mode, receive the data in all slots for rx mode. */
} i2s_slot_mode_t;

/**
 * @brief I2S channel direction
 */
typedef enum {
    I2S_DIR_RX = (1u << 0),         /*!< I2S channel direction RX */
    I2S_DIR_TX = (1u << 1),         /*!< I2S channel direction TX */
} i2s_dir_t;

/**
 * @brief I2S controller role
 */
typedef enum {
    I2S_ROLE_MASTER,                /*!< I2S controller master role, bclk and ws signal will be set to output */
    I2S_ROLE_SLAVE                  /*!< I2S controller slave role, bclk and ws signal will be set to input */
} i2s_role_t;

/**
 * @brief Available data bit width in one slot
 */
typedef enum {
    I2S_DATA_BIT_WIDTH_8BIT    = 8,            /*!< I2S channel data bit-width: 8 */
    I2S_DATA_BIT_WIDTH_16BIT   = 16,           /*!< I2S channel data bit-width: 16 */
    I2S_DATA_BIT_WIDTH_24BIT   = 24,           /*!< I2S channel data bit-width: 24 */
    I2S_DATA_BIT_WIDTH_32BIT   = 32,           /*!< I2S channel data bit-width: 32 */
} i2s_data_bit_width_t;

/**
 * @brief Total slot bit width in one slot
 */
typedef enum {
    I2S_SLOT_BIT_WIDTH_AUTO           = (0),      /*!< I2S channel slot bit-width equals to data bit-width */
    I2S_SLOT_BIT_WIDTH_8BIT           = (8),      /*!< I2S channel slot bit-width: 8 */
    I2S_SLOT_BIT_WIDTH_16BIT          = (16),     /*!< I2S channel slot bit-width: 16 */
    I2S_SLOT_BIT_WIDTH_24BIT          = (24),     /*!< I2S channel slot bit-width: 24 */
    I2S_SLOT_BIT_WIDTH_32BIT          = (32),     /*!< I2S channel slot bit-width: 32 */
} i2s_slot_bit_width_t;

/**
 * @brief I2S clock source — opaque int in the sim (recorded, never used).
 */
typedef int i2s_clock_src_t;
#define I2S_CLK_SRC_DEFAULT        0   /*!< Default (PLL_D2 on S3) */

/**
 * @brief I2S PDM RX down-sampling mode
 */
typedef enum {
    I2S_PDM_DSR_8S = 0,  /*!< downsampling number is 8 for PDM RX mode*/
    I2S_PDM_DSR_16S,     /*!< downsampling number is 16 for PDM RX mode*/
    I2S_PDM_DSR_MAX,
} i2s_pdm_dsr_t;

/**
 * @brief I2S slot select in standard mode
 */
typedef enum {
    I2S_STD_SLOT_LEFT   = (1u << 0),               /*!< I2S transmits or receives left slot */
    I2S_STD_SLOT_RIGHT  = (1u << 1),               /*!< I2S transmits or receives right slot */
    I2S_STD_SLOT_BOTH   = (1u << 0) | (1u << 1),   /*!< I2S transmits or receives both left and right slot */
} i2s_std_slot_mask_t;

/**
 * @brief I2S slot select in PDM mode
 */
typedef enum {
    I2S_PDM_SLOT_RIGHT      = (1u << 0),                /*!< I2S PDM only transmits or receives the PDM device whose 'select' pin is pulled up */
    I2S_PDM_SLOT_LEFT       = (1u << 1),                /*!< I2S PDM only transmits or receives the PDM device whose 'select' pin is pulled down */
    I2S_PDM_SLOT_BOTH       = (1u << 0) | (1u << 1),    /*!< I2S PDM transmits or receives both two slots */
} i2s_pdm_slot_mask_t;

/**
 * @brief pdm tx signal scaling mode (enum kept for header parity; the sim
 *        implements no PDM TX path)
 */
typedef enum {
    I2S_PDM_SIG_SCALING_DIV_2 = 0,   /*!< I2S TX PDM signal scaling: /2 */
    I2S_PDM_SIG_SCALING_MUL_1 = 1,   /*!< I2S TX PDM signal scaling: x1 */
    I2S_PDM_SIG_SCALING_MUL_2 = 2,   /*!< I2S TX PDM signal scaling: x2 */
    I2S_PDM_SIG_SCALING_MUL_4 = 3,   /*!< I2S TX PDM signal scaling: x4 */
} i2s_pdm_sig_scale_t;

/* ----------------- driver/i2s_types.h portion ------------------------ */

/**
 * @brief I2S controller port number, the max port number is (SOC_I2S_NUM -1).
 */
typedef enum {
    I2S_NUM_0 = 0,                 /*!< I2S controller port 0 */
    I2S_NUM_1 = 1,                 /*!< I2S controller port 1 */
    I2S_NUM_AUTO,                  /*!< Select whichever port is available */
} i2s_port_t;

/**
 * @brief I2S controller communication mode
 */
typedef enum {
    I2S_COMM_MODE_STD,              /*!< I2S controller using standard communication mode, support Philips/MSB/PCM format */
    I2S_COMM_MODE_PDM,              /*!< I2S controller using PDM communication mode, support PDM output or input */
    I2S_COMM_MODE_TDM,              /*!< I2S controller using TDM communication mode, support up to 16 slots per frame */
    I2S_COMM_MODE_NONE,             /*!< Unspecified I2S controller mode */
} i2s_comm_mode_t;

/**
 * @brief The multiple of MCLK to sample rate
 */
typedef enum {
    I2S_MCLK_MULTIPLE_128       = 128,     /*!< MCLK = sample_rate * 128 */
    I2S_MCLK_MULTIPLE_192       = 192,     /*!< MCLK = sample_rate * 192 (24-bit compatible) */
    I2S_MCLK_MULTIPLE_256       = 256,     /*!< MCLK = sample_rate * 256 */
    I2S_MCLK_MULTIPLE_384       = 384,     /*!< MCLK = sample_rate * 384 (24-bit compatible) */
    I2S_MCLK_MULTIPLE_512       = 512,     /*!< MCLK = sample_rate * 512 */
    I2S_MCLK_MULTIPLE_576       = 576,     /*!< MCLK = sample_rate * 576 (24-bit compatible) */
    I2S_MCLK_MULTIPLE_768       = 768,     /*!< MCLK = sample_rate * 768 (24-bit compatible) */
    I2S_MCLK_MULTIPLE_1024      = 1024,    /*!< MCLK = sample_rate * 1024 */
    I2S_MCLK_MULTIPLE_1152      = 1152,    /*!< MCLK = sample_rate * 1152 (24-bit compatible) */
} i2s_mclk_multiple_t;

/**
 * @brief Event structure used in I2S event queue
 */
typedef struct {
    void                *dma_buf;/**< The first level pointer of DMA buffer that just finished sending or receiving for `on_recv` and `on_sent` callback
                                  *  NULL for `on_recv_q_ovf` and `on_send_q_ovf` callback
                                  */
    size_t              size;   /**< The buffer size of DMA buffer when success to send or receive,
                                  *  also the buffer size that dropped when queue overflow.
                                  *  It is related to the dma_frame_num and data_bit_width, typically it is fixed when data_bit_width is not changed.
                                  */
} i2s_event_data_t;

typedef struct i2s_channel_obj_t       *i2s_chan_handle_t;     /*!< I2S channel object handle, the control unit of the I2S driver*/

/**
 * @brief I2S event callback
 * @param[in] handle    I2S channel handle, created from `i2s_new_channel()`
 * @param[in] event     I2S event data
 * @param[in] user_ctx  User registered context, passed from `i2s_channel_register_event_callback()`
 *
 * @return Whether a high priority task has been waken up by this callback function
 */
typedef bool (*i2s_isr_callback_t)(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx);

#ifdef __cplusplus
}
#endif
