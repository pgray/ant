#include "internal.h"
#include "gc.h"
#include "gc/roots.h"
#include "esm/library.h"
#include "bootstrap.h"
#include "runtime.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ant_value_t foreign_ns;
static bool saw_foreign;

static void CheckRoot(ant_t *js, ant_value_t value) {
  (void)js;
  if (value == foreign_ns) saw_foreign = true;
}

static char *argv0[] = { "test-isolate-libraries", NULL };

static ant_t *Create(void) {
  ant_t *js = ant_create(nullptr);
  assert(js);
  js_setstackbase(js, NULL);
  ant_runtime_init(js, 1, argv0, NULL);
  ant_bootstrap_modules(js);
  assert(!Ant_Exception_Pending(js));
  return js;
}

static const ant_library_entry_t *Find(void) {
  const char *name = "node:string_decoder";
  ant_builtin_name_kind_t kind = ANT_BUILTIN_NAME_NONE;
  const ant_library_entry_t *lib = ant_library_find(name, strlen(name), &kind);
  assert(lib && kind == ANT_BUILTIN_NAME_NODE);
  assert(ant_library_find("string_decoder", 14, NULL) == lib);
  assert(ant_library_find("ant:string_decoder", 18, NULL) == lib);
  assert(!ant_library_find("node:ffi", 8, NULL));
  return lib;
}

static ant_value_t Load(ant_t *js) {
  ant_value_t ns = ant_library_load(js, Find());
  assert(is_object_type(ns));
  assert(ant_library_load(js, Find()) == ns);
  ant_value_t esm = ant_library_namespace(js, Find());
  assert(is_object_type(esm));
  assert(ant_library_namespace(js, Find()) == esm);
  return ns;
}

int main(void) {
  for (int destroy_first = 0; destroy_first < 2; destroy_first++) {
    ant_t *first = Create();
    ant_t *second = Create();

    ant_value_t a = Load(first);
    ant_value_t b = Load(second);
    assert(a != b);

    foreign_ns = b;
    saw_foreign = false;
    gc_visit_roots(first, CheckRoot);
    assert(!saw_foreign);

    ant_t *survivor = destroy_first ? second : first;
    ant_value_t kept = destroy_first ? b : a;
    js_destroy(destroy_first ? first : second);
    gc_run(survivor);
    gc_run(survivor);
    assert(Load(survivor) == kept);
    js_destroy(survivor);
  }

  puts("PASS registered library namespaces are per isolate");
}
