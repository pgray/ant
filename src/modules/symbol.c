#include <string.h>
#include <stdlib.h>

#include "ant.h"
#include "cfunc.h"
#include "errors.h"
#include "internal.h"
#include "silver/call.h"
#include "modules/symbol.h"
#include "descriptors.h"
#include "gc/modules.h"

static ant_value_t builtin_Symbol(ant_params_t) {
  if (vtype(call_new_target) != kTypeUndefined)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Symbol is not a constructor");

  if (nargs == 0 || vtype(args[0]) == kTypeUndefined) return js_mksym(js, NULL);
  
  ant_value_t desc = js_template_to_string(js, args[0]);
  if (is_err(desc)) return desc;
  
  return js_mksym(js, js_getstr(js, desc, NULL));
}

static ant_value_t builtin_Symbol_for(ant_params_t) {
  ant_value_t key_str = js_template_to_string(js, nargs > 0 ? args[0] : js_mkundef());
  if (is_err(key_str)) return key_str;
  
  char *key = js_getstr(js, key_str, NULL);
  if (!key) return js_mkerr(js, "Invalid key");
  
  return js_mksym_for(js, key);
}

static ant_value_t builtin_Symbol_keyFor(ant_params_t) {
  if (nargs < 1 || vtype(args[0]) != kTypeSymbol) return js_mkundef();
  
  const char *key = js_sym_key(args[0]);
  if (!key) return js_mkundef();
  
  return js_mkstr(js, key, strlen(key));
}

static ant_value_t builtin_Symbol_toString(ant_params_t) {
  ant_value_t this_val = call_this;
  
  if (vtype(this_val) != kTypeSymbol && is_object_type(this_val)) {
    ant_value_t prim = js_get_slot(this_val, SLOT_PRIMITIVE);
    if (vtype(prim) == kTypeSymbol) this_val = prim;
  }

  if (vtype(this_val) != kTypeSymbol)
    return js_mkerr(js, "Symbol.prototype.toString requires a symbol");
  
  return js_symbol_to_string(js, this_val);
}

static ant_value_t builtin_Symbol_valueOf(ant_params_t) {
  if (vtype(call_this) != kTypeSymbol && is_object_type(call_this)) {
    ant_value_t prim = js_get_slot(call_this, SLOT_PRIMITIVE);
    if (vtype(prim) == kTypeSymbol) return prim;
  }

  if (vtype(call_this) == kTypeSymbol) return call_this;
  return js_mkerr_typed(js, JS_ERR_TYPE, "Symbol.prototype.valueOf requires a symbol");
}

static ant_value_t builtin_Symbol_description(ant_params_t) {
  ant_value_t sym = call_this;

  if (vtype(sym) != kTypeSymbol && is_object_type(sym)) {
    ant_value_t prim = js_get_slot(sym, SLOT_PRIMITIVE);
    if (vtype(prim) == kTypeSymbol) sym = prim;
  }

  if (vtype(sym) != kTypeSymbol)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Symbol.prototype.description requires a symbol");

  return js_symbol_description_value(js, sym);
}

bool js_is_symbol_description_getter(ant_value_t getter) {
  return 
    vtype(getter) == kTypeBuiltin && 
    js_cfunc_same_entrypoint(getter, builtin_Symbol_description);
}

ant_value_t maybe_call_symbol_method(
  ant_t *js, ant_value_t target, ant_value_t sym,
  ant_value_t this_arg, ant_args_t, bool *called
) {
  *called = false;
  if (vtype(sym) != kTypeSymbol || !is_object_type(target)) return js_mkundef();

  ant_value_t method = js_get_sym(js, target, sym);
  if (is_err(method)) return method;

  uint8_t mt = vtype(method);
  if (mt == kTypeUndefined || mt == kTypeNull) return js_mkundef();
  if (!is_callable(method)) {
    return js_mkerr_typed(js, JS_ERR_TYPE, "Symbol method is not callable");
  }

  *called = true;
  return sv_vm_call(js->vm, js, method, this_arg, args, nargs, NULL, js_mkundef());
}

ant_value_t js_return_this(ant_params_t) {
  return call_this;
}

void js_define_species_getter(ant_t *js, ant_value_t ctor) {
  if (!is_object_type(ctor) || vtype(js->sym.species_sym) != kTypeSymbol) return;
  ctor = js_as_obj(ctor);
  js_set_sym_getter_desc(js, ctor, js->sym.species_sym, Ant_Function_Create(js_return_this), JS_DESC_C);
}

void init_well_known_symbols(ant_t *js) {
  #define ANT_SYMBOL(name, desc) js->sym.name##_sym = js_mksym_well_known(js, desc);
  #include "symbol_list.h"
}

void init_symbol_module(ant_t *js) {
  init_well_known_symbols(js);
  init_symbol_ctor(js);
}

void init_symbol_ctor(ant_t *js) {
  ant_value_t proto = Ant_Object_CreateBare(js);
  js->sym.symbol_proto = proto;
  js_set_proto_init(proto, js->sym.object_proto);
  
  defmethod(js, proto, "toString", 8, Ant_Function_Create(builtin_Symbol_toString));
  defmethod(js, proto, "valueOf", 7, Ant_Function_Create(builtin_Symbol_valueOf));
  
  mkprop(js, proto, js->sym.toPrimitive_sym, Ant_Function_CreateWithMeta(builtin_Symbol_valueOf, "[Symbol.toPrimitive]", 1, 0), ANT_PROP_ATTR_CONFIGURABLE);
  mkprop(js, proto, js->sym.toStringTag_sym, Ant_String_FromLiteral(js, "Symbol"), ANT_PROP_ATTR_CONFIGURABLE);
  js_set_getter_desc(js, proto, "description", 11, Ant_Function_Create(builtin_Symbol_description), JS_DESC_C);

  ant_value_t ctor = Ant_Object_CreateBare(js);
  js_set_slot(ctor, SLOT_CFUNC, Ant_Function_Create(builtin_Symbol));
  js_set_function_name(js, ctor, "Symbol", 6);
  
  defmethod(js, ctor, "for", 3, Ant_Function_CreateWithArity(builtin_Symbol_for, 1));
  defmethod(js, ctor, "keyFor", 6, Ant_Function_CreateWithArity(builtin_Symbol_keyFor, 1));
  
  js_set(js, ctor, "prototype", proto);
  js_set_descriptor(js, ctor, "prototype", 9, 0);

  #define ANT_SYMBOL(name, _desc)                \
    js_set(js, ctor, #name, js->sym.name##_sym); \
    js_set_descriptor(js, ctor, #name, sizeof(#name) - 1, 0);
  #include "symbol_list.h"

  ant_value_t fn = js_obj_to_func(js, ctor);
  defmethod(js, proto, "constructor", 11, fn);
  js_set_global_builtin(js, "Symbol", fn);
}

void gc_mark_symbols(ant_t *js, gc_mark_fn mark) {
  #define ANT_SYMBOL(name, _desc) mark(js, js->sym.name##_sym);
  #include "symbol_list.h"
}

typedef struct {
  uint64_t gc_epoch;
  const char *key;
  uint32_t flags;
  uint32_t desc_len;
  char desc[];
} ant_symbol_heap_t;

enum {
  SYM_FLAG_GLOBAL     = 1u,
  SYM_FLAG_WELL_KNOWN = 2u,
  SYM_FLAG_HAS_DESC   = 4u,
  SYM_FLAG_DESC_ASCII = 8u,
};

static inline ant_symbol_heap_t *sym_ptr(ant_value_t v);

ant_value_t js_mksym(ant_t *js, const char *desc) {
  bool has_desc = desc != NULL;
  
  size_t desc_len = has_desc ? strlen(desc) : 0;
  size_t total = offsetof(ant_symbol_heap_t, desc) +
    (has_desc ? desc_len + 1 : 0);
  
  ant_symbol_heap_t *sym_ptr = (ant_symbol_heap_t *)js_type_alloc(
    js, ANT_ALLOC_SYMBOL, total, 
    _Alignof(ant_symbol_heap_t)
  ); if (!sym_ptr) return js_mkerr(js, "oom");

  sym_ptr->gc_epoch = 0;
  sym_ptr->key = NULL;
  sym_ptr->flags = has_desc ? SYM_FLAG_HAS_DESC : 0;

  if (has_desc && str_detect_ascii_bytes(desc, desc_len) == STR_ASCII_YES)
    sym_ptr->flags |= SYM_FLAG_DESC_ASCII;
  sym_ptr->desc_len = (uint32_t)desc_len;
  
  if (has_desc) {
    memcpy(sym_ptr->desc, desc, desc_len);
    sym_ptr->desc[desc_len] = '\0';
  }

  return mkref(kTypeSymbol, sym_ptr);
}

ant_value_t js_mksym_well_known(ant_t *js, const char *desc) {
  ant_value_t sym = js_mksym(js, desc);
  if (is_err(sym)) return sym;
  ant_symbol_heap_t *ptr = (ant_symbol_heap_t *)vptr(sym);
  if (ptr) ptr->flags |= SYM_FLAG_WELL_KNOWN;
  return sym;
}

static inline ant_symbol_heap_t *sym_ptr(ant_value_t v) {
  return (ant_symbol_heap_t *)vptr(v);
}

static inline uint32_t sym_get_flags(ant_value_t v) {
  ant_symbol_heap_t *ptr = sym_ptr(v);
  return ptr ? ptr->flags : 0;
}

static inline uintptr_t sym_get_key_ptr(ant_value_t v) {
  ant_symbol_heap_t *ptr = sym_ptr(v);
  return ptr ? (uintptr_t)ptr->key : 0;
}

const inline char *js_sym_desc(ant_value_t sym) {
  ant_symbol_heap_t *ptr = sym_ptr(sym);
  if (!ptr || !(ptr->flags & SYM_FLAG_HAS_DESC)) return NULL;
  return ptr->desc;
}

ant_value_t js_symbol_description_value(ant_t *js, ant_value_t symbol) {
  ant_symbol_heap_t *ptr = sym_ptr(symbol);
  if (!ptr || !(ptr->flags & SYM_FLAG_HAS_DESC)) return js_mkundef();
  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, symbol);
  ant_value_t result = mkstr_with_ascii(
    js, ptr->desc, ptr->desc_len, (ptr->flags & SYM_FLAG_DESC_ASCII) != 0);
  GC_ROOT_RESTORE(js, mark);
  return result;
}

ant_value_t js_mksym_for(ant_t *js, const char *key) {
  const char *interned = intern_string(js, key, strlen(key));

  sym_registry_entry_t *reg = js->sym.registry;
  sym_registry_entry_t *found = NULL;
  HASH_FIND_PTR(reg, &interned, found);
  if (found) return found->sym;

  ant_value_t sym = js_mksym(js, key);
  if (is_err(sym)) return sym;

  ant_symbol_heap_t *ptr = sym_ptr(sym);
  if (!ptr) return js_mkerr(js, "oom");
  ptr->flags |= SYM_FLAG_GLOBAL;
  ptr->key = interned;

  sym_registry_entry_t *entry = calloc(1, sizeof(sym_registry_entry_t));
  if (entry) {
    entry->key = interned;
    entry->sym = sym;
    HASH_ADD_PTR(reg, key, entry);
    js->sym.registry = reg;
  }

  return sym;
}

const char *js_sym_key(ant_value_t sym) {
  if (vtype(sym) != kTypeSymbol) return NULL;
  uint32_t flags = sym_get_flags(sym);
  if (!(flags & SYM_FLAG_GLOBAL) || (flags & SYM_FLAG_WELL_KNOWN)) return NULL;
  return (const char *)sym_get_key_ptr(sym);
}

bool js_symbol_gc_mark(ant_value_t sym, uint64_t epoch) {
  if (vtype(sym) != kTypeSymbol) return false;
  ant_symbol_heap_t *ptr = sym_ptr(sym);
  if (!ptr || ptr->gc_epoch == epoch) return false;
  ptr->gc_epoch = epoch;
  return true;
}

bool js_symbol_gc_is_marked(ant_value_t sym, uint64_t epoch) {
  if (vtype(sym) != kTypeSymbol) return false;
  ant_symbol_heap_t *ptr = sym_ptr(sym);
  return ptr && ptr->gc_epoch == epoch;
}

bool js_symbol_gc_is_permanent(ant_value_t sym) {
  if (vtype(sym) != kTypeSymbol) return false;
  uint32_t flags = sym_get_flags(sym);
  return (flags & (SYM_FLAG_GLOBAL | SYM_FLAG_WELL_KNOWN)) != 0;
}

ant_value_t js_symbol_to_string(ant_t *js, ant_value_t sym) {
  const char *desc = js_sym_desc(sym);
  if (!desc) return js_mkstr(js, "Symbol()", 8);
  
  size_t desc_len = strlen(desc);
  size_t total = 7 + desc_len + 1;
  
  char stack_buf[128];
  char *buf = (total + 1 <= sizeof(stack_buf)) ? stack_buf : malloc(total + 1);
  if (!buf) return Ant_Error_CreateOutOfMemory(js);
  
  memcpy(buf, "Symbol(", 7);
  memcpy(buf + 7, desc, desc_len);
  buf[7 + desc_len] = ')';
  buf[total] = '\0';
  
  ant_value_t result = js_mkstr(js, buf, total);
  if (buf != stack_buf) free(buf);
  return result;
}
