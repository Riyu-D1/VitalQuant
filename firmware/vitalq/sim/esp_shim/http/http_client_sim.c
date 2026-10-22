/* http_client_sim.c — real-socket esp_http_client for the host sim.
 *
 * perform() issues a genuine HTTP/1.1 request over a blocking POSIX
 * socket so a localhost mock (sim/mock/server.py) sees the exact
 * method/path/headers/body cloud_client.c emitted.  The response is
 * parsed by Content-Length, chunked, or close-delimited (the python
 * mock speaks HTTP/1.0 and closes — exactly what the close-delimited
 * path exists for), then replayed through the standard IDF event
 * sequence: ON_CONNECTED → HEADERS_SENT → ON_HEADER* → ON_DATA* →
 * ON_FINISH → DISCONNECTED.
 *
 * Sim semantics (header contract in include/esp_http_client.h):
 *  - https:// → plain TCP, logged "[VB-HTTP-HTTPS→plain]".
 *  - g_vb_faults.cloud_drop → connect() ECONNREFUSED → ESP_FAIL + ERROR.
 *  - g_vsim_cloud_port > 0 → steer to 127.0.0.1:g_vsim_cloud_port.
 *  - cfg.timeout_ms → poll() connect deadline + SO_RCVTIMEO/SO_SNDTIMEO.
 */
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "esp_http_client.h"   /* must precede vboard.h (esp_err_t) */
#include "vboard.h"            /* g_vb_faults.cloud_drop            */
#include "vsim.h"              /* g_vsim_cloud_port                 */

/* Weak defaults: the real vboard/vsim objects (strong symbols) override
 * these when linked.  With neither present the shim still links and
 * behaves benignly — no faults, no steering. */
vboard_faults_t g_vb_faults __attribute__((weak));
int             g_vsim_cloud_port __attribute__((weak));

#define VB_DEF_TIMEOUT_MS   10000          /* cfg.timeout_ms <= 0      */
#define VB_DEF_RX_BUF       512            /* IDF DEFAULT_HTTP_BUF_SIZE */
#define VB_AGENT            "ESP32 HTTP Client/1.0"

typedef struct { char *k, *v; } kv_t;

typedef enum { RX_NONE = 0, RX_LEN, RX_CHUNKED, RX_CLOSE } rx_mode_t;

struct esp_http_client {
    /* ---- config snapshot ---- */
    char    *url;                          /* as passed to init/set_url */
    char     host[128];
    int      port;
    char     path[640];                    /* path + ?query verbatim    */
    char     username[64];
    char     password[64];
    bool     https;
    esp_http_client_method_t     method;
    int      timeout_ms;
    bool     disable_auto_redirect;
    int      buffer_size;
    int      buffer_size_tx;
    const char *user_agent;                /* borrowed (config-owned)   */
    esp_http_client_auth_type_t  auth_type;
    esp_http_client_transport_t  transport;
    bool     is_async;
    bool     keep_alive_enable;
    const char *cert_pem;
    esp_err_t (*crt_attach)(void *conf);
    esp_http_client_event_handler_t handler;
    void    *user_data;

    /* ---- request ---- */
    kv_t    *hdrs;
    int      n_hdrs, cap_hdrs;
    uint8_t *post;
    size_t   post_len;
    bool     post_set;

    /* ---- wire state ---- */
    int      fd;
    int      status;                       /* -1 until a status line    */
    int64_t  content_length;               /* -1 if not given           */
    rx_mode_t rx;
    int64_t  rx_left;                      /* RX_LEN bytes outstanding  */
    long     chunk_left;                   /* RX_CHUNKED cur-chunk left */
    bool     headers_done;
    bool     rx_done;
    bool     complete;
    int      redir_count;
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static int vb_timeout(const struct esp_http_client *c)
{
    return c->timeout_ms > 0 ? c->timeout_ms : VB_DEF_TIMEOUT_MS;
}

static int vb_bufsz(const struct esp_http_client *c)
{
    return c->buffer_size > 0 ? c->buffer_size : VB_DEF_RX_BUF;
}

static void vb_evt(struct esp_http_client *c, esp_http_client_event_id_t id,
                   void *data, int len, const char *hk, const char *hv)
{
    if (!c->handler) return;
    esp_http_client_event_t e = {
        .event_id     = id,
        .client       = c,
        .data         = data,
        .data_len     = len,
        .user_data    = c->user_data,
        .header_key   = (char *)hk,
        .header_value = (char *)hv,
    };
    (void)c->handler(&e);
}

static void vb_error(struct esp_http_client *c)
{
    vb_evt(c, HTTP_EVENT_ERROR, NULL, 0, NULL, NULL);
}

static const char *method_str(esp_http_client_method_t m)
{
    static const char *const names[HTTP_METHOD_MAX] = {
        [HTTP_METHOD_GET]        = "GET",
        [HTTP_METHOD_POST]       = "POST",
        [HTTP_METHOD_PUT]        = "PUT",
        [HTTP_METHOD_PATCH]      = "PATCH",
        [HTTP_METHOD_DELETE]     = "DELETE",
        [HTTP_METHOD_HEAD]       = "HEAD",
        [HTTP_METHOD_NOTIFY]     = "NOTIFY",
        [HTTP_METHOD_SUBSCRIBE]  = "SUBSCRIBE",
        [HTTP_METHOD_UNSUBSCRIBE]= "UNSUBSCRIBE",
        [HTTP_METHOD_OPTIONS]    = "OPTIONS",
        [HTTP_METHOD_COPY]       = "COPY",
        [HTTP_METHOD_MOVE]       = "MOVE",
        [HTTP_METHOD_LOCK]       = "LOCK",
        [HTTP_METHOD_UNLOCK]     = "UNLOCK",
        [HTTP_METHOD_PROPFIND]   = "PROPFIND",
        [HTTP_METHOD_PROPPATCH]  = "PROPPATCH",
        [HTTP_METHOD_MKCOL]      = "MKCOL",
    };
    const char *s = (m >= 0 && m < HTTP_METHOD_MAX) ? names[m] : NULL;
    return s ? s : "GET";
}

static bool method_has_body(esp_http_client_method_t m)
{
    return m == HTTP_METHOD_POST || m == HTTP_METHOD_PUT ||
           m == HTTP_METHOD_PATCH;
}

/* ------------------------------------------------------------------ */
/* URL parsing — [http|https]://[user[:pass]@]host[:port][/path[?q]]   */
/* ------------------------------------------------------------------ */
static esp_err_t parse_url(struct esp_http_client *c, const char *url)
{
    const char *p = url;
    c->https = false;
    if      (strncasecmp(p, "http://", 7) == 0)  p += 7;
    else if (strncasecmp(p, "https://", 8) == 0) { p += 8; c->https = true; }
    else if (strstr(p, "://")) return ESP_ERR_NOT_SUPPORTED; /* other scheme */

    /* optional userinfo — must precede the first '/' */
    const char *slash = strchr(p, '/');
    const char *at    = strchr(p, '@');
    if (at && (!slash || at < slash)) {
        const char *colon = memchr(p, ':', (size_t)(at - p));
        size_t ul = colon ? (size_t)(colon - p) : (size_t)(at - p);
        size_t pl = colon ? (size_t)(at - colon - 1) : 0;
        if (ul >= sizeof(c->username) || pl >= sizeof(c->password))
            return ESP_ERR_INVALID_ARG;
        memcpy(c->username, p, ul); c->username[ul] = 0;
        if (colon) { memcpy(c->password, colon + 1, pl); c->password[pl] = 0; }
        p = at + 1;
    }

    /* host — [v6-literal] or name up to ':' or '/' */
    const char *hend;
    if (*p == '[') {
        const char *rb = strchr(p, ']');
        if (!rb) return ESP_ERR_INVALID_ARG;
        size_t hl = (size_t)(rb - p - 1);
        if (!hl || hl >= sizeof(c->host)) return ESP_ERR_INVALID_ARG;
        memcpy(c->host, p + 1, hl); c->host[hl] = 0;
        hend = rb + 1;
    } else {
        hend = p;
        while (*hend && *hend != ':' && *hend != '/') hend++;
        size_t hl = (size_t)(hend - p);
        if (!hl || hl >= sizeof(c->host)) return ESP_ERR_INVALID_ARG;
        memcpy(c->host, p, hl); c->host[hl] = 0;
    }
    p = hend;

    c->port = c->https ? 443 : 80;
    if (*p == ':') {
        p++;
        const char *s = p;
        long v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        if (p == s || v <= 0 || v > 65535) return ESP_ERR_INVALID_ARG;
        c->port = (int)v;
    }

    if (*p) {
        if (*p != '/' && *p != '?') return ESP_ERR_INVALID_ARG;
        if (*p == '?') {
            if (strlen(p) + 1 >= sizeof(c->path)) return ESP_ERR_INVALID_ARG;
            c->path[0] = '/';
            strcpy(c->path + 1, p);        /* "/?query"               */
        } else {
            if (strlen(p) >= sizeof(c->path)) return ESP_ERR_INVALID_ARG;
            strcpy(c->path, p);            /* keeps ?query verbatim   */
        }
    } else {
        strcpy(c->path, "/");
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Header list (insertion order; replace is case-insensitive)          */
/* ------------------------------------------------------------------ */
static kv_t *hdr_find(struct esp_http_client *c, const char *key)
{
    for (int i = 0; i < c->n_hdrs; i++)
        if (strcasecmp(c->hdrs[i].k, key) == 0) return &c->hdrs[i];
    return NULL;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,
                                     const char *key, const char *value)
{
    if (!c || !key) return ESP_ERR_INVALID_ARG;
    if (!value) return esp_http_client_delete_header(c, key); /* IDF: NULL ⇒ del */
    kv_t *e = hdr_find(c, key);
    if (e) {
        char *nv = strdup(value);
        if (!nv) return ESP_ERR_NO_MEM;
        free(e->v); e->v = nv;
        return ESP_OK;
    }
    if (c->n_hdrs == c->cap_hdrs) {
        int nc = c->cap_hdrs ? c->cap_hdrs * 2 : 8;
        kv_t *nh = realloc(c->hdrs, (size_t)nc * sizeof(*nh));
        if (!nh) return ESP_ERR_NO_MEM;
        c->hdrs = nh; c->cap_hdrs = nc;
    }
    e = &c->hdrs[c->n_hdrs];
    e->k = strdup(key);
    e->v = strdup(value);
    if (!e->k || !e->v) { free(e->k); free(e->v); return ESP_ERR_NO_MEM; }
    c->n_hdrs++;
    return ESP_OK;
}

esp_err_t esp_http_client_get_header(esp_http_client_handle_t c,
                                     const char *key, char **value)
{
    if (!c || !key || !value) return ESP_ERR_INVALID_ARG;
    kv_t *e = hdr_find(c, key);
    if (!e) return ESP_ERR_NOT_FOUND;
    *value = e->v;
    return ESP_OK;
}

esp_err_t esp_http_client_delete_header(esp_http_client_handle_t c,
                                        const char *key)
{
    if (!c || !key) return ESP_ERR_INVALID_ARG;
    kv_t *e = hdr_find(c, key);
    if (!e) return ESP_ERR_NOT_FOUND;
    free(e->k); free(e->v);
    *e = c->hdrs[--c->n_hdrs];
    return ESP_OK;
}

esp_err_t esp_http_client_delete_all_headers(esp_http_client_handle_t c)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < c->n_hdrs; i++) { free(c->hdrs[i].k); free(c->hdrs[i].v); }
    c->n_hdrs = 0;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Request shaping                                                     */
/* ------------------------------------------------------------------ */
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t c,
                                         const char *data, int len)
{
    if (!c || len < 0) return ESP_ERR_INVALID_ARG;
    free(c->post); c->post = NULL; c->post_len = 0; c->post_set = false;
    if (data && len) {
        c->post = malloc((size_t)len);
        if (!c->post) return ESP_ERR_NO_MEM;
        memcpy(c->post, data, (size_t)len);
        c->post_len = (size_t)len;
        c->post_set = true;
    }
    return ESP_OK;
}

int esp_http_client_get_post_field(esp_http_client_handle_t c, char **data)
{
    if (!c || !data) return -1;
    *data = (char *)c->post;
    return (int)c->post_len;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t c,
                                     esp_http_client_method_t method)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    c->method = method;
    return ESP_OK;
}

esp_err_t esp_http_client_set_authtype(esp_http_client_handle_t c,
                                       esp_http_client_auth_type_t auth_type)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    c->auth_type = auth_type;
    return ESP_OK;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t c, const char *url)
{
    if (!c || !url) return ESP_ERR_INVALID_ARG;
    struct esp_http_client tmp = *c;
    esp_err_t e = parse_url(&tmp, url);
    if (e != ESP_OK) return e;
    char *u = strdup(url);
    if (!u) return ESP_ERR_NO_MEM;
    free(c->url); c->url = u;
    strcpy(c->host, tmp.host); c->port = tmp.port;
    strcpy(c->path, tmp.path); c->https = tmp.https;
    strcpy(c->username, tmp.username); strcpy(c->password, tmp.password);
    return ESP_OK;
}

esp_err_t esp_http_client_get_url(esp_http_client_handle_t c, char **url)
{
    if (!c || !url) return ESP_ERR_INVALID_ARG;
    *url = c->url;
    return c->url ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t c, void *data)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    c->user_data = data;
    return ESP_OK;
}

esp_err_t esp_http_client_get_user_data(esp_http_client_handle_t c, void **data)
{
    if (!c || !data) return ESP_ERR_INVALID_ARG;
    *data = c->user_data;
    return ESP_OK;
}

esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t c)
{
    /* The sim never follows redirects (disable_auto_redirect is the
     * only mode) — but the call is accepted so app code is portable.  */
    if (!c) return ESP_ERR_INVALID_ARG;
    c->redir_count++;
    return ESP_OK;
}

void esp_http_client_reset_redirect_counter(esp_http_client_handle_t c)
{
    if (c) c->redir_count = 0;
}

/* ------------------------------------------------------------------ */
/* Socket layer                                                        */
/* ------------------------------------------------------------------ */
static int vb_tcp_connect(struct esp_http_client *c,
                          const char *host, int port)
{
    char serv[8];
    snprintf(serv, sizeof(serv), "%d", port);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_NUMERICSERV;
    if (getaddrinfo(host, serv, &hints, &res) != 0) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai && fd < 0; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl < 0) fl = 0;
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc < 0 && errno == EINPROGRESS) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            rc = poll(&pfd, 1, vb_timeout(c));
            if (rc == 0) { errno = ETIMEDOUT; rc = -1; }
            else if (rc > 0) {
                int soerr = 0; socklen_t sl = sizeof(soerr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) == 0 &&
                    soerr) { errno = soerr; rc = -1; }
            }
        }
        if (rc < 0) { close(fd); fd = -1; continue; }
        fcntl(fd, F_SETFL, fl);            /* restore blocking mode    */
        break;
    }
    freeaddrinfo(res);
    if (fd < 0) return -1;

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    int to = vb_timeout(c);
    struct timeval tv = { .tv_sec  = to / 1000,
                          .tv_usec = (to % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

static esp_err_t send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len) {
        ssize_t w = send(fd, p, len, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return ESP_FAIL;
        }
        if (w == 0) return ESP_FAIL;
        p += (size_t)w; len -= (size_t)w;
    }
    return ESP_OK;
}

/* Read one byte; 1 ok / 0 orderly EOF / -1 error (timeout = -2). */
static int recv1(int fd, uint8_t *b)
{
    for (;;) {
        ssize_t n = recv(fd, b, 1, 0);
        if (n == 1) return 1;
        if (n == 0) return 0;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
        return -1;
    }
}

/* Read a line terminated by CRLF (byte-at-a-time — heads/chunk sizes
 * are small, and this guarantees no body bytes are over-consumed).
 * Returns >=0 line length (0 = blank line), -1 error, -2 timeout,
 * -3 EOF with no complete line pending.                             */
static int recv_line(int fd, char *buf, size_t cap)
{
    size_t n = 0;
    uint8_t b = 0, prev = 0;
    for (;;) {
        int r = recv1(fd, &b);
        if (r == 0)  return -3;            /* EOF                      */
        if (r < 0)   return r;             /* -1 error / -2 timeout    */
        if (n + 1 < cap) buf[n] = (char)b;
        n++;
        if (prev == '\r' && b == '\n') {
            n -= 2;
            buf[n < cap ? n : cap - 1] = 0;
            return (int)n;
        }
        prev = b;
    }
}

/* ------------------------------------------------------------------ */
/* Request head — byte-exact with what the firmware configured         */
/* ------------------------------------------------------------------ */
static esp_err_t send_request_head(struct esp_http_client *c, int write_len)
{
    /* app() — bounded append into a growable buffer */
    size_t cap = 2048 + strlen(c->path);
    size_t n = 0;
    char  *b = malloc(cap);
    if (!b) return ESP_ERR_NO_MEM;
#define VB_APP(...) do { \
        int _r = snprintf(b + n, cap - n, __VA_ARGS__); \
        if (_r < 0) { free(b); return ESP_FAIL; } \
        n += (size_t)_r; \
        if (n >= cap) { free(b); return ESP_ERR_INVALID_SIZE; } \
    } while (0)

    VB_APP("%s %s HTTP/1.1\r\n", method_str(c->method), c->path);

    bool ua_set = false, cl_set = false, host_set = false;
    for (int i = 0; i < c->n_hdrs; i++) {
        if (!strcasecmp(c->hdrs[i].k, "Host"))           host_set = true;
        if (!strcasecmp(c->hdrs[i].k, "User-Agent"))     ua_set   = true;
        if (!strcasecmp(c->hdrs[i].k, "Content-Length")) cl_set   = true;
    }

    if (!host_set) {
        int dflt = c->https ? 443 : 80;
        VB_APP("Host: %s", c->host);
        if (c->port != dflt) VB_APP(":%d", c->port);
        VB_APP("\r\n");
    }
    for (int i = 0; i < c->n_hdrs; i++)
        VB_APP("%s: %s\r\n", c->hdrs[i].k, c->hdrs[i].v);
    if (!ua_set)
        VB_APP("User-Agent: %s\r\n",
               c->user_agent ? c->user_agent : VB_AGENT);
    if (!cl_set) {
        int eff = write_len >= 0 ? write_len
                                 : (c->post_set ? (int)c->post_len : -1);
        if (eff < 0 && method_has_body(c->method)) eff = 0;
        if (eff >= 0) VB_APP("Content-Length: %d\r\n", eff);
    }
    VB_APP("\r\n");
#undef VB_APP

    esp_err_t e = send_all(c->fd, b, n);
    free(b);
    if (e != ESP_OK) return e;
    vb_evt(c, HTTP_EVENT_HEADERS_SENT, NULL, 0, NULL, NULL);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Response parsing                                                    */
/* ------------------------------------------------------------------ */
static esp_err_t read_response_head(struct esp_http_client *c)
{
    /* status line */
    char line[512];
    int ln = recv_line(c->fd, line, sizeof(line));
    if (ln == -2) return ESP_ERR_TIMEOUT;
    if (ln <= 0) return ESP_FAIL;          /* EOF/garbage/empty        */
    if (strncmp(line, "HTTP/", 5) != 0) return ESP_ERR_INVALID_RESPONSE;
    int status = -1;
    const char *sp = strchr(line, ' ');
    if (sp) status = (int)strtol(sp + 1, NULL, 10);
    if (status < 0) return ESP_ERR_INVALID_RESPONSE;
    c->status = status;

    /* headers → ON_HEADER events; capture framing fields */
    for (;;) {
        ln = recv_line(c->fd, line, sizeof(line));
        if (ln == -2) return ESP_ERR_TIMEOUT;
        if (ln < 0) return ESP_FAIL;                 /* EOF mid-head    */
        if (ln == 0) break;                          /* blank line      */
        char *colon = strchr(line, ':');
        char *val = colon ? colon + 1 : (char *)"";
        while (*val == ' ' || *val == '\t') val++;
        if (colon) *colon = 0;
        /* rstrip the key for tidier events */
        if (colon) {
            char *e = colon;
            while (e > line && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        }
        if (!strcasecmp(line, "Content-Length"))
            c->content_length = strtoll(val, NULL, 10);
        else if (!strcasecmp(line, "Transfer-Encoding") &&
                 strcasestr(val, "chunked"))
            c->rx = RX_CHUNKED;
        vb_evt(c, HTTP_EVENT_ON_HEADER, NULL, 0, line, val);
    }

    /* body framing (only if chunked didn't already claim it) */
    if (c->rx != RX_CHUNKED) {
        if (c->method == HTTP_METHOD_HEAD || status == 204 || status == 304 ||
            (status >= 100 && status < 200))
            c->rx = RX_NONE;
        else if (c->content_length >= 0) {
            c->rx = RX_LEN;
            c->rx_left = c->content_length;
        } else {
            c->rx = RX_CLOSE;                        /* HTTP/1.0 style  */
        }
    }
    c->headers_done = true;
    c->rx_done = (c->rx == RX_NONE);
    if (c->rx_done) c->complete = true;
    return ESP_OK;
}

/* Pump ≤want body bytes into buf.  >0 bytes / 0 clean end / -1 error. */
static int pump(struct esp_http_client *c, char *buf, int want)
{
    if (c->rx_done) return 0;

    if (c->rx == RX_LEN) {
        if (c->rx_left <= 0) { c->rx_done = true; return 0; }
        if ((int64_t)want > c->rx_left) want = (int)c->rx_left;
        ssize_t n;
        do { n = recv(c->fd, buf, (size_t)want, 0); }
        while (n < 0 && errno == EINTR);
        if (n <= 0) return -1;                 /* short/timeout: bad   */
        c->rx_left -= n;
        if (c->rx_left <= 0) { c->rx_done = true; c->complete = true; }
        return (int)n;
    }

    if (c->rx == RX_CHUNKED) {
        if (c->chunk_left == 0) {
            char line[64];
            int ln = recv_line(c->fd, line, sizeof(line));
            if (ln < 0) return -1;
            long sz = strtol(line, NULL, 16);   /* ignores ;extensions */
            if (ln == 0 || sz < 0) return -1;
            if (sz == 0) {
                /* trailers until the blank line, then done            */
                while ((ln = recv_line(c->fd, line, sizeof(line))) > 0) {}
                if (ln < 0) return -1;
                c->rx_done = true; c->complete = true;
                return 0;
            }
            c->chunk_left = sz;
        }
        if ((long)want > c->chunk_left) want = (int)c->chunk_left;
        ssize_t n;
        do { n = recv(c->fd, buf, (size_t)want, 0); }
        while (n < 0 && errno == EINTR);
        if (n <= 0) return -1;
        c->chunk_left -= n;
        if (c->chunk_left == 0) {
            uint8_t crlf[2];
            if (recv1(c->fd, &crlf[0]) <= 0 || recv1(c->fd, &crlf[1]) <= 0)
                return -1;
        }
        return (int)n;
    }

    /* RX_CLOSE — body runs until the peer closes (HTTP/1.0 mock).  A
     * recv timeout with the head already parsed counts as end too: the
     * alternative would be hanging on a misbehaving keep-alive peer.  */
    {
        ssize_t n;
        do { n = recv(c->fd, buf, (size_t)want, 0); }
        while (n < 0 && errno == EINTR);
        if (n == 0) { c->rx_done = true; c->complete = true; return 0; }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                c->rx_done = true; c->complete = true;
                return 0;
            }
            return -1;
        }
        return (int)n;
    }
}

/* ------------------------------------------------------------------ */
/* Staged API                                                          */
/* ------------------------------------------------------------------ */
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int write_len)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    if (c->fd >= 0) return ESP_ERR_INVALID_STATE;

    /* vsim.h contract: with the mock armed, all cloud traffic is
     * steered at 127.0.0.1:g_vsim_cloud_port. */
    const char *host = c->host;
    int         port = c->port;
    if (g_vsim_cloud_port > 0) { host = "127.0.0.1"; port = g_vsim_cloud_port; }
    fprintf(stderr, "[VB-HTTP] %s:%d%s (steer=%d)\n", host, port, c->path,
            g_vsim_cloud_port);

    if (c->https)
        fprintf(stderr, "[VB-HTTP-HTTPS→plain] https://%s:%d%s — TLS off-path, "
                "using plain TCP to %s:%d\n", c->host, c->port, c->path,
                host, port);

    if (g_vb_faults.cloud_drop) {
        /* connect() → ECONNREFUSED semantics */
        errno = ECONNREFUSED;
        vb_error(c);
        return ESP_FAIL;
    }

    int fd = vb_tcp_connect(c, host, port);
    if (fd < 0) {
        vb_error(c);
        return ESP_FAIL;
    }
    c->fd = fd;
    vb_evt(c, HTTP_EVENT_ON_CONNECTED, NULL, 0, NULL, NULL);

    esp_err_t e = send_request_head(c, write_len);
    if (e != ESP_OK) {
        vb_error(c);
        close(c->fd); c->fd = -1;
        vb_evt(c, HTTP_EVENT_DISCONNECTED, NULL, 0, NULL, NULL);
        return e == ESP_ERR_NO_MEM ? e : ESP_FAIL;
    }
    return ESP_OK;
}

int esp_http_client_write(esp_http_client_handle_t c,
                          const char *buffer, int len)
{
    if (!c || c->fd < 0 || len < 0) return ESP_FAIL;
    if (send_all(c->fd, buffer, (size_t)len) != ESP_OK) return ESP_FAIL;
    return len;
}

int esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    if (!c || c->fd < 0) return ESP_FAIL;
    if (!c->headers_done &&
        read_response_head(c) != ESP_OK) return ESP_FAIL;
    return (int)c->content_length;
}

int esp_http_client_read(esp_http_client_handle_t c, char *buffer, int len)
{
    if (!c || c->fd < 0 || !buffer || len < 0) return ESP_FAIL;
    if (!c->headers_done &&
        read_response_head(c) != ESP_OK) return ESP_FAIL;
    return pump(c, buffer, len);
}

int64_t esp_http_client_read_response(esp_http_client_handle_t c,
                                      char *buffer, int len)
{
    if (!c || !buffer || len < 0) return ESP_FAIL;
    int64_t total = 0;
    while (total < len) {
        int n = esp_http_client_read(c, buffer + total, len - (int)total);
        if (n < 0) return total ? total : ESP_FAIL;
        if (n == 0) break;
        total += n;
    }
    return total;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t c)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    if (c->fd >= 0) {
        shutdown(c->fd, SHUT_RDWR);
        close(c->fd);
        c->fd = -1;
        vb_evt(c, HTTP_EVENT_DISCONNECTED, NULL, 0, NULL, NULL);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* perform — the synchronous path cloud_client uses                    */
/* ------------------------------------------------------------------ */
esp_err_t esp_http_client_perform(esp_http_client_handle_t c)
{
    if (!c) return ESP_ERR_INVALID_ARG;

    esp_err_t e = esp_http_client_open(c, c->post_set ? (int)c->post_len : -1);
    if (e != ESP_OK) return e;                     /* ERROR already out */

    if (c->post_set && c->post_len) {
        if (esp_http_client_write(c, (const char *)c->post,
                                  (int)c->post_len) < 0) {
            vb_error(c);
            esp_http_client_close(c);
            return ESP_FAIL;
        }
    }

    /* fetch_headers' return is the Content-Length, which is -1 for
     * close-delimited responses — parse status is tracked separately. */
    if (!c->headers_done && read_response_head(c) != ESP_OK) {
        vb_error(c);
        esp_http_client_close(c);
        return ESP_FAIL;
    }

    /* drain the body, delivering every chunk as ON_DATA */
    int bufsz = vb_bufsz(c);
    char *buf = malloc((size_t)bufsz);
    if (!buf) { esp_http_client_close(c); return ESP_ERR_NO_MEM; }
    for (;;) {
        int n = pump(c, buf, bufsz);
        if (n < 0) {
            free(buf);
            vb_error(c);
            esp_http_client_close(c);
            return ESP_FAIL;
        }
        if (n == 0) break;
        vb_evt(c, HTTP_EVENT_ON_DATA, buf, n, NULL, NULL);
    }
    free(buf);

    c->complete = true;
    vb_evt(c, HTTP_EVENT_ON_FINISH, NULL, 0, NULL, NULL);
    esp_http_client_close(c);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Accessors / lifecycle                                               */
/* ------------------------------------------------------------------ */
int esp_http_client_get_status_code(esp_http_client_handle_t c)
{
    return c ? c->status : -1;
}

int64_t esp_http_client_get_content_length(esp_http_client_handle_t c)
{
    return c ? c->content_length : -1;
}

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c)
{
    return c ? c->complete : false;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    if (!cfg) return NULL;
    struct esp_http_client *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd             = -1;
    c->status         = -1;
    c->content_length = -1;
    c->rx             = RX_NONE;
    c->method         = cfg->method;
    c->timeout_ms     = cfg->timeout_ms;
    c->disable_auto_redirect = cfg->disable_auto_redirect;
    c->buffer_size    = cfg->buffer_size;
    c->buffer_size_tx = cfg->buffer_size_tx;
    c->user_agent     = cfg->user_agent;
    c->auth_type      = cfg->auth_type;
    c->transport      = cfg->transport_type;
    c->is_async       = cfg->is_async;
    c->keep_alive_enable = cfg->keep_alive_enable;
    c->cert_pem       = cfg->cert_pem;
    c->crt_attach     = cfg->crt_bundle_attach;
    c->handler        = cfg->event_handler;
    c->user_data      = cfg->user_data;
    strcpy(c->path, "/");
    c->port = 80;

    if (cfg->url) {
        if (parse_url(c, cfg->url) != ESP_OK) { free(c); return NULL; }
        c->url = strdup(cfg->url);
    } else if (cfg->host) {
        snprintf(c->host, sizeof(c->host), "%s", cfg->host);
        if (cfg->port > 0) c->port = cfg->port;
        if (cfg->path) snprintf(c->path, sizeof(c->path), "%s", cfg->path);
        if (cfg->username) snprintf(c->username, sizeof(c->username), "%s",
                                    cfg->username);
        if (cfg->password) snprintf(c->password, sizeof(c->password), "%s",
                                    cfg->password);
        if (cfg->transport_type == HTTP_TRANSPORT_OVER_SSL) {
            c->https = true;
            if (!cfg->port) c->port = 443;
        }
    } else {
        free(c);
        return NULL;
    }
    if (cfg->query) {
        size_t l = strlen(c->path);
        snprintf(c->path + l, sizeof(c->path) - l, "?%s", cfg->query);
    }
    return c;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    if (c->fd >= 0) { shutdown(c->fd, SHUT_RDWR); close(c->fd); }
    for (int i = 0; i < c->n_hdrs; i++) { free(c->hdrs[i].k); free(c->hdrs[i].v); }
    free(c->hdrs);
    free(c->post);
    free(c->url);
    free(c);
    return ESP_OK;
}
