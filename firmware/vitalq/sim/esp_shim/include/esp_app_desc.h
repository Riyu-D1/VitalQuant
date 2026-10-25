/* Host-sim esp_app_desc.h — application descriptor.  On target this
 * struct is generated into the binary by the build; the sim returns a
 * static equivalent populated with the sim firmware identity. */
#ifndef ESP_APP_DESC_H
#define ESP_APP_DESC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_APP_DESC_MAGIC_WORD 0xABCD5432

typedef struct {
    uint32_t magic_word;            /* ESP_APP_DESC_MAGIC_WORD                */
    uint32_t secure_version;        /* secure version used by secure boot/OTA */
    uint32_t reserv1[2];
    char     version[32];           /* project version (from project config)  */
    char     project_name[32];      /* project name                           */
    char     time[16];              /* compile time                           */
    char     date[16];              /* compile date                           */
    char     idf_ver[32];           /* IDF version the app was built with     */
    uint8_t  app_elf_sha256[32];    /* sha256 of elf file                     */
    uint8_t  reserv2[32];
} esp_app_desc_t;

/** The sim's application descriptor: project "vitalq", version
 *  "2.0.0-sim", IDF "v5.4.2". */
const esp_app_desc_t *esp_app_get_description(void);

/** Copy the app ELF SHA256 (hex chars) into dst; returns length. */
int esp_app_get_elf_sha256(char *dst, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ESP_APP_DESC_H */
