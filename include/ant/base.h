#ifndef ANT_API_BASE_H
#define ANT_API_BASE_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

#include "types.h"
#include "common.h"
#include "value.h"

static constexpr uint8_t CFUNC_HAS_PROTOTYPE  = 0x01u;
static constexpr uint8_t CFUNC_IS_CONSTRUCTOR = 0x02u;
static constexpr uint8_t CFUNC_NATIVE_GETTER  = 0x04u;
static constexpr uint8_t CFUNC_REALM_FREE     = 0x08u;

#define ANT_ASSERT(cond, msg) ({                                        \
if (!(cond)) {                                                          \
  fprintf(stderr, "ANT FATAL: %s\n  %s:%d\n", msg, __FILE__, __LINE__); \
  abort();                                                              \
}})

#define ANT_STRING(s)         js_mkstr(js, s, sizeof(s) - 1)
#define ANT_COPY(buf, len, s) cpy(buf, len, s, sizeof(s) - 1)
#define REMAIN(n, len)        ((n) >= (len) ? 0 : (len) - (n))

#define JS_NAN     ((double)NAN)
#define JS_INF     ((double)INFINITY)
#define JS_NEG_INF ((double)(-INFINITY))

#endif
