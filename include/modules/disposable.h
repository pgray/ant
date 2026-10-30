#ifndef ANT_MODULES_DISPOSABLE_H
#define ANT_MODULES_DISPOSABLE_H

#include "internal.h"

void js_disposable_stack_init(ant_t *js, ant_value_t object_proto, ant_value_t function_proto);

#endif
