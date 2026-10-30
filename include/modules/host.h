#ifndef ANT_MODULES_HOST_H
#define ANT_MODULES_HOST_H

#include <stdbool.h>

#include "types.h"
#include "gc/modules.h"

#define HOST_HOOK(ret, name, params, stub) ret name params;
#include "modules/host_hooks.h"

#endif
