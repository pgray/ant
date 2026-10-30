#include <stdlib.h>
#include <string.h>
#include <uthash.h>

#include "ant.h"
#include "cfunc.h"
#include "errors.h"
#include "internal.h"
#include "descriptors.h"

#include "modules/sessionstorage.h"
#include "sync.h"

typedef struct storage_entry {
  char *key;
  char *value;
  UT_hash_handle hh;
} storage_entry_t;

static storage_entry_t *session_storage = NULL;
static ant_mutex_t session_storage_lock = ANT_MUTEX_INIT;

static void storage_set_item(const char *key, size_t key_len, const char *value, size_t value_len) {
  storage_entry_t *entry = NULL;
  
  HASH_FIND(hh, session_storage, key, key_len, entry);
  
  if (entry) {
    HASH_DEL(session_storage, entry);
    free(entry);
  }
  
  entry = calloc(1, sizeof(storage_entry_t) + key_len + 1 + value_len + 1);
  if (!entry) return;
  
  entry->key = (char *)(entry + 1);
  entry->value = entry->key + key_len + 1;
  
  memcpy(entry->key, key, key_len);
  entry->key[key_len] = '\0';
  memcpy(entry->value, value, value_len);
  entry->value[value_len] = '\0';
  
  HASH_ADD_KEYPTR(hh, session_storage, entry->key, key_len, entry);
}

static char *storage_get_item(const char *key, size_t key_len) {
  storage_entry_t *entry = NULL;
  HASH_FIND(hh, session_storage, key, key_len, entry);
  return entry ? entry->value : NULL;
}

static void storage_remove_item(const char *key, size_t key_len) {
  storage_entry_t *entry = NULL;
  HASH_FIND(hh, session_storage, key, key_len, entry);
  
  if (entry) {
    HASH_DEL(session_storage, entry);
    free(entry);
  }
}

static void storage_clear(void) {
  storage_entry_t *entry, *tmp;
  HASH_ITER(hh, session_storage, entry, tmp) {
    HASH_DEL(session_storage, entry);
    free(entry);
  }
}

static size_t storage_length(void) {
  return HASH_COUNT(session_storage);
}

static char *storage_key(size_t index) {
  storage_entry_t *entry;
  size_t i = 0;
  
  for (entry = session_storage; entry != NULL; entry = entry->hh.next) {
    if (i == index) return entry->key;
    i++;
  }
  
  return NULL;
}

// sessionStorage.setItem(key, value)
static ant_value_t js_sessionstorage_setItem(ant_params_t) {
  if (nargs < 2) {
    return js_mkerr(js, "Failed to execute 'setItem' on 'Storage': 2 arguments required");
  }
  
  size_t key_len, value_len;
  char *key = js_getstr(js, args[0], &key_len);
  char *value = js_getstr(js, js_tostring_val(js, args[1]), &value_len);
  
  ant_mutex_lock(&session_storage_lock);
  storage_set_item(key, key_len, value, value_len);
  ant_mutex_unlock(&session_storage_lock);
  
  return js_mkundef();
}

// sessionStorage.getItem(key)
static ant_value_t js_sessionstorage_getItem(ant_params_t) {
  if (nargs < 1) {
    return js_mkerr(js, "Failed to execute 'getItem' on 'Storage': 1 argument required");
  }
  
  size_t key_len;
  char *key = js_getstr(js, args[0], &key_len);
  ant_mutex_lock(&session_storage_lock);
  
  char *value = storage_get_item(key, key_len);
  ant_value_t result = value ? js_mkstr(js, value, strlen(value)) : js_mknull();
  ant_mutex_unlock(&session_storage_lock);
  
  return result;
}

// sessionStorage.removeItem(key)
static ant_value_t js_sessionstorage_removeItem(ant_params_t) {
  if (nargs < 1) {
    return js_mkerr(js, "Failed to execute 'removeItem' on 'Storage': 1 argument required");
  }
  
  size_t key_len;
  char *key = js_getstr(js, args[0], &key_len);
  ant_mutex_lock(&session_storage_lock);
  
  storage_remove_item(key, key_len);
  ant_mutex_unlock(&session_storage_lock);
  
  return js_mkundef();
}

// sessionStorage.clear()
static ant_value_t js_sessionstorage_clear(ant_params_t) {
  ant_mutex_lock(&session_storage_lock);
  storage_clear();
  ant_mutex_unlock(&session_storage_lock);
  return js_mkundef();
}

// sessionStorage.key(index)
static ant_value_t js_sessionstorage_key(ant_params_t) {
  if (nargs < 1) {
    return js_mkerr(js, "Failed to execute 'key' on 'Storage': 1 argument required");
  }
  
  if (vtype(args[0]) != kTypeNumber) {
    return js_mknull();
  }
  
  double idx = Ant_Value_AsNumberUnchecked(args[0]);
  if (idx < 0) return js_mknull();
  
  size_t index = (size_t)idx;
  ant_mutex_lock(&session_storage_lock);
  
  char *key = storage_key(index);
  ant_value_t result = key ? js_mkstr(js, key, strlen(key)) : js_mknull();
  ant_mutex_unlock(&session_storage_lock);
  
  return result;
}

// sessionStorage.length
static ant_value_t js_sessionstorage_length(ant_params_t) {
  ant_mutex_lock(&session_storage_lock);
  size_t length = storage_length();
  
  ant_mutex_unlock(&session_storage_lock);
  return Ant_Value_FromNumber((double)length);
}

void init_sessionstorage_module(ant_t *js) {
  ant_value_t glob = js_glob(js);
  ant_value_t storage_obj = Ant_Object_Create(js);
  
  js_set(js, storage_obj, "setItem", Ant_Function_Create(js_sessionstorage_setItem));
  js_set(js, storage_obj, "getItem", Ant_Function_Create(js_sessionstorage_getItem));
  js_set(js, storage_obj, "removeItem", Ant_Function_Create(js_sessionstorage_removeItem));
  js_set(js, storage_obj, "clear", Ant_Function_Create(js_sessionstorage_clear));
  js_set(js, storage_obj, "key", Ant_Function_Create(js_sessionstorage_key));
  
  ant_value_t length_getter = Ant_Function_Create(js_sessionstorage_length);
  js_set_getter_desc(js, storage_obj, "length", 6, length_getter, 0);
  
  js_set_tostringtag(js, storage_obj, ANT_STRING("Storage"));
  js_set(js, glob, "sessionStorage", storage_obj);
}
