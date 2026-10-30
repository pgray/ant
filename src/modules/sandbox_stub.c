#include "ant.h"
#include "errors.h"
#include "modules/sandbox.h"

ant_value_t sandbox_library(ant_t *js) {
  return js_mkerr(js, "ant:sandbox is not available in this build");
}

void gc_mark_sandbox(ant_t *js, gc_mark_fn mark) { (void)js; (void)mark; }
