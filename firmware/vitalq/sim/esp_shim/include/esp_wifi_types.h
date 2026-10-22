/* esp_wifi_types.h — host-sim shim of ESP-IDF wifi shared types.
 *
 * Field layouts mirror IDF 5.4 where the firmware touches them
 * (wifi_sta_config_t.ssid/password/threshold, the event structs).
 */
#ifndef ESP_WIFI_TYPES_H
#define ESP_WIFI_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MODE_NULL = 0,  /**< Null mode */
    WIFI_MODE_STA,       /**< WiFi STA mode */
    WIFI_MODE_AP,        /**< WiFi soft-AP mode */
    WIFI_MODE_APSTA,     /**< WiFi station + soft-AP mode */
    WIFI_MODE_NAN,       /**< WiFi NAN mode */
    WIFI_MODE_MAX
} wifi_mode_t;

typedef enum {
    WIFI_IF_STA = 0,     /**< Station interface */
    WIFI_IF_AP,          /**< Soft-AP interface */
    WIFI_IF_NAN,         /**< NAN interface */
    WIFI_IF_MAX          /**< Maximum number of interfaces */
} wifi_interface_t;

/* note: ESP_IF_WIFI_STA/ESP_IF_* live in esp_interface.h on target;
 * WIFI_IF_* are self-contained here to avoid a cross-shim dependency. */

typedef enum {
    WIFI_AUTH_OPEN = 0,         /**< Authenticate mode : open */
    WIFI_AUTH_WEP,              /**< Authenticate mode : WEP */
    WIFI_AUTH_WPA_PSK,          /**< Authenticate mode : WPA_PSK */
    WIFI_AUTH_WPA2_PSK,         /**< Authenticate mode : WPA2_PSK */
    WIFI_AUTH_WPA_WPA2_PSK,     /**< Authenticate mode : WPA_WPA2_PSK */
    WIFI_AUTH_ENTERPRISE,       /**< Authenticate mode : WiFi EAP security */
    WIFI_AUTH_WPA2_ENTERPRISE = WIFI_AUTH_ENTERPRISE,
    WIFI_AUTH_WPA3_PSK,         /**< Authenticate mode : WPA3_PSK */
    WIFI_AUTH_WPA2_WPA3_PSK,    /**< Authenticate mode : WPA2_WPA3_PSK */
    WIFI_AUTH_WAPI_PSK,         /**< Authenticate mode : WAPI_PSK */
    WIFI_AUTH_OWE,              /**< Authenticate mode : OWE */
    WIFI_AUTH_WPA3_ENT_192,     /**< Authenticate mode : WPA3_ENT_SUITE_B_192_BIT */
    WIFI_AUTH_WPA3_EXT_PSK,
    WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE,
    WIFI_AUTH_DPP,              /**< Authenticate mode : DPP */
    WIFI_AUTH_WPA3_ENTERPRISE,  /**< Authenticate mode : WPA3-Enterprise */
    WIFI_AUTH_WPA2_WPA3_ENTERPRISE,
    WIFI_AUTH_MAX
} wifi_auth_mode_t;

typedef enum {
    WIFI_REASON_UNSPECIFIED                        = 1,
    WIFI_REASON_AUTH_EXPIRE                        = 2,
    WIFI_REASON_AUTH_LEAVE                         = 3,
    WIFI_REASON_ASSOC_EXPIRE                       = 4,
    WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY         = 4,
    WIFI_REASON_ASSOC_TOOMANY                      = 5,
    WIFI_REASON_NOT_AUTHED                         = 6,
    WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA      = 6,
    WIFI_REASON_NOT_ASSOCED                        = 7,
    WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA     = 7,
    WIFI_REASON_ASSOC_LEAVE                        = 8,
    WIFI_REASON_ASSOC_NOT_AUTHED                   = 9,
    WIFI_REASON_DISASSOC_PWRCAP_BAD                = 10,
    WIFI_REASON_DISASSOC_SUPCHAN_BAD               = 11,
    WIFI_REASON_BSS_TRANSITION_DISASSOC            = 12,
    WIFI_REASON_IE_INVALID                         = 13,
    WIFI_REASON_MIC_FAILURE                        = 14,
    WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT             = 15,
    WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT           = 16,
    WIFI_REASON_IE_IN_4WAY_DIFFERS                 = 17,
    WIFI_REASON_GROUP_CIPHER_INVALID               = 18,
    WIFI_REASON_PAIRWISE_CIPHER_INVALID            = 19,
    WIFI_REASON_AKMP_INVALID                       = 20,
    WIFI_REASON_UNSUPP_RSN_IE_VERSION              = 21,
    WIFI_REASON_INVALID_RSN_IE_CAP                 = 22,
    WIFI_REASON_802_1X_AUTH_FAILED                 = 23,
    WIFI_REASON_CIPHER_SUITE_REJECTED              = 24,
    WIFI_REASON_TDLS_PEER_UNREACHABLE              = 25,
    WIFI_REASON_TDLS_UNSPECIFIED                   = 26,
    WIFI_REASON_SSP_REQUESTED_DISASSOC             = 27,
    WIFI_REASON_NO_SSP_ROAMING_AGREEMENT           = 28,
    WIFI_REASON_BAD_CIPHER_OR_AKM                  = 29,
    WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION       = 30,
    WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS        = 31,
    WIFI_REASON_UNSPECIFIED_QOS                    = 32,
    WIFI_REASON_NOT_ENOUGH_BANDWIDTH               = 33,
    WIFI_REASON_MISSING_ACKS                       = 34,
    WIFI_REASON_EXCEEDED_TXOP                      = 35,
    WIFI_REASON_STA_LEAVING                        = 36,
    WIFI_REASON_END_BA                             = 37,
    WIFI_REASON_UNKNOWN_BA                         = 38,
    WIFI_REASON_TIMEOUT                            = 39,
    WIFI_REASON_PEER_INITIATED                     = 46,
    WIFI_REASON_AP_INITIATED                       = 47,
    WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT      = 48,
    WIFI_REASON_INVALID_PMKID                      = 49,
    WIFI_REASON_INVALID_MDE                        = 50,
    WIFI_REASON_INVALID_FTE                        = 51,
    WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED = 52,
    WIFI_REASON_ALTERNATIVE_CHANNEL_OCCUPIED       = 53,

    WIFI_REASON_BEACON_TIMEOUT                     = 200,
    WIFI_REASON_NO_AP_FOUND                        = 201,
    WIFI_REASON_AUTH_FAIL                          = 202,
    WIFI_REASON_ASSOC_FAIL                         = 203,
    WIFI_REASON_HANDSHAKE_TIMEOUT                  = 204,
    WIFI_REASON_CONNECTION_FAIL                    = 205,
    WIFI_REASON_AP_TSF_RESET                       = 206,
    WIFI_REASON_ROAMING                            = 207,
    WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG       = 208,
    WIFI_REASON_SA_QUERY_TIMEOUT                   = 209,
    WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY  = 210,
    WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD  = 211,
    WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD      = 212,
} wifi_err_reason_t;

typedef enum {
    WIFI_SECOND_CHAN_NONE = 0,  /**< the channel width is HT20 */
    WIFI_SECOND_CHAN_ABOVE,     /**< the channel width is HT40 and the secondary channel is above the primary channel */
    WIFI_SECOND_CHAN_BELOW,     /**< the channel width is HT40 and the secondary channel is below the primary channel */
} wifi_second_chan_t;

typedef enum {
    WIFI_SCAN_TYPE_ACTIVE = 0,  /**< active scan */
    WIFI_SCAN_TYPE_PASSIVE,     /**< passive scan */
} wifi_scan_type_t;

typedef enum {
    WIFI_SCAN_METHOD_FAST = 0,       /**< fast scan */
    WIFI_SCAN_METHOD_ALL_CHANNEL,    /**< all channel scan */
} wifi_scan_method_t;

typedef enum {
    WIFI_CONNECT_AP_BY_SIGNAL = 0,   /**< sort matched APs by signal */
    WIFI_CONNECT_AP_BY_SECURITY,     /**< sort matched APs by security mode */
} wifi_sort_method_t;

typedef enum {
    WIFI_CIPHER_TYPE_NONE = 0,   /**< the cipher type is none */
    WIFI_CIPHER_TYPE_WEP40,      /**< the cipher type is WEP40 */
    WIFI_CIPHER_TYPE_WEP104,     /**< the cipher type is WEP104 */
    WIFI_CIPHER_TYPE_TKIP,       /**< the cipher type is TKIP */
    WIFI_CIPHER_TYPE_CCMP,       /**< the cipher type is CCMP */
    WIFI_CIPHER_TYPE_TKIP_CCMP,  /**< the cipher type is TKIP and CCMP */
    WIFI_CIPHER_TYPE_AES_CMAC128,/**< the cipher type is AES-CMAC-128 */
    WIFI_CIPHER_TYPE_SMS4,       /**< the cipher type is SMS4 */
    WIFI_CIPHER_TYPE_GCMP,       /**< the cipher type is GCMP */
    WIFI_CIPHER_TYPE_GCMP256,    /**< the cipher type is GCMP-256 */
    WIFI_CIPHER_TYPE_AES_GMAC128,/**< the cipher type is AES-GMAC-128 */
    WIFI_CIPHER_TYPE_AES_GMAC256,/**< the cipher type is AES-GMAC-256 */
    WIFI_CIPHER_TYPE_UNKNOWN,    /**< the cipher type is unknown */
} wifi_cipher_type_t;

typedef enum {
    WIFI_APPIE_PROBEREQ = 0,
    WIFI_APPIE_ASSOC_REQ,
    WIFI_APPIE_ASSOC_RESP,
    WIFI_APPIE_MAX
} wifi_appie_t;

typedef enum {
    WIFI_VND_IE_TYPE_BEACON,
    WIFI_VND_IE_TYPE_PROBE_REQ,
    WIFI_VND_IE_TYPE_PROBE_RESP,
    WIFI_VND_IE_TYPE_ASSOC_REQ,
    WIFI_VND_IE_TYPE_ASSOC_RESP,
} wifi_vendor_ie_type_t;

typedef enum {
    WIFI_BAND_2GHZ = 0,     /**< 2.4GHz wifi band */
    WIFI_BAND_5GHZ,         /**< 5GHz wifi band */
    WIFI_BAND_MAX
} wifi_band_t;

typedef enum {
    WIFI_PHY_MODE_LR,     /**< PHY mode for Low Rate */
    WIFI_PHY_MODE_11B,    /**< PHY mode for 11b */
    WIFI_PHY_MODE_11G,    /**< PHY mode for 11g */
    WIFI_PHY_MODE_HT20,   /**< PHY mode for Bandwidth HT20 */
    WIFI_PHY_MODE_HT40,   /**< PHY mode for Bandwidth HT40 */
    WIFI_PHY_MODE_HE20,   /**< PHY mode for Bandwidth HE20 */
    WIFI_PHY_MODE_VHT20,  /**< PHY mode for Bandwidth VHT20 */
    WIFI_PHY_MODE_MAX
} wifi_phy_mode_t;

typedef enum {
    WIFI_BW_HT20 = 1,       /**< Bandwidth is HT20 */
    WIFI_BW20 = WIFI_BW_HT20,
    WIFI_BW_HT40 = 2,       /**< Bandwidth is HT40 */
    WIFI_BW40 = WIFI_BW_HT40,
    WIFI_BW80 = 3,          /**< Bandwidth is 80 MHz */
    WIFI_BW160 = 4,         /**< Bandwidth is 160 MHz */
    WIFI_BW80_BW80 = 5,     /**< Bandwidth is 80 + 80 MHz */
} wifi_bandwidth_t;

typedef enum {
    WIFI_PS_NONE,        /**< No power save */
    WIFI_PS_MIN_MODEM,   /**< Minimum modem power saving */
    WIFI_PS_MAX_MODEM,   /**< Maximum modem power saving */
} wifi_ps_type_t;

typedef enum {
    WIFI_STORAGE_FLASH,  /**< all configuration will store in both memory and flash */
    WIFI_STORAGE_RAM,    /**< all configuration will only store in the memory */
} wifi_storage_t;

/** Wi-Fi RX buffer / TX buffer type enums used by wifi_init_config_t */
typedef enum {
    WIFI_TX_BUFFER_TYPE_STATIC = 0,
    WIFI_TX_BUFFER_TYPE_DYNAMIC,
} wifi_tx_buffer_type_t;

typedef enum {
    WIFI_RX_MGMT_BUF_TYPE_STATIC = 0,
    WIFI_RX_MGMT_BUF_TYPE_DYNAMIC,
} wifi_rx_mgmt_buf_type_t;

#define WIFI_PROTOCOL_11B         0x1
#define WIFI_PROTOCOL_11G         0x2
#define WIFI_PROTOCOL_11N         0x4
#define WIFI_PROTOCOL_LR          0x8
#define WIFI_PROTOCOL_11A         0x10
#define WIFI_PROTOCOL_11AC        0x20
#define WIFI_PROTOCOL_11AX        0x40
#define WIFI_PROTOCOL_11BE        0x80

/** @brief PMF configuration */
typedef struct {
    bool capable;            /**< Device can support PMF */
    bool required;           /**< Device requires PMF */
} wifi_pmf_config_t;

/** @brief SAE-PWE method enumeration */
typedef enum {
    WPA3_SAE_PWE_UNSPECIFIED,
    WPA3_SAE_PWE_HUNT_AND_PECK,
    WPA3_SAE_PWE_HASH_TO_ELEMENT,
    WPA3_SAE_PWE_BOTH,
} wifi_sae_pwe_method_t;

/** @brief SAE-PK mode enumeration */
typedef enum {
    WPA3_SAE_PK_MODE_AUTOMATIC = 0,
    WPA3_SAE_PK_MODE_ONLY = 1,
    WPA3_SAE_PK_MODE_DISABLED = 2,
} wifi_sae_pk_mode_t;

/** @brief Scan threshold: only APs more secure than authmode and stronger
 *         than rssi are considered. */
typedef struct {
    int8_t              rssi;               /**< min RSSI to accept in fast scan (default -127 if >= 0) */
    wifi_auth_mode_t    authmode;           /**< weakest auth mode to accept in fast scan */
    uint8_t             rssi_5g_adjustment; /**< 5G RSSI adjustment for 5G preference */
} wifi_scan_threshold_t;

#define SAE_H2E_IDENTIFIER_LEN 32    /**< Length of the password identifier for H2E */

/** @brief Soft-AP configuration settings */
typedef struct {
    uint8_t ssid[32];             /**< SSID of soft-AP (nul-terminated if ssid_len==0) */
    uint8_t password[64];         /**< Password of soft-AP */
    uint8_t ssid_len;             /**< Optional length of SSID field */
    uint8_t channel;              /**< Channel of soft-AP */
    wifi_auth_mode_t authmode;    /**< Auth mode of soft-AP */
    uint8_t ssid_hidden;          /**< Broadcast SSID or not, default 0 */
    uint8_t max_connection;       /**< Max number of stations allowed to connect in */
    uint16_t beacon_interval;     /**< Beacon interval, units TU. Default 100 */
    uint8_t csa_count;            /**< Channel Switch Announcement Count */
    uint8_t dtim_period;          /**< DTIM period. Range 1-10, default 1 */
    wifi_cipher_type_t pairwise_cipher;  /**< Pairwise cipher of SoftAP */
    bool ftm_responder;           /**< Enable FTM Responder mode */
    wifi_pmf_config_t pmf_cfg;    /**< PMF configuration */
    wifi_sae_pwe_method_t sae_pwe_h2e;   /**< SAE PWE derivation */
    uint8_t transition_disable;   /**< Transition disable feature */
} wifi_ap_config_t;

/** @brief STA configuration settings */
typedef struct {
    uint8_t ssid[32];             /**< SSID of target AP */
    uint8_t password[64];         /**< Password of target AP */
    wifi_scan_method_t scan_method;      /**< all-channel or fast scan */
    bool bssid_set;               /**< whether set MAC address of target AP */
    uint8_t bssid[6];             /**< MAC address of target AP */
    uint8_t channel;              /**< channel hint for target AP */
    uint16_t listen_interval;     /**< listen interval for WIFI_PS_MAX_MODEM */
    wifi_sort_method_t sort_method;      /**< sort by rssi or security mode */
    wifi_scan_threshold_t threshold;     /**< scan/auth threshold */
    wifi_pmf_config_t pmf_cfg;    /**< PMF configuration */
    uint32_t rm_enabled: 1;       /**< Radio Measurements enabled */
    uint32_t btm_enabled: 1;      /**< BSS Transition Management enabled */
    uint32_t mbo_enabled: 1;      /**< MBO enabled */
    uint32_t ft_enabled: 1;       /**< FT enabled */
    uint32_t owe_enabled: 1;      /**< OWE enabled */
    uint32_t transition_disable: 1;
    uint32_t reserved: 26;
    wifi_sae_pwe_method_t sae_pwe_h2e;   /**< SAE PWE derivation method */
    wifi_sae_pk_mode_t sae_pk_mode;      /**< SAE-PK mode */
    uint8_t failure_retry_cnt;    /**< retries before moving to next AP */
    uint32_t he_dcm_set: 1;
    uint32_t he_dcm_max_constellation_tx: 2;
    uint32_t he_dcm_max_constellation_rx: 2;
    uint32_t he_mcs9_enabled: 1;
    uint32_t he_su_beamformee_disabled: 1;
    uint32_t he_trig_su_bmforming_feedback_disabled: 1;
    uint32_t he_trig_mu_bmforming_partial_feedback_disabled: 1;
    uint32_t he_trig_cqi_feedback_disabled: 1;
    uint32_t he_reserved: 22;
    uint8_t sae_h2e_identifier[SAE_H2E_IDENTIFIER_LEN];
} wifi_sta_config_t;

/** @brief NAN Discovery start configuration */
typedef struct {
    uint8_t op_channel;           /**< NAN Discovery operating channel */
    uint8_t master_pref;          /**< Device's preference value to serve as master */
    uint8_t scan_time;            /**< Scan time in seconds */
    uint16_t warm_up_sec;         /**< Warm up time before NAN Anchor Master */
} wifi_nan_config_t;

/** @brief Configuration data for AP / STA / NAN — selected by interface arg */
typedef union {
    wifi_ap_config_t  ap;   /**< configuration of AP */
    wifi_sta_config_t sta;  /**< configuration of STA */
    wifi_nan_config_t nan;  /**< configuration of NAN */
} wifi_config_t;

/** @brief Description of STA associated with AP */
typedef struct {
    uint8_t mac[6];          /**< mac address */
    int8_t  rssi;            /**< current average rssi of sta connected */
    uint32_t phy_11b: 1;
    uint32_t phy_11g: 1;
    uint32_t phy_11n: 1;
    uint32_t phy_lr: 1;
    uint32_t phy_11a: 1;
    uint32_t phy_11ac: 1;
    uint32_t phy_11ax: 1;
    uint32_t is_mesh_child: 1;
    uint32_t reserved: 24;
} wifi_sta_info_t;

#define ESP_WIFI_MAX_CONN_NUM  (10)   /**< max number of stations which can connect to ESP32 soft-AP */

/** @brief List of stations associated with the soft-AP */
typedef struct {
    wifi_sta_info_t sta[ESP_WIFI_MAX_CONN_NUM]; /**< station list */
    int             num;                        /**< number of stations */
} wifi_sta_list_t;

/** @brief Description of a scanned AP */
typedef struct {
    uint8_t bssid[6];                     /**< MAC address of AP */
    uint8_t ssid[33];                     /**< SSID of AP */
    uint8_t primary;                      /**< channel of AP */
    wifi_second_chan_t second;            /**< secondary channel of AP */
    int8_t  rssi;                         /**< signal strength of AP */
    wifi_auth_mode_t authmode;            /**< authmode of AP */
    wifi_cipher_type_t pairwise_cipher;   /**< pairwise cipher of AP */
    wifi_cipher_type_t group_cipher;      /**< group cipher of AP */
    uint32_t phy_11b: 1;
    uint32_t phy_11g: 1;
    uint32_t phy_11n: 1;
    uint32_t phy_lr: 1;
    uint32_t phy_11a: 1;
    uint32_t phy_11ac: 1;
    uint32_t phy_11ax: 1;
    uint32_t wps: 1;
    uint32_t ftm_responder: 1;
    uint32_t ftm_initiator: 1;
    uint32_t reserved: 22;
    wifi_band_t band;                     /**< 2.4GHz or 5GHz */
    uint8_t vht_ch_freq1;                 /**< VHT primary channel freq */
    uint8_t vht_ch_freq2;                 /**< VHT secondary channel freq */
} wifi_ap_record_t;

/** @brief Scan parameters */
typedef struct {
    uint8_t *ssid;               /**< SSID of AP (NULL = all) */
    uint8_t *bssid;              /**< MAC address of AP (NULL = all) */
    uint8_t channel;             /**< channel, 0 = all-channel scan */
    bool show_hidden;            /**< whether to show AP with hidden SSID */
    wifi_scan_type_t scan_type;  /**< scan type, active or passive */
    union {
        struct {
            uint32_t min;        /**< min scan time per channel (active) */
            uint32_t max;        /**< max scan time per channel (active) */
        } active;
        struct {
            uint32_t time;       /**< scan time per channel (passive) */
        } passive;
    } scan_time;
    uint8_t home_chan_dwell_time; /**< home channel dwell between scan channels */
    uint16_t channel_bitmap;      /**< bitmap of channels to scan */
} wifi_scan_config_t;

/** @brief Default scan parameters */
typedef struct {
    wifi_scan_type_t scan_type;
    union {
        struct {
            uint32_t min;
            uint32_t max;
        } active;
        struct {
            uint32_t time;
        } passive;
    } scan_time;
    uint8_t home_chan_dwell_time;
    uint16_t channel_bitmap;
} wifi_scan_default_params_t;

typedef enum {
    WIFI_COUNTRY_POLICY_AUTO,    /**< country policy is auto */
    WIFI_COUNTRY_POLICY_MANUAL,  /**< country policy is manual */
} wifi_country_policy_t;

typedef struct {
    char                  cc[3];   /**< country code string */
    uint8_t               schan;   /**< start channel */
    uint8_t               nchan;   /**< total channel number */
    int8_t                max_tx_power;  /**< maximum tx power (0.25dBm units) */
    wifi_country_policy_t policy;  /**< country policy */
    wifi_band_t           wifi_band;     /**< wifi band */
} wifi_country_t;

/** @cond */
/** @brief Wi-Fi event base declaration */
ESP_EVENT_DECLARE_BASE(WIFI_EVENT);
/** @endcond */

/** @brief Wi-Fi event IDs */
typedef enum {
    WIFI_EVENT_WIFI_READY = 0,           /**< WiFi ready */
    WIFI_EVENT_SCAN_DONE,                /**< Finished scanning AP */
    WIFI_EVENT_STA_START,                /**< Station start */
    WIFI_EVENT_STA_STOP,                 /**< Station stop */
    WIFI_EVENT_STA_CONNECTED,            /**< Station connected to AP */
    WIFI_EVENT_STA_DISCONNECTED,         /**< Station disconnected from AP */
    WIFI_EVENT_STA_AUTHMODE_CHANGE,      /**< the auth mode of AP connected by device's station changed */

    WIFI_EVENT_STA_WPS_ER_SUCCESS,       /**< Station WPS succeeds in enrollee mode */
    WIFI_EVENT_STA_WPS_ER_FAILED,        /**< Station WPS fails in enrollee mode */
    WIFI_EVENT_STA_WPS_ER_TIMEOUT,       /**< Station WPS timeout in enrollee mode */
    WIFI_EVENT_STA_WPS_ER_PIN,           /**< Station WPS pin code in enrollee mode */
    WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP,   /**< Station WPS overlap in enrollee mode */

    WIFI_EVENT_AP_START,                 /**< Soft-AP start */
    WIFI_EVENT_AP_STOP,                  /**< Soft-AP stop */
    WIFI_EVENT_AP_STACONNECTED,          /**< a station connected to Soft-AP */
    WIFI_EVENT_AP_STADISCONNECTED,       /**< a station disconnected from Soft-AP */
    WIFI_EVENT_AP_PROBEREQRECVED,        /**< Receive probe request packet in soft-AP */

    WIFI_EVENT_FTM_REPORT,               /**< Receive report of FTM procedure */

    WIFI_EVENT_STA_BSS_RSSI_LOW,         /**< AP's RSSI crossed configured threshold */
    WIFI_EVENT_ACTION_TX_STATUS,         /**< Status indication of Action Tx operation */
    WIFI_EVENT_ROC_DONE,                 /**< Remain-on-Channel operation complete */

    WIFI_EVENT_STA_BEACON_TIMEOUT,       /**< Station beacon timeout */

    WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START,

    WIFI_EVENT_AP_WPS_RG_SUCCESS,
    WIFI_EVENT_AP_WPS_RG_FAILED,
    WIFI_EVENT_AP_WPS_RG_TIMEOUT,
    WIFI_EVENT_AP_WPS_RG_PIN,
    WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP,

    WIFI_EVENT_ITWT_SETUP,
    WIFI_EVENT_ITWT_TEARDOWN,
    WIFI_EVENT_ITWT_PROBE,
    WIFI_EVENT_ITWT_SUSPEND,
    WIFI_EVENT_TWT_WAKEUP,
    WIFI_EVENT_BTWT_SETUP,
    WIFI_EVENT_BTWT_TEARDOWN,
    WIFI_EVENT_NAN_SVC_MATCH,
    WIFI_EVENT_NAN_REPLIED,
    WIFI_EVENT_NAN_RECEIVE,
    WIFI_EVENT_NDP_INDICATION,
    WIFI_EVENT_NDP_CONFIRM,
    WIFI_EVENT_NDP_TERMINATED,
    WIFI_EVENT_HOME_CHANNEL_CHANGE,
    WIFI_EVENT_STA_NEIGHBOR_REP,

    WIFI_EVENT_MAX,                      /**< Invalid WiFi event ID */
} wifi_event_t;

/** @cond */
/** @brief Argument structure for WIFI_EVENT_SCAN_DONE event */
typedef struct {
    uint32_t status;          /**< status of scanning APs: 0 — success, 1 — failure */
    uint8_t  number;          /**< number of scan results */
    uint8_t  scan_id;         /**< scan sequence number, used for block scan */
} wifi_event_sta_scan_done_t;

/** @brief Argument structure for WIFI_EVENT_STA_CONNECTED event */
typedef struct {
    uint8_t ssid[32];         /**< SSID of connected AP */
    uint8_t ssid_len;         /**< SSID length of connected AP */
    uint8_t bssid[6];         /**< BSSID of connected AP */
    uint8_t channel;          /**< channel of connected AP */
    wifi_auth_mode_t authmode;/**< authentication mode used by AP */
    uint16_t aid;             /**< authentication id assigned by the connected AP */
} wifi_event_sta_connected_t;

/** @brief Argument structure for WIFI_EVENT_STA_DISCONNECTED event */
typedef struct {
    uint8_t ssid[32];         /**< SSID of disconnected AP */
    uint8_t ssid_len;         /**< SSID length of disconnected AP */
    uint8_t bssid[6];         /**< BSSID of disconnected AP */
    uint8_t reason;           /**< reason of disconnection */
    int8_t  rssi;             /**< rssi of disconnection */
} wifi_event_sta_disconnected_t;

/** @brief Argument structure for WIFI_EVENT_STA_AUTHMODE_CHANGE event */
typedef struct {
    wifi_auth_mode_t old_mode;         /**< the old auth mode of AP */
    wifi_auth_mode_t new_mode;         /**< the new auth mode of AP */
} wifi_event_sta_authmode_change_t;

/** @brief Argument structure for WIFI_EVENT_STA_WPS_ER_PIN event */
typedef struct {
    uint8_t pin_code[8];         /**< PIN code of station in enrollee mode */
} wifi_event_sta_wps_er_pin_t;

/** @brief Argument structure for WIFI_EVENT_AP_STACONNECTED event */
typedef struct {
    uint8_t mac[6];           /**< MAC address of the station connected to Soft-AP */
    uint8_t aid;              /**< the aid that Soft-AP gives to the station */
    bool is_mesh_child;       /**< flag to identify mesh child */
} wifi_event_ap_staconnected_t;

/** @brief Argument structure for WIFI_EVENT_AP_STADISCONNECTED event */
typedef struct {
    uint8_t mac[6];           /**< MAC address of the station disconnects to soft-AP */
    uint8_t aid;              /**< the aid that soft-AP gave to the station */
    bool is_mesh_child;       /**< flag to identify mesh child */
    uint16_t reason;          /**< reason of disconnection */
} wifi_event_ap_stadisconnected_t;

/** @brief Argument structure for WIFI_EVENT_STA_BSS_RSSI_LOW event */
typedef struct {
    int32_t rssi;             /**< RSSI value of bss */
} wifi_event_bss_rssi_low_t;

/** @brief Argument structure for WIFI_EVENT_HOME_CHANNEL_CHANGE */
typedef struct {
    uint8_t old_chan;         /**< old home channel of AP/STA */
    wifi_second_chan_t old_snd;  /**< old second channel */
    uint8_t new_chan;         /**< new home channel */
    wifi_second_chan_t new_snd;  /**< new second channel */
} wifi_event_home_channel_change_t;

/** @endcond */

/** @cond */
/** OSI function table / crypto table — opaque in the sim */
typedef struct wifi_osi_funcs_t wifi_osi_funcs_t;
typedef struct { const void *dummy; } wpa_crypto_funcs_t;
/** @endcond */

#ifdef __cplusplus
}
#endif

#endif /* ESP_WIFI_TYPES_H */
