/* esp_http_client.h — host-sim shim for the ESP-IDF HTTP client API.
 *
 * Backed by real POSIX sockets (sim/esp_shim/http/http_client_sim.c):
 * esp_http_client_perform() issues a genuine HTTP/1.1 request so a
 * localhost mock server (sim/mock/server.py) observes the exact
 * method/path/headers/body the firmware emitted.
 *
 * Sim semantics:
 *  - http://host:port/path is parsed verbatim (default port 80).
 *  - https:// URLs are downgraded to plain TCP — TLS is off-path in the
 *    sim; the connect logs "[VB-HTTP-HTTPS→plain]".  The URL port is
 *    always used verbatim (incl. 127.0.0.1/localhost).
 *  - g_vb_faults.cloud_drop → connect() gets ECONNREFUSED semantics:
 *    perform()/open() return ESP_FAIL after an HTTP_EVENT_ERROR.
 *  - g_vsim_cloud_port > 0 (vsim.h contract) steers the socket to
 *    127.0.0.1:g_vsim_cloud_port regardless of the URL host.
 *  - cfg.timeout_ms is honoured via a poll() connect deadline and
 *    SO_RCVTIMEO/SO_SNDTIMEO on the socket.
 */
#ifndef ESP_HTTP_CLIENT_H
#define ESP_HTTP_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* esp_err_t / ESP_* — guarded so this header composes with the rest of
 * the shim layer (vboard.h, a future esp_err.h) whichever order wins. */
#ifndef ESP_ERR_T_DECLARED
#define ESP_ERR_T_DECLARED
typedef int esp_err_t;
#endif
#ifndef ESP_OK
#define ESP_OK                  0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL                (-1)
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM          0x101
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG     0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE   0x103
#endif
#ifndef ESP_ERR_INVALID_SIZE
#define ESP_ERR_INVALID_SIZE    0x104
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND       0x105
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED   0x106
#endif
#ifndef ESP_ERR_TIMEOUT
#define ESP_ERR_TIMEOUT         0x107
#endif
#ifndef ESP_ERR_INVALID_RESPONSE
#define ESP_ERR_INVALID_RESPONSE 0x108
#endif

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    HTTP_EVENT_ERROR = 0,           /* transport-level failure          */
    HTTP_EVENT_ON_CONNECTED,        /* TCP connection established       */
    HTTP_EVENT_HEADERS_SENT,        /* request line + headers flushed   */
    HTTP_EVENT_HEADER_SENT = HTTP_EVENT_HEADERS_SENT, /* legacy alias   */
    HTTP_EVENT_ON_HEADER,           /* one per response header          */
    HTTP_EVENT_ON_DATA,             /* response-body chunk              */
    HTTP_EVENT_ON_FINISH,           /* response body fully received     */
    HTTP_EVENT_DISCONNECTED,        /* socket closed                    */
    HTTP_EVENT_REDIRECT,            /* redirect followed (unused in sim)*/
} esp_http_client_event_id_t;

/* Alternate spellings some app code uses. */
#define HTTP_EVENT_CONNECTED        HTTP_EVENT_ON_CONNECTED

typedef struct esp_http_client *esp_http_client_handle_t;

typedef struct esp_http_client_event {
    esp_http_client_event_id_t   event_id;
    esp_http_client_handle_t     client;
    void                        *data;          /* ON_DATA chunk        */
    int                          data_len;
    void                        *user_data;     /* cfg.user_data        */
    char                        *header_key;    /* ON_HEADER            */
    char                        *header_value;  /* ON_HEADER            */
} esp_http_client_event_t;

typedef esp_http_client_event_t esp_http_client_event_handle_t;

typedef esp_err_t (*esp_http_client_event_handler_t)(esp_http_client_event_t *evt);

/* ------------------------------------------------------------------ */
/* Enums                                                               */
/* ------------------------------------------------------------------ */
typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_NOTIFY,
    HTTP_METHOD_SUBSCRIBE,
    HTTP_METHOD_UNSUBSCRIBE,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_COPY,
    HTTP_METHOD_MOVE,
    HTTP_METHOD_LOCK,
    HTTP_METHOD_UNLOCK,
    HTTP_METHOD_PROPFIND,
    HTTP_METHOD_PROPPATCH,
    HTTP_METHOD_MKCOL,
    HTTP_METHOD_MAX,
} esp_http_client_method_t;

typedef enum {
    HTTP_AUTH_TYPE_NONE = 0,
    HTTP_AUTH_TYPE_BASIC,
    HTTP_AUTH_TYPE_DIGEST,
} esp_http_client_auth_type_t;

typedef enum {
    HTTP_TRANSPORT_UNKNOWN = 0x0,
    HTTP_TRANSPORT_OVER_TCP,
    HTTP_TRANSPORT_OVER_SSL,
} esp_http_client_transport_t;

/* ------------------------------------------------------------------ */
/* Config — mirrors the ESP-IDF v5 field set (designated-init safe).   */
/* ------------------------------------------------------------------ */
typedef struct {
    const char                     *url;          /* full URL, parsed   */
    const char                     *host;         /* or split fields    */
    int                             port;
    const char                     *username;
    const char                     *password;
    esp_http_client_auth_type_t     auth_type;
    const char                     *path;
    const char                     *query;
    const char                     *cert_pem;          /* recorded only */
    const char                     *client_cert_pem;   /* recorded only */
    const char                     *client_key_pem;    /* recorded only */
    const char                     *client_cert_password;
    size_t                          client_cert_password_len;
    const char                     *user_agent;
    esp_http_client_method_t        method;
    int                             timeout_ms;
    bool                            disable_auto_redirect;
    int                             max_redirection_count;
    int                             max_authorization_retries;
    esp_http_client_event_handler_t event_handler;
    esp_http_client_transport_t     transport_type;
    int                             buffer_size;      /* rx chunk bytes */
    int                             buffer_size_tx;
    void                           *user_data;
    bool                            is_async;
    bool                            use_global_ca_store;
    bool                            skip_cert_common_name_check;
    const char                     *common_name;
    bool                            keep_alive_enable;
    int                             keep_alive_idle;
    int                             keep_alive_interval;
    int                             keep_alive_count;
    esp_err_t                     (*crt_bundle_attach)(void *conf);
    void                           *ds_data;
} esp_http_client_config_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t                esp_http_client_cleanup(esp_http_client_handle_t client);

/* Synchronous request: connect → send → read the whole response,
 * dispatching events.  ESP_OK means an HTTP response was received
 * (any status code); ESP_FAIL is a transport-level failure.         */
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);

/* ------------------------------------------------------------------ */
/* Staged API (open/write/fetch/read/close)                            */
/* ------------------------------------------------------------------ */
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len);
int       esp_http_client_write(esp_http_client_handle_t client,
                                const char *buffer, int len);
int       esp_http_client_fetch_headers(esp_http_client_handle_t client);
int       esp_http_client_read(esp_http_client_handle_t client,
                               char *buffer, int len);
int64_t   esp_http_client_read_response(esp_http_client_handle_t client,
                                        char *buffer, int len);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);

/* ------------------------------------------------------------------ */
/* Request shaping                                                     */
/* ------------------------------------------------------------------ */
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client,
                                  const char *url);
esp_err_t esp_http_client_get_url(esp_http_client_handle_t client,
                                  char **url);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client,
                                     esp_http_client_method_t method);
esp_err_t esp_http_client_set_authtype(esp_http_client_handle_t client,
                                       esp_http_client_auth_type_t auth_type);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char *key, const char *value);
esp_err_t esp_http_client_get_header(esp_http_client_handle_t client,
                                     const char *key, char **value);
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t client,
                                        const char *key);
esp_err_t esp_http_client_delete_all_headers(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
                                         const char *data, int len);
int       esp_http_client_get_post_field(esp_http_client_handle_t client,
                                         char **data);
esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t client,
                                        void *data);
esp_err_t esp_http_client_get_user_data(esp_http_client_handle_t client,
                                        void **data);
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t client);
void      esp_http_client_reset_redirect_counter(esp_http_client_handle_t client);

/* ------------------------------------------------------------------ */
/* Response accessors                                                  */
/* ------------------------------------------------------------------ */
int       esp_http_client_get_status_code(esp_http_client_handle_t client);
int64_t   esp_http_client_get_content_length(esp_http_client_handle_t client);
bool      esp_http_client_is_complete_data_received(esp_http_client_handle_t client);

#ifdef __cplusplus
}
#endif
#endif /* ESP_HTTP_CLIENT_H */
