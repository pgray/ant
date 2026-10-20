#ifndef ANT_BOOTSTRAP_H
#define ANT_BOOTSTRAP_H

#include "types.h"

void ant_bootstrap_modules(ant_t *js);
void ant_bootstrap_realm_intrinsics(ant_t *js);
bool ant_bootstrap_bare(ant_t *js);

#endif
