/* rom.c — host-sim implementations for the ESP32-S3 ROM-layer +
 * SoC-level shims declared in sim/esp_shim/include:
 *
 *   esp_rom_crc.c    — bit-faithful CRC8/16/32 LE+BE
 *   esp_rom_sys.c    — delay (virtual clock), printf, cpu ticks
 *   esp_rom_gpio.c   — GPIO-matrix/pad calls, record-only
 *   esp_intr_alloc   — table-registry interrupt allocator stub
 *   soc register file — REG_READ/REG_WRITE backend + IO_MUX pad cells
 *   soc/gpio_periph.c — GPIO_PIN_MUX_REG / GPIO_HOLD_MASK tables
 *   soc/spi_periph.c  — spi_periph_signal[] (real esp32s3 values)
 *   esp_flash         — 16 MB flash image (RAM, optional file backing)
 *   esp_ota_ops       — ota_0/ota_1 slot bookkeeping
 *
 * C11, self-contained; every public symbol matches its IDF prototype.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "esp_intr_alloc.h"
#include "esp_flash.h"
#include "esp_ota_ops.h"

#include "soc/soc.h"
#include "soc/gpio_periph.h"
#include "soc/spi_periph.h"

#include "vboard.h"

/* ================================================================== */
/*  esp_rom_crc — algorithms identical to the silicon ROM routines     */
/*                                                                    */
/*  ROM convention: out = ~CRC(~crc_in, buf, len).  The reflected (_le) */
/*  variants use reflected polys 0xEDB88320/0x8408/0xE0; the _be ones  */
/*  the normal polys 0x04C11DB7/0x1021/0x07.  Bitwise loops below are  */
/*  exactly what the ROM's per-byte tables encode.                     */
/* ================================================================== */

uint8_t esp_rom_crc8_le(uint8_t crc, uint8_t const *buf, uint32_t len)
{
    crc = (uint8_t)~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (uint8_t)((crc >> 1) ^ 0xE0u) : (uint8_t)(crc >> 1);
        }
    }
    return (uint8_t)~crc;
}

uint8_t esp_rom_crc8_be(uint8_t crc, uint8_t const *buf, uint32_t len)
{
    crc = (uint8_t)~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
        }
    }
    return (uint8_t)~crc;
}

uint16_t esp_rom_crc16_le(uint16_t crc, uint8_t const *buf, uint32_t len)
{
    crc = (uint16_t)~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
        }
    }
    return (uint16_t)~crc;
}

uint16_t esp_rom_crc16_be(uint16_t crc, uint8_t const *buf, uint32_t len)
{
    crc = (uint16_t)~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                 : (uint16_t)(crc << 1);
        }
    }
    return (uint16_t)~crc;
}

uint32_t esp_rom_crc32_le(uint32_t crc, uint8_t const *buf, uint32_t len)
{
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

uint32_t esp_rom_crc32_be(uint32_t crc, uint8_t const *buf, uint32_t len)
{
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)buf[i] << 24;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u
                                      : crc << 1;
        }
    }
    return ~crc;
}

/* ================================================================== */
/*  esp_rom_sys                                                        */
/* ================================================================== */

static uint32_t s_ticks_per_us = 240;   /* CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240 */
static void (*s_rom_putc[4])(char c);

void esp_rom_delay_us(uint32_t us)
{
    /* The ONLY time source is the vboard virtual clock; sleeping here
     * honours VSIM_TIME_SCALE and lets tick-thread device models run.  */
    vboard_clock_sleep_us((int64_t)us);
}

int esp_rom_vprintf(const char *fmt, va_list ap)
{
    int n = vfprintf(stdout, fmt, ap);
    fflush(stdout);
    return n;
}

int esp_rom_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
    return n;
}

void esp_rom_install_channel_putc(int channel, void (*putc)(char c))
{
    if (channel >= 1 && channel < (int)(sizeof(s_rom_putc) / sizeof(s_rom_putc[0]))) {
        s_rom_putc[channel] = putc;
    }
}

void esp_rom_output_to_channels(char c)
{
    for (size_t i = 1; i < sizeof(s_rom_putc) / sizeof(s_rom_putc[0]); i++) {
        if (s_rom_putc[i] != NULL) {
            s_rom_putc[i](c);
        }
    }
    (void)fputc(c, stdout);
    fflush(stdout);
}

void esp_rom_install_uart_printf(void)
{
    esp_rom_install_channel_putc(1, NULL);
}

uint32_t esp_rom_get_cpu_ticks_per_us(void)
{
    return s_ticks_per_us;
}

void esp_rom_set_cpu_ticks_per_us(uint32_t ticks_per_us)
{
    s_ticks_per_us = ticks_per_us;
}

void esp_rom_software_reset_system(void)
{
    fprintf(stderr, "[sim] esp_rom_software_reset_system() — treating as abort\n");
    abort();
}

void esp_rom_software_reset_cpu(int cpu_no)
{
    fprintf(stderr, "[sim] esp_rom_software_reset_cpu(%d) — treating as abort\n",
            cpu_no);
    abort();
}

/* ================================================================== */
/*  esp_rom_gpio — GPIO matrix routing, record-only                    */
/* ================================================================== */

#define SIM_GPIO_N      49                  /* ESP32-S3 pads 0..48 */
#define SIM_SIG_N       384                 /* covers SIG_GPIO_OUT_IDX=256 */

static int16_t  s_gpio_out_sig[SIM_GPIO_N]; /* last out-signal per pad   */
static uint8_t  s_gpio_out_inv[SIM_GPIO_N];
static uint8_t  s_gpio_oen_inv[SIM_GPIO_N];
static uint8_t  s_gpio_pad_sel[SIM_GPIO_N]; /* pad_select_gpio ran       */
static uint8_t  s_gpio_drv[SIM_GPIO_N];
static int16_t  s_sig_in_gpio[SIM_SIG_N];   /* last pad routed to signal */
static uint8_t  s_sig_in_inv[SIM_SIG_N];
static bool     s_gpio_matrix_init;

static void gpio_matrix_init(void)
{
    memset(s_gpio_out_sig, 0xFF, sizeof(s_gpio_out_sig));
    memset(s_sig_in_gpio,  0xFF, sizeof(s_sig_in_gpio));
    s_gpio_matrix_init = true;
}

void esp_rom_gpio_pad_select_gpio(uint32_t iopad_num)
{
    if (!s_gpio_matrix_init) gpio_matrix_init();
    if (iopad_num < SIM_GPIO_N) {
        s_gpio_pad_sel[iopad_num] = 1;
    }
}

void esp_rom_gpio_pad_pullup_only(uint32_t iopad_num)
{
    (void)iopad_num;   /* pad pull state is the gpio shim's business */
}

void esp_rom_gpio_pad_unhold(uint32_t gpio_num)
{
    (void)gpio_num;
}

void esp_rom_gpio_pad_set_drv(uint32_t iopad_num, uint32_t drv)
{
    if (!s_gpio_matrix_init) gpio_matrix_init();
    if (iopad_num < SIM_GPIO_N) {
        s_gpio_drv[iopad_num] = (uint8_t)(drv & 0x3);
    }
}

void esp_rom_gpio_connect_in_signal(uint32_t gpio_num, uint32_t signal_idx,
                                    bool inv)
{
    if (!s_gpio_matrix_init) gpio_matrix_init();
    /* gpio_num may be a real pad or GPIO_MATRIX_CONST_{ZERO,ONE}_INPUT —
     * record the raw selector either way (>=49 marks a constant).      */
    if (signal_idx < SIM_SIG_N) {
        s_sig_in_gpio[signal_idx] = (int16_t)gpio_num;
        s_sig_in_inv[signal_idx]  = inv ? 1 : 0;
    }
}

void esp_rom_gpio_connect_out_signal(uint32_t gpio_num, uint32_t signal_idx,
                                     bool out_inv, bool oen_inv)
{
    if (!s_gpio_matrix_init) gpio_matrix_init();
    if (gpio_num < SIM_GPIO_N) {
        s_gpio_out_sig[gpio_num] = (int16_t)signal_idx;
        s_gpio_out_inv[gpio_num] = out_inv ? 1 : 0;
        s_gpio_oen_inv[gpio_num] = oen_inv ? 1 : 0;
    }
}

int esp_shim_gpio_out_signal(int gpio_num)
{
    if (!s_gpio_matrix_init || gpio_num < 0 || gpio_num >= SIM_GPIO_N) {
        return -1;
    }
    return s_gpio_out_sig[gpio_num];
}

int esp_shim_gpio_in_signal_gpio(int signal_idx)
{
    if (!s_gpio_matrix_init || signal_idx < 0 || signal_idx >= SIM_SIG_N) {
        return -1;
    }
    return s_sig_in_gpio[signal_idx];
}

int esp_shim_gpio_pad_is_gpio(int gpio_num)
{
    if (!s_gpio_matrix_init || gpio_num < 0 || gpio_num >= SIM_GPIO_N) {
        return -1;
    }
    return s_gpio_pad_sel[gpio_num];
}

/* ================================================================== */
/*  esp_intr_alloc — table-registry interrupt allocator                */
/* ================================================================== */

#define SIM_INTR_SLOTS 64

struct intr_handle_data_t {
    int            source;
    int            flags;
    intr_handler_t isr;
    void          *arg;
    int            intno;          /* sim CPU interrupt number */
    int            cpu;
    bool           enabled;
    bool           in_iram;
    bool           used;
};

static struct intr_handle_data_t s_intr[SIM_INTR_SLOTS];
static int  s_next_intno = 5;      /* plausible CPU int numbers 5..31    */

esp_err_t esp_intr_alloc(int source, int flags, intr_handler_t handler,
                         void *arg, intr_handle_t *ret_handle)
{
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < SIM_INTR_SLOTS; i++) {
        if (!s_intr[i].used) {
            s_intr[i].used    = true;
            s_intr[i].source  = source;
            s_intr[i].flags   = flags;
            s_intr[i].isr     = handler;
            s_intr[i].arg     = arg;
            s_intr[i].cpu     = 0;
            s_intr[i].intno   = (s_next_intno <= 31) ? s_next_intno++ : -1;
            s_intr[i].in_iram = (flags & ESP_INTR_FLAG_IRAM) != 0;
            s_intr[i].enabled = !(flags & ESP_INTR_FLAG_INTRDISABLED);
            if (ret_handle != NULL) {
                *ret_handle = &s_intr[i];
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t esp_intr_alloc_intrstatus(int source, int flags,
                                    uint32_t intrstatusreg,
                                    uint32_t intrstatusmask,
                                    intr_handler_t handler, void *arg,
                                    intr_handle_t *ret_handle)
{
    (void)intrstatusreg;
    (void)intrstatusmask;
    return esp_intr_alloc(source, flags, handler, arg, ret_handle);
}

esp_err_t esp_intr_free(intr_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < SIM_INTR_SLOTS; i++) {
        if (&s_intr[i] == handle && s_intr[i].used) {
            memset(&s_intr[i], 0, sizeof(s_intr[i]));
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t esp_intr_mark_shared(int intno, int cpu, bool is_in_iram)
{
    (void)is_in_iram;
    if (intno < 0 || intno > 31 || cpu < 0 || cpu > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t esp_intr_reserve(int intno, int cpu)
{
    if (intno < 0 || intno > 31 || cpu < 0 || cpu > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

int esp_intr_get_cpu(intr_handle_t handle)
{
    return handle ? handle->cpu : -1;
}

int esp_intr_get_intno(intr_handle_t handle)
{
    return handle ? handle->intno : -1;
}

esp_err_t esp_intr_disable(intr_handle_t handle)
{
    if (!handle || !handle->used) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->enabled = false;
    return ESP_OK;
}

esp_err_t esp_intr_enable(intr_handle_t handle)
{
    if (!handle || !handle->used) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->enabled = true;
    return ESP_OK;
}

esp_err_t esp_intr_set_in_iram(intr_handle_t handle, bool is_in_iram)
{
    if (!handle || !handle->used) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->in_iram = is_in_iram;
    return ESP_OK;
}

void esp_intr_enable_source(int inum)  { (void)inum; }
void esp_intr_disable_source(int inum) { (void)inum; }

int esp_shim_intr_dispatch(int source)
{
    int n = 0;
    for (int i = 0; i < SIM_INTR_SLOTS; i++) {
        if (s_intr[i].used && s_intr[i].enabled && s_intr[i].source == source) {
            s_intr[i].isr(s_intr[i].arg);
            n++;
        }
    }
    return n;
}

int esp_shim_intr_count(void)
{
    int n = 0;
    for (int i = 0; i < SIM_INTR_SLOTS; i++) {
        n += s_intr[i].used ? 1 : 0;
    }
    return n;
}

/* ================================================================== */
/*  Sim register file — backend for REG_READ/REG_WRITE                 */
/*                                                                     */
/*  IO_MUX pad registers (DR_REG_IO_MUX_BASE + 4*gpio) get dedicated   */
/*  cells so GPIO_PIN_MUX_REG save/restore works; every other written  */
/*  address lands in a small sparse file (read-of-never-written = 0).  */
/* ================================================================== */

static uint32_t s_iomux[SOC_GPIO_PIN_COUNT];

#define SIM_REGFILE_N 256
static struct { uint32_t addr, val; } s_regfile[SIM_REGFILE_N];
static unsigned s_regfile_used;

static uint32_t *regfile_cell(uint32_t addr, bool create)
{
    if (addr >= DR_REG_IO_MUX_BASE &&
        addr <  DR_REG_IO_MUX_BASE + 4u * SOC_GPIO_PIN_COUNT &&
        (addr & 3u) == 0) {
        return &s_iomux[(addr - DR_REG_IO_MUX_BASE) / 4u];
    }
    unsigned free_i = SIM_REGFILE_N;
    for (unsigned i = 0; i < s_regfile_used; i++) {
        if (s_regfile[i].addr == addr) {
            return &s_regfile[i].val;
        }
    }
    if (!create || s_regfile_used >= SIM_REGFILE_N) {
        return NULL;
    }
    free_i = s_regfile_used++;
    s_regfile[free_i].addr = addr;
    s_regfile[free_i].val  = 0;
    return &s_regfile[free_i].val;
}

uint32_t esp_shim_reg_read(uint32_t addr)
{
    uint32_t *c = regfile_cell(addr, false);
    return c ? *c : 0;
}

void esp_shim_reg_write(uint32_t addr, uint32_t val)
{
    uint32_t *c = regfile_cell(addr, true);
    if (c) {
        *c = val;
    }
}

/* ================================================================== */
/*  soc/gpio_periph tables                                             */
/* ================================================================== */

#define SIM_IOMUX_ADDR(g) (DR_REG_IO_MUX_BASE + 4u * (g))

const uint32_t GPIO_PIN_MUX_REG[SOC_GPIO_PIN_COUNT] = {
    SIM_IOMUX_ADDR(0),  SIM_IOMUX_ADDR(1),  SIM_IOMUX_ADDR(2),
    SIM_IOMUX_ADDR(3),  SIM_IOMUX_ADDR(4),  SIM_IOMUX_ADDR(5),
    SIM_IOMUX_ADDR(6),  SIM_IOMUX_ADDR(7),  SIM_IOMUX_ADDR(8),
    SIM_IOMUX_ADDR(9),  SIM_IOMUX_ADDR(10), SIM_IOMUX_ADDR(11),
    SIM_IOMUX_ADDR(12), SIM_IOMUX_ADDR(13), SIM_IOMUX_ADDR(14),
    SIM_IOMUX_ADDR(15), SIM_IOMUX_ADDR(16), SIM_IOMUX_ADDR(17),
    SIM_IOMUX_ADDR(18), SIM_IOMUX_ADDR(19), SIM_IOMUX_ADDR(20),
    SIM_IOMUX_ADDR(21),
    0, 0, 0, 0,                          /* GPIO 22-25 do not exist */
    SIM_IOMUX_ADDR(26), SIM_IOMUX_ADDR(27), SIM_IOMUX_ADDR(28),
    SIM_IOMUX_ADDR(29), SIM_IOMUX_ADDR(30), SIM_IOMUX_ADDR(31),
    SIM_IOMUX_ADDR(32), SIM_IOMUX_ADDR(33), SIM_IOMUX_ADDR(34),
    SIM_IOMUX_ADDR(35), SIM_IOMUX_ADDR(36), SIM_IOMUX_ADDR(37),
    SIM_IOMUX_ADDR(38), SIM_IOMUX_ADDR(39), SIM_IOMUX_ADDR(40),
    SIM_IOMUX_ADDR(41), SIM_IOMUX_ADDR(42), SIM_IOMUX_ADDR(43),
    SIM_IOMUX_ADDR(44), SIM_IOMUX_ADDR(45), SIM_IOMUX_ADDR(46),
    SIM_IOMUX_ADDR(47), SIM_IOMUX_ADDR(48),
};

const uint32_t GPIO_HOLD_MASK[SOC_GPIO_PIN_COUNT] = { 0 };

/* ================================================================== */
/*  soc/spi_periph table — real ESP32-S3 values                        */
/* ================================================================== */

const spi_signal_conn_t spi_periph_signal[SOC_SPI_PERIPH_NUM] = {
    {
        /* SPI0/1 — MSPI: dedicated iomux pins, no matrix routing */
        .spiclk_out = (uint8_t)-1, .spiclk_in = (uint8_t)-1,
        .spid_out   = (uint8_t)-1, .spiq_out  = (uint8_t)-1,
        .spiwp_out  = (uint8_t)-1, .spihd_out = (uint8_t)-1,
        .spid4_out  = (uint8_t)-1, .spid5_out = (uint8_t)-1,
        .spid6_out  = (uint8_t)-1, .spid7_out = (uint8_t)-1,
        .spid_in    = (uint8_t)-1, .spiq_in   = (uint8_t)-1,
        .spiwp_in   = (uint8_t)-1, .spihd_in  = (uint8_t)-1,
        .spid4_in   = (uint8_t)-1, .spid5_in  = (uint8_t)-1,
        .spid6_in   = (uint8_t)-1, .spid7_in  = (uint8_t)-1,
        .spics_out  = {(uint8_t)-1},
        .spics_in   = (uint8_t)-1,
        .spidqs_out = (uint8_t)-1, .spicd_out = (uint8_t)-1,
        .spiclk_iomux_pin = (uint8_t)-1, .spid_iomux_pin = (uint8_t)-1,
        .spiq_iomux_pin   = (uint8_t)-1, .spiwp_iomux_pin = (uint8_t)-1,
        .spihd_iomux_pin  = (uint8_t)-1, .spics0_iomux_pin = (uint8_t)-1,
        .irq = (uint8_t)-1, .irq_dma = (uint8_t)-1,
        .func = -1,
        .hw = NULL,
    }, {
        /* GPSPI2 / FSPI — the vq_spi bus host */
        .spiclk_out = FSPICLK_OUT_IDX, .spiclk_in = FSPICLK_IN_IDX,
        .spid_out   = FSPID_OUT_IDX,  .spiq_out  = FSPIQ_OUT_IDX,
        .spiwp_out  = FSPIWP_OUT_IDX, .spihd_out = FSPIHD_OUT_IDX,
        .spid4_out  = FSPIIO4_OUT_IDX, .spid5_out = FSPIIO5_OUT_IDX,
        .spid6_out  = FSPIIO6_OUT_IDX, .spid7_out = FSPIIO7_OUT_IDX,
        .spid_in    = FSPID_IN_IDX,   .spiq_in   = FSPIQ_IN_IDX,
        .spiwp_in   = FSPIWP_IN_IDX,  .spihd_in  = FSPIHD_IN_IDX,
        .spid4_in   = FSPIIO4_IN_IDX,  .spid5_in = FSPIIO5_IN_IDX,
        .spid6_in   = FSPIIO6_IN_IDX,  .spid7_in = FSPIIO7_IN_IDX,
        .spics_out  = {FSPICS0_OUT_IDX, FSPICS1_OUT_IDX, FSPICS2_OUT_IDX,
                      FSPICS3_OUT_IDX, FSPICS4_OUT_IDX, FSPICS5_OUT_IDX},
        .spics_in   = FSPICS0_IN_IDX,
        .spidqs_out = FSPIDQS_OUT_IDX, .spicd_out = (uint8_t)-1,
        .spiclk_iomux_pin = SPI2_IOMUX_PIN_NUM_CLK,
        .spid_iomux_pin   = SPI2_IOMUX_PIN_NUM_MOSI,
        .spiq_iomux_pin   = SPI2_IOMUX_PIN_NUM_MISO,
        .spiwp_iomux_pin  = SPI2_IOMUX_PIN_NUM_WP,
        .spihd_iomux_pin  = SPI2_IOMUX_PIN_NUM_HD,
        .spics0_iomux_pin = SPI2_IOMUX_PIN_NUM_CS,
        .irq = ETS_SPI2_INTR_SOURCE, .irq_dma = (uint8_t)-1,
        .func = SPI2_FUNC_NUM,
        .hw = NULL,
    }, {
        /* GPSPI3 — matrix-only (no iomux pins on ESP32-S3) */
        .spiclk_out = SPI3_CLK_OUT_IDX, .spiclk_in = SPI3_CLK_IN_IDX,
        .spid_out   = SPI3_D_OUT_IDX,  .spiq_out  = SPI3_Q_OUT_IDX,
        .spiwp_out  = SPI3_WP_OUT_IDX, .spihd_out = SPI3_HD_OUT_IDX,
        .spid4_out  = (uint8_t)-1, .spid5_out = (uint8_t)-1,
        .spid6_out  = (uint8_t)-1, .spid7_out = (uint8_t)-1,
        .spid_in    = SPI3_D_IN_IDX,   .spiq_in   = SPI3_Q_IN_IDX,
        .spiwp_in   = SPI3_WP_IN_IDX,  .spihd_in  = SPI3_HD_IN_IDX,
        .spid4_in   = (uint8_t)-1, .spid5_in  = (uint8_t)-1,
        .spid6_in   = (uint8_t)-1, .spid7_in  = (uint8_t)-1,
        .spics_out  = {SPI3_CS0_OUT_IDX, SPI3_CS1_OUT_IDX, SPI3_CS2_OUT_IDX},
        .spics_in   = SPI3_CS0_IN_IDX,
        .spidqs_out = (uint8_t)-1, .spicd_out = (uint8_t)-1,
        .spiclk_iomux_pin = (uint8_t)-1, .spid_iomux_pin = (uint8_t)-1,
        .spiq_iomux_pin   = (uint8_t)-1, .spiwp_iomux_pin = (uint8_t)-1,
        .spihd_iomux_pin  = (uint8_t)-1, .spics0_iomux_pin = (uint8_t)-1,
        .irq = ETS_SPI3_INTR_SOURCE, .irq_dma = (uint8_t)-1,
        .func = -1,
        .hw = NULL,
    },
};

/* ================================================================== */
/*  esp_flash — supplementary functions                                */
/*                                                                     */
/*  The chip driver itself (16 MiB image, read/write/erase/get_size/   */
/*  read_id/get_id/init, esp_flash_default_chip) lives in              */
/*  esp_shim/flash/esp_flash_sim.c — file-backed by                    */
/*  sim/build/flash_image.bin (or $VSIM_FLASH_PATH).  Below are the    */
/*  esp_flash.h declarations it does not implement.                    */
/* ================================================================== */

bool esp_flash_chip_driver_initialized(const esp_flash_t *chip)
{
    (void)chip;
    return true;                         /* sim driver is always up    */
}

esp_err_t esp_flash_get_physical_size(esp_flash_t *chip,
                                      uint32_t *flash_size)
{
    return esp_flash_get_size(chip, flash_size);
}

esp_err_t esp_flash_read_unique_chip_id(esp_flash_t *chip, uint64_t *out_id)
{
    (void)chip;
    if (out_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Plausible unique id folded from the factory MAC. */
    *out_id = 0x00246F2812345600ULL;
    return ESP_OK;
}

esp_err_t esp_flash_get_chip_write_protect(esp_flash_t *chip, bool *write_protected)
{
    (void)chip;
    if (write_protected == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *write_protected = false;
    return ESP_OK;
}

esp_err_t esp_flash_set_chip_write_protect(esp_flash_t *chip, bool write_protect)
{
    (void)chip; (void)write_protect;
    return ESP_OK;
}

esp_err_t esp_flash_get_protectable_regions(const esp_flash_t *chip,
                                            const esp_flash_region_t **out_regions,
                                            uint32_t *out_num_regions)
{
    static const esp_flash_region_t s_regions[] = {
        { 0, ESP_FLASH_SIM_SIZE_BYTES },
    };
    (void)chip;
    if (out_regions == NULL || out_num_regions == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_regions     = s_regions;
    *out_num_regions = sizeof(s_regions) / sizeof(s_regions[0]);
    return ESP_OK;
}

esp_err_t esp_flash_get_protected_region(esp_flash_t *chip,
                                         const esp_flash_region_t *region,
                                         bool *out_protected)
{
    (void)chip; (void)region;
    if (out_protected == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_protected = false;
    return ESP_OK;
}

esp_err_t esp_flash_set_protected_region(esp_flash_t *chip,
                                         const esp_flash_region_t *region,
                                         bool protect)
{
    (void)chip; (void)region; (void)protect;
    return ESP_OK;
}

esp_err_t esp_flash_write_encrypted(esp_flash_t *chip, uint32_t address,
                                    const void *buffer, uint32_t length)
{
    return esp_flash_write(chip, buffer, address, length);
}

esp_err_t esp_flash_read_encrypted(esp_flash_t *chip, uint32_t address,
                                   void *out_buffer, uint32_t length)
{
    return esp_flash_read(chip, out_buffer, address, length);
}

esp_err_t esp_flash_encryption_enabled(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool esp_flash_encryption_cfg_check(esp_flash_enc_mode_t mode, bool enabled)
{
    (void)mode;
    return !enabled;
}

bool esp_flash_encryption_check_mode_supported(esp_flash_enc_mode_t mode)
{
    (void)mode;
    return false;
}

/* ================================================================== */
/*  esp_ota_ops — ota_0 / ota_1 slots (see partitions.csv)             */
/*                                                                     */
/*  Row data comes straight out of the partition shim's table          */
/*  (esp_partition_find_first) so OTA handles and partition handles    */
/*  are interchangeable.  Boot selection is recorded in s_boot_sel.    */
/* ================================================================== */

static const esp_partition_t *s_boot_sel;

const esp_partition_t *esp_ota_get_running_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
}

const esp_partition_t *esp_ota_get_boot_partition(void)
{
    return s_boot_sel ? s_boot_sel : esp_ota_get_running_partition();
}

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from)
{
    const esp_partition_t *from =
        start_from ? start_from : esp_ota_get_running_partition();
    /* Alternate between the two OTA slots. */
    if (from != NULL && from->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                        ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
    }
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_OTA_1, "ota_1");
}

const esp_partition_t *esp_ota_get_next_update_partition_from(const esp_partition_t *start_from)
{
    return esp_ota_get_next_update_partition(start_from);
}

const esp_partition_t *esp_ota_get_last_invalid_partition(void)
{
    return NULL;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition)
{
    if (partition == NULL || partition->type != ESP_PARTITION_TYPE_APP) {
        return ESP_ERR_INVALID_ARG;
    }
    s_boot_sel = partition;
    return ESP_OK;
}

esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition,
                                            void *app_desc)
{
    if (partition == NULL || app_desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(app_desc, 0, 256);       /* esp_app_desc_t is 256 bytes */
    return ESP_OK;
}

esp_err_t esp_ota_get_boot_partition_description(const esp_partition_t *partition,
                                                 void *app_desc)
{
    return esp_ota_get_partition_description(partition, app_desc);
}

esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition,
                                      esp_ota_img_states_t *ota_state)
{
    if (partition == NULL || ota_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *ota_state = ESP_OTA_IMG_VALID;
    return ESP_OK;
}

esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
    return ESP_OK;
}

esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void)
{
    return ESP_OK;
}

esp_err_t esp_ota_erase_last_boot_app_partition(void)
{
    return ESP_OK;
}

int esp_ota_check_rollback_is_possible(void)
{
    return 0;
}

/* OTA write sessions: a small table of open writes over
 * esp_partition_write; refuses the running partition like silicon.   */
#define SIM_OTA_HANDLES 8
static struct {
    const esp_partition_t *part;
    uint32_t               offset;
    bool                   used;
} s_ota_w[SIM_OTA_HANDLES];

esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size,
                        esp_ota_handle_t *out_handle)
{
    (void)image_size;
    if (partition == NULL || out_handle == NULL ||
        partition->type != ESP_PARTITION_TYPE_APP) {
        return ESP_ERR_INVALID_ARG;
    }
    if (partition == esp_ota_get_running_partition()) {
        return ESP_ERR_OTA_PARTITION_CONFLICT;
    }
    for (int i = 0; i < SIM_OTA_HANDLES; i++) {
        if (!s_ota_w[i].used) {
            s_ota_w[i].used   = true;
            s_ota_w[i].part   = partition;
            s_ota_w[i].offset = 0;
            *out_handle = (esp_ota_handle_t)(i + 1);
            return ESP_OK;
        }
    }
    return ESP_ERR_OTA_PARTITION_CONFLICT;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size)
{
    if (handle == 0 || handle > SIM_OTA_HANDLES ||
        !s_ota_w[handle - 1].used || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    struct { const esp_partition_t *part; uint32_t off; } s;
    s.part = s_ota_w[handle - 1].part;
    s.off  = s_ota_w[handle - 1].offset;
    esp_err_t r = esp_partition_write(s.part, s.off, data, size);
    if (r == ESP_OK) {
        s_ota_w[handle - 1].offset += (uint32_t)size;
    }
    return r;
}

esp_err_t esp_ota_write_with_offset(esp_ota_handle_t handle, const void *data,
                                    size_t size, uint32_t offset)
{
    if (handle == 0 || handle > SIM_OTA_HANDLES ||
        !s_ota_w[handle - 1].used || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_partition_write(s_ota_w[handle - 1].part, offset, data, size);
}

esp_err_t esp_ota_end(esp_ota_handle_t handle)
{
    if (handle == 0 || handle > SIM_OTA_HANDLES || !s_ota_w[handle - 1].used) {
        return ESP_ERR_INVALID_ARG;
    }
    s_ota_w[handle - 1].used = false;
    return ESP_OK;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{
    return esp_ota_end(handle);
}
