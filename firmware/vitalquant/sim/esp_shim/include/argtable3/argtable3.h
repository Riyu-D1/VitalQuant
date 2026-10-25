/* argtable3/argtable3.h — host-sim stub.
 *
 * Nothing in firmware/vitalquant includes argtable3 (verified): vq_console
 * commands parse argv by hand, so esp_console_cmd_t.argtable stays NULL.
 * This stub exists only so shim-side code that references the IDF type
 * names compiles.
 */
#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct arg_hdr;
struct arg_end;

int  arg_parse(int argc, char **argv, void **argtable);
void arg_print_errors(FILE *a_stream, struct arg_end *end, const char *progname);

#ifdef __cplusplus
}
#endif
