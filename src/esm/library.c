#include "esm/library.h"
#include "internal.h"
#include "loader_internal.h"
#include <string.h>

#define ANT_LIBRARY(scope, name, fn) ant_value_t fn(ant_t *js);
#include "esm/libraries.h"

#define LIBRARY_PREFIX_ANT "ant:"
#define LIBRARY_PREFIX_NODE "node:"
#define LIBRARY_STANDARD_ANT false
#define LIBRARY_STANDARD_NODE true

static const ant_library_entry_t libraries[] = {
  #define ANT_LIBRARY(scope, name, fn) \
    { name, LIBRARY_PREFIX_##scope name, fn, sizeof(name) - 1, LIBRARY_STANDARD_##scope },
  #include "esm/libraries.h"
};

_Static_assert(sizeof(libraries) / sizeof(libraries[0]) == ANT_LIBRARY_COUNT, "library count");

static inline ant_library_ns_t *library_ns(ant_t *js, const ant_library_entry_t *lib) {
  return &js->libraries[lib - libraries];
}

static bool library_strip_prefix(const char **specifier, size_t *spec_len, const char *prefix, size_t prefix_len) {
  if (*spec_len < prefix_len || memcmp(*specifier, prefix, prefix_len) != 0) return false;
  *specifier += prefix_len;
  *spec_len -= prefix_len;
  return true;
}

ant_value_t ant_library_namespace(ant_t *js, const ant_library_entry_t *lib) {
  ant_library_ns_t *cache = library_ns(js, lib);
  if (is_object_type(cache->esm_ns)) return cache->esm_ns;

  ant_value_t exports = ant_library_load(js, lib);
  if (is_err(exports) || !is_object_type(exports)) return exports;

  ant_value_t default_value = js_get_slot(exports, SLOT_DEFAULT);
  if (vtype(default_value) == kTypeUndefined) default_value = exports;

  ant_value_t ns = esm_value_namespace(js, exports, default_value);
  if (is_err(ns)) return ns;

  js_set_slot(ns, SLOT_MODULE_LOADING, Ant_Value_Undefined);
  js_set_slot_wb(js, ns, SLOT_MODULE_CTX, js_get_slot(exports, SLOT_MODULE_CTX));

  cache->esm_ns = ns;
  return ns;
}

const ant_library_entry_t *ant_library_find(const char *specifier, size_t spec_len, ant_builtin_name_kind_t *kind) {
  ant_builtin_name_kind_t found = ANT_BUILTIN_NAME_BARE;

  if (library_strip_prefix(&specifier, &spec_len, LIBRARY_PREFIX_NODE, sizeof(LIBRARY_PREFIX_NODE) - 1))
    found = ANT_BUILTIN_NAME_NODE;
  else if (library_strip_prefix(&specifier, &spec_len, LIBRARY_PREFIX_ANT, sizeof(LIBRARY_PREFIX_ANT) - 1))
    found = ANT_BUILTIN_NAME_ANT;

  for (size_t i = 0; i < ANT_LIBRARY_COUNT; i++) {
    const ant_library_entry_t *lib = &libraries[i];
    if (!lib->standard && found != ANT_BUILTIN_NAME_ANT) continue;
    if (lib->name_len != spec_len || memcmp(lib->name, specifier, spec_len) != 0) continue;
    if (kind) *kind = found;
    return lib;
  }

  return NULL;
}

void ant_library_foreach(ant_library_iter_fn cb, void *userdata) {
  for (size_t i = 0; i < ANT_LIBRARY_COUNT; i++)
    if (libraries[i].standard) cb(libraries[i].name, userdata);
}

const char *ant_library_name(const ant_library_entry_t *lib) {
  return lib->display_name;
}

ant_value_t ant_library_load(ant_t *js, const ant_library_entry_t *lib) {
  ant_library_ns_t *cache = library_ns(js, lib);
  if (cache->loaded) return cache->ns;

  ant_value_t ns = lib->init_fn(js);
  if (is_err(ns)) return ns;
  cache->ns = ns;

  if (is_object_type(ns)) {
    ant_value_t module_ctx = js_create_module_context(js, lib->display_name, false);
    if (is_err(module_ctx)) return module_ctx;
    js_module_ctx_link_namespace(js, module_ctx, ns);
  }

  cache->loaded = true;
  return ns;
}
