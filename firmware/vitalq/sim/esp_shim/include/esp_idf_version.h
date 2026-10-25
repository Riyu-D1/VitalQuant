/* Host-sim esp_idf_version.h — the sim reports the same IDF version the
 * target build uses (v5.4.2, per AGENTS.md / sdkconfig). */
#ifndef ESP_IDF_VERSION_H
#define ESP_IDF_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_IDF_VERSION_MAJOR 5
#define ESP_IDF_VERSION_MINOR 4
#define ESP_IDF_VERSION_PATCH 2

#define ESP_IDF_VERSION_VAL(major, minor, patch) \
    (((major) << 16) | ((minor) << 8) | (patch))

#define ESP_IDF_VERSION \
    ESP_IDF_VERSION_VAL(ESP_IDF_VERSION_MAJOR, \
                        ESP_IDF_VERSION_MINOR, \
                        ESP_IDF_VERSION_PATCH)

/** Return IDF version string, e.g. "v5.4.2". */
const char *esp_get_idf_version(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_IDF_VERSION_H */
