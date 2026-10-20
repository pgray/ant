#ifndef ANT_ISOLATE_MODULE_H
#define ANT_ISOLATE_MODULE_H

#include "types.h"

ant_value_t isolate_library(ant_t *js);
void cleanup_isolate_module(ant_t *js);

#endif
