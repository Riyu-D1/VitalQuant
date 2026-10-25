/* vsim — host-side scenario API for running the real firmware.
 *
 * A scenario binary links: all firmware components + esp_shim + vboard.
 * main() of the firmware (app_main) is renamed at compile time to
 * fw_app_main() via -Dapp_main=fw_app_main, so tests own main().
 */
#ifndef VSIM_H
#define VSIM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The firmware's real entry point (renamed by the build). */
void fw_app_main(void);

/* Scenario lifecycle */
void vsim_boot(void);          /* vboard_init + fw_app_main on its thread */
void vsim_reset(void);         /* tear down tasks; vboard_reset_all; reboot */

/* Console driving — the esp_console REPL sits on sim stdin/stdout.
 * vsim_cmd() injects a command line and returns captured output.     */
int  vsim_cmd(const char *cmd, char *out, size_t out_len, int timeout_ms);

/* Sleep interception — esp_deep_sleep_start() must NOT kill the
 * process in sim.  The shim records what the firmware armed and
 * either (a) simulates a wake → esp_restart path, or (b) exits so the
 * harness can inspect.  Controlled per-scenario.                   */
typedef struct {
    bool     armed_ext0, armed_ext1;
    uint64_t ext1_mask;
    int      ext0_gpio;
    int      holds[64]; int n_holds;    /* pins with gpio_hold_en      */
    bool     deep_sleep_entered;
} vsim_sleep_state_t;
extern vsim_sleep_state_t g_vsim_sleep;
void  vsim_sleep_wake_with_gpio(int gpio);  /* inject the wake source   */

/* Fast-forward helper: run sim for virtual-µs, letting tick/timer
 * threads advance async device state.  Sleeps wall-clock accordingly. */
void  vsim_advance_us(int64_t virt_us);

/* Mock-cloud control: the http shim connects to 127.0.0.1:g_vsim_cloud_port.
 * Scenario starts mock_server.py first, sets the port + URL env.     */
extern int g_vsim_cloud_port;

/* Test reporting */
#define VSIM_CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
                   g_vsim_failures++; } \
} while (0)
extern int g_vsim_failures;

#endif /* VSIM_H */
