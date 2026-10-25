/* soc/soc.h — host-sim shim for ESP32-S3 SoC register access.
 *
 * On target, REG_READ/REG_WRITE dereference MMIO addresses.  In sim,
 * peripheral addresses route through a small sparse register file in
 * the shim (esp_shim_reg_read/write in rom/rom.c): IO_MUX pad registers
 * (DR_REG_IO_MUX_BASE + n*4, see soc/gpio_periph.h) live there, so the
 * firmware's save/restore of GPIO_PIN_MUX_REG works on real memory-
 * backed cells.
 */
#pragma once

#include <stdint.h>

/* esp_bit_defs.h is a sibling shim header; identical fallback here. */
#if defined(__has_include)
#  if __has_include("esp_bit_defs.h")
#    include "esp_bit_defs.h"
#  endif
#endif
#ifndef BIT
#define BIT(nr)                 (1UL << (nr))
#endif
#ifndef BIT64
#define BIT64(nr)               (1ULL << (nr))
#endif

/* Selected peripheral base addresses (real ESP32-S3 values — register
 * accesses to these land in the sim register file, not real MMIO).   */
#define DR_REG_MMU_TABLE                        0x600C5000
#define DR_REG_ITAG_TABLE                       0x600C6000
#define DR_REG_DTAG_TABLE                       0x600C8000
#define DR_REG_EXT_MEM_ENC                      0x600CC000
#define DR_REG_UART_BASE                        0x60000000
#define DR_REG_SPI1_BASE                        0x60002000
#define DR_REG_SPI0_BASE                        0x60003000
#define DR_REG_GPIO_BASE                        0x60004000
#define DR_REG_FE2_BASE                         0x60005000
#define DR_REG_FE_BASE                          0x60006000
#define DR_REG_RTCCNTL_BASE                     0x60008000
#define DR_REG_RTCIO_BASE                       0x60008400
#define DR_REG_SENS_BASE                        0x60008800
#define DR_REG_IO_MUX_BASE                      0x60009000
#define DR_REG_HINF_BASE                        0x6000B000
#define DR_REG_UHCI0_BASE                       0x6000C000
#define DR_REG_I2S_BASE                         0x6000F000
#define DR_REG_UART1_BASE                       0x60010000
#define DR_REG_I2C_EXT_BASE                     0x60013000
#define DR_REG_UHCI1_BASE                       0x60014000
#define DR_REG_SPI2_BASE                        0x60024000
#define DR_REG_SPI3_BASE                        0x60025000
#define DR_REG_TIMERGROUP0_BASE                 0x6001F000
#define DR_REG_TIMERGROUP1_BASE                 0x60020000

#define REG_SPI_BASE(i)         (((i)>=2) ? (DR_REG_SPI2_BASE + (i-2) * 0x1000) : (0))
#define REG_SPI_MEM_BASE(i)     (DR_REG_SPI0_BASE - (i) * 0x1000)
#define REG_I2C_BASE(i)         (DR_REG_I2C_EXT_BASE + (i) * 0x14000)
#define REG_UART_BASE(i)        (DR_REG_UART_BASE + (i) * 0x10000 + ((i) > 1 ? 0xe000 : 0))
#define REG_I2S_BASE(i)         (DR_REG_I2S_BASE + (i) * 0x1E000)
#define REG_TIMG_BASE(i)        (DR_REG_TIMERGROUP0_BASE + (i)*0x1000)

/* Address annotation — sim registers are host-backed already. */
#define ETS_UNCACHED_ADDR(addr) (addr)
#define ETS_CACHED_ADDR(addr)   (addr)

#ifdef __cplusplus
extern "C" {
#endif

/* Sim register-file backend (implemented in rom/rom.c). */
uint32_t esp_shim_reg_read(uint32_t addr);
void     esp_shim_reg_write(uint32_t addr, uint32_t val);

#ifdef __cplusplus
}
#endif

#ifndef __ASSEMBLER__

/* Register access — routed through the sim register file. */
#define REG_WRITE(_r, _v)  do {                                                 \
            esp_shim_reg_write((uint32_t)(_r), (uint32_t)(_v));                 \
        } while(0)

#define REG_READ(_r) ({                                                         \
            esp_shim_reg_read((uint32_t)(_r));                                  \
        })

#define REG_GET_BIT(_r, _b)  ({                                                 \
            (esp_shim_reg_read((uint32_t)(_r)) & (uint32_t)(_b));               \
        })

#define REG_SET_BIT(_r, _b)  do {                                               \
            esp_shim_reg_write((uint32_t)(_r),                                  \
                esp_shim_reg_read((uint32_t)(_r)) | (uint32_t)(_b));            \
        } while(0)

#define REG_CLR_BIT(_r, _b)  do {                                               \
            esp_shim_reg_write((uint32_t)(_r),                                  \
                esp_shim_reg_read((uint32_t)(_r)) & ~(uint32_t)(_b));           \
        } while(0)

#define REG_SET_BITS(_r, _b, _m) do {                                           \
            esp_shim_reg_write((uint32_t)(_r),                                  \
                (esp_shim_reg_read((uint32_t)(_r)) & ~(uint32_t)(_m))           \
                | ((uint32_t)(_b) & (uint32_t)(_m)));                           \
        } while(0)

/* get field from register, uses field _S & _V to determine mask */
#define REG_GET_FIELD(_r, _f) ({                                                \
            ((REG_READ(_r) >> (_f##_S)) & (_f##_V));                            \
        })

/* set field of a register from variable, uses field _S & _V to determine mask */
#define REG_SET_FIELD(_r, _f, _v) do {                                          \
            REG_WRITE((_r),((REG_READ(_r) & ~((_f##_V) << (_f##_S)))            \
                          |(((_v) & (_f##_V))<<(_f##_S))));                     \
        } while(0)

#define VALUE_GET_FIELD(_r, _f)   (((_r) >> (_f##_S)) & (_f))
#define VALUE_GET_FIELD2(_r, _f)  (((_r) & (_f))>> (_f##_S))
#define VALUE_SET_FIELD(_r, _f, _v)  ((_r)=(((_r) & ~((_f) << (_f##_S)))|((_v)<<(_f##_S))))
#define VALUE_SET_FIELD2(_r, _f, _v) ((_r)=(((_r) & ~(_f))|((_v)<<(_f##_S))))
#define FIELD_TO_VALUE(_f, _v)   (((_v)&(_f))<<_f##_S)
#define FIELD_TO_VALUE2(_f, _v)  (((_v)<<_f##_S) & (_f))

#define READ_PERI_REG(addr) ({                                                  \
            esp_shim_reg_read(ETS_UNCACHED_ADDR((uint32_t)(addr)));             \
        })

#define WRITE_PERI_REG(addr, val) do {                                          \
            esp_shim_reg_write(ETS_UNCACHED_ADDR((uint32_t)(addr)),             \
                               (uint32_t)(val));                                \
        } while(0)

#define CLEAR_PERI_REG_MASK(reg, mask)  do {                                    \
            WRITE_PERI_REG((reg), (READ_PERI_REG(reg)&(~(mask))));              \
        } while(0)

#define SET_PERI_REG_MASK(reg, mask) do {                                       \
            WRITE_PERI_REG((reg), (READ_PERI_REG(reg)|(mask)));                 \
        } while(0)

#define GET_PERI_REG_MASK(reg, mask) ({                                         \
            (READ_PERI_REG(reg) & (mask));                                      \
        })

#define GET_PERI_REG_BITS(reg, hipos, lowpos) ({                                \
            ((READ_PERI_REG(reg)>>(lowpos))&((1<<((hipos)-(lowpos)+1))-1));     \
        })

#define SET_PERI_REG_BITS(reg,bit_map,value,shift) do {                         \
            WRITE_PERI_REG((reg),(READ_PERI_REG(reg)&(~((bit_map)<<(shift))))   \
                | (((value) & (bit_map))<<(shift)) );                           \
        } while(0)

#define GET_PERI_REG_BITS2(reg, mask, shift) ({                                 \
            ((READ_PERI_REG(reg)>>(shift))&(mask));                             \
        })

#endif /* !__ASSEMBLER__ */
