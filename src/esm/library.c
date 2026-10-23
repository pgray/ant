#include "esm/library.h"
#include "internal.h"
#include "loader_internal.h"
#include "sync.h"

#include <string.h>
#include <uthash.h>

static uint32_t library_slot_count = 0;
static ant_library_entry_t *library_registry = NULL;
static ant_mutex_t library_lock = ANT_MUTEX_INIT;

static ant_library_entry_t *library_add(
  ant_library_init_fn init_fn, const char *name,
  ant_builtin_name_kind_t kind, ant_library_entry_t *canonical
) {
  ant_library_entry_t *lib = NULL;
  HASH_FIND_STR(library_registry, name, lib);
  if (lib) return lib->canonical;

  lib = (ant_library_entry_t *)calloc(1, sizeof(ant_library_entry_t));
  if (!lib) return canonical;

  strncpy(lib->name, name, sizeof(lib->name) - 1);
  lib->init_fn = init_fn;
  lib->kind = kind;

  if (!canonical) {
    canonical = lib;
    lib->slot = library_slot_count++;
  }
  
  lib->canonical = canonical;

  if (kind == ANT_BUILTIN_NAME_NODE || !canonical->display_name[0])
    strncpy(canonical->display_name, name, sizeof(canonical->display_name) - 1);

  HASH_ADD_STR(library_registry, name, lib);
  return canonical;
}

void ant_register_library(ant_library_init_fn init_fn, const char *name) {
  ant_mutex_lock(&library_lock);
  library_add(init_fn, name, ANT_BUILTIN_NAME_ANT, NULL);
  ant_mutex_unlock(&library_lock);
}

void ant_register_standard_library(ant_library_init_fn init_fn, const char *bare, const char *ant_name, const char *node_name) {
  ant_mutex_lock(&library_lock);
  ant_library_entry_t *canonical = library_add(init_fn, bare, ANT_BUILTIN_NAME_BARE, NULL);
  library_add(init_fn, ant_name, ANT_BUILTIN_NAME_ANT, canonical);
  library_add(init_fn, node_name, ANT_BUILTIN_NAME_NODE, canonical);
  ant_mutex_unlock(&library_lock);
}

static ant_library_ns_t *library_ns(ant_t *js, const ant_library_entry_t *canon) {
  if (canon->slot >= js->libraries.cap) {
    uint32_t cap = js->libraries.cap ? js->libraries.cap : 64;
    while (cap <= canon->slot) cap *= 2;
    
    ant_library_ns_t *next = realloc(js->libraries.entries, cap * sizeof(*next));
    if (!next) return NULL;
    
    memset(next + js->libraries.cap, 0, (cap - js->libraries.cap) * sizeof(*next));
    js->libraries.entries = next;
    js->libraries.cap = cap;
  }
  
  return &js->libraries.entries[canon->slot];
}

void ant_library_cache_free(ant_t *js) {
  free(js->libraries.entries);
  js->libraries.entries = NULL;
  js->libraries.cap = 0;
}

ant_value_t ant_library_namespace(ant_t *js, ant_library_entry_t *lib) {
  ant_library_entry_t *canon = lib->canonical;
  ant_library_ns_t *cache = library_ns(js, canon);
  
  if (!cache) return Ant_Error_CreateOutOfMemory(js);
  if (is_object_type(cache->esm_ns)) return cache->esm_ns;

  ant_value_t exports = ant_library_load(js, lib);
  if (is_err(exports) || !is_object_type(exports)) return exports;

  ant_value_t default_value = js_get_slot(exports, SLOT_DEFAULT);
  if (vtype(default_value) == kTypeUndefined) default_value = exports;

  ant_value_t ns = esm_value_namespace(js, exports, default_value);
  if (is_err(ns)) return ns;
  
  js_set_slot(ns, SLOT_MODULE_LOADING, js_mkundef());
  js_set_slot_wb(js, ns, SLOT_MODULE_CTX, js_get_slot(exports, SLOT_MODULE_CTX));
  
  cache = library_ns(js, canon);
  cache->esm_ns = ns;
  
  return ns;
}

ant_library_entry_t *ant_library_find(const char *specifier, size_t spec_len) {
  ant_library_entry_t *lib = NULL;
  char key[256];

  if (spec_len >= sizeof(key) || memchr(specifier, 0, spec_len)) return NULL;
  memcpy(key, specifier, spec_len);
  key[spec_len] = '\0';

  ant_mutex_lock(&library_lock);
  HASH_FIND_STR(library_registry, key, lib);
  ant_mutex_unlock(&library_lock);
  return lib;
}

void ant_library_foreach(ant_library_iter_fn cb, void *userdata) {
  ant_library_entry_t *lib, *tmp;
  ant_mutex_lock(&library_lock);
  HASH_ITER(hh, library_registry, lib, tmp) if (lib->kind == ANT_BUILTIN_NAME_BARE) cb(lib->name, userdata);
  ant_mutex_unlock(&library_lock);
}

ant_builtin_name_kind_t ant_library_kind(const ant_library_entry_t *lib) {
  return lib->kind;
}

const char *ant_library_name(const ant_library_entry_t *lib) {
  return lib->canonical->display_name;
}

ant_value_t ant_library_load(ant_t *js, ant_library_entry_t *lib) {
  ant_library_entry_t *canon = lib->canonical;
  ant_library_ns_t *cache = library_ns(js, canon);
  
  if (!cache) return Ant_Error_CreateOutOfMemory(js);
  if (cache->loaded) return cache->ns;

  ant_value_t ns = canon->init_fn(js);
  cache = library_ns(js, canon);
  cache->ns = ns;
  
  if (is_object_type(ns)) {
    ant_value_t module_ctx = js_create_module_context(js, canon->display_name, false);
    if (is_err(module_ctx)) return module_ctx;
    js_module_ctx_link_namespace(js, module_ctx, ns);
  }

  library_ns(js, canon)->loaded = true;
  return ns;
}
