/* esp_wifi.h — host-sim shim of the ESP-IDF wifi API.
 *
 * STA semantics are modelled in sim/esp_shim/net/wifi_sim.c against the
 * vboard fault knobs (g_vb_faults.wifi_link_down). Delays run on the
 * vboard clock so scenario time-acceleration applies.
 */
#ifndef _ESP_WIFI_H
#define _ESP_WIFI_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wi-Fi specific error codes — defined here when the shim esp_err.h
 * doesn't provide them (target esp_err.h does). */
#ifndef ESP_ERR_WIFI_BASE
#define ESP_ERR_WIFI_BASE          0x3000
#endif

#define ESP_ERR_WIFI_NOT_INIT          (ESP_ERR_WIFI_BASE + 1)   /*!< WiFi driver was not installed by esp_wifi_init */
#define ESP_ERR_WIFI_NOT_STARTED       (ESP_ERR_WIFI_BASE + 2)   /*!< WiFi driver was not started by esp_wifi_start */
#define ESP_ERR_WIFI_NOT_STOPPED       (ESP_ERR_WIFI_BASE + 3)   /*!< WiFi driver was not stopped by esp_wifi_stop */
#define ESP_ERR_WIFI_IF                (ESP_ERR_WIFI_BASE + 4)   /*!< WiFi interface error */
#define ESP_ERR_WIFI_MODE              (ESP_ERR_WIFI_BASE + 5)   /*!< WiFi mode error */
#define ESP_ERR_WIFI_STATE             (ESP_ERR_WIFI_BASE + 6)   /*!< WiFi internal state error */
#define ESP_ERR_WIFI_CONN              (ESP_ERR_WIFI_BASE + 7)   /*!< WiFi internal control block of station or soft-AP error */
#define ESP_ERR_WIFI_NVS               (ESP_ERR_WIFI_BASE + 8)   /*!< WiFi internal NVS module error */
#define ESP_ERR_WIFI_MAC               (ESP_ERR_WIFI_BASE + 9)   /*!< MAC address is invalid */
#define ESP_ERR_WIFI_SSID              (ESP_ERR_WIFI_BASE + 10)  /*!< SSID is invalid */
#define ESP_ERR_WIFI_PASSWORD          (ESP_ERR_WIFI_BASE + 11)  /*!< Password is invalid */
#define ESP_ERR_WIFI_TIMEOUT           (ESP_ERR_WIFI_BASE + 12)  /*!< Timeout error */
#define ESP_ERR_WIFI_WAKE_FAIL         (ESP_ERR_WIFI_BASE + 13)  /*!< WiFi is in sleep state(RF closed) and wakeup fail */
#define ESP_ERR_WIFI_WOULD_BLOCK       (ESP_ERR_WIFI_BASE + 14)  /*!< The caller would block */
#define ESP_ERR_WIFI_NOT_CONNECT       (ESP_ERR_WIFI_BASE + 15)  /*!< Station still in disconnect status */
#define ESP_ERR_WIFI_POST              (ESP_ERR_WIFI_BASE + 18)  /*!< Failed to post the event to WiFi task */
#define ESP_ERR_WIFI_INIT_STATE        (ESP_ERR_WIFI_BASE + 19)  /*!< Invalid WiFi state when init/deinit is called */
#define ESP_ERR_WIFI_STOP_STATE        (ESP_ERR_WIFI_BASE + 20)  /*!< Returned when WiFi is stopping */
#define ESP_ERR_WIFI_NOT_ASSOC         (ESP_ERR_WIFI_BASE + 21)  /*!< The WiFi connection is not associated */
#define ESP_ERR_WIFI_TX_DISALLOW       (ESP_ERR_WIFI_BASE + 22)  /*!< The WiFi TX is disallowed */
#define ESP_ERR_WIFI_TWT_FULL          (ESP_ERR_WIFI_BASE + 23)  /*!< no available flow id */
#define ESP_ERR_WIFI_TWT_SETUP_TIMEOUT (ESP_ERR_WIFI_BASE + 24)  /*!< Timeout of receiving twt setup response */
#define ESP_ERR_WIFI_TWT_SETUP_TXFAIL  (ESP_ERR_WIFI_BASE + 25)  /*!< TWT setup frame tx failed */
#define ESP_ERR_WIFI_TWT_SETUP_REJECT  (ESP_ERR_WIFI_BASE + 26)  /*!< The twt setup request was rejected by the AP */
#define ESP_ERR_WIFI_DISCARD           (ESP_ERR_WIFI_BASE + 27)  /*!< Discard frame */
#define ESP_ERR_WIFI_ROC_IN_PROGRESS   (ESP_ERR_WIFI_BASE + 28)  /*!< ROC op is in progress */

#define WIFI_INIT_CONFIG_MAGIC   0x1F2F3F4F

/**
 * @brief WiFi stack configuration parameters passed to esp_wifi_init call.
 *        Field-for-field with IDF 5.4; the sim only reads .magic.
 */
typedef struct {
    wifi_osi_funcs_t*      osi_funcs;              /**< WiFi OS functions */
    wpa_crypto_funcs_t     wpa_crypto_funcs;       /**< WiFi station crypto functions when connect */
    int                    static_rx_buf_num;      /**< WiFi static RX buffer number */
    int                    dynamic_rx_buf_num;     /**< WiFi dynamic RX buffer number */
    int                    tx_buf_type;            /**< WiFi TX buffer type */
    int                    static_tx_buf_num;      /**< WiFi static TX buffer number */
    int                    dynamic_tx_buf_num;     /**< WiFi dynamic TX buffer number */
    int                    rx_mgmt_buf_type;       /**< WiFi RX MGMT buffer type */
    int                    rx_mgmt_buf_num;        /**< WiFi RX MGMT buffer number */
    int                    cache_tx_buf_num;       /**< WiFi TX cache buffer number */
    int                    csi_enable;             /**< WiFi channel state information enable flag */
    int                    ampdu_rx_enable;        /**< WiFi AMPDU RX feature enable flag */
    int                    ampdu_tx_enable;        /**< WiFi AMPDU TX feature enable flag */
    int                    amsdu_tx_enable;        /**< WiFi AMSDU TX feature enable flag */
    int                    nvs_enable;             /**< WiFi NVS flash enable flag */
    int                    nano_enable;            /**< Nano option for printf/scan family enable flag */
    int                    rx_ba_win;              /**< WiFi Block Ack RX window size */
    int                    wifi_task_core_id;      /**< WiFi Task Core ID */
    int                    beacon_max_len;         /**< WiFi softAP maximum length of the beacon */
    int                    mgmt_sbuf_num;          /**< WiFi management short buffer number */
    uint64_t               feature_caps;           /**< Enables additional WiFi features and capabilities */
    bool                   sta_disconnected_pm;    /**< WiFi Power Management for station at disconnected status */
    int                    espnow_max_encrypt_num; /**< Maximum encrypt number of peers supported by espnow */
    int                    tx_hetb_queue_num;      /**< WiFi TX HE TB QUEUE number for STA HE TB PPDU transmission */
    bool                   dump_hesigb_enable;     /**< enable dump sigb field */
    int                    magic;                  /**< WiFi init magic number, it should be the last field */
} wifi_init_config_t;

/**
 * @brief WiFi init configuration default values (sim-plausible constants;
 *        only .magic is checked by the shim).
 */
#define WIFI_INIT_CONFIG_DEFAULT() { \
    .osi_funcs = NULL, \
    .wpa_crypto_funcs = {0}, \
    .static_rx_buf_num = 10, \
    .dynamic_rx_buf_num = 32, \
    .tx_buf_type = WIFI_TX_BUFFER_TYPE_DYNAMIC, \
    .static_tx_buf_num = 16, \
    .dynamic_tx_buf_num = 32, \
    .rx_mgmt_buf_type = WIFI_RX_MGMT_BUF_TYPE_DYNAMIC, \
    .rx_mgmt_buf_num = 5, \
    .cache_tx_buf_num = 32, \
    .csi_enable = 0, \
    .ampdu_rx_enable = 1, \
    .ampdu_tx_enable = 1, \
    .amsdu_tx_enable = 0, \
    .nvs_enable = 1, \
    .nano_enable = 0, \
    .rx_ba_win = 6, \
    .wifi_task_core_id = 0, \
    .beacon_max_len = 752, \
    .mgmt_sbuf_num = 32, \
    .feature_caps = 0, \
    .sta_disconnected_pm = false, \
    .espnow_max_encrypt_num = 7, \
    .tx_hetb_queue_num = 3, \
    .dump_hesigb_enable = false, \
    .magic = WIFI_INIT_CONFIG_MAGIC \
}

/**
 * @brief  Initialize WiFi — validates config->magic, marks driver inited.
 */
esp_err_t esp_wifi_init(const wifi_init_config_t *config);

/**
 * @brief  Deinit WiFi — frees the driver state.
 */
esp_err_t esp_wifi_deinit(void);

/**
 * @brief  Set the WiFi operating mode. Sim: recorded (AP modes accepted
 *         but never actually bring a network up).
 */
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);

/**
 * @brief  Get current WiFi mode.
 */
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);

/**
 * @brief  Start WiFi — posts WIFI_EVENT_WIFI_READY once, then
 *         WIFI_EVENT_STA_START (STA mode).
 */
esp_err_t esp_wifi_start(void);

/**
 * @brief  Stop WiFi — posts WIFI_EVENT_STA_STOP, drops any association.
 */
esp_err_t esp_wifi_stop(void);

/**
 * @brief  Restore WiFi default state (drops association + config).
 */
esp_err_t esp_wifi_restore(void);

/**
 * @brief  Connect the STA to the configured AP. Sim: a worker waits
 *         ~30 ms (vboard clock) then posts STA_CONNECTED + IP GOT_IP,
 *         or STA_DISCONNECTED when g_vb_faults.wifi_link_down is set.
 */
esp_err_t esp_wifi_connect(void);

/**
 * @brief  Disconnect the STA — posts WIFI_EVENT_STA_DISCONNECTED.
 */
esp_err_t esp_wifi_disconnect(void);

/**
 * @brief  Clear fast-connect info — recorded only.
 */
esp_err_t esp_wifi_clear_fast_connect(void);

/**
 * @brief  Deauth a soft-AP station — ESP_OK (no AP model in sim).
 */
esp_err_t esp_wifi_deauth_sta(uint16_t aid);

/**
 * @brief  Scan for APs. Sim: posts WIFI_EVENT_SCAN_DONE with one
 *         fabricated result after ~10 ms (vboard clock).
 */
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);

esp_err_t esp_wifi_set_scan_parameters(const wifi_scan_default_params_t *config);
esp_err_t esp_wifi_get_scan_parameters(wifi_scan_default_params_t *config);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records);
esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *ap_record);
esp_err_t esp_wifi_clear_ap_list(void);

/**
 * @brief  Info about the AP the STA is associated with.
 */
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info);

/**
 * @brief  Set power save type — recorded.
 */
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *type);

/**
 * @brief  Set protocol bitmap — recorded.
 */
esp_err_t esp_wifi_set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap);
esp_err_t esp_wifi_get_protocol(wifi_interface_t ifx, uint8_t *protocol_bitmap);

/**
 * @brief  Set bandwidth — recorded.
 */
esp_err_t esp_wifi_set_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t bw);
esp_err_t esp_wifi_get_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t *bw);

/**
 * @brief  Set channel — recorded.
 */
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second);
esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second);

/**
 * @brief  Set country — recorded.
 */
esp_err_t esp_wifi_set_country(const wifi_country_t *country);
esp_err_t esp_wifi_get_country(wifi_country_t *country);

/**
 * @brief  Set MAC address of an interface — recorded.
 */
esp_err_t esp_wifi_set_mac(wifi_interface_t ifx, const uint8_t mac[6]);

/**
 * @brief  Get MAC address of an interface.
 */
esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6]);

/**
 * @brief  Set promiscuous mode — recorded.
 */
esp_err_t esp_wifi_set_promiscuous(bool en);
esp_err_t esp_wifi_get_promiscuous(bool *en);
esp_err_t esp_wifi_set_promiscuous_rx_cb(void *cb);
esp_err_t esp_wifi_set_promiscuous_filter(const void *filter);
esp_err_t esp_wifi_get_promiscuous_filter(void *filter);
esp_err_t esp_wifi_set_promiscuous_ctrl_filter(const void *filter);
esp_err_t esp_wifi_get_promiscuous_ctrl_filter(void *filter);

/**
 * @brief  Set interface configuration. Mirrors IDF: while the STA is
 *         associated this returns ESP_ERR_WIFI_CONN (wifi_mgr retries).
 */
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);
esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf);

/**
 * @brief  Get the info of the AP to which the STA tried to connect last.
 */
esp_err_t esp_wifi_sta_get_ap_config(wifi_config_t *ap_config);

/**
 * @brief  The AP list the STA has attempted (sim: the one AP model).
 */
esp_err_t esp_wifi_sta_get_ap_list(wifi_config_t *ap_list);

/**
 * @brief  Get associated station list (AP mode — empty in sim).
 */
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *sta);
esp_err_t esp_wifi_ap_get_sta_aid(const uint8_t mac[6], uint16_t *aid);

/**
 * @brief  Set storage — recorded (WIFI_STORAGE_RAM keeps cfg in RAM).
 */
esp_err_t esp_wifi_set_storage(wifi_storage_t storage);

/**
 * @brief  Auto-reconnect flag — recorded.
 */
esp_err_t esp_wifi_set_auto_connect(bool en);
esp_err_t esp_wifi_get_auto_connect(bool *en);

/**
 * @brief  Set vendor IE — recorded.
 */
esp_err_t esp_wifi_set_vendor_ie(bool enable, uint32_t vnd_ie_type,
                                 uint32_t vendor_ie_index, const void *vnd_ie);
typedef void (*esp_vendor_ie_cb_t)(void *ctx, wifi_vendor_ie_type_t type,
                                   const uint8_t sa[6], const void *vnd_ie,
                                   int rssi);
esp_err_t esp_wifi_set_vendor_ie_cb(esp_vendor_ie_cb_t cb, void *ctx);

/**
 * @brief  Set maximum WiFi TX power — recorded only. The hw brief forbids
 *         RF tuning; values above the 20 dBm driver default are logged.
 *         Units: 0.25 dBm (80 = 20 dBm).
 */
esp_err_t esp_wifi_set_max_tx_power(int8_t power);
esp_err_t esp_wifi_get_max_tx_power(int8_t *power);

/**
 * @brief  CSI config — recorded.
 */
esp_err_t esp_wifi_set_csi(const void *config);
esp_err_t esp_wifi_set_csi_rx_cb(void *cb, void *ctx);
esp_err_t esp_wifi_set_csi_config(const void *config);

/**
 * @brief  antenna config / GPIO config — recorded.
 */
esp_err_t esp_wifi_set_ant(const void *config);
esp_err_t esp_wifi_get_ant(void *config);
esp_err_t esp_wifi_set_ant_gpio(const void *config);
esp_err_t esp_wifi_get_ant_gpio(void *config);

/**
 * @brief  TSF time — sim returns vboard virtual µs.
 */
int64_t esp_wifi_get_tsf_time(wifi_interface_t interface);

/**
 * @brief  Inactive time — 0 in sim.
 */
esp_err_t esp_wifi_set_inactive_time(wifi_interface_t ifx, uint16_t sec);
esp_err_t esp_wifi_get_inactive_time(wifi_interface_t ifx, uint16_t *sec);

/**
 * @brief  Dump internal stats — prints to stdout in sim.
 */
esp_err_t esp_wifi_statis_dump(uint32_t modules);

/**
 * @brief  RSSI threshold config — recorded.
 */
esp_err_t esp_wifi_set_rssi_threshold(int32_t rssi);

/**
 * @brief  Wake reason — 0 in sim.
 */
esp_err_t esp_wifi_get_wake_reason(int *reason);

/**
 * @brief  802.11 tx — not modelled; returns ESP_ERR_NOT_SUPPORTED.
 */
esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer,
                            int len, bool en_sys_seq);

/**
 * @brief  Negotiated PHY mode of current association.
 */
esp_err_t esp_wifi_sta_get_negotiated_phymode(wifi_phy_mode_t *phymode);

/**
 * @brief  Auxiliary receive info — empty in sim.
 */
esp_err_t esp_wifi_register_txrx_csi_callback(void *cb, void *ctx);

/**
 * @brief  Get RSSI of the associated AP — -58 dBm, or -95 dBm while
 *         g_vb_faults.wifi_link_down marks the link degraded.
 *         ESP_ERR_WIFI_NOT_ASSOC when not connected.
 */
esp_err_t esp_wifi_sta_get_rssi(int *rssi);

/**
 * @brief  Set RSSI threshold event config — recorded.
 */
esp_err_t esp_wifi_config_80211_tx_rate(wifi_interface_t ifx, uint32_t rate);

/**
 * @brief  ESP-NOW / power mgmt glue — stubs for parity.
 */
esp_err_t esp_wifi_set_espnow_rate(wifi_interface_t ifx, uint32_t rate);

#ifdef __cplusplus
}
#endif

#endif /* _ESP_WIFI_H */
