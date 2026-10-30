#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gc.h"
#include "hash.h"
#include "shapes.h"
#include "arguments.h"
#include "arrays.h"
#include "keys.h"
#include "props.h"
#include "internal.h"
#include "protectors.h"
#include "descriptors.h"

#include "silver/engine.h"
#include "modules/proxy.h"
#include "modules/object.h"

static descriptor_entry_t arr_length_desc = {
  .key = 0,
  .obj_off = 0,
  .prop_name = "length",
  .prop_len = 6,
  .writable = true,
  .enumerable = false,
  .configurable = false,
  .has_getter = false,
  .has_setter = false,
  .getter = 0,
  .setter = 0,
};

static descriptor_entry_t arr_length_readonly_desc = {
  .key = 0,
  .obj_off = 0,
  .prop_name = "length",
  .prop_len = 6,
  .writable = false,
  .enumerable = false,
  .configurable = false,
  .has_getter = false,
  .has_setter = false,
  .getter = 0,
  .setter = 0,
};

static inline bool is_canonical_desc_obj(ant_value_t obj) {
  return vtype(obj) == kTypeObject;
}

static inline bool is_exotic_desc_obj(ant_value_t obj) {
  ant_object_t *ptr = js_obj_ptr(obj);
  return ptr && ptr->flags.is_exotic;
}

static inline bool desc_registry_allowed(ant_value_t obj) {
  if (is_exotic_desc_obj(obj)) return true;
#ifndef NDEBUG
  assert(false && "desc_registry is exotic-only; ordinary objects must use shape metadata");
#endif
  return false;
}

static bool ensure_added_shape_slot_storage(ant_t *js, ant_object_t *ptr, uint32_t slot) {
  if (!ptr || !ptr->shape) return false;
  if (!js_obj_ensure_prop_capacity(js, ptr, ant_shape_count(ptr->shape))) return false;
  if (slot >= ptr->prop_count && !js_obj_ensure_prop_capacity(js, ptr, slot + 1)) return false;
  return true;
}

static inline uint8_t attrs_from_flags(int flags, bool include_writable) {
  uint8_t attrs = 0;
  if (include_writable && (flags & JS_DESC_W)) attrs |= ANT_PROP_ATTR_WRITABLE;
  if (flags & JS_DESC_E) attrs |= ANT_PROP_ATTR_ENUMERABLE;
  if (flags & JS_DESC_C) attrs |= ANT_PROP_ATTR_CONFIGURABLE;
  return attrs;
}

uint64_t make_desc_key(ant_value_t obj, const char *key, size_t klen) {
  assert(is_canonical_desc_obj(obj) && "descriptor APIs require canonical js_as_obj(...) value");
  if (!is_canonical_desc_obj(obj)) return hash_key(key, klen);
  uintptr_t obj_off = (uintptr_t)vdata(obj);
  uint64_t key_hash = hash_key(key, klen);
  uint64_t h = (uint64_t)obj_off ^ ((uint64_t)obj_off >> 33);
  h ^= key_hash + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}

uint64_t make_sym_desc_key(ant_value_t obj, ant_offset_t sym_off) {
  assert(is_canonical_desc_obj(obj) && "descriptor APIs require canonical js_as_obj(...) value");
  if (!is_canonical_desc_obj(obj)) return ((uint64_t)sym_off << 1) | 1u;
  uintptr_t obj_off = (uintptr_t)vdata(obj);
  uint64_t h = (uint64_t)obj_off ^ ((uint64_t)obj_off >> 33);
  h ^= ((uint64_t)sym_off << 1) | 1u;
  h += 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}

static descriptor_entry_t *registry_lookup_desc(ant_t *js, ant_value_t obj, const char *key, size_t klen) {
  descriptor_entry_t *entry = NULL;
  uint64_t desc_key = make_desc_key(obj, key, klen);
  HASH_FIND(hh, js->desc_registry, &desc_key, sizeof(uint64_t), entry);
  return entry;
}

static descriptor_entry_t *registry_lookup_sym_desc(ant_t *js, ant_value_t obj, ant_offset_t sym_off) {
  descriptor_entry_t *entry = NULL;
  uint64_t k = make_sym_desc_key(obj, sym_off);
  HASH_FIND(hh, js->desc_registry, &k, sizeof(uint64_t), entry);
  return entry;
}

descriptor_entry_t *lookup_descriptor(ant_t *js, ant_value_t obj, const char *key, size_t klen) {
  assert(js && is_canonical_desc_obj(obj) && "lookup_descriptor expects an isolate and js_as_obj(...)");
  if (!js || !is_canonical_desc_obj(obj)) return NULL;

  ant_object_t *ptr = js_obj_ptr(obj);
  if (klen == 6 && memcmp(key, "length", 6) == 0 && ant_object_is_array(ptr)) {
    if (ptr->flags.frozen) return &arr_length_readonly_desc;
    descriptor_entry_t *own = ptr->flags.is_exotic ? registry_lookup_desc(js, obj, key, klen) : NULL;
    return own ? own : &arr_length_desc;
  }

  if (!is_exotic_desc_obj(obj)) return NULL;
  return registry_lookup_desc(js, obj, key, klen);
}


descriptor_entry_t *lookup_sym_descriptor(ant_t *js, ant_value_t obj, ant_offset_t sym_off) {
  assert(js && is_canonical_desc_obj(obj) && "lookup_sym_descriptor expects an isolate and js_as_obj(...)");
  if (!js || !is_canonical_desc_obj(obj)) return NULL;

  if (!is_exotic_desc_obj(obj)) return NULL;
  return registry_lookup_sym_desc(js, obj, sym_off);
}

static descriptor_entry_t *get_or_create_desc(ant_t *js, ant_value_t obj, const char *key, size_t klen) {
  if (!desc_registry_allowed(obj)) return NULL;
  descriptor_entry_t *entry = registry_lookup_desc(js, obj, key, klen);
  if (entry) return entry;

  entry = (descriptor_entry_t *)calloc(1, sizeof(descriptor_entry_t) + klen + 1);
  if (!entry) return NULL;

  entry->key = make_desc_key(obj, key, klen);
  entry->obj_off = (uintptr_t)vdata(obj);
  entry->prop_name = (char *)(entry + 1);
  memcpy(entry->prop_name, key, klen);
  entry->prop_name[klen] = '\0';
  entry->prop_len = klen;
  entry->writable = true;
  entry->enumerable = true;
  entry->configurable = true;
  entry->has_getter = false;
  entry->has_setter = false;
  entry->getter = js_mkundef();
  entry->setter = js_mkundef();

  HASH_ADD(hh, js->desc_registry, key, sizeof(uint64_t), entry);
  return entry;
}

static descriptor_entry_t *get_or_create_sym_desc(ant_t *js, ant_value_t obj, ant_offset_t sym_off) {
  if (!desc_registry_allowed(obj)) return NULL;
  descriptor_entry_t *entry = registry_lookup_sym_desc(js, obj, sym_off);
  if (entry) return entry;

  entry = (descriptor_entry_t *)calloc(1, sizeof(descriptor_entry_t));
  if (!entry) return NULL;

  entry->key = make_sym_desc_key(obj, sym_off);
  entry->obj_off = (uintptr_t)vdata(obj);
  entry->sym_off = sym_off;
  entry->writable = true;
  entry->enumerable = true;
  entry->configurable = true;
  entry->has_getter = false;
  entry->has_setter = false;
  entry->getter = js_mkundef();
  entry->setter = js_mkundef();

  HASH_ADD(hh, js->desc_registry, key, sizeof(uint64_t), entry);
  return entry;
}

static bool ensure_string_shape_slot(ant_t *js, ant_value_t obj, const char *key, size_t klen, uint32_t *out_slot) {
  ant_object_t *ptr = js_obj_ptr(obj);
  if (!ptr || !ptr->shape) return false;

  const char *interned = intern_string(js, key, klen);
  if (!interned) return false;

  int32_t slot = ant_shape_lookup_interned(ptr->shape, interned);
  if (slot < 0) {
    uint32_t added_slot = 0;
    if (!ant_shape_add_interned_tr(js, &ptr->shape, interned, ANT_PROP_ATTR_DEFAULT, &added_slot)) return false;
    ant_object_invalidate_guarded_absence(js, ptr);
    if (!ensure_added_shape_slot_storage(js, ptr, added_slot)) return false;
    slot = (int32_t)added_slot;
  }

  if (out_slot) *out_slot = (uint32_t)slot;
  return true;
}

static bool ensure_symbol_shape_slot(ant_t *js, ant_value_t obj, ant_offset_t sym_off, uint32_t *out_slot) {
  ant_object_t *ptr = js_obj_ptr(obj);
  if (!ptr || !ptr->shape) return false;

  int32_t slot = ant_shape_lookup_symbol(ptr->shape, sym_off);
  if (slot < 0) {
    ant_value_t sym = mkval(kTypeSymbol, sym_off);
    if (is_err(mkprop(js, obj, sym, js_mkundef(), 0))) return false;
    slot = ant_shape_lookup_symbol(ptr->shape, sym_off);
    if (slot < 0) return false;
  }

  if (out_slot) *out_slot = (uint32_t)slot;
  return true;
}

static ant_shape_prop_t *ensure_string_shape_prop_at(
  ant_t *js, ant_value_t obj, const char *key, size_t klen, uint32_t *slot_out
) {
  uint32_t slot = 0;
  if (!ensure_string_shape_slot(js, obj, key, klen, &slot)) return NULL;
  if (slot_out) *slot_out = slot;
  
  ant_object_t *ptr = js_obj_ptr(obj);
  if (!ptr || !ptr->shape) return NULL;
  if (!js_obj_ensure_unique_shape(ptr)) return NULL;
  
  return ant_shape_prop_mut_at(ptr->shape, slot);
}

static ant_shape_prop_t *ensure_symbol_shape_prop_at(
  ant_t *js, ant_value_t obj, ant_offset_t sym_off, uint32_t *slot_out
) {
  uint32_t slot = 0;
  if (!ensure_symbol_shape_slot(js, obj, sym_off, &slot)) return NULL;
  if (slot_out) *slot_out = slot;
  
  ant_object_t *ptr = js_obj_ptr(obj);
  if (!ptr || !ptr->shape) return NULL;
  if (!js_obj_ensure_unique_shape(ptr)) return NULL;
  
  return ant_shape_prop_mut_at(ptr->shape, slot);
}

static inline ant_shape_prop_t *ensure_string_shape_prop(
  ant_t *js, ant_value_t obj, const char *key, size_t klen
) {
  return ensure_string_shape_prop_at(js, obj, key, klen, NULL);
}

static inline ant_shape_prop_t *ensure_symbol_shape_prop(
  ant_t *js, ant_value_t obj, ant_offset_t sym_off
) {
  return ensure_symbol_shape_prop_at(js, obj, sym_off, NULL);
}

static void clear_accessor_slot(ant_value_t obj, uint32_t slot) {
  ant_object_t *ptr = js_obj_ptr(obj);
  if (ptr && slot < ptr->prop_count) ant_object_prop_set_unchecked(ptr, slot, js_mkundef());
}

static void apply_shape_desc_update(
  ant_shape_prop_t *prop,
  int flags,
  bool include_writable,
  bool set_getter_flag,
  bool has_getter,
  ant_value_t getter,
  bool set_setter_flag,
  bool has_setter,
  ant_value_t setter
) {
  if (!prop) return;
  prop->attrs = attrs_from_flags(flags, include_writable);
  if (set_getter_flag) {
    prop->has_getter = has_getter ? 1 : 0;
    prop->getter = has_getter ? getter : js_mkundef();
  }
  if (set_setter_flag) {
    prop->has_setter = has_setter ? 1 : 0;
    prop->setter = has_setter ? setter : js_mkundef();
  }
}

static void apply_registry_desc_update(
  descriptor_entry_t *entry,
  int flags,
  bool include_writable,
  bool set_getter_flag,
  bool has_getter,
  ant_value_t getter,
  bool set_setter_flag,
  bool has_setter,
  ant_value_t setter
) {
  if (!entry) return;
  if (include_writable) entry->writable = (flags & JS_DESC_W) != 0;
  entry->enumerable = (flags & JS_DESC_E) != 0;
  entry->configurable = (flags & JS_DESC_C) != 0;
  if (set_getter_flag) {
    entry->has_getter = has_getter;
    entry->getter = has_getter ? getter : js_mkundef();
  }
  if (set_setter_flag) {
    entry->has_setter = has_setter;
    entry->setter = has_setter ? setter : js_mkundef();
  }
}

void js_set_descriptor(ant_t *js, ant_value_t obj, const char *key, size_t klen, int flags) {
  assert(is_canonical_desc_obj(obj) && "js_set_descriptor expects js_as_obj(...)");

  if (!is_canonical_desc_obj(obj)) return;
  ant_shape_prop_t *prop = ensure_string_shape_prop(js, obj, key, klen);

  if (prop) {
    apply_shape_desc_update(
      prop, flags, true,
      false, false, js_mkundef(),
      false, false, js_mkundef()
    );

    ant_ic_epoch_bump(js);
    return;
  }

  if (!desc_registry_allowed(obj)) return;
  descriptor_entry_t *entry = get_or_create_desc(js, obj, key, klen);

  apply_registry_desc_update(
    entry, flags, true,
    false, false, js_mkundef(),
    false, false, js_mkundef()
  );
}

void js_set_sym_descriptor(ant_t *js, ant_value_t obj, ant_value_t sym, int flags) {
  assert(is_canonical_desc_obj(obj) && "js_set_sym_descriptor expects js_as_obj(...)");

  if (!is_canonical_desc_obj(obj) || vtype(sym) != kTypeSymbol) return;
  ant_offset_t sym_off = (ant_offset_t)vdata(sym);
  ant_shape_prop_t *prop = ensure_symbol_shape_prop(js, obj, sym_off);

  if (prop) {
    apply_shape_desc_update(
      prop, flags, true,
      false, false, js_mkundef(),
      false, false, js_mkundef()
    );

    ant_symbol_property_mutation_invalidate(js, js_obj_ptr(obj), sym_off);
    ant_ic_epoch_bump(js);
    return;
  }

  if (!desc_registry_allowed(obj)) return;
  descriptor_entry_t *entry = get_or_create_sym_desc(js, obj, sym_off);

  apply_registry_desc_update(
    entry, flags, true,
    false, false, js_mkundef(),
    false, false, js_mkundef()
  );
}

void js_set_getter_desc(ant_t *js, ant_value_t obj, const char *key, size_t klen, ant_value_t getter, int flags) {
  getter = js_cfunc_expose_property(js, getter, JS_CFUNC_GETTER, key, klen, false);
  assert(is_canonical_desc_obj(obj) && "js_set_getter_desc expects js_as_obj(...)");

  if (!is_canonical_desc_obj(obj)) return;
  uint32_t slot = 0;
  ant_shape_prop_t *prop = ensure_string_shape_prop_at(js, obj, key, klen, &slot);

  if (prop) {
    apply_shape_desc_update(
      prop, flags, false,
      true, true, getter,
      false, false, js_mkundef()
    );
    clear_accessor_slot(obj, slot);

    ant_property_mutation_invalidate(
      js, js_obj_ptr(obj),
      prop->key.interned
    );

    ant_ic_epoch_bump(js);
    gc_write_barrier(js, js_obj_ptr(js_as_obj(obj)), getter);

    return;
  }

  if (!desc_registry_allowed(obj)) return;
  descriptor_entry_t *entry = get_or_create_desc(js, obj, key, klen);

  apply_registry_desc_update(
    entry, flags, false,
    true, true, getter,
    false, false, js_mkundef()
  );
}

void js_set_setter_desc(ant_t *js, ant_value_t obj, const char *key, size_t klen, ant_value_t setter, int flags) {
  setter = js_cfunc_expose_property(js, setter, JS_CFUNC_SETTER, key, klen, false);
  assert(is_canonical_desc_obj(obj) && "js_set_setter_desc expects js_as_obj(...)");
  if (!is_canonical_desc_obj(obj)) return;
  
  uint32_t slot = 0;
  ant_shape_prop_t *prop = ensure_string_shape_prop_at(js, obj, key, klen, &slot);

  if (prop) {
    apply_shape_desc_update(
      prop, flags, false,
      false, false, js_mkundef(),
      true, true, setter
    );
    
    clear_accessor_slot(obj, slot);
    
    ant_property_mutation_invalidate(
      js, js_obj_ptr(obj),
      prop->key.interned
    );

    ant_ic_epoch_bump(js);
    gc_write_barrier(js, js_obj_ptr(js_as_obj(obj)), setter);

    return;
  }

  if (!desc_registry_allowed(obj)) return;
  descriptor_entry_t *entry = get_or_create_desc(js, obj, key, klen);

  apply_registry_desc_update(
    entry, flags, false,
    false, false, js_mkundef(),
    true, true, setter
  );
}

void js_define_accessor_desc(ant_t *js, ant_value_t obj, const char *key, size_t klen, ant_value_t getter, ant_value_t setter, int flags) {
  getter = js_cfunc_expose_property(js, getter, JS_CFUNC_GETTER, key, klen, false);
  setter = js_cfunc_expose_property(js, setter, JS_CFUNC_SETTER, key, klen, false);
  
  ant_object_t *ptr = js_obj_ptr(obj);
  const char *interned = ptr && ptr->shape && !ptr->flags.is_exotic ? intern_string(js, key, klen) : NULL;
  
  uint32_t slot = 0;
  if (
    !interned || ant_shape_lookup_interned(ptr->shape, interned) >= 0 ||
    !ant_shape_add_accessor_tr(js, &ptr->shape, interned, attrs_from_flags(flags, false), getter, setter, &slot)
  ) {
    js_set_accessor_desc(js, obj, key, klen, getter, setter, flags);
    return;
  }

  ant_object_invalidate_guarded_absence(js, ptr);
  if (!ensure_added_shape_slot_storage(js, ptr, slot)) return;
  
  ant_object_prop_set_unchecked(ptr, slot, js_mkundef());
  ant_property_mutation_invalidate(js, ptr, interned);
  gc_write_barrier(js, ptr, getter);
  gc_write_barrier(js, ptr, setter);
}

void js_set_accessor_desc(ant_t *js, ant_value_t obj, const char *key, size_t klen, ant_value_t getter, ant_value_t setter, int flags) {
  getter = js_cfunc_expose_property(js, getter, JS_CFUNC_GETTER, key, klen, false);
  setter = js_cfunc_expose_property(js, setter, JS_CFUNC_SETTER, key, klen, false);
  assert(is_canonical_desc_obj(obj) && "js_set_accessor_desc expects js_as_obj(...)");

  if (!is_canonical_desc_obj(obj)) return;
  uint32_t slot = 0;
  ant_shape_prop_t *prop = ensure_string_shape_prop_at(js, obj, key, klen, &slot);

  if (prop) {
    apply_shape_desc_update(
      prop, flags, false,
      true, true, getter,
      true, true, setter
    );
    clear_accessor_slot(obj, slot);

    ant_property_mutation_invalidate(
      js, js_obj_ptr(obj),
      prop->key.interned
    );

    ant_ic_epoch_bump(js);
    gc_write_barrier(js, js_obj_ptr(js_as_obj(obj)), getter);
    gc_write_barrier(js, js_obj_ptr(js_as_obj(obj)), setter);

    return;
  }

  if (!desc_registry_allowed(obj)) return;
  descriptor_entry_t *entry = get_or_create_desc(js, obj, key, klen);

  apply_registry_desc_update(
    entry, flags, false,
    true, true, getter,
    true, true, setter
  );
}

void js_set_sym_getter_desc(ant_t *js, ant_value_t obj, ant_value_t sym, ant_value_t getter, int flags) {
  const char *desc = vtype(sym) == kTypeSymbol ? js_sym_desc(sym) : NULL;
  getter = js_cfunc_expose_property(js, getter, JS_CFUNC_GETTER, desc, desc ? strlen(desc) : 0, true);
  assert(is_canonical_desc_obj(obj) && "js_set_sym_getter_desc expects js_as_obj(...)");

  if (!is_canonical_desc_obj(obj)) return;
  if (vtype(sym) != kTypeSymbol) return;

  ant_offset_t sym_off = (ant_offset_t)vdata(sym);
  uint32_t slot = 0;
  ant_shape_prop_t *prop = ensure_symbol_shape_prop_at(js, obj, sym_off, &slot);

  if (prop) {
    apply_shape_desc_update(
      prop, flags, false,
      true, true, getter,
      false, false, js_mkundef()
    );
    clear_accessor_slot(obj, slot);

    ant_symbol_property_mutation_invalidate(js, js_obj_ptr(obj), sym_off);
    ant_ic_epoch_bump(js);
    gc_write_barrier(js, js_obj_ptr(js_as_obj(obj)), getter);

    return;
  }

  if (!desc_registry_allowed(obj)) return;
  descriptor_entry_t *entry = get_or_create_sym_desc(js, obj, sym_off);

  apply_registry_desc_update(
    entry, flags, false,
    true, true, getter,
    false, false, js_mkundef()
  );
}

void js_set_sym_setter_desc(ant_t *js, ant_value_t obj, ant_value_t sym, ant_value_t setter, int flags) {
  const char *desc = vtype(sym) == kTypeSymbol ? js_sym_desc(sym) : NULL;
  setter = js_cfunc_expose_property(js, setter, JS_CFUNC_SETTER, desc, desc ? strlen(desc) : 0, true);
  assert(is_canonical_desc_obj(obj) && "js_set_sym_setter_desc expects js_as_obj(...)");

  if (!is_canonical_desc_obj(obj)) return;
  if (vtype(sym) != kTypeSymbol) return;

  ant_offset_t sym_off = (ant_offset_t)vdata(sym);
  uint32_t slot = 0;
  ant_shape_prop_t *prop = ensure_symbol_shape_prop_at(js, obj, sym_off, &slot);

  if (prop) {
    apply_shape_desc_update(
      prop, flags, false,
      false, false, js_mkundef(),
      true, true, setter
    );
    clear_accessor_slot(obj, slot);

    ant_symbol_property_mutation_invalidate(js, js_obj_ptr(obj), sym_off);
    ant_ic_epoch_bump(js);
    gc_write_barrier(js, js_obj_ptr(js_as_obj(obj)), setter);

    return;
  }

  if (!desc_registry_allowed(obj)) return;
  descriptor_entry_t *entry = get_or_create_sym_desc(js, obj, sym_off);

  apply_registry_desc_update(
    entry, flags, false,
    false, false, js_mkundef(),
    true, true, setter
  );
}

void js_descriptor_registry_cleanup(ant_t *js) {
  if (!js) return;

  descriptor_entry_t *entry, *tmp;
  HASH_ITER(hh, js->desc_registry, entry, tmp) {
    HASH_DEL(js->desc_registry, entry);
    free(entry);
  }
}

bool js_array_make_length_readonly(ant_t *js, ant_value_t arr) {
  ant_value_t obj = js_as_obj(arr);
  ant_object_t *ptr = js_obj_ptr(obj);
  if (!ptr || ptr->type_tag != kTypeArray) return false;
  
  ptr->flags.is_exotic = 1;
  descriptor_entry_t *entry = get_or_create_desc(js, obj, "length", 6);
  if (!entry) return false;
  
  entry->writable = false;
  entry->enumerable = false;
  entry->configurable = false;
  ant_ic_epoch_bump(js);
  
  return true;
}

static bool define_lookup_existing_meta(
  ant_t *js, ant_value_t obj,
  bool sym_key,
  ant_offset_t sym_off,
  const char *prop_str,
  ant_offset_t prop_len,
  ant_prop_loc_t existing,
  prop_meta_t *out
) {
  if (sym_key && lookup_symbol_prop_meta(js, obj, sym_off, out)) return true;
  if (!sym_key && lookup_string_prop_meta(js, obj, prop_str, (size_t)prop_len, out)) return true;
  if (!existing.obj || !existing.obj->shape) return false;

  const ant_shape_prop_t *prop = ant_shape_prop_at(existing.obj->shape, existing.slot);
  if (!prop) return false;

  prop_meta_from_shape(out, prop);
  return true;
}

static bool define_desc_field(
  ant_t *js, ant_value_t descriptor, const char *name, size_t name_len,
  bool *has_out, ant_value_t *val_out, ant_value_t *err_out
) {
  ant_prop_loc_t off = lkp(js, descriptor, name, name_len);
  if (off.obj) {
    const ant_shape_prop_t *meta = prop_shape_meta(off);
    if (!meta || (!meta->has_getter && !meta->has_setter)) {
      *has_out = true;
      *val_out = js_prop_load(off);
      return true;
    }
  } else if (!lkp_proto(js, descriptor, name, name_len).obj) return true;

  ant_value_t v = js_getprop_fallback_len(js, descriptor, name, name_len);

  if (is_err(v)) {
    *err_out = v;
    return false;
  }

  if (Ant_Exception_Pending(js)) {
    *err_out = js_throw(js, js_take_thrown(js, js_mkundef()));
    return false;
  }

  *has_out = true;
  *val_out = v;
  
  return true;
}

static bool define_desc_read_plain(ant_t *js, ant_value_t descriptor, define_desc_raw_t *raw) {
  ant_object_t *ptr = js_obj_ptr(descriptor);
  if (!ptr || ptr->flags.is_exotic || !ptr->shape) return false;

  const char *names[DESC_FIELD_COUNT] = {
    js->intern.enumerable,
    js->intern.configurable,
    js->intern.value,
    js->intern.writable,
    js->intern.get,
    js->intern.set,
  };

  bool missing = false;
  for (int f = 0; f < DESC_FIELD_COUNT; f++) {
    int32_t slot = ant_shape_lookup_interned(ptr->shape, names[f]);
    const ant_shape_prop_t *prop = slot >= 0 ? ant_shape_prop_at(ptr->shape, (uint32_t)slot) : NULL;
    if (!prop) { missing = true; continue; }
    if (prop->has_getter || prop->has_setter) return false;
    raw->has[f] = true;
    raw->val[f] = ant_object_prop_get_unchecked(ptr, (uint32_t)slot);
  }

  int depth = 0;
  for (ant_value_t proto = ptr->proto; missing && is_object_type(proto);) {
    const ant_object_t *p = js_obj_ptr(proto);
    if (!p || p->flags.is_exotic || !p->shape || ++depth > MAX_PROTO_CHAIN_DEPTH) return false;
    for (int f = 0; f < DESC_FIELD_COUNT; f++)
      if (!raw->has[f] && ant_shape_lookup_interned(p->shape, names[f]) >= 0) return false;
    proto = p->proto;
  }
  
  return true;
}

static inline bool define_desc_accessor_invalid(ant_value_t fn) {
  uint8_t t = vtype(fn);
  return t != kTypeFunction && t != kTypeBuiltin && t != kTypeUndefined;
}

static const struct { const char *name; size_t len; } define_desc_field_names[DESC_FIELD_COUNT] = {
  { "enumerable", 10 },
  { "configurable", 12 },
  { "value", 5 },
  { "writable", 8 },
  { "get", 3 },
  { "set", 3 },
};

static void describe_value_text(ant_t *js, ant_value_t value, char *buf, size_t cap) {
  uint8_t t = vtype(value);
  if (t == kTypeArray) {
    snprintf(buf, cap, "%s", js_is_arguments(value) ? "[object Arguments]" : "[object Array]");
    return;
  }
  if (is_object_type(value) && t != kTypeFunction) {
    ant_offset_t len = 0;
    const char *name = get_class_name(js, value, &len, NULL);
    snprintf(buf, cap, "#<%.*s>", name ? (int)len : 6, name ? name : "Object");
    return;
  }
  char stack_buf[96];
  js_cstr_t text = js_to_cstr(js, value, stack_buf, sizeof(stack_buf));
  snprintf(buf, cap, "%.*s", (int)text.len, text.ptr ? text.ptr : "");
  if (text.needs_free) free((void *)text.ptr);
}

static __attribute__((noinline, cold)) ant_value_t descriptor_value_error(ant_t *js, const char *prefix, ant_value_t value) {
  char text[128];
  describe_value_text(js, value, text, sizeof(text));
  return js_mkerr(js, "%s%s", prefix, text);
}

static ant_value_t define_desc_check(ant_t *js, ant_value_t descriptor, const define_desc_raw_t *raw) {
  if (raw->has[DESC_GET] && define_desc_accessor_invalid(raw->val[DESC_GET]))
    return descriptor_value_error(js, "Getter must be a function: ", raw->val[DESC_GET]);
  if (raw->has[DESC_SET] && define_desc_accessor_invalid(raw->val[DESC_SET]))
    return descriptor_value_error(js, "Setter must be a function: ", raw->val[DESC_SET]);
  if ((raw->has[DESC_VALUE] || raw->has[DESC_WRITABLE]) && (raw->has[DESC_GET] || raw->has[DESC_SET]))
    return descriptor_value_error(js, "Invalid property descriptor. Cannot both specify accessors and a value or writable attribute, ", descriptor);
  return js_mkundef();
}

ant_value_t define_desc_read(ant_t *js, ant_value_t descriptor, define_desc_raw_t *raw) {
  const __typeof__(define_desc_field_names[0]) *fields = define_desc_field_names;

  bool proxy = is_proxy(descriptor);
  if (proxy || !define_desc_read_plain(js, descriptor, raw)) for (int f = 0; f < DESC_FIELD_COUNT; f++) {
    ant_value_t err = 0;
    
    if (proxy) {
      ant_value_t has = proxy_has(js, descriptor, fields[f].name, fields[f].len);
      
      if (is_err(has)) return has;
      if (!js_truthy(js, has)) continue;
      
      ant_value_t val = proxy_get(js, descriptor, fields[f].name, fields[f].len);
      if (is_err(val)) return val;
      
      raw->has[f] = true;
      raw->val[f] = val;
    } 
    
    else if (!define_desc_field(js, descriptor, fields[f].name, fields[f].len, &raw->has[f], &raw->val[f], &err)) return err;
    if (f == DESC_GET && raw->has[f] && define_desc_accessor_invalid(raw->val[f]))
      return descriptor_value_error(js, "Getter must be a function: ", raw->val[f]);
  }

  return define_desc_check(js, descriptor, raw);
}

static void array_materialize_dense_for_define(ant_t *js, ant_value_t obj, const char *key, size_t klen) {
  if (!key || !array_obj_ptr(obj)) return;

  unsigned long idx = 0;
  if (!parse_array_index(key, klen, ((ant_offset_t)UINT32_MAX), &idx)) return;

  ant_offset_t doff = get_dense_buf(obj);
  ant_offset_t cap = doff ? dense_capacity(doff) : 0;

  for (ant_offset_t i = 0; i < cap; i++) {
    ant_value_t existing = dense_get(doff, i);
    if (is_empty_slot(existing)) continue;

    char idxstr[16];
    size_t idxlen = uint_to_str(idxstr, sizeof(idxstr), (unsigned)i);

    dense_set(js, doff, i, T_EMPTY);
    mkprop(
      js, obj, js_mkstr(js, idxstr, idxlen), existing,
      ANT_PROP_ATTR_ENUMERABLE | ANT_PROP_ATTR_WRITABLE | ANT_PROP_ATTR_CONFIGURABLE
    );
  }

  ant_object_t *ptr = array_obj_ptr(obj);
  if (ptr) ptr->flags.fast_array = 0;
  array_mark_may_have_holes(obj);
}

static __attribute__((noinline)) bool string_wrapper_define(
  ant_t *js, ant_value_t obj, ant_value_t as_obj, const char *key, size_t key_len,
  const define_desc_fields_t *desc, ant_value_t *result
) {
  ant_value_t current = js_mkundef();
  bool is_length = false;
  if (!js_try_get_string_own_exotic(js, as_obj, key, key_len, &current, &is_length)) return false;

  bool compatible =
    !desc->accessor && !(desc->has_writable && desc->writable) &&
    !(desc->has_configurable && desc->configurable) &&
    !(desc->has_enumerable && desc->enumerable == is_length) &&
    !(desc->has_value && !same_value_values(js, desc->value, current));
    
  *result = compatible ? obj : js_mkerr_typed(js, JS_ERR_TYPE, "Cannot redefine property: %.*s", (int)key_len, key);
  return true;
}

static ant_value_t define_desc_object(ant_t *js, const define_desc_raw_t *raw) {
  GC_ROOT_SAVE(mark, js);
  define_desc_raw_t pinned = *raw;
  for (int f = 0; f < DESC_FIELD_COUNT; f++) GC_ROOT_PIN(js, pinned.val[f]);
  
  ant_value_t object = Ant_Object_CreateBare(js);
  GC_ROOT_PIN(js, object);
  
  for (int f = 0; f < DESC_FIELD_COUNT && !is_err(object); f++) if (pinned.has[f]) {
    ant_value_t key = js_mkstr(js, define_desc_field_names[f].name, define_desc_field_names[f].len);
    ant_value_t set = js_setprop(js, object, key, pinned.val[f]);
    if (is_err(set)) object = set;
  }
  
  GC_ROOT_RESTORE(js, mark);
  return object;
}

// TODO: decompose this huge function into small pieces
static ant_value_t object_define_property_keyed(
  ant_t *js, ant_value_t obj, property_key_view_t *key, 
  ant_value_t descriptor, const define_desc_raw_t *pre_raw, define_desc_fields_t *fields
) {
  bool sym_key = key->is_symbol;
  ant_value_t prop = key->js_key;

  if (vtype(descriptor) == kTypeBuiltin) descriptor = js_cfunc_promote(js, descriptor);
  if (!pre_raw && !is_object_type(descriptor)) return descriptor_value_error(js, "Property description must be an object: ", descriptor);
  
  ant_value_t as_obj = js_as_obj(obj);
  if (is_proxy(as_obj)) {
    if (!is_object_type(descriptor)) descriptor = define_desc_object(js, pre_raw);
    if (is_err(descriptor)) return descriptor;
    prop = property_key_view_materialize(js, key);
    if (is_err(prop)) return prop;
    ant_value_t proxy_result = proxy_define_property(js, as_obj, prop, descriptor);
    if (is_err(proxy_result)) return proxy_result;
    if (!js_truthy(js, proxy_result))
      return proxy_trap_error(js, "defineProperty", "trap returned falsy");
    return obj;
  }
  
  ant_offset_t prop_len = 0;
  const char *prop_str = NULL;
  ant_offset_t sym_off = 0;
  
  if (sym_key) {
    sym_off = (ant_offset_t)vdata(prop);
    const char *desc = js_sym_desc(prop);
    prop_str = desc ? desc : "symbol";
    prop_len = (ant_offset_t)strlen(prop_str);
  } else {
    prop_str = key->bytes;
    prop_len = (ant_offset_t)key->length;
  }
    
  bool has_value = false, has_get = false, has_set = false;
  bool has_writable = false, has_enumerable = false, has_configurable = false;
  ant_value_t value = js_mkundef();
  ant_value_t getter_val = js_mkundef();
  ant_value_t setter_val = js_mkundef();
  bool writable = false, enumerable = false, configurable = false;

  {
    define_desc_raw_t raw = {0};
    if (pre_raw) raw = *pre_raw;
    else {
      ant_value_t read_err = define_desc_read(js, descriptor, &raw);
      if (is_err(read_err)) return read_err;
    }

    has_enumerable = raw.has[DESC_ENUMERABLE];
    enumerable = has_enumerable && js_truthy(js, raw.val[DESC_ENUMERABLE]);
    
    has_configurable = raw.has[DESC_CONFIGURABLE];
    configurable = has_configurable && js_truthy(js, raw.val[DESC_CONFIGURABLE]);
    has_value = raw.has[DESC_VALUE];
    
    if (has_value) value = raw.val[DESC_VALUE];
    has_writable = raw.has[DESC_WRITABLE];
    writable = has_writable && js_truthy(js, raw.val[DESC_WRITABLE]);
    has_get = raw.has[DESC_GET];
    
    if (has_get) getter_val = raw.val[DESC_GET];
    has_set = raw.has[DESC_SET];
    
    if (has_set) setter_val = raw.val[DESC_SET];
  }

  if ((has_value || has_writable) && (has_get || has_set))
    return descriptor_value_error(js, "Invalid property descriptor. Cannot both specify accessors and a value or writable attribute, ", descriptor);

  define_desc_fields_t desc_fields = {
    .accessor = has_get || has_set, .has_value = has_value,
    .has_writable = has_writable, .writable = writable,
    .has_enumerable = has_enumerable, .enumerable = enumerable,
    .has_configurable = has_configurable, .configurable = configurable,
    .value = value,
  };
  
  if (fields) *fields = desc_fields;

  const ant_object_t *def_ptr = js_obj_ptr(as_obj);
  if (!sym_key && def_ptr && def_ptr->flags.is_exotic && def_ptr->exotic_ops && def_ptr->exotic_ops->definer) {
    bool plain_data =
      !has_get && !has_set && !(has_configurable && !configurable) &&
      !(has_enumerable && !enumerable) && !(has_writable && !writable);
    ant_value_t defined = def_ptr->exotic_ops->definer(js, as_obj, prop_str, (size_t)prop_len, value, has_value, plain_data);
    if (defined == T_EMPTY) return js_mkerr_typed(js, JS_ERR_TYPE, "Cannot define property %.*s", (int)prop_len, prop_str);
    if (is_err(defined)) return define_abrupt(fields, defined);
    if (vtype(defined) != kTypeUndefined) return obj;
  }

  ant_object_t *wrapper_ptr = !sym_key && vtype(as_obj) == kTypeObject ? (ant_object_t *)vptr(as_obj) : NULL;
  if (wrapper_ptr && wrapper_ptr->extra_slots) {
    define_desc_fields_t wrapper_desc = desc_fields;
    ant_value_t wrapper_result;
    if (string_wrapper_define(js, obj, as_obj, prop_str, (size_t)prop_len, &wrapper_desc, &wrapper_result)) return wrapper_result;
  }

  ant_object_t *arr_ptr = 
    (!sym_key && is_length_key(prop_str, prop_len))
    ? real_array_obj_ptr(as_obj) : NULL;
    
  if (arr_ptr) {
    if (has_get || has_set) return js_mkerr(js, "Invalid property descriptor. Cannot use accessors for array length");
    if ((has_enumerable && enumerable) || (has_configurable && configurable))
      return js_mkerr(js, "Cannot redefine array length attributes");

    ant_offset_t new_len = (ant_offset_t)arr_ptr->u.array.len;
    if (has_value && vtype(value) != kTypeNumber) {
      double number = js_to_number(js, value);
      if (Ant_Exception_Pending(js)) return define_abrupt(fields, Ant_Exception_Current(js));
      value = Ant_Value_FromNumber(number);
    }
    if (has_value) {
      ant_value_t len_err = validate_array_length(js, value);
      if (is_err(len_err)) return define_abrupt(fields, len_err);
      new_len = (ant_offset_t)Ant_Value_AsNumberUnchecked(value);
    }
    
    if (array_length_readonly(js, as_obj)) {
      if ((has_writable && writable) || new_len != get_array_length(js, as_obj))
        return js_mkerr_typed(js, JS_ERR_TYPE, "Cannot redefine property: length");
      return obj;
    }

    if (__builtin_expect(!array_length_has_guarded_elements(arr_ptr), 1)) {
      array_length_set_unguarded(js, as_obj, new_len);
      if (has_writable && !writable && !js_array_make_length_readonly(js, as_obj)) return js_mkerr(js, "oom");
      return obj;
    }

    ant_offset_t final_len = new_len;
    ant_value_t truncated = array_truncate_length(js, as_obj, new_len, &final_len);
    
    if (is_err(truncated)) return define_abrupt(fields, truncated);
    if (has_writable && !writable && !js_array_make_length_readonly(js, as_obj)) return js_mkerr(js, "oom");
    if (final_len != new_len) return array_truncate_stopped_error(js, final_len);
    
    return obj;
  }
  
  unsigned long define_idx = 0;
  if (
    !sym_key && real_array_obj_ptr(as_obj) &&
    parse_array_index(prop_str, (size_t)prop_len, ((ant_offset_t)UINT32_MAX), &define_idx) &&
    (ant_offset_t)define_idx >= get_array_length(js, as_obj) && array_length_readonly(js, as_obj)
  ) return js_mkerr_typed(js, JS_ERR_TYPE, "Cannot define property %lu, object is not extensible", define_idx);
  
  if (!sym_key && !has_get && !has_set && has_value
      && has_writable && writable
      && has_enumerable && enumerable
      && has_configurable && configurable) {
    ant_object_t *fast_arr = array_obj_ptr(as_obj);
    unsigned long fast_idx = 0;

    if (
      fast_arr && fast_arr->flags.fast_array &&
      fast_arr->flags.extensible && !fast_arr->flags.sealed && !fast_arr->flags.frozen &&
      parse_array_index(prop_str, (size_t)prop_len, ((ant_offset_t)UINT32_MAX), &fast_idx)
    ) {
      ant_offset_t doff = get_dense_buf(as_obj);
      if (doff && (ant_offset_t)fast_idx < dense_capacity(doff)) {
        dense_set(js, doff, (ant_offset_t)fast_idx, value);
        array_define_or_set_index(js, as_obj, prop_str, (size_t)prop_len);
        return obj;
      }
    }
  }

  if (!sym_key) array_materialize_dense_for_define(js, as_obj, prop_str, (size_t)prop_len);

  ant_prop_loc_t existing_off = sym_key
    ? lkp_sym(as_obj, sym_off)
    : lkp(js, as_obj, prop_str, prop_len);
  
  prop_meta_t existing_meta;
  bool has_existing_meta = define_lookup_existing_meta(
    js, as_obj, sym_key, sym_off, prop_str, 
    prop_len, existing_off, &existing_meta
  );
  
  bool has_existing_prop = existing_off.obj || has_existing_meta;
  ant_object_t *obj_ptr = js_obj_ptr(as_obj);
  
  if (!has_existing_prop) {
    if (obj_ptr && obj_ptr->flags.frozen)
      return js_mkerr(js, "Cannot define property %.*s, object is not extensible", (int)prop_len, prop_str);
    if (obj_ptr && obj_ptr->flags.sealed)
      return js_mkerr(js, "Cannot define property %.*s, object is not extensible", (int)prop_len, prop_str);
    if (obj_ptr && !obj_ptr->flags.extensible)
      return js_mkerr(js, "Cannot define property %.*s, object is not extensible", (int)prop_len, prop_str);
  }
  
  if (has_existing_meta) {
    if (!has_writable) writable = existing_meta.writable;
    if (!has_enumerable) enumerable = existing_meta.enumerable;
    if (!has_configurable) configurable = existing_meta.configurable;
  }

  if (has_existing_prop) {
    bool existing_accessor = has_existing_meta && (existing_meta.has_getter || existing_meta.has_setter);
    bool new_accessor = has_get || has_set;
    bool new_data = has_value || has_writable;
    bool existing_nonconfig =
      (has_existing_meta && !existing_meta.configurable) ||
      (existing_off.obj && is_nonconfig_prop(existing_off)) ||
      (obj_ptr && (obj_ptr->flags.sealed || obj_ptr->flags.frozen));
    bool existing_readonly =
      (has_existing_meta && !existing_accessor && !existing_meta.writable) ||
      (existing_off.obj && is_const_prop(existing_off)) ||
      (obj_ptr && obj_ptr->flags.frozen);
    if (existing_nonconfig) {
      if (has_configurable && configurable) return js_mkerr(js,
        "Cannot redefine property %.*s: cannot change configurable from false to true",
        (int)prop_len, prop_str
      );
      if (has_existing_meta && has_enumerable && enumerable != existing_meta.enumerable)
        return js_mkerr(js,
          "Cannot redefine property %.*s: cannot change enumerable of a non-configurable property",
          (int)prop_len, prop_str
        );
      if (has_existing_meta && !existing_meta.writable && has_writable && writable)
        return js_mkerr(js,
          "Cannot redefine property %.*s: cannot change writable from false to true",
          (int)prop_len, prop_str
        );
      if (has_existing_meta && existing_accessor && new_data)
        return js_mkerr(js,
          "Cannot redefine property %.*s: cannot convert accessor property to data property",
          (int)prop_len, prop_str
        );
      if (has_existing_meta && !existing_accessor && new_accessor)
        return js_mkerr(js,
          "Cannot redefine property %.*s: cannot convert data property to accessor property",
          (int)prop_len, prop_str
        );
      if (has_existing_meta && existing_accessor && has_get) {
        ant_value_t existing_getter = existing_meta.has_getter ? existing_meta.getter : js_mkundef();
        if (getter_val != existing_getter)
          return js_mkerr(js,
            "Cannot redefine property %.*s: cannot replace getter of a non-configurable property",
            (int)prop_len, prop_str
          );
      }
      if (has_existing_meta && existing_accessor && has_set) {
        ant_value_t existing_setter = existing_meta.has_setter ? existing_meta.setter : js_mkundef();
        if (setter_val != existing_setter)
          return js_mkerr(js,
            "Cannot redefine property %.*s: cannot replace setter of a non-configurable property",
            (int)prop_len, prop_str
          );
      }
      if (existing_readonly && has_writable && writable)
        return js_mkerr(js,
          "Cannot redefine property %.*s: cannot change writable from false to true",
          (int)prop_len, prop_str
        );
      if (existing_readonly && has_value &&
          (!existing_off.obj || !same_value_values(js, js_prop_load(existing_off), value)))
        return js_mkerr(js, "Cannot assign to read-only property '%.*s'", (int)prop_len, prop_str);
    }
  }

  if (!has_value && !has_writable && !has_get && !has_set && has_existing_prop) {
    int desc_flags =
      (writable ? JS_DESC_W : 0) |
      (enumerable ? JS_DESC_E : 0) |
      (configurable ? JS_DESC_C : 0);

    if (has_existing_meta && (existing_meta.has_getter || existing_meta.has_setter)) {
      if (sym_key) {
        if (existing_meta.has_getter) js_set_sym_getter_desc(js, as_obj, prop, existing_meta.getter, desc_flags);
        if (existing_meta.has_setter) js_set_sym_setter_desc(js, as_obj, prop, existing_meta.setter, desc_flags);
      } else {
        if (existing_meta.has_getter && existing_meta.has_setter)
          js_set_accessor_desc(js, as_obj, prop_str, prop_len, existing_meta.getter, existing_meta.setter, desc_flags);
        else if (existing_meta.has_getter)
          js_set_getter_desc(js, as_obj, prop_str, prop_len, existing_meta.getter, desc_flags);
        else
          js_set_setter_desc(js, as_obj, prop_str, prop_len, existing_meta.setter, desc_flags);
      }
    } else if (!sym_key) {
      js_set_descriptor(js, as_obj, prop_str, prop_len, desc_flags);
    } else {
      uint8_t attrs = 0;
      if (writable) attrs |= ANT_PROP_ATTR_WRITABLE;
      if (enumerable) attrs |= ANT_PROP_ATTR_ENUMERABLE;
      if (configurable) attrs |= ANT_PROP_ATTR_CONFIGURABLE;

      if (existing_off.obj) {
        if (!js_obj_ensure_unique_shape(existing_off.obj)) return js_mkerr(js, "oom");
        ant_shape_set_attrs_symbol(js, existing_off.obj->shape, sym_off, attrs);
      }
    }

    if (!sym_key) array_define_or_set_index(js, as_obj, prop_str, (size_t)prop_len);
    return obj;
  }

  if (has_get || has_set) {
    int desc_flags = 
      (enumerable ? JS_DESC_E : 0) |
      (configurable ? JS_DESC_C : 0);
    
    ant_value_t getter = has_get ? getter_val : js_mkundef();
    ant_value_t setter = has_set ? setter_val : js_mkundef();

    if (sym_key) {
      if (has_get) js_set_sym_getter_desc(js, as_obj, prop, getter, desc_flags);
      if (has_set) js_set_sym_setter_desc(js, as_obj, prop, setter, desc_flags);
    } else {
      if (has_get && has_set) js_set_accessor_desc(js, as_obj, prop_str, prop_len, getter, setter, desc_flags);
      else if (has_get) js_set_getter_desc(js, as_obj, prop_str, prop_len, getter, desc_flags);
      else js_set_setter_desc(js, as_obj, prop_str, prop_len, setter, desc_flags);
    }
  } else {
    int desc_flags = 
      (writable ? JS_DESC_W : 0) | 
      (enumerable ? JS_DESC_E : 0) | 
      (configurable ? JS_DESC_C : 0);

    uint8_t attrs = 0;
    if (writable) attrs |= ANT_PROP_ATTR_WRITABLE;
    if (enumerable) attrs |= ANT_PROP_ATTR_ENUMERABLE;
    if (configurable) attrs |= ANT_PROP_ATTR_CONFIGURABLE;

    if (existing_off.obj) {
      bool is_frozen = obj_ptr ? obj_ptr->flags.frozen : false;
      bool is_nonconfig = is_nonconfig_prop(existing_off) || is_frozen;
      bool is_readonly = is_const_prop(existing_off) || is_frozen;
      
      if (is_nonconfig) {
        if (configurable) return js_mkerr(js,
          "Cannot redefine property %.*s: cannot change configurable from false to true", 
          (int)prop_len, prop_str
        );
        
        if (is_readonly && has_writable && writable) return js_mkerr(js, 
          "Cannot redefine property %.*s: cannot change writable from false to true", 
          (int)prop_len, prop_str
        );
      }
      
      if (is_nonconfig && is_readonly && has_value && !same_value_values(js, js_prop_load(existing_off), value))
        return js_mkerr(js, "Cannot assign to read-only property '%.*s'", (int)prop_len, prop_str);
      if (has_value) {
        js_prop_store(js, existing_off, value);
        if (sym_key) ant_symbol_property_mutation_invalidate(js, existing_off.obj, sym_off);
      }
      if (!sym_key) js_set_descriptor(js, as_obj, prop_str, prop_len, desc_flags);

      if (existing_off.obj) {
        if (!js_obj_ensure_unique_shape(existing_off.obj)) return js_mkerr(js, "oom");
        if (sym_key) ant_shape_set_attrs_symbol(js, existing_off.obj->shape, sym_off, attrs);
        else ant_shape_set_attrs_interned(js, existing_off.obj->shape, intern_string(js, prop_str, prop_len), attrs);
        ant_shape_clear_accessor_slot(js, existing_off.obj->shape, existing_off.slot);
      }
    } else {
      if (!has_value) value = js_mkundef();      
      ant_value_t added = sym_key
        ? mkprop_exact_attrs(js, as_obj, prop, value, attrs)
        : mkprop_bytes_exact_attrs(
          js, as_obj, prop_str, 
          (size_t)prop_len, value, attrs
        );
      if (is_err(added)) return added;
    }
  }

  if (!sym_key) array_define_or_set_index(js, as_obj, prop_str, (size_t)prop_len);

  return obj;
}

ant_value_t object_define_property_reporting(
  ant_t *js, ant_value_t obj, ant_value_t prop, ant_value_t descriptor,
  const define_desc_raw_t *raw, bool *abrupt
) {
  if (vtype(obj) == kTypeBuiltin) obj = js_cfunc_promote(js, obj);
  if (!is_object_type(obj))
    return js_mkerr(js, "Object.defineProperty called on non-object");

  char key_stack[128];
  property_key_view_t key;
  ant_value_t error = property_key_view_init(
    js, prop, &key, 
    key_stack, sizeof(key_stack)
  );
  
  if (is_err(error)) {
    property_key_view_dispose(&key);
    return error;
  }

  bool root_key = property_key_view_requires_pin(&key, is_proxy(js_as_obj(obj)));
  size_t root_mark = root_key ? gc_root_scope(js) : 0;
  if (root_key) GC_ROOT_PIN(js, key.js_key);

  define_desc_fields_t fields = {0};
  ant_value_t result = object_define_property_keyed(js, obj, &key, descriptor, raw, &fields);
  
  if (!is_err(result) && !key.is_symbol && js_is_arguments(obj))
    js_arguments_after_define(js, obj, key.bytes, key.length, &fields);
  
  property_key_view_dispose(&key);
  if (root_key) GC_ROOT_RESTORE(js, root_mark);
  if (abrupt) *abrupt = fields.abrupt;
  
  return result;
}

ant_value_t object_define_property(
  ant_t *js, ant_value_t obj, ant_value_t prop, ant_value_t descriptor
) {
  return object_define_property_reporting(js, obj, prop, descriptor, NULL, NULL);
}

static ant_value_t define_desc_snapshot(ant_t *js, ant_value_t descriptor) {
  if (vtype(descriptor) == kTypeBuiltin) descriptor = js_cfunc_promote(js, descriptor);
  if (!is_object_type(descriptor)) return descriptor_value_error(js, "Property description must be an object: ", descriptor);
  
  define_desc_raw_t raw = {0};
  ant_value_t err = define_desc_read(js, descriptor, &raw);
  return is_err(err) ? err : define_desc_object(js, &raw);
}

static ant_value_t reflect_define_property(ant_t *js, ant_value_t obj, ant_value_t prop, ant_value_t descriptor) {
  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, obj);
  
  char key_stack[128];
  property_key_view_t key;
  
  ant_value_t key_val = property_key_view_init(js, prop, &key, key_stack, sizeof(key_stack));
  if (!is_err(key_val)) key_val = property_key_view_materialize(js, &key);
  property_key_view_dispose(&key);
  
  if (is_err(key_val)) {
    GC_ROOT_RESTORE(js, mark);
    return key_val;
  }
  
  GC_ROOT_PIN(js, key_val);
  ant_value_t snapshot = define_desc_snapshot(js, descriptor);
  
  if (is_err(snapshot)) {
    GC_ROOT_RESTORE(js, mark);
    return snapshot;
  }
  
  GC_ROOT_PIN(js, snapshot);
  if (is_object_type(obj) && is_proxy(js_as_obj(obj))) {
    ant_value_t trapped = proxy_define_property(js, js_as_obj(obj), key_val, snapshot);
    GC_ROOT_RESTORE(js, mark);
    return is_err(trapped) ? trapped : js_bool(js_truthy(js, trapped));
  }
  
  ant_value_t saved = Ant_Exception_Peek(js);
  GC_ROOT_PIN(js, saved);
  bool abrupt = false;
  ant_value_t result = object_define_property_reporting(js, obj, key_val, snapshot, NULL, &abrupt);
  
  if (is_err(result) && !abrupt) {
    Ant_Exception_Set(js, saved);
    result = js_false;
  } else if (!is_err(result)) result = js_true;
  
  GC_ROOT_RESTORE(js, mark);
  return result;
}

ant_value_t js_define_property(ant_t *js, ant_value_t obj, ant_value_t prop, ant_value_t descriptor, bool reflect_mode) {
  return reflect_mode
    ? reflect_define_property(js, obj, prop, descriptor)
    : object_define_property(js, obj, prop, descriptor);
}

static ant_value_t object_define_properties_from_slots(
  ant_t *js, ant_value_t obj, ant_object_t *ptr, uint32_t count,
  const define_desc_raw_t *raws, bool symbols
) {
  for (uint32_t i = 0; i < count; i++) {
    const ant_shape_prop_t *prop = ant_shape_prop_at(ptr->shape, i);
    if (!prop || !(ant_shape_get_attrs(ptr->shape, i) & ANT_PROP_ATTR_ENUMERABLE)) continue;
    if ((prop->type == ANT_SHAPE_KEY_SYMBOL) != symbols) continue;
    ant_value_t key = symbols
      ? mkval(kTypeSymbol, prop->key.sym_off)
      : js_mkstr(js, prop->key.interned, intern_length(prop->key.interned));
    ant_value_t desc = ant_object_prop_get_unchecked(ptr, i);
    ant_value_t result = object_define_property_reporting(js, obj, key, desc, &raws[i], NULL);
    if (is_err(result)) return result;
  }
  return obj;
}

static ant_value_t object_define_properties_from_data(ant_t *js, ant_value_t obj, ant_value_t props, bool *handled) {
  *handled = false;
  
  if (vtype(props) != kTypeObject || is_proxy(props) || is_proxy(js_as_obj(obj))) return js_mkundef();
  if (js_as_obj(obj) == props) return js_mkundef();
  ant_object_t *ptr = js_obj_ptr(props);
  
  if (!ptr || !ptr->shape || ptr->flags.is_exotic || ant_object_extra_count(ptr) != 0) return js_mkundef();
  if (ant_shape_may_have_index_keys(ptr->shape)) return js_mkundef();

  uint32_t count = ant_shape_count(ptr->shape);
  for (uint32_t i = 0; i < count; i++) {
    const ant_shape_prop_t *prop = ant_shape_prop_at(ptr->shape, i);
    if (!prop || i >= ptr->prop_count) return js_mkundef();
    if (prop->has_getter || prop->has_setter) return js_mkundef();
  }

  define_desc_raw_t stack_raws[32];
  define_desc_raw_t *raws = count <= 32 ? stack_raws : calloc(count, sizeof(*raws));
  
  if (!raws) return js_mkerr(js, "oom");
  if (raws == stack_raws) memset(stack_raws, 0, count * sizeof(*raws));

  ant_value_t result = js_mkundef();
  bool target_is_descriptor = false;
  
  for (uint32_t i = 0; i < count; i++) {
    if (!(ant_shape_get_attrs(ptr->shape, i) & ANT_PROP_ATTR_ENUMERABLE)) continue;
    ant_value_t desc = ant_object_prop_get_unchecked(ptr, i);
    if (vtype(desc) == kTypeBuiltin) goto done;
    target_is_descriptor |= desc == js_as_obj(obj);
    
    if (!is_object_type(desc)) {
      *handled = true;
      result = descriptor_value_error(js, "Property description must be an object: ", desc);
      goto done;
    }
    
    if (!define_desc_read_plain(js, desc, &raws[i])) goto done;
    
    ant_value_t err = define_desc_check(js, desc, &raws[i]);
    if (is_err(err)) {
      *handled = true;
      result = err;
      goto done;
    }
  }

  *handled = true;
  GC_ROOT_SAVE(root_mark, js);
  for (uint32_t i = 0; target_is_descriptor && i < count; i++)
    for (int f = 0; f < DESC_FIELD_COUNT; f++) if (raws[i].has[f]) GC_ROOT_PIN(js, raws[i].val[f]);

  result = object_define_properties_from_slots(js, obj, ptr, count, raws, false);
  if (!is_err(result)) result = object_define_properties_from_slots(js, obj, ptr, count, raws, true);
  GC_ROOT_RESTORE(js, root_mark);

done:
  if (raws != stack_raws) free(raws);
  return result;
}

static ant_value_t object_define_properties_get(ant_t *js, ant_value_t source, bool proxied, ant_value_t key) {
  if (proxied) return proxy_get_val(js, source, key);
  if (vtype(key) == kTypeSymbol) return js_get_sym(js, source, key);
  
  ant_offset_t key_len = 0;
  ant_offset_t key_off = vstr(js, key, &key_len);
  
  return js_getprop_fallback_len(js, source, (const char *)(uintptr_t)key_off, (size_t)key_len);
}

ant_value_t object_define_properties(ant_t *js, ant_value_t obj, ant_value_t props) {
  uint8_t pt = vtype(props);
  if (pt == kTypeUndefined || pt == kTypeNull)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Cannot convert undefined or null to object");

  if (pt == kTypeString) {
    props = builtin_Object(js, &props, 1, js_mkundef());
    if (is_err(props)) return props;
  } else if (pt == kTypeBuiltin) props = js_cfunc_promote(js, props);
  else if (!is_object_type(props)) return obj;

  GC_ROOT_SAVE(root_mark, js);
  GC_ROOT_PIN(js, obj);
  GC_ROOT_PIN(js, props);

  bool handled = false;
  ant_value_t result = object_define_properties_from_data(js, obj, props, &handled);
  if (handled) {
    GC_ROOT_RESTORE(js, root_mark);
    return result;
  }

  ant_value_t source = js_as_obj(props);
  bool proxied = is_proxy(source);
  
  ant_value_t keys = proxied ? proxy_enum(js, source, OBJ_ENUM_KEYS) : js_own_property_keys(js, props, true, true);
  GC_ROOT_PIN(js, keys);
  
  ant_value_t descriptors = is_err(keys) ? keys : mkarr(js);
  GC_ROOT_PIN(js, descriptors);
  
  if (is_err(descriptors)) {
    GC_ROOT_RESTORE(js, root_mark);
    return descriptors;
  }

  ant_offset_t key_count = js_arr_len(js, keys);
  for (ant_offset_t i = 0; i < key_count; i++) {
    ant_value_t desc = object_define_properties_get(js, source, proxied, js_arr_get(js, keys, i));
    if (vtype(desc) == kTypeBuiltin) desc = js_cfunc_promote(js, desc);
    if (!is_err(desc) && !is_object_type(desc))
      desc = descriptor_value_error(js, "Property description must be an object: ", desc);
    if (is_err(desc)) {
      GC_ROOT_RESTORE(js, root_mark);
      return desc;
    }
    js_arr_push(js, descriptors, desc);
  }

  for (ant_offset_t i = 0; i < key_count; i++) {
    ant_value_t desc = define_desc_snapshot(js, js_arr_get(js, descriptors, i));
    if (is_err(desc)) {
      GC_ROOT_RESTORE(js, root_mark);
      return desc;
    }
    arr_set(js, descriptors, i, desc);
  }

  for (ant_offset_t i = 0; i < key_count; i++) {
    result = object_define_property(js, obj, js_arr_get(js, keys, i), js_arr_get(js, descriptors, i));
    if (is_err(result)) {
      GC_ROOT_RESTORE(js, root_mark);
      return result;
    }
  }

  GC_ROOT_RESTORE(js, root_mark);
  return obj;
}

static ant_value_t descriptor_template(ant_t *js, bool accessor) {
  ant_value_t *slot = accessor ? &js->builtins.accessor_desc_template : &js->builtins.data_desc_template;
  if (vtype(*slot) == kTypeObject) return *slot;
  
  ant_value_t template = Ant_Object_Create(js);
  if (is_err(template)) return template;
  
  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, template);
  
  ant_value_t first = mkprop_interned(js, template, accessor ? js->intern.get : js->intern.value, js_mkundef(), 0);
  ant_value_t second = mkprop_interned(js, template, accessor ? js->intern.set : js->intern.writable, accessor ? js_mkundef() : js_false, 0);
  
  ant_value_t third = mkprop_interned(js, template, js->intern.enumerable, js_false, 0);
  ant_value_t fourth = mkprop_interned(js, template, js->intern.configurable, js_false, 0);
  
  GC_ROOT_RESTORE(js, mark);

  if (is_err(first)) return first;
  if (is_err(second)) return second;
  if (is_err(third)) return third;
  if (is_err(fourth)) return fourth;
  
  ant_object_t *ptr = js_obj_ptr(template);
  if (!ptr->shape || ant_shape_count(ptr->shape) != 4 || ptr->prop_count != 4)
    return js_mkerr(js, "invalid descriptor template");
  
  *slot = template;
  return template;
}

static ant_value_t from_property_descriptor(
  ant_t *js, bool accessor, ant_value_t first, ant_value_t second, bool enumerable, bool configurable
) {
  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, first);
  GC_ROOT_PIN(js, second);
  
  ant_value_t template = descriptor_template(js, accessor);
  ant_value_t desc = is_err(template) ? template : js_mkobj_from_template(js, template);
  GC_ROOT_RESTORE(js, mark);
  if (is_err(desc)) return desc;
  
  ant_object_t *obj = js_obj_ptr(desc);
  ant_object_prop_set_unchecked(obj, 0, first);
  ant_object_prop_set_unchecked(obj, 1, second);
  ant_object_prop_set_unchecked(obj, 2, js_bool(enumerable));
  ant_object_prop_set_unchecked(obj, 3, js_bool(configurable));
  gc_write_barrier(js, obj, first);
  gc_write_barrier(js, obj, second);
  
  return desc;
}

ant_value_t js_from_data_descriptor(ant_t *js, ant_value_t value, bool writable, bool enumerable, bool configurable) {
  return from_property_descriptor(js, false, value, js_bool(writable), enumerable, configurable);
}

ant_value_t js_from_accessor_descriptor(ant_t *js, ant_value_t getter, ant_value_t setter, bool enumerable, bool configurable) {
  return from_property_descriptor(js, true, getter, setter, enumerable, configurable);
}

ant_value_t js_get_own_property_descriptor(ant_t *js, ant_value_t obj, ant_value_t key) {
  ant_value_t args[2] = { obj, key };
  return builtin_object_getOwnPropertyDescriptor(js, args, 2, js_mkundef());
}
