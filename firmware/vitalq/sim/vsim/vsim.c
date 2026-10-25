/* vsim — scenario runtime: boots the real firmware on a thread,
 * captures console output, drives commands, owns the sleep checkpoint.
 */
#include "vsim.h"
#include "vboard.h"
#include "esp_sleep.h"
#include "esp_log.h"

#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- globals other shims weak-reference -------------------------- */
int  g_vsim_failures;
int  g_vsim_cloud_port;
vsim_sleep_state_t g_vsim_sleep;              /* strong def (sleep shim is weak) */

/* g_vsim_sleep_reboot_jb / g_vsim_sleep_reboot_armed are defined by the
 * sleep shim (esp_shim/sleep/sleep_sim.c); declared extern in esp_sleep.h. */

static pthread_t     s_fw_thread;
static volatile int  s_fw_running;

/* ---- console capture ring ---------------------------------------- */
#define CAP_SZ (1u << 20)
static char          s_cap[CAP_SZ];
static size_t        s_cap_head;              /* write pos (monotonic) */
static pthread_mutex_t s_cap_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_cap_cv  = PTHREAD_COND_INITIALIZER;

/* Strong override of the console shim's weak hook. */
void vsim_console_out(const char *data, size_t len)
{
    pthread_mutex_lock(&s_cap_mtx);
    for (size_t i = 0; i < len; i++) {
        s_cap[s_cap_head % CAP_SZ] = data[i];
        s_cap_head++;
    }
    pthread_cond_broadcast(&s_cap_cv);
    pthread_mutex_unlock(&s_cap_mtx);
}

static void cap_copy_range(size_t from, char *out, size_t out_len)
{
    if (!out || !out_len) return;
    size_t avail = s_cap_head - from;
    size_t n = avail < out_len - 1 ? avail : out_len - 1;
    for (size_t i = 0; i < n; i++)
        out[i] = s_cap[(from + i) % CAP_SZ];
    out[n] = 0;
}

static int cap_has_marker(size_t from, int *rc_out)
{
    /* scan for "===CMD-END rc=%d===" after position `from` */
    for (size_t i = from; i + 16 < s_cap_head; i++) {
        if (s_cap[i % CAP_SZ] == '=') {
            char buf[40]; size_t j = 0;
            while (i + j < s_cap_head && j < sizeof(buf) - 1) {
                buf[j] = s_cap[(i + j) % CAP_SZ]; j++;
            }
            buf[j] = 0;
            int rc;
            if (sscanf(buf, "===CMD-END rc=%d===", &rc) == 1) {
                *rc_out = rc;
                return 1;
            }
            if (strncmp(buf, "===CMD-END", 10) == 0) { *rc_out = -999; return 1; }
        }
    }
    return 0;
}

/* ---- command injection ------------------------------------------- */
/* Commands reach the REPL via a self-pipe installed over fd 0 at boot —
 * stdin from the shell is replaced so scenarios own command injection. */
static int s_cmd_wr = -1;

int vsim_cmd(const char *cmd, char *out, size_t out_len, int timeout_ms)
{
    pthread_mutex_lock(&s_cap_mtx);
    size_t from = s_cap_head;
    pthread_mutex_unlock(&s_cap_mtx);

    if (s_cmd_wr < 0) return -1;
    char line[512];
    snprintf(line, sizeof(line), "%s\n", cmd);
    if (write(s_cmd_wr, line, strlen(line)) < 0) return -1;

    int rc = -2;
    int64_t deadline = vboard_now_us() + (int64_t)timeout_ms * 1000;
    for (;;) {
        pthread_mutex_lock(&s_cap_mtx);
        int found = cap_has_marker(from, &rc);
        if (!found && vboard_now_us() < deadline) {
            struct timespec ts = {0, 2 * 1000 * 1000};
            pthread_cond_timedwait(&s_cap_cv, &s_cap_mtx, &ts);
        }
        if (found || vboard_now_us() >= deadline) {
            if (out) cap_copy_range(from, out, out_len);
            pthread_mutex_unlock(&s_cap_mtx);
            return found ? rc : -3;     /* -3 = timeout */
        }
        pthread_mutex_unlock(&s_cap_mtx);
    }
}

/* ---- firmware thread --------------------------------------------- */
static void *fw_thread(void *arg)
{
    (void)arg;
    for (;;) {
        g_vsim_sleep_reboot_armed = 1;
        if (setjmp(g_vsim_sleep_reboot_jb) != 0) {
            /* woke from deep sleep: re-enter as a warm reboot */
            fprintf(stderr, "[VSIM] re-entering fw_app_main post-wake\n");
        }
        fw_app_main();
        /* app_main returning = firmware exited loop; treat as done */
        fprintf(stderr, "[VSIM] fw_app_main returned\n");
        s_fw_running = 0;
        return NULL;
    }
}

void vsim_boot(void)
{
    /* Install the command pipe BEFORE the fw thread spawns the console
     * REPL — the REPL's fgets(stdin) then reads what vsim_cmd writes.  */
    int p[2];
    if (pipe(p) == 0) {
        dup2(p[0], STDIN_FILENO);
        close(p[0]);
        s_cmd_wr = p[1];
    }
    vboard_init();
    vboard_tick_start(500);
    s_fw_running = 1;
    pthread_create(&s_fw_thread, NULL, fw_thread, NULL);
    pthread_detach(s_fw_thread);
}

void vsim_reset(void)
{
    g_vsim_sleep_reboot_armed = 0;
    vboard_reset_all();
    vsim_sleep_state_clear();
}

void vsim_advance_us(int64_t virt_us)
{
    vboard_clock_sleep_us(virt_us);
}
