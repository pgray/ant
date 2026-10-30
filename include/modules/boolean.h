#ifndef ANT_MODULES_BOOLEAN_H
#define ANT_MODULES_BOOLEAN_H

#include "internal.h"

void js_boolean_init_prototype(ant_t *js, ant_value_t boolean_proto);
void js_boolean_init_constructor(ant_t *js, ant_value_t bool_ctor_obj, ant_value_t boolean_proto);

#endif
