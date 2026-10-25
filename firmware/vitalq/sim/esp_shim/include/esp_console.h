/* esp_console.h — host-sim shim of the ESP-IDF esp_console API.
 *
 * On target the REPL sits on USB-Serial/JTAG (GPIO19/20); in the host
 * sim it sits on process stdin/stdout so a scenario drives EVERY console
 * command by writing lines to the sim's stdin — a pipe is fine:
 *
 *     printf "selftest all\nstatus\n" | ./build/my_scenario
 *
 * REPL model: esp_console_start_repl() spawns ONE reader thread doing
 *
 *     fgets(stdin) -> esp_console_run(line)
 *
 * stdin EOF ends the reader thread quietly — it NEVER exits the process
 * (unlike target behaviour where killing the console needs esp_restart).
 *
 * Command handlers use plain printf; output goes to the real stdout.
 *
 * Scenario sync / capture (SIM_HOST_BUILD builds, i.e. always in sim):
 *  - after every dispatched line the shim writes
 *        ===CMD-END rc=%d===
 *    on its own line to stdout (and through vsim_console_out).  rc is the
 *    command's return value; when the line could not dispatch it is the
 *    esp_err_t instead (e.g. 261 = ESP_ERR_NOT_FOUND for a typo'd name).
 *  - while a command executes, fd-1 writes are tee'd: everything printed
 *    still reaches stdout AND is replayed through vsim_console_out().
 *    sim/vsim provides the strong definition (capture for vsim_cmd);
 *    the weak default in console_sim.c is a no-op.
 *  - no echo: piped input is not echoed back (keeps captures clean);
 *    prompt + CMD-END markers provide the sync points instead.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- *
 *  Command registry
 * --------------------------------------------------------------- */

typedef int (*esp_console_cmd_func_t)(int argc, char **argv);

typedef struct {
    const char           *command;   /* command name (no spaces)      */
    const char           *help;      /* help text for 'help' listing  */
    const char           *hint;      /* argument hint, may be NULL    */
    esp_console_cmd_func_t func;     /* handler                       */
    void                 *argtable;  /* unused in sim (argtable3)     */
} esp_console_cmd_t;

typedef struct {
    size_t max_cmdline_length;       /* line buffer, bytes            */
    size_t max_cmdline_args;         /* argv cap                      */
    int    hint_color;               /* kept for parity; unused       */
    int    hint_bold;                /* kept for parity; unused       */
} esp_console_config_t;

#define ESP_CONSOLE_CONFIG_DEFAULT()           \
    {                                          \
        .max_cmdline_length = 256,             \
        .max_cmdline_args   = 32,              \
        .hint_color         = -1,              \
        .hint_bold          = 0,               \
    }

esp_err_t esp_console_init(const esp_console_config_t *config);
esp_err_t esp_console_deinit(void);
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd);
esp_err_t esp_console_run(const char *cmdline, int *cmd_ret);

/* Builtin 'help' — lists every registered command (name, hint, help). */
esp_err_t esp_console_register_help_command(void);

typedef enum {
    ESP_CONSOLE_HELP_VERBOSE_LEVEL_0 = 0,
    ESP_CONSOLE_HELP_VERBOSE_LEVEL_1,
    ESP_CONSOLE_HELP_VERBOSE_LEVEL_2,
} esp_console_help_verbose_level_t;
esp_err_t esp_console_set_help_verbose_level(esp_console_help_verbose_level_t level);

/* --------------------------------------------------------------- *
 *  REPL
 * --------------------------------------------------------------- */

typedef struct esp_console_repl esp_console_repl_t;   /* opaque */

typedef struct {
    uint32_t    max_history_len;     /* linenoise history cap (stub)  */
    const char *history_save_path;   /* unused in sim                 */
    uint32_t    task_stack_size;     /* accepted, ignored (host stack)*/
    uint32_t    task_priority;       /* accepted, ignored (host sched)*/
    const char *prompt;              /* printed before each line read */
    size_t      max_cmdline_length;  /* line buffer, bytes            */
    uint32_t    task_core_id;        /* parity with newer IDF; unused */
} esp_console_repl_config_t;

#define ESP_CONSOLE_REPL_CONFIG_DEFAULT()        \
    {                                            \
        .max_history_len     = 32,               \
        .history_save_path   = NULL,             \
        .task_stack_size     = 4096,             \
        .task_priority       = 2,                \
        .prompt              = "esp> ",          \
        .max_cmdline_length  = 256,              \
        .task_core_id        = 0,                \
    }

typedef struct {
    uint32_t tx_buffer_size;         /* parity only; sim uses stdio   */
    uint32_t rx_buffer_size;
} esp_console_dev_usb_serial_jtag_config_t;

#define ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT() \
    {                                                    \
        .tx_buffer_size = 256,                           \
        .rx_buffer_size = 256,                           \
    }

typedef struct {
    int      channel;                /* parity only                   */
    uint32_t baud_rate;
    int      tx_gpio_num;
    int      rx_gpio_num;
} esp_console_dev_uart_config_t;

#define ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT() \
    {                                         \
        .channel     = 0,                     \
        .baud_rate   = 115200,                \
        .tx_gpio_num = -1,                    \
        .rx_gpio_num = -1,                    \
    }

typedef struct {
    uint32_t _reserved;
} esp_console_dev_usb_cdc_config_t;

#define ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT() { ._reserved = 0 }

/* All three constructors build the same stdin/stdout REPL in the sim;
 * the "device" config is accepted and ignored (stdio is always the dev). */
esp_err_t esp_console_new_repl_usb_serial_jtag(
    const esp_console_dev_usb_serial_jtag_config_t *dev_config,
    const esp_console_repl_config_t *repl_config,
    esp_console_repl_t **ret_repl);
esp_err_t esp_console_new_repl_uart(
    const esp_console_dev_uart_config_t *dev_config,
    const esp_console_repl_config_t *repl_config,
    esp_console_repl_t **ret_repl);
esp_err_t esp_console_new_repl_usb_cdc(
    const esp_console_dev_usb_cdc_config_t *dev_config,
    const esp_console_repl_config_t *repl_config,
    esp_console_repl_t **ret_repl);

/* Spawns the reader thread (fgets(stdin) -> esp_console_run per line).
 * Only ONE live REPL reader may exist at a time — a second start fails
 * ESP_ERR_INVALID_STATE (stdin cannot be meaningfully shared).        */
esp_err_t esp_console_start_repl(esp_console_repl_t *repl);

/* --------------------------------------------------------------- *
 *  Sim-only capture hook (strong def lives in sim/vsim).
 * --------------------------------------------------------------- */
#ifdef SIM_HOST_BUILD
/* Called with every chunk the console emits: tee'd command output,
 * prompts, REPL error lines, and the ===CMD-END rc=%d=== markers.
 * vsim defines this strongly to implement vsim_cmd()'s capture.       */
void vsim_console_out(const char *data, size_t len);
#endif

#ifdef __cplusplus
}
#endif
