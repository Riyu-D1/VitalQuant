/* efuse_mac.c — host-sim eFuse storage + MAC functions (ESP32-S3).
 *
 * The sim eFuse array is 11 blocks x 256 bits (8 words each).  All fuse
 * bits power up 0 — matching the fix_v22 hardware brief — EXCEPT the
 * factory MAC burned into BLK1:
 *
 *   MAC = {0x24,0x6F,0x28,0x12,0x34,0x56}
 *   ESP_EFUSE_MAC field descs (MSB-first byte order):
 *       {BLK1,40,8}=mac[0] {BLK1,32,8}=mac[1] {BLK1,24,8}=mac[2]
 *       {BLK1,16,8}=mac[3] {BLK1,8,8}=mac[4]  {BLK1,0,8}=mac[5]
 *   => BLK1 word0 = 0x28123456, word1 = 0x0000246F.
 *
 * Field desc tables mirror the real esp32s3 esp_efuse_table.c bit
 * positions so esp_efuse_read_field_* sees the same picture as silicon.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_mac.h"

/* ================================================================== */
/*  Sim eFuse array                                                    */
/* ================================================================== */

#define EFUSE_WORDS_PER_BLK 8            /* 256 bits per block */

static uint32_t s_efuse[EFUSE_BLK_MAX][EFUSE_WORDS_PER_BLK];
static bool     s_efuse_inited;

static const uint8_t s_factory_mac[6] = { 0x24, 0x6F, 0x28, 0x12, 0x34, 0x56 };

static void efuse_init(void)
{
    memset(s_efuse, 0, sizeof(s_efuse));
    /* BLK1 word0 = {mac2,mac3,mac4,mac5} big-endian-packed into bits
     * 0-31 (bit0-7=mac[5]); word1 low bits = {mac1,mac0}.            */
    s_efuse[EFUSE_BLK1][0] = ((uint32_t)s_factory_mac[2] << 24) |
                             ((uint32_t)s_factory_mac[3] << 16) |
                             ((uint32_t)s_factory_mac[4] <<  8) |
                             ((uint32_t)s_factory_mac[5]);
    s_efuse[EFUSE_BLK1][1] = ((uint32_t)s_factory_mac[0] << 8) |
                             ((uint32_t)s_factory_mac[1]);
    s_efuse_inited = true;
}

static void efuse_ensure(void)
{
    if (!s_efuse_inited) {
        efuse_init();
    }
}

void esp_shim_efuse_reset(void)
{
    efuse_init();
}

static int efuse_bit(esp_efuse_block_t blk, uint32_t bit)
{
    if (blk >= EFUSE_BLK_MAX || bit >= 256u) {
        return 0;
    }
    return (int)((s_efuse[blk][bit >> 5] >> (bit & 31u)) & 1u);
}

esp_err_t esp_shim_efuse_burn(uint32_t blk, uint32_t bit_start,
                              uint32_t bit_count, uint64_t value)
{
    efuse_ensure();
    if (blk >= (uint32_t)EFUSE_BLK_MAX || bit_start + bit_count > 256u ||
        bit_count > 64u) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint32_t i = 0; i < bit_count; i++) {
        const uint32_t bit = bit_start + i;
        if ((value >> i) & 1u) {
            s_efuse[blk][bit >> 5] |=  (1u << (bit & 31u));   /* burn 1s */
        }
    }
    return ESP_OK;
}

/* ================================================================== */
/*  Field readers (walk esp_efuse_desc_t arrays)                       */
/* ================================================================== */

int esp_efuse_get_field_size(const esp_efuse_desc_t* field[])
{
    int bits = 0;
    if (field == NULL) {
        return 0;
    }
    for (int i = 0; field[i] != NULL; i++) {
        bits += field[i]->bit_count;
    }
    return bits;
}

esp_err_t esp_efuse_read_field_blob(const esp_efuse_desc_t* field[],
                                    void* dst, size_t dst_size_bits)
{
    if (field == NULL || dst == NULL || dst_size_bits == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    efuse_ensure();
    uint8_t *out = dst;
    memset(out, 0, (dst_size_bits + 7u) / 8u);
    size_t out_bit = 0;
    for (int i = 0; field[i] != NULL && out_bit < dst_size_bits; i++) {
        const esp_efuse_desc_t *d = field[i];
        if (d->efuse_block >= EFUSE_BLK_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        for (uint32_t b = 0; b < d->bit_count && out_bit < dst_size_bits;
             b++, out_bit++) {
            const int v = efuse_bit(d->efuse_block, d->bit_start + b);
            if (v) {
                out[out_bit >> 3] |= (uint8_t)(1u << (out_bit & 7u));
            }
        }
    }
    return ESP_OK;
}

bool esp_efuse_read_field_bit(const esp_efuse_desc_t *field[])
{
    if (field == NULL || field[0] == NULL || field[0]->bit_count < 1 ||
        field[0]->efuse_block >= EFUSE_BLK_MAX) {
        return false;
    }
    efuse_ensure();
    return efuse_bit(field[0]->efuse_block, field[0]->bit_start) != 0;
}

esp_err_t esp_efuse_read_field_cnt(const esp_efuse_desc_t* field[],
                                   size_t* out_cnt)
{
    if (field == NULL || out_cnt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    efuse_ensure();
    size_t cnt = 0;
    for (int i = 0; field[i] != NULL; i++) {
        const esp_efuse_desc_t *d = field[i];
        if (d->efuse_block >= EFUSE_BLK_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        for (uint32_t b = 0; b < d->bit_count; b++) {
            cnt += (size_t)efuse_bit(d->efuse_block, d->bit_start + b);
        }
    }
    *out_cnt = cnt;
    return ESP_OK;
}

/* eFuses are READ-ONLY on fix_v22 — writes report NOT_SUPPORTED so a
 * scenario can notice if firmware ever tries to burn.               */
esp_err_t esp_efuse_write_field_blob(const esp_efuse_desc_t* field[],
                                     const void* src, size_t src_size_bits)
{
    (void)field; (void)src; (void)src_size_bits;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_efuse_write_field_cnt(const esp_efuse_desc_t* field[], size_t cnt)
{
    (void)field; (void)cnt;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_efuse_write_field_bit(const esp_efuse_desc_t* field[])
{
    (void)field;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_efuse_set_write_protect(esp_efuse_block_t blk)
{
    (void)blk;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_efuse_set_read_protect(esp_efuse_block_t blk)
{
    (void)blk;
    return ESP_ERR_NOT_SUPPORTED;
}

uint32_t esp_efuse_read_reg(esp_efuse_block_t blk, unsigned int num_reg)
{
    efuse_ensure();
    if (blk >= EFUSE_BLK_MAX || num_reg >= EFUSE_WORDS_PER_BLK) {
        return 0;
    }
    return s_efuse[blk][num_reg];
}

esp_err_t esp_efuse_write_reg(esp_efuse_block_t blk, unsigned int num_reg,
                              uint32_t val)
{
    efuse_ensure();
    if (blk >= EFUSE_BLK_MAX || num_reg >= EFUSE_WORDS_PER_BLK) {
        return ESP_ERR_INVALID_ARG;
    }
    s_efuse[blk][num_reg] |= val;   /* fuse-program semantics: set 1s */
    return ESP_OK;
}

uint32_t esp_efuse_get_pkg_ver(void)
{
    return 0;
}

/* ================================================================== */
/*  Field descriptor tables — real ESP32-S3 bit positions              */
/* ================================================================== */

#define DESC1(blk, st, n)   {{blk, st, n}}
#define PUB(name, arr)      const esp_efuse_desc_t* name[] = { arr, NULL };

/* ---------- BLK0 : REPEAT_DATA ---------- */
static const esp_efuse_desc_t d_WR_DIS[]        = DESC1(EFUSE_BLK0, 0, 32);
static const esp_efuse_desc_t d_WR_DIS_RD_DIS[] = DESC1(EFUSE_BLK0, 0, 1);
static const esp_efuse_desc_t d_WR_DIS_DIS_USB_JTAG[]         = DESC1(EFUSE_BLK0, 2, 1);
static const esp_efuse_desc_t d_WR_DIS_DIS_USB_SERIAL_JTAG[]  = DESC1(EFUSE_BLK0, 2, 1);
static const esp_efuse_desc_t d_WR_DIS_STRAP_JTAG_SEL[]       = DESC1(EFUSE_BLK0, 2, 1);
static const esp_efuse_desc_t d_WR_DIS_VDD_SPI_XPD[]          = DESC1(EFUSE_BLK0, 3, 1);
static const esp_efuse_desc_t d_WR_DIS_VDD_SPI_TIEH[]         = DESC1(EFUSE_BLK0, 3, 1);
static const esp_efuse_desc_t d_WR_DIS_VDD_SPI_FORCE[]        = DESC1(EFUSE_BLK0, 3, 1);
static const esp_efuse_desc_t d_WR_DIS_SPI_BOOT_CRYPT_CNT[]   = DESC1(EFUSE_BLK0, 4, 1);
static const esp_efuse_desc_t d_WR_DIS_SECURE_BOOT_EN[]       = DESC1(EFUSE_BLK0, 15, 1);
static const esp_efuse_desc_t d_RD_DIS[]          = DESC1(EFUSE_BLK0, 32, 7);
static const esp_efuse_desc_t d_DIS_ICACHE[]      = DESC1(EFUSE_BLK0, 40, 1);
static const esp_efuse_desc_t d_DIS_DCACHE[]      = DESC1(EFUSE_BLK0, 41, 1);
static const esp_efuse_desc_t d_DIS_DOWNLOAD_ICACHE[] = DESC1(EFUSE_BLK0, 42, 1);
static const esp_efuse_desc_t d_DIS_DOWNLOAD_DCACHE[] = DESC1(EFUSE_BLK0, 43, 1);
static const esp_efuse_desc_t d_DIS_FORCE_DOWNLOAD[]  = DESC1(EFUSE_BLK0, 44, 1);
static const esp_efuse_desc_t d_DIS_USB_OTG[]     = DESC1(EFUSE_BLK0, 45, 1);
static const esp_efuse_desc_t d_DIS_TWAI[]        = DESC1(EFUSE_BLK0, 46, 1);
static const esp_efuse_desc_t d_DIS_APP_CPU[]     = DESC1(EFUSE_BLK0, 47, 1);
static const esp_efuse_desc_t d_SOFT_DIS_JTAG[]   = DESC1(EFUSE_BLK0, 48, 3);
static const esp_efuse_desc_t d_DIS_PAD_JTAG[]    = DESC1(EFUSE_BLK0, 51, 1);
static const esp_efuse_desc_t d_DIS_DOWNLOAD_MANUAL_ENCRYPT[] = DESC1(EFUSE_BLK0, 52, 1);
static const esp_efuse_desc_t d_USB_EXCHG_PINS[]  = DESC1(EFUSE_BLK0, 57, 1);
static const esp_efuse_desc_t d_USB_EXT_PHY_ENABLE[] = DESC1(EFUSE_BLK0, 58, 1);
static const esp_efuse_desc_t d_VDD_SPI_XPD[]     = DESC1(EFUSE_BLK0, 68, 1);
static const esp_efuse_desc_t d_VDD_SPI_TIEH[]    = DESC1(EFUSE_BLK0, 69, 1);
static const esp_efuse_desc_t d_VDD_SPI_FORCE[]   = DESC1(EFUSE_BLK0, 70, 1);
static const esp_efuse_desc_t d_WDT_DELAY_SEL[]   = DESC1(EFUSE_BLK0, 80, 2);
static const esp_efuse_desc_t d_SPI_BOOT_CRYPT_CNT[] = DESC1(EFUSE_BLK0, 82, 3);
static const esp_efuse_desc_t d_SECURE_BOOT_KEY_REVOKE0[] = DESC1(EFUSE_BLK0, 85, 1);
static const esp_efuse_desc_t d_SECURE_BOOT_KEY_REVOKE1[] = DESC1(EFUSE_BLK0, 86, 1);
static const esp_efuse_desc_t d_SECURE_BOOT_KEY_REVOKE2[] = DESC1(EFUSE_BLK0, 87, 1);
static const esp_efuse_desc_t d_SECURE_BOOT_EN[]  = DESC1(EFUSE_BLK0, 116, 1);
static const esp_efuse_desc_t d_SECURE_BOOT_AGGRESSIVE_REVOKE[] = DESC1(EFUSE_BLK0, 117, 1);
static const esp_efuse_desc_t d_DIS_USB_JTAG[]    = DESC1(EFUSE_BLK0, 118, 1);
static const esp_efuse_desc_t d_DIS_USB_SERIAL_JTAG[] = DESC1(EFUSE_BLK0, 119, 1);
static const esp_efuse_desc_t d_STRAP_JTAG_SEL[]  = DESC1(EFUSE_BLK0, 120, 1);
static const esp_efuse_desc_t d_USB_PHY_SEL[]     = DESC1(EFUSE_BLK0, 121, 1);
static const esp_efuse_desc_t d_FLASH_TPUW[]      = DESC1(EFUSE_BLK0, 124, 4);
static const esp_efuse_desc_t d_DIS_DOWNLOAD_MODE[] = DESC1(EFUSE_BLK0, 128, 1);
static const esp_efuse_desc_t d_DIS_DIRECT_BOOT[] = DESC1(EFUSE_BLK0, 129, 1);
static const esp_efuse_desc_t d_DIS_USB_SERIAL_JTAG_ROM_PRINT[] = DESC1(EFUSE_BLK0, 130, 1);
static const esp_efuse_desc_t d_FLASH_ECC_MODE[]  = DESC1(EFUSE_BLK0, 131, 1);
static const esp_efuse_desc_t d_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE[] = DESC1(EFUSE_BLK0, 132, 1);
static const esp_efuse_desc_t d_ENABLE_SECURITY_DOWNLOAD[] = DESC1(EFUSE_BLK0, 133, 1);
static const esp_efuse_desc_t d_UART_PRINT_CONTROL[] = DESC1(EFUSE_BLK0, 134, 2);
static const esp_efuse_desc_t d_PIN_POWER_SELECTION[] = DESC1(EFUSE_BLK0, 136, 1);
static const esp_efuse_desc_t d_FLASH_TYPE[]      = DESC1(EFUSE_BLK0, 137, 1);
static const esp_efuse_desc_t d_FLASH_PAGE_SIZE[] = DESC1(EFUSE_BLK0, 138, 2);
static const esp_efuse_desc_t d_FLASH_ECC_EN[]    = DESC1(EFUSE_BLK0, 140, 1);
static const esp_efuse_desc_t d_FORCE_SEND_RESUME[] = DESC1(EFUSE_BLK0, 141, 1);
static const esp_efuse_desc_t d_SECURE_VERSION[]  = DESC1(EFUSE_BLK0, 142, 16);
static const esp_efuse_desc_t d_DIS_USB_OTG_DOWNLOAD_MODE[] = DESC1(EFUSE_BLK0, 159, 1);
static const esp_efuse_desc_t d_DISABLE_WAFER_VERSION_MAJOR[] = DESC1(EFUSE_BLK0, 160, 1);
static const esp_efuse_desc_t d_DISABLE_BLK_VERSION_MAJOR[]   = DESC1(EFUSE_BLK0, 161, 1);

/* ---------- BLK1 : MAC_SPI_8M_SYS ---------- */
static const esp_efuse_desc_t d_MAC[] = {
    {EFUSE_BLK1, 40, 8},   /* mac[0] */
    {EFUSE_BLK1, 32, 8},   /* mac[1] */
    {EFUSE_BLK1, 24, 8},   /* mac[2] */
    {EFUSE_BLK1, 16, 8},   /* mac[3] */
    {EFUSE_BLK1,  8, 8},   /* mac[4] */
    {EFUSE_BLK1,  0, 8},   /* mac[5] */
};
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_CLK[] = DESC1(EFUSE_BLK1, 48, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_Q[]   = DESC1(EFUSE_BLK1, 54, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_D[]   = DESC1(EFUSE_BLK1, 60, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_CS[]  = DESC1(EFUSE_BLK1, 66, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_HD[]  = DESC1(EFUSE_BLK1, 72, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_WP[]  = DESC1(EFUSE_BLK1, 78, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_DQS[] = DESC1(EFUSE_BLK1, 84, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_D4[]  = DESC1(EFUSE_BLK1, 90, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_D5[]  = DESC1(EFUSE_BLK1, 96, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_D6[]  = DESC1(EFUSE_BLK1, 102, 6);
static const esp_efuse_desc_t d_SPI_PAD_CONFIG_D7[]  = DESC1(EFUSE_BLK1, 108, 6);

/* ---------- BLK3 : USER_DATA ---------- */
static const esp_efuse_desc_t d_USER_DATA[]            = DESC1(EFUSE_BLK3, 0, 256);
static const esp_efuse_desc_t d_USER_DATA_MAC_CUSTOM[] = DESC1(EFUSE_BLK3, 200, 48);

/* ---------- public arrays ---------- */
PUB(ESP_EFUSE_WR_DIS,                        d_WR_DIS)
PUB(ESP_EFUSE_WR_DIS_RD_DIS,                 d_WR_DIS_RD_DIS)
PUB(ESP_EFUSE_WR_DIS_DIS_USB_JTAG,           d_WR_DIS_DIS_USB_JTAG)
PUB(ESP_EFUSE_WR_DIS_DIS_USB_SERIAL_JTAG,    d_WR_DIS_DIS_USB_SERIAL_JTAG)
PUB(ESP_EFUSE_WR_DIS_STRAP_JTAG_SEL,         d_WR_DIS_STRAP_JTAG_SEL)
PUB(ESP_EFUSE_WR_DIS_VDD_SPI_XPD,            d_WR_DIS_VDD_SPI_XPD)
PUB(ESP_EFUSE_WR_DIS_VDD_SPI_TIEH,           d_WR_DIS_VDD_SPI_TIEH)
PUB(ESP_EFUSE_WR_DIS_VDD_SPI_FORCE,          d_WR_DIS_VDD_SPI_FORCE)
PUB(ESP_EFUSE_WR_DIS_SPI_BOOT_CRYPT_CNT,     d_WR_DIS_SPI_BOOT_CRYPT_CNT)
PUB(ESP_EFUSE_WR_DIS_SECURE_BOOT_EN,         d_WR_DIS_SECURE_BOOT_EN)
PUB(ESP_EFUSE_RD_DIS,                        d_RD_DIS)
PUB(ESP_EFUSE_DIS_ICACHE,                    d_DIS_ICACHE)
PUB(ESP_EFUSE_DIS_DCACHE,                    d_DIS_DCACHE)
PUB(ESP_EFUSE_DIS_DOWNLOAD_ICACHE,           d_DIS_DOWNLOAD_ICACHE)
PUB(ESP_EFUSE_DIS_DOWNLOAD_DCACHE,           d_DIS_DOWNLOAD_DCACHE)
PUB(ESP_EFUSE_DIS_FORCE_DOWNLOAD,            d_DIS_FORCE_DOWNLOAD)
PUB(ESP_EFUSE_DIS_USB_OTG,                   d_DIS_USB_OTG)
PUB(ESP_EFUSE_DIS_TWAI,                      d_DIS_TWAI)
PUB(ESP_EFUSE_DIS_APP_CPU,                   d_DIS_APP_CPU)
PUB(ESP_EFUSE_SOFT_DIS_JTAG,                 d_SOFT_DIS_JTAG)
PUB(ESP_EFUSE_DIS_PAD_JTAG,                  d_DIS_PAD_JTAG)
PUB(ESP_EFUSE_DIS_DOWNLOAD_MANUAL_ENCRYPT,   d_DIS_DOWNLOAD_MANUAL_ENCRYPT)
PUB(ESP_EFUSE_USB_EXCHG_PINS,                d_USB_EXCHG_PINS)
PUB(ESP_EFUSE_USB_EXT_PHY_ENABLE,            d_USB_EXT_PHY_ENABLE)
PUB(ESP_EFUSE_VDD_SPI_XPD,                   d_VDD_SPI_XPD)
PUB(ESP_EFUSE_VDD_SPI_TIEH,                  d_VDD_SPI_TIEH)
PUB(ESP_EFUSE_VDD_SPI_FORCE,                 d_VDD_SPI_FORCE)
PUB(ESP_EFUSE_WDT_DELAY_SEL,                 d_WDT_DELAY_SEL)
PUB(ESP_EFUSE_SPI_BOOT_CRYPT_CNT,            d_SPI_BOOT_CRYPT_CNT)
PUB(ESP_EFUSE_SECURE_BOOT_KEY_REVOKE0,       d_SECURE_BOOT_KEY_REVOKE0)
PUB(ESP_EFUSE_SECURE_BOOT_KEY_REVOKE1,       d_SECURE_BOOT_KEY_REVOKE1)
PUB(ESP_EFUSE_SECURE_BOOT_KEY_REVOKE2,       d_SECURE_BOOT_KEY_REVOKE2)
PUB(ESP_EFUSE_SECURE_BOOT_EN,                d_SECURE_BOOT_EN)
PUB(ESP_EFUSE_SECURE_BOOT_AGGRESSIVE_REVOKE, d_SECURE_BOOT_AGGRESSIVE_REVOKE)
PUB(ESP_EFUSE_DIS_USB_JTAG,                  d_DIS_USB_JTAG)
PUB(ESP_EFUSE_DIS_USB_SERIAL_JTAG,           d_DIS_USB_SERIAL_JTAG)
PUB(ESP_EFUSE_STRAP_JTAG_SEL,                d_STRAP_JTAG_SEL)
PUB(ESP_EFUSE_USB_PHY_SEL,                   d_USB_PHY_SEL)
PUB(ESP_EFUSE_FLASH_TPUW,                    d_FLASH_TPUW)
PUB(ESP_EFUSE_DIS_DOWNLOAD_MODE,             d_DIS_DOWNLOAD_MODE)
PUB(ESP_EFUSE_DIS_DIRECT_BOOT,               d_DIS_DIRECT_BOOT)
PUB(ESP_EFUSE_DIS_USB_SERIAL_JTAG_ROM_PRINT, d_DIS_USB_SERIAL_JTAG_ROM_PRINT)
PUB(ESP_EFUSE_FLASH_ECC_MODE,                d_FLASH_ECC_MODE)
PUB(ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE, d_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE)
PUB(ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD,      d_ENABLE_SECURITY_DOWNLOAD)
PUB(ESP_EFUSE_UART_PRINT_CONTROL,            d_UART_PRINT_CONTROL)
PUB(ESP_EFUSE_PIN_POWER_SELECTION,           d_PIN_POWER_SELECTION)
PUB(ESP_EFUSE_FLASH_TYPE,                    d_FLASH_TYPE)
PUB(ESP_EFUSE_FLASH_PAGE_SIZE,               d_FLASH_PAGE_SIZE)
PUB(ESP_EFUSE_FLASH_ECC_EN,                  d_FLASH_ECC_EN)
PUB(ESP_EFUSE_FORCE_SEND_RESUME,             d_FORCE_SEND_RESUME)
PUB(ESP_EFUSE_SECURE_VERSION,                d_SECURE_VERSION)
PUB(ESP_EFUSE_DIS_USB_OTG_DOWNLOAD_MODE,     d_DIS_USB_OTG_DOWNLOAD_MODE)
PUB(ESP_EFUSE_DISABLE_WAFER_VERSION_MAJOR,   d_DISABLE_WAFER_VERSION_MAJOR)
PUB(ESP_EFUSE_DISABLE_BLK_VERSION_MAJOR,     d_DISABLE_BLK_VERSION_MAJOR)
/* ESP_EFUSE_MAC is a 6-desc field — list each desc explicitly.
 * (ESP_EFUSE_MAC_FACTORY/CUSTOM_MAC/etc. are header-side macro
 * aliases onto this and the other arrays.)                          */
const esp_efuse_desc_t* ESP_EFUSE_MAC[] = {
    &d_MAC[0], &d_MAC[1], &d_MAC[2], &d_MAC[3], &d_MAC[4], &d_MAC[5], NULL,
};
PUB(ESP_EFUSE_SPI_PAD_CONFIG_CLK,            d_SPI_PAD_CONFIG_CLK)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_Q,              d_SPI_PAD_CONFIG_Q)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_D,              d_SPI_PAD_CONFIG_D)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_CS,             d_SPI_PAD_CONFIG_CS)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_HD,             d_SPI_PAD_CONFIG_HD)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_WP,             d_SPI_PAD_CONFIG_WP)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_DQS,            d_SPI_PAD_CONFIG_DQS)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_D4,             d_SPI_PAD_CONFIG_D4)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_D5,             d_SPI_PAD_CONFIG_D5)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_D6,             d_SPI_PAD_CONFIG_D6)
PUB(ESP_EFUSE_SPI_PAD_CONFIG_D7,             d_SPI_PAD_CONFIG_D7)
PUB(ESP_EFUSE_USER_DATA,                     d_USER_DATA)
PUB(ESP_EFUSE_USER_DATA_MAC_CUSTOM,          d_USER_DATA_MAC_CUSTOM)

/* ================================================================== */
/*  esp_mac.h — MAC address functions                                  */
/* ================================================================== */

#define MAC_TBL_N (ESP_MAC_EFUSE_EXT + 1)

static struct {
    bool    set;
    uint8_t mac[8];
    uint8_t len;
} s_mac_tbl[MAC_TBL_N];

size_t esp_mac_addr_len_get(esp_mac_type_t type)
{
    switch (type) {
    case ESP_MAC_IEEE802154: return 8;
    case ESP_MAC_EFUSE_EXT:  return 2;
    case ESP_MAC_WIFI_STA:
    case ESP_MAC_WIFI_SOFTAP:
    case ESP_MAC_BT:
    case ESP_MAC_ETH:
    case ESP_MAC_BASE:
    case ESP_MAC_EFUSE_FACTORY:
    case ESP_MAC_EFUSE_CUSTOM:
        return 6;
    default:
        return 0;
    }
}

int esp_mac_get_len(esp_mac_type_t type)
{
    return (int)esp_mac_addr_len_get(type);
}

bool esp_mac_is_uninitialized(const uint8_t *mac)
{
    if (mac == NULL) {
        return true;
    }
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            return false;
        }
    }
    return true;
}

esp_err_t esp_efuse_mac_get_default(uint8_t *mac)
{
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_efuse_read_field_blob(ESP_EFUSE_MAC_FACTORY, mac,
                                     (size_t)esp_efuse_get_field_size(ESP_EFUSE_MAC_FACTORY));
}

esp_err_t esp_efuse_mac_get_custom(uint8_t *mac)
{
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 48 contiguous bits at BLK3:200 → mac[0] = bits 200-207, ...     */
    return esp_efuse_read_field_blob(ESP_EFUSE_MAC_CUSTOM, mac,
                                     (size_t)esp_efuse_get_field_size(ESP_EFUSE_MAC_CUSTOM));
}

esp_err_t esp_derive_local_mac(uint8_t *local_mac, const uint8_t *universal_mac)
{
    if (local_mac == NULL || universal_mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(local_mac, universal_mac, 6);
    local_mac[0] |= 0x2;                          /* set U/L bit */
    if (local_mac[0] == universal_mac[0]) {
        local_mac[0] ^= 0x4;                      /* already local */
    }
    return ESP_OK;
}

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
    if (mac == NULL || (int)type < 0 || type > ESP_MAC_EFUSE_EXT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mac_tbl[type].set) {
        memcpy(mac, s_mac_tbl[type].mac, s_mac_tbl[type].len);
        return ESP_OK;
    }
    uint8_t base[6];
    esp_err_t e = esp_efuse_mac_get_default(base);
    if (e != ESP_OK) {
        return e;
    }
    switch (type) {
    case ESP_MAC_WIFI_STA:
    case ESP_MAC_BASE:
    case ESP_MAC_EFUSE_FACTORY:
        memcpy(mac, base, 6);
        break;
    case ESP_MAC_WIFI_SOFTAP:
        memcpy(mac, base, 6); mac[5] += 1;
        break;
    case ESP_MAC_BT:
        memcpy(mac, base, 6); mac[5] += 2;
        break;
    case ESP_MAC_ETH:
        memcpy(mac, base, 6); mac[5] += 3;
        break;
    case ESP_MAC_EFUSE_CUSTOM:
        return esp_efuse_mac_get_custom(mac);
    case ESP_MAC_EFUSE_EXT:
        mac[0] = mac[1] = 0;                      /* no 802154 ext fused */
        break;
    case ESP_MAC_IEEE802154:
        memcpy(mac, base, 3);
        mac[3] = 0xFF; mac[4] = 0xFE;
        memcpy(&mac[5], &base[3], 3);
        break;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

esp_err_t esp_iface_mac_addr_set(const uint8_t *mac, esp_mac_type_t type)
{
    if (mac == NULL || (int)type < 0 || type > ESP_MAC_EFUSE_EXT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (type == ESP_MAC_EFUSE_FACTORY || type == ESP_MAC_EFUSE_CUSTOM) {
        return ESP_ERR_INVALID_ARG;   /* eFuse MACs cannot be set */
    }
    const size_t len = esp_mac_addr_len_get(type);
    if (len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_mac_tbl[type].mac, mac, len);
    s_mac_tbl[type].len = (uint8_t)len;
    s_mac_tbl[type].set = true;
    return ESP_OK;
}

esp_err_t esp_base_mac_addr_set(const uint8_t *mac)
{
    return esp_iface_mac_addr_set(mac, ESP_MAC_BASE);
}

esp_err_t esp_base_mac_addr_get(uint8_t *mac)
{
    return esp_read_mac(mac, ESP_MAC_BASE);
}
