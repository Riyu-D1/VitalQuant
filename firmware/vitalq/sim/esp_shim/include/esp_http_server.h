/* esp_http_server.h — header-only STUB for the ESP-IDF HTTP server API.
 *
 * The sim has no inbound socket listener for the device: every entry
 * point returns ESP_ERR_NOT_SUPPORTED, so firmware status-page paths
 * (e.g. vq_wifi_status_httpd_start → httpd_start) degrade cleanly —
 * the start fails, the firmware logs it and carries on.  All types are
 * real so the firmware's handler functions still compile.
 */
#ifndef ESP_HTTP_SERVER_H
#define ESP_HTTP_SERVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>      /* ssize_t */

#ifdef __cplusplus
extern "C" {
#endif

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
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG     0x102
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND       0x105
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED   0x106
#endif

/* ------------------------------------------------------------------ */
/* Types (real shapes; httpd_req_t itself stays opaque)                */
/* ------------------------------------------------------------------ */
typedef void *httpd_handle_t;
typedef struct httpd_req httpd_req_t;

typedef enum http_method {
    HTTP_DELETE, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT,
    HTTP_CONNECT, HTTP_OPTIONS, HTTP_TRACE, HTTP_COPY, HTTP_LOCK,
    HTTP_MKCOL, HTTP_MOVE, HTTP_PROPFIND, HTTP_PROPPATCH, HTTP_SEARCH,
    HTTP_UNLOCK, HTTP_BIND, HTTP_REBIND, HTTP_UNBIND, HTTP_ACL, HTTP_REPORT,
    HTTP_MKACTIVITY, HTTP_CHECKOUT, HTTP_MERGE, HTTP_MSEARCH, HTTP_NOTIFY,
    HTTP_SUBSCRIBE, HTTP_UNSUBSCRIBE, HTTP_PATCH, HTTP_PURGE, HTTP_MKCALENDAR,
    HTTP_LINK, HTTP_UNLINK
} httpd_method_t;

typedef esp_err_t (*httpd_uri_handler_t)(httpd_req_t *r);

typedef struct httpd_uri {
    const char         *uri;
    httpd_method_t      method;
    httpd_uri_handler_t handler;
    void               *user_ctx;
    bool                is_websocket;
    bool                handle_ws_control_frames;
    uint16_t            supported_subprotocol;
    struct httpd_uri   *next;
} httpd_uri_t;

typedef void (*httpd_free_ctx_fn_t)(void *ctx);
typedef int  (*httpd_open_func_t)(httpd_handle_t hd, int sockfd);
typedef void (*httpd_close_func_t)(httpd_handle_t hd, int sockfd);
typedef bool (*httpd_uri_match_func_t)(const char *uri_template,
                                       const char *uri, size_t len);

typedef struct httpd_config {
    unsigned                task_priority;
    size_t                  stack_size;
    unsigned                core_id;
    uint16_t                server_port;
    uint16_t                ctrl_port;
    uint16_t                max_open_sockets;
    uint16_t                max_uri_handlers;
    uint16_t                max_resp_headers;
    uint16_t                backlog_wait;
    bool                    lru_purge_enable;
    uint16_t                recv_wait_timeout;
    uint16_t                send_wait_timeout;
    void                   *global_user_ctx;
    httpd_free_ctx_fn_t     global_user_ctx_free_fn;
    void                   *global_transport_ctx;
    httpd_free_ctx_fn_t     global_transport_ctx_free_fn;
    bool                    enable_so_linger;
    int                     linger_timeout;
    bool                    keep_alive_enable;
    int                     keep_alive_idle;
    int                     keep_alive_interval;
    int                     keep_alive_count;
    httpd_open_func_t       open_fn;
    httpd_close_func_t      close_fn;
    httpd_uri_match_func_t  uri_match_fn;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() {            \
    .task_priority        = 4,              \
    .stack_size           = 4096,           \
    .core_id              = 0,              \
    .server_port          = 80,             \
    .ctrl_port            = 32768,          \
    .max_open_sockets     = 7,              \
    .max_uri_handlers     = 8,              \
    .max_resp_headers     = 8,              \
    .backlog_wait         = 5,              \
    .lru_purge_enable     = false,          \
    .recv_wait_timeout    = 5,              \
    .send_wait_timeout    = 5,              \
    .global_user_ctx      = NULL,           \
    .global_user_ctx_free_fn = NULL,        \
    .global_transport_ctx = NULL,           \
    .global_transport_ctx_free_fn = NULL,   \
    .enable_so_linger     = false,          \
    .linger_timeout       = 0,              \
    .keep_alive_enable    = false,          \
    .keep_alive_idle      = 0,              \
    .keep_alive_interval  = 0,              \
    .keep_alive_count     = 0,              \
    .open_fn              = NULL,           \
    .close_fn             = NULL,           \
    .uri_match_fn         = NULL            \
}

/* httpd_resp_send(): use strlen(buf) */
#define HTTPD_RESP_USE_STRLEN   (-1)

/* Response-type shorthands from the real header. */
#define HTTPD_TYPE_JSON         "application/json"
#define HTTPD_TYPE_TEXT         "text/plain"
#define HTTPD_TYPE_HTML         "text/html"

typedef enum {
    HTTPD_404_NOT_FOUND = -1,
    HTTPD_405_METHOD_NOT_ALLOWED,
    HTTPD_408_REQ_TIMEOUT,
    HTTPD_411_LENGTH_REQUIRED,
    HTTPD_414_URI_TOO_LONG,
    HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE,
    HTTPD_500_INTERNAL_SERVER_ERROR = -8
} httpd_err_code_t;

/* ------------------------------------------------------------------ */
/* Stubs — the status-page path degrades on ESP_ERR_NOT_SUPPORTED.     */
/* ------------------------------------------------------------------ */
static inline esp_err_t httpd_start(httpd_handle_t *handle,
                                    const httpd_config_t *config)
{ (void)handle; (void)config; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_stop(httpd_handle_t handle)
{ (void)handle; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_register_uri_handler(httpd_handle_t handle,
                                                   const httpd_uri_t *uri_handler)
{ (void)handle; (void)uri_handler; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_unregister_uri_handler(httpd_handle_t handle,
                                                     const char *uri,
                                                     httpd_method_t method)
{ (void)handle; (void)uri; (void)method; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_unregister_uri(httpd_handle_t handle,
                                             const char *uri)
{ (void)handle; (void)uri; return ESP_ERR_NOT_SUPPORTED; }

/* Request/response helpers — never reachable (no handler ever runs)
 * but kept compiling so firmware handler bodies type-check.          */
static inline esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf,
                                        ssize_t buf_len)
{ (void)r; (void)buf; (void)buf_len; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf,
                                              ssize_t buf_len)
{ (void)r; (void)buf; (void)buf_len; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str)
{ (void)r; (void)str; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_resp_send_err(httpd_req_t *r,
                                            httpd_err_code_t error,
                                            const char *msg)
{ (void)r; (void)error; (void)msg; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{ (void)r; (void)type; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status)
{ (void)r; (void)status; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field,
                                           const char *value)
{ (void)r; (void)field; (void)value; return ESP_ERR_NOT_SUPPORTED; }

static inline int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len)
{ (void)r; (void)buf; (void)buf_len; return -1; }

static inline int httpd_recv(httpd_req_t *r, char *buf, size_t buf_len)
{ (void)r; (void)buf; (void)buf_len; return -1; }

static inline size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *field)
{ (void)r; (void)field; return 0; }

static inline esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,
                                                    const char *field,
                                                    char *val, size_t val_size)
{ (void)r; (void)field; (void)val; (void)val_size; return ESP_ERR_NOT_SUPPORTED; }

static inline size_t httpd_req_get_url_query_len(httpd_req_t *r)
{ (void)r; return 0; }

static inline esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf,
                                                    size_t buf_len)
{ (void)r; (void)buf; (void)buf_len; return ESP_ERR_NOT_SUPPORTED; }

static inline esp_err_t httpd_query_key_value(const char *qry, const char *key,
                                              char *val, size_t val_size)
{ (void)qry; (void)key; (void)val; (void)val_size; return ESP_ERR_NOT_FOUND; }

static inline int httpd_req_to_sockfd(httpd_req_t *r)
{ (void)r; return -1; }

static inline void *httpd_sess_get_ctx(httpd_handle_t handle, int sockfd)
{ (void)handle; (void)sockfd; return NULL; }

static inline void httpd_sess_set_ctx(httpd_handle_t handle, int sockfd,
                                      void *ctx, httpd_free_ctx_fn_t free_fn)
{ (void)handle; (void)sockfd; (void)ctx; (void)free_fn; }

#ifdef __cplusplus
}
#endif
#endif /* ESP_HTTP_SERVER_H */
