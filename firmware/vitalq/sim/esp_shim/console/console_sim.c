/* console_sim.c — esp_console REPL on host stdin/stdout.
 *
 * Contract (see sim/esp_shim/include/esp_console.h):
 *   - one reader thread per esp_console_start_repl(): fgets(stdin),
 *     esp_console_run(line) per line; stdin may be a PIPE; EOF ends the
 *     thread quietly — never exits the process.
 *   - command output = plain printf on real stdout. While a command
 *     executes, fd-1 is tee'd into the weak vsim_console_out() hook so
 *     sim/vsim can capture for vsim_cmd() (strong def lives there).
 *   - after every dispatched (or unrecognized) line a marker
 *     ===CMD-END rc=%d=== is written to stdout + the hook, so a
 *     scenario can sync on command completion.
 *   - genuinely blocking commands just block the reader thread — by
 *     design, like the low-priority REPL task on target.
 */
#define _POSIX_C_SOURCE 200809L   /* strdup/pipe/dup2/read under -std=c11 */
#define _DEFAULT_SOURCE

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_console.h"

#include "driver/usb_serial_jtag.h"
#include "esp_vfs_console.h"
#include "linenoise/linenoise.h"

#define CONSOLE_DEFAULT_LINELEN  256
#define CONSOLE_DEFAULT_MAXARGS  32

/* ------------------------------------------------------------------ */
/* Capture hook — weak default; sim/vsim may provide the strong one.  */
/* ------------------------------------------------------------------ */
#ifdef SIM_HOST_BUILD
__attribute__((weak))
void vsim_console_out(const char *data, size_t len)
{
    (void)data; (void)len;
}
#endif

/* ------------------------------------------------------------------ */
/* Console output helper — everything the console itself prints goes  */
/* to stdout AND through the capture hook.                            */
/* ------------------------------------------------------------------ */
static void con_write_n(const char *s, size_t n)
{
    if (n) {
        fwrite(s, 1, n, stdout);
    }
    fflush(stdout);
#ifdef SIM_HOST_BUILD
    vsim_console_out(s, n);
#endif
}

static void con_write(const char *s)
{
    con_write_n(s, strlen(s));
}

static void con_writef(const char *fmt, int arg)
{
    char buf[96];
    int n = snprintf(buf, sizeof(buf), fmt, arg);
    if (n > 0) {
        con_write_n(buf, (size_t)n);
    }
}

/* End-of-command marker — the scenario sync point. */
static void con_emit_cmd_end(int rc)
{
    con_writef("\n===CMD-END rc=%d===\n", rc);
}

/* ------------------------------------------------------------------ */
/* Registry                                                           */
/* ------------------------------------------------------------------ */
typedef struct cmd_node {
    esp_console_cmd_t   cmd;
    struct cmd_node    *next;
} cmd_node_t;

static pthread_mutex_t  s_reg_mtx   = PTHREAD_MUTEX_INITIALIZER;
static cmd_node_t      *s_cmds;                       /* append order */
static esp_console_repl_t *s_repls;                   /* all repls    */
static esp_console_repl_t *s_active_repl;             /* stdin reader */
static bool             s_inited;
static size_t           s_max_args  = CONSOLE_DEFAULT_MAXARGS;
static size_t           s_max_line  = CONSOLE_DEFAULT_LINELEN;
static esp_console_help_verbose_level_t s_help_verbose =
    ESP_CONSOLE_HELP_VERBOSE_LEVEL_1;

esp_err_t esp_console_init(const esp_console_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_reg_mtx);
    if (s_inited) {
        pthread_mutex_unlock(&s_reg_mtx);
        return ESP_ERR_INVALID_STATE;   /* still up — do NOT re-register */
    }
    s_inited   = true;
    s_max_args = config->max_cmdline_args ? config->max_cmdline_args
                                          : CONSOLE_DEFAULT_MAXARGS;
    s_max_line = config->max_cmdline_length ? config->max_cmdline_length
                                            : CONSOLE_DEFAULT_LINELEN;
    pthread_mutex_unlock(&s_reg_mtx);
    return ESP_OK;
}

esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd)
{
    if (!cmd || !cmd->command || !cmd->func) {
        return ESP_ERR_INVALID_ARG;
    }
    cmd_node_t *n = malloc(sizeof(*n));
    if (!n) {
        return ESP_ERR_NO_MEM;
    }
    n->cmd  = *cmd;
    n->next = NULL;
    pthread_mutex_lock(&s_reg_mtx);
    if (!s_inited) {
        pthread_mutex_unlock(&s_reg_mtx);
        free(n);
        return ESP_ERR_INVALID_STATE;
    }
    /* Duplicate names replace the old entry (IDF refuses them; replacing
     * is friendlier for scenarios re-registering after a sim reset).   */
    cmd_node_t **pp = &s_cmds;
    while (*pp) {
        if (strcmp((*pp)->cmd.command, cmd->command) == 0) {
            n->next = (*pp)->next;
            *pp = n;
            pthread_mutex_unlock(&s_reg_mtx);
            return ESP_OK;
        }
        pp = &(*pp)->next;
    }
    *pp = n;
    pthread_mutex_unlock(&s_reg_mtx);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* argv split — whitespace-delimited, quote-aware ('...' "..."),      */
/* backslash escapes the next char inside or outside quotes.          */
/* ------------------------------------------------------------------ */
static size_t split_argv(char *s, char **argv, size_t max_args)
{
    size_t argc = 0;
    char  *p    = s;
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        argv[argc++] = p;
        char *w = p;                     /* in-place quote collapse */
        bool  in_q = false;
        char  qc   = 0;
        while (*p) {
            char c = *p;
            if (in_q) {
                if (c == qc) {
                    in_q = false;
                    p++;
                } else if (c == '\\' && p[1]) {
                    *w++ = p[1];
                    p += 2;
                } else {
                    *w++ = c;
                    p++;
                }
            } else {
                if (c == ' ' || c == '\t') {
                    break;
                }
                if (c == '"' || c == '\'') {
                    in_q = true;
                    qc   = c;
                    p++;
                } else if (c == '\\' && p[1]) {
                    *w++ = p[1];
                    p += 2;
                } else {
                    *w++ = c;
                    p++;
                }
            }
        }
        if (*p) {
            p++;                          /* skip the delimiter */
        }
        *w = '\0';
    }
    return argc;                          /* surplus words truncated */
}

/* ------------------------------------------------------------------ */
/* Command dispatch with stdout capture tee.                          */
/*                                                                    */
/* fd 1 is redirected into a pipe while the command runs; a drainer   */
/* thread streams everything to the saved real stdout AND the         */
/* vsim_console_out() hook. Output stays live (important for slow     */
/* commands like selftests), ordering is preserved, and a blocking    */
/* command simply keeps streaming — no marker is emitted until it     */
/* returns (e.g. 'power sleep' never produces one, like target).      */
/* ------------------------------------------------------------------ */
#ifdef SIM_HOST_BUILD

typedef struct {
    int rfd;
    int outfd;
} tee_ctx_t;

static void *tee_drainer(void *arg)
{
    tee_ctx_t *c = (tee_ctx_t *)arg;
    char buf[512];
    for (;;) {
        ssize_t n = read(c->rfd, buf, sizeof(buf));
        if (n <= 0) {
            break;                        /* write end closed -> EOF */
        }
        size_t off = 0;
        while (off < (size_t)n) {
            ssize_t w = write(c->outfd, buf + off, (size_t)n - off);
            if (w <= 0) {
                break;                    /* real stdout gone — drop */
            }
            off += (size_t)w;
        }
        vsim_console_out(buf, (size_t)n);
    }
    return NULL;
}

static pthread_mutex_t  s_cap_mtx = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int s_in_dispatch;   /* nested run -> no re-tee  */

/* Capture window state — torn down identically on the normal path and
 * on pthread_cancel (esp_console_deinit vsim_reset may kill a REPL
 * thread that sits mid-command holding s_cap_mtx with fd 1 redirected;
 * the cleanup handler puts stdout back and releases the mutex).      */
typedef struct {
    int       saved;
    int       rfd;
    bool      tee;
    pthread_t th;
    bool      th_started;
} cap_guard_t;

static void cap_cleanup(void *arg)
{
    cap_guard_t *g = (cap_guard_t *)arg;
    if (g->tee) {
        fflush(stdout);                    /* buffered out -> pipe      */
        if (g->saved >= 0) {
            dup2(g->saved, STDOUT_FILENO); /* write end closed -> EOF   */
        }
        if (g->th_started) {
            pthread_join(g->th, NULL);     /* drain rest + hook         */
        }
        if (g->rfd >= 0) {
            close(g->rfd);
        }
        if (g->saved >= 0) {
            close(g->saved);
        }
    } else {
        if (g->rfd >= 0) {
            close(g->rfd);
        }
        if (g->saved >= 0) {
            close(g->saved);
        }
    }
    pthread_mutex_unlock(&s_cap_mtx);
    s_in_dispatch = 0;
}

static int dispatch_cmd(esp_console_cmd_func_t fn, int argc, char **argv)
{
    if (s_in_dispatch) {
        return fn(argc, argv);               /* nested run — plain call */
    }
    s_in_dispatch = 1;
    if (pthread_mutex_lock(&s_cap_mtx) != 0) {
        int rc0 = fn(argc, argv);
        s_in_dispatch = 0;
        return rc0;
    }

    cap_guard_t g = {
        .saved = -1, .rfd = -1, .tee = false, .th_started = false
    };
    int rc;
    pthread_cleanup_push(cap_cleanup, &g);

    int p[2] = { -1, -1 };
    g.saved  = dup(STDOUT_FILENO);
    if (g.saved >= 0 && pipe(p) == 0) {
        fflush(stdout);                    /* pending stdio -> real out */
        dup2(p[1], STDOUT_FILENO);
        close(p[1]);
        g.rfd       = p[0];
        tee_ctx_t tc = { .rfd = p[0], .outfd = g.saved };
        if (pthread_create(&g.th, NULL, tee_drainer, &tc) == 0) {
            g.th_started = true;
            g.tee        = true;
        } else {
            /* No drainer thread — restore and run uncaptured rather
             * than risk blocking on a full pipe.                      */
            dup2(g.saved, STDOUT_FILENO);
            close(p[0]);
            g.rfd = -1;
        }
    } else {
        if (p[0] >= 0) {
            close(p[0]);
            close(p[1]);
        }
    }

    rc = fn(argc, argv);                     /* stdout is tee'd here   */

    pthread_cleanup_pop(1);                  /* restores fd1 + mutex   */
    return rc;
}

#else /* !SIM_HOST_BUILD */

static int dispatch_cmd(esp_console_cmd_func_t fn, int argc, char **argv)
{
    return fn(argc, argv);
}

#endif /* SIM_HOST_BUILD */

/* ------------------------------------------------------------------ */
/* esp_console_run — tokenize + dispatch + marker.                    */
/* Dispatch-failure text is emitted BEFORE the marker so a captured   */
/* window (start-of-line .. CMD-END) always contains it.              */
/* ------------------------------------------------------------------ */
esp_err_t esp_console_run(const char *cmdline, int *cmd_ret)
{
    if (!cmdline || !cmd_ret) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_reg_mtx);
    bool up = s_inited;
    pthread_mutex_unlock(&s_reg_mtx);
    if (!up) {
        return ESP_ERR_INVALID_STATE;    /* no marker — nothing ran */
    }

    char *scratch = strdup(cmdline);
    if (!scratch) {
        return ESP_ERR_NO_MEM;
    }
    char **argv = malloc((s_max_args + 1) * sizeof(*argv));
    if (!argv) {
        free(scratch);
        return ESP_ERR_NO_MEM;
    }
    size_t argc = split_argv(scratch, argv, s_max_args + 1);
    if (argc == 0) {
        free(argv);
        free(scratch);
        return ESP_ERR_INVALID_ARG;      /* empty line — no marker  */
    }

    /* Copy the command entry under the lock so a concurrent
     * register/deinit can never pull the node out mid-dispatch.      */
    esp_console_cmd_t found;
    bool have = false;
    pthread_mutex_lock(&s_reg_mtx);
    for (cmd_node_t *n = s_cmds; n; n = n->next) {
        if (strcmp(n->cmd.command, argv[0]) == 0) {
            found = n->cmd;
            have  = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_reg_mtx);

    esp_err_t err;
    if (!have) {
        err = ESP_ERR_NOT_FOUND;
        con_write("unrecognized command — try 'help'\n");
        con_emit_cmd_end((int)err);
    } else {
        int rc = dispatch_cmd(found.func, (int)argc, argv);
        *cmd_ret = rc;
        if (rc != 0) {
            con_writef("command exited with code %d\n", rc);
        }
        err = ESP_OK;
        con_emit_cmd_end(rc);
    }
    free(argv);
    free(scratch);
    return err;
}

/* ------------------------------------------------------------------ */
/* Builtin 'help'                                                     */
/* ------------------------------------------------------------------ */
/* printf->write() is a pthread cancellation point: a REPL thread killed
 * mid-'help' by deinit must not leak s_reg_mtx. */
static void reg_mtx_cleanup(void *m)
{
    pthread_mutex_unlock((pthread_mutex_t *)m);
}

static int cmd_help_builtin(int argc, char **argv)
{
    (void)argc; (void)argv;
    pthread_mutex_lock(&s_reg_mtx);
    pthread_cleanup_push(reg_mtx_cleanup, &s_reg_mtx);
    for (cmd_node_t *n = s_cmds; n; n = n->next) {
        if (n->cmd.hint) {
            printf("%s %s\n", n->cmd.command, n->cmd.hint);
        } else {
            printf("%s\n", n->cmd.command);
        }
        if (s_help_verbose > ESP_CONSOLE_HELP_VERBOSE_LEVEL_0 &&
            n->cmd.help) {
            printf("  %s\n", n->cmd.help);
        }
    }
    pthread_cleanup_pop(1);              /* unlocks s_reg_mtx */
    return 0;
}

esp_err_t esp_console_register_help_command(void)
{
    const esp_console_cmd_t c = {
        .command = "help",
        .help    = "Print the list of registered commands",
        .hint    = NULL,
        .func    = cmd_help_builtin,
        .argtable = NULL,
    };
    return esp_console_cmd_register(&c);
}

esp_err_t esp_console_set_help_verbose_level(esp_console_help_verbose_level_t level)
{
    s_help_verbose = level;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* REPL — one reader thread on stdin                                  */
/* ------------------------------------------------------------------ */
struct esp_console_repl {
    esp_console_repl_config_t cfg;
    char                     *prompt_copy;
    pthread_t                 thread;
    int                       started;   /* thread spawned            */
    volatile int              running;   /* cleared on exit/stop      */
    esp_console_repl_t       *next_all;
};

/* fgets -> esp_console_run per line.  EOF exits the thread quietly —
 * deliberately NOT the process (scenarios own the process lifetime). */
static void *repl_reader(void *arg)
{
    esp_console_repl_t *r = (esp_console_repl_t *)arg;
    size_t cap = (r->cfg.max_cmdline_length ? r->cfg.max_cmdline_length
                                            : CONSOLE_DEFAULT_LINELEN) + 1;
    char *line = malloc(cap);
    while (line && r->running) {
        if (r->prompt_copy && r->prompt_copy[0]) {
            con_write(r->prompt_copy);       /* prompt = sync point too */
        }
        if (!fgets(line, (int)cap, stdin)) {
            break;                           /* EOF — quiet exit        */
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) {
            continue;
        }
        int cmd_ret = 0;
        /* Dispatch + error text + CMD-END marker all happen inside
         * esp_console_run — the REPL itself prints nothing extra so
         * a command's transcript stays a contiguous capture window.  */
        (void)esp_console_run(line, &cmd_ret);
    }
    free(line);
    pthread_mutex_lock(&s_reg_mtx);
    r->running = 0;
    if (s_active_repl == r) {
        s_active_repl = NULL;
    }
    pthread_mutex_unlock(&s_reg_mtx);
    return NULL;
}

static esp_err_t repl_alloc(const esp_console_repl_config_t *rc,
                            esp_console_repl_t **out)
{
    if (!rc || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_console_repl_t *r = calloc(1, sizeof(*r));
    if (!r) {
        /* Mirror IDF: failed new_repl tears the console module down so
         * esp_console_init returns ESP_OK (not INVALID_STATE) after —
         * vq_console's fallback path relies on that distinction.       */
        (void)esp_console_deinit();
        return ESP_ERR_NO_MEM;
    }
    r->cfg         = *rc;
    r->prompt_copy = strdup(rc->prompt ? rc->prompt : "");
    if (!r->prompt_copy) {
        free(r);
        (void)esp_console_deinit();
        return ESP_ERR_NO_MEM;
    }
    if (r->cfg.max_cmdline_length == 0) {
        r->cfg.max_cmdline_length = s_max_line;
    }
    pthread_mutex_lock(&s_reg_mtx);
    r->next_all = s_repls;
    s_repls     = r;
    pthread_mutex_unlock(&s_reg_mtx);
    *out = r;
    return ESP_OK;
}

esp_err_t esp_console_new_repl_usb_serial_jtag(
    const esp_console_dev_usb_serial_jtag_config_t *dev_config,
    const esp_console_repl_config_t *repl_config,
    esp_console_repl_t **ret_repl)
{
    (void)dev_config;                      /* stdio is always the dev */
    return repl_alloc(repl_config, ret_repl);
}

esp_err_t esp_console_new_repl_uart(
    const esp_console_dev_uart_config_t *dev_config,
    const esp_console_repl_config_t *repl_config,
    esp_console_repl_t **ret_repl)
{
    (void)dev_config;
    return repl_alloc(repl_config, ret_repl);
}

esp_err_t esp_console_new_repl_usb_cdc(
    const esp_console_dev_usb_cdc_config_t *dev_config,
    const esp_console_repl_config_t *repl_config,
    esp_console_repl_t **ret_repl)
{
    (void)dev_config;
    return repl_alloc(repl_config, ret_repl);
}

esp_err_t esp_console_start_repl(esp_console_repl_t *repl)
{
    if (!repl) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_reg_mtx);
    if (!s_inited) {
        pthread_mutex_unlock(&s_reg_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    if (repl->started || s_active_repl) {
        pthread_mutex_unlock(&s_reg_mtx);
        return ESP_ERR_INVALID_STATE;   /* stdin has exactly one reader */
    }
    repl->running = 1;
    if (pthread_create(&repl->thread, NULL, repl_reader, repl) != 0) {
        repl->running = 0;
        pthread_mutex_unlock(&s_reg_mtx);
        return ESP_ERR_NO_MEM;
    }
    repl->started  = 1;
    s_active_repl  = repl;
    pthread_mutex_unlock(&s_reg_mtx);
    return ESP_OK;
}

static void repl_destroy(esp_console_repl_t *r)
{
    pthread_mutex_lock(&s_reg_mtx);
    esp_console_repl_t **pp = &s_repls;
    while (*pp && *pp != r) {
        pp = &(*pp)->next_all;
    }
    if (*pp) {
        *pp = r->next_all;
    }
    if (s_active_repl == r) {
        s_active_repl = NULL;
    }
    r->running = 0;
    /* Self-destroy (a console command calling esp_console_deinit) can
     * never join itself — unlink + flag only; the object leaks and the
     * reader exits at its next loop check instead of deadlocking.      */
    bool self = r->started && pthread_equal(pthread_self(), r->thread);
    bool join = r->started && !self;
    pthread_mutex_unlock(&s_reg_mtx);
    if (self) {
        return;                          /* leaked repl — thread exits */
    }
    if (join) {
        /* Thread sits in fgets->read(), a cancellation point; if it
         * already exited at EOF the join just reaps it.               */
        pthread_cancel(r->thread);
        pthread_join(r->thread, NULL);
    }
    free(r->prompt_copy);
    free(r);
}

esp_err_t esp_console_deinit(void)
{
    for (;;) {
        pthread_mutex_lock(&s_reg_mtx);
        esp_console_repl_t *r = s_repls;
        pthread_mutex_unlock(&s_reg_mtx);
        if (!r) {
            break;
        }
        repl_destroy(r);
    }
    pthread_mutex_lock(&s_reg_mtx);
    while (s_cmds) {
        cmd_node_t *n = s_cmds;
        s_cmds = n->next;
        free(n);
    }
    s_inited   = false;
    s_max_args = CONSOLE_DEFAULT_MAXARGS;
    s_max_line = CONSOLE_DEFAULT_LINELEN;
    pthread_mutex_unlock(&s_reg_mtx);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Stubs: usb_serial_jtag / esp_vfs_console / linenoise                */
/* ------------------------------------------------------------------ */

esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *cfg)
{
    (void)cfg;
    return ESP_OK;
}

esp_err_t usb_serial_jtag_driver_uninstall(void)
{
    return ESP_OK;
}

bool usb_serial_jtag_is_connected(void)
{
    return true;                  /* stdio is always "attached" */
}

int usb_serial_jtag_read_bytes(void *buf, uint32_t length,
                               uint32_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!buf || !length) {
        return 0;
    }
    return (int)fread(buf, 1, length, stdin);
}

int usb_serial_jtag_write_bytes(const void *src, uint32_t length)
{
    if (!src) {
        return -1;
    }
    size_t n = fwrite(src, 1, length, stdout);
    fflush(stdout);
    return (int)n;
}

esp_err_t usb_serial_jtag_vfs_register(void)      { return ESP_OK; }
esp_err_t usb_serial_jtag_vfs_use_driver(void)    { return ESP_OK; }
esp_err_t usb_serial_jtag_vfs_use_nonblocking(void){ return ESP_OK; }

esp_err_t esp_vfs_console_register(void)          { return ESP_OK; }

char *linenoise(const char *prompt)
{
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    char buf[512];
    if (!fgets(buf, sizeof(buf), stdin)) {
        return NULL;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    return strdup(buf);
}

int  linenoiseHistoryAdd(const char *line)             { (void)line; return 1; }
int  linenoiseHistorySetMaxLen(int len)                { return len > 0; }
int  linenoiseHistorySave(const char *path)            { (void)path; return 0; }
int  linenoiseHistoryLoad(const char *path)            { (void)path; return 0; }
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *cb)
{ (void)cb; }
void linenoiseSetHintsCallback(linenoiseHintsCallback *cb)
{ (void)cb; }
void linenoiseSetFreeHintsCallback(linenoiseFreeHintCallback *cb)
{ (void)cb; }
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str)
{ (void)lc; (void)str; }
void linenoiseSetMultiLine(int ml)                     { (void)ml; }
void linenoisePrintKeyCodes(void)                      { }
void linenoiseClearScreen(void)                        { }
void linenoiseFree(void *ptr)                          { free(ptr); }
