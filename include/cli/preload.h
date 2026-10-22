#ifndef ANT_CLI_PRELOAD_H
#define ANT_CLI_PRELOAD_H

#include "types.h"

#define ANT_OPTIONS_ENV "ANT_OPTIONS"

char **ant_options_split(const char *src, int *count);
int ant_preload_run(ant_t *js, const char *const *files, int count);

#endif
