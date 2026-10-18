#ifndef ESM_LIBRARY_H
#define ESM_LIBRARY_H

#include "types.h"
#include <uthash.h>
#include <stdbool.h>
#include <stddef.h>

typedef ant_value_t (*ant_library_init_fn)(ant_t *js);
typedef void (*ant_library_iter_fn)(const char *name, void *userdata);

typedef struct ant_library_ns {
  ant_value_t ns;
  ant_value_t esm_ns;
  bool loaded;
} ant_library_ns_t;

typedef enum {
  ANT_BUILTIN_NAME_NONE,
  ANT_BUILTIN_NAME_ANT,   // ant:x, Ant-only
  ANT_BUILTIN_NAME_NODE,  // node:x
  ANT_BUILTIN_NAME_BARE,  // x
} ant_builtin_name_kind_t;

// TODO: optimize
typedef struct ant_library_entry {
  char name[256];
  char display_name[256];
  ant_library_init_fn init_fn;
  ant_builtin_name_kind_t kind;
  uint32_t slot;
  struct ant_library_entry *canonical;
  UT_hash_handle hh;
} ant_library_entry_t;

void ant_library_cache_free(ant_t *js);
void ant_library_foreach(ant_library_iter_fn cb, void *userdata);
void ant_register_library(ant_library_init_fn init_fn, const char *name);
void ant_register_standard_library(ant_library_init_fn init_fn, const char *bare, const char *ant_name, const char *node_name);

ant_value_t ant_library_load(ant_t *js, ant_library_entry_t *lib);
ant_value_t ant_library_namespace(ant_t *js, ant_library_entry_t *lib);
ant_library_entry_t *ant_library_find(const char *specifier, size_t spec_len);

ant_builtin_name_kind_t ant_library_kind(const ant_library_entry_t *lib);
const char *ant_library_name(const ant_library_entry_t *lib);

#define ant_standard_library(name, lib) \
  ant_register_standard_library(lib, name, "ant:" name, "node:" name)

#endif
