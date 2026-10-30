#include "modules/host.h"

#define HOST_HOOK(ret, name, params, stub) ret name params { return stub; }
#include "modules/host_hooks.h"
