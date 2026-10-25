/* linenoise/linenoise.h — host-sim stub of the IDF linenoise fork.
 *
 * Signature-compatible with what the esp_console REPL calls
 * (linenoise / linenoiseHistoryAdd / linenoiseHistorySave / linenoiseFree).
 * The sim's REPL reads with plain fgets() and does not use these, but
 * they are provided so anything linking against linenoise compiles and
 * runs: linenoise() itself is a prompt+fgets implementation.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct linenoiseCompletions {
    size_t  len;
    char  **cvec;
} linenoiseCompletions;

typedef void(linenoiseCompletionCallback)(const char *, linenoiseCompletions *);
typedef char *(linenoiseHintsCallback)(const char *, int *color, int *bold);
typedef void(linenoiseFreeHintCallback)(void *);

/* Reads one line: prints prompt, fgets(stdin), strips the trailing
 * newline, returns a malloc'd string (free with linenoiseFree/free).
 * Returns NULL on stdin EOF.                                          */
char *linenoise(const char *prompt);

/* History — in-memory stubs; Save/Load touch nothing (NULL path ok). */
int  linenoiseHistoryAdd(const char *line);
int  linenoiseHistorySetMaxLen(int len);
int  linenoiseHistorySave(const char *path);
int  linenoiseHistoryLoad(const char *path);

void linenoiseSetCompletionCallback(linenoiseCompletionCallback *cb);
void linenoiseSetHintsCallback(linenoiseHintsCallback *cb);
void linenoiseSetFreeHintsCallback(linenoiseFreeHintCallback *cb);
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str);
void linenoiseSetMultiLine(int ml);
void linenoisePrintKeyCodes(void);
void linenoiseClearScreen(void);
void linenoiseFree(void *ptr);

#ifdef __cplusplus
}
#endif
