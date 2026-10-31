#include <compat.h> // IWYU pragma: keep
#include <string.h>

#include "utf8.h"
#include "cfunc.h"
#include "arrays.h"
#include "props.h"
#include "proto.h"
#include "stringify.h"
#include "protectors.h"
#include "silver/call.h"
#include "arguments.h"
#include "modules/proxy.h"

static ant_value_t js_setprop_array_fast(
  ant_t *js, ant_value_t obj, ant_value_t k,
  ant_value_t v, ant_offset_t klen, const char *key, bool *declined
) {
  unsigned long idx;
  if (!parse_array_index(key, klen, (ant_offset_t)-1, &idx))
    return JS_SETPROP_ARRAY_NOT_HANDLED;
  
  ant_offset_t cur_len = get_array_length(js, obj);
  ant_offset_t doff = get_dense_buf(obj);
  ant_object_t *ptr = array_obj_ptr(obj);

  if (idx >= cur_len && array_length_readonly(js, obj))
    return ptr && array_store_reaches_proto(js, obj, ptr, idx)
      ? JS_SETPROP_ARRAY_NOT_HANDLED : array_grow_readonly_result(js, idx, v, declined);
  
  if (doff) {
    ant_offset_t dense_len = dense_iterable_length(js, obj);
    if (idx < dense_len) {
      if (ptr && ptr->flags.frozen) 
        return sv_is_strict_context(js) ? js_mkerr(js, "assignment to read-only array element") : set_declined(declined, v);
      if (is_empty_slot(dense_get(doff, (ant_offset_t)idx))) {
        if (array_store_reaches_proto(js, obj, ptr, idx)) return JS_SETPROP_ARRAY_NOT_HANDLED;
        ant_value_t extensibility_error = check_object_extensibility(js, obj);
        if (is_err(extensibility_error)) return extensibility_error;
        if (extensibility_error == js_false) return set_declined(declined, v);
      }
      dense_set(js, doff, (ant_offset_t)idx, v);
      return v;
    }

    ant_offset_t density_limit = dense_len * 4 > ARRAY_DENSE_GAP ? dense_len * 4 : ARRAY_DENSE_GAP;
    if (idx >= density_limit) goto sparse;
    if (array_store_reaches_proto(js, obj, ptr, idx)) return JS_SETPROP_ARRAY_NOT_HANDLED;
    
    ant_value_t extensibility_error = check_object_extensibility(js, obj);
    if (is_err(extensibility_error)) return extensibility_error;
    if (extensibility_error == js_false) return set_declined(declined, v);
    
    arr_set(js, obj, (ant_offset_t)idx, v);
    return v;
  }
  
  sparse:;
  if (idx < cur_len || (ptr && array_store_reaches_proto(js, obj, ptr, idx))) return JS_SETPROP_ARRAY_NOT_HANDLED;
  
  ant_value_t extensibility_error = check_object_extensibility(js, obj);
  if (is_err(extensibility_error)) return extensibility_error;
  if (extensibility_error == js_false) return set_declined(declined, v);
  
  ant_value_t result = mkprop(js, obj, k, v, 0);
  if (is_err(result)) return result;
  array_define_or_set_index(js, obj, key, (size_t)klen);
  
  return v;
}

static __attribute__((noinline)) bool setprop_sym_reaches_proxy(
  ant_t *js, ant_value_t obj, ant_value_t proxy, ant_value_t sym, ant_value_t v,
  bool keyed, bool *declined, ant_value_t *done, ant_value_t *next
) {
  ant_value_t target = js_mkundef();
  ant_value_t result = proxy_set_sym(js, proxy, sym, v, obj, &target);
  
  if (is_err(result)) *done = result;
  else if (result == js_false) *done = proxy_set_rejected_sym(js, sym, v, declined);
  else if (result == js_true) *done = v;
  else if (same_object_identity(proxy, obj)) *done = setprop_impl(js, target, sym, v, keyed, declined);
  
  else {
    *next = target;
    return false;
  }
  
  return true;
}

bool string_wrapper_owns_key(ant_t *js, ant_value_t obj, const char *key, size_t key_len) {
  ant_value_t str = js_string_wrapper_value(obj);
  if (vtype(str) != kTypeString) return false;
  unsigned long idx = 0;
  return is_length_key(key, key_len) || parse_array_index(key, key_len, str_utf16_len(js, str), &idx);
}

static inline bool string_wrapper_owns_index(ant_t *js, ant_value_t obj, uint32_t idx) {
  ant_value_t str = js_string_wrapper_value(obj);
  return vtype(str) == kTypeString && (ant_offset_t)idx < str_utf16_len(js, str);
}

static ant_value_t string_wrapper_write_refused(ant_t *js, const char *key, size_t key_len, ant_value_t value, bool *declined) {
  if (!sv_is_strict_context(js)) return set_declined(declined, value);
  
  return js_mkerr_typed(js, JS_ERR_TYPE,
    "Cannot assign to read only property '%.*s' of object '[object String]'", 
    (int)key_len, key
  );
}

static inline bool exotic_has_getter(const ant_object_t *ptr) {
  return ptr->flags.is_exotic && ptr->exotic_ops && ptr->exotic_ops->getter;
}

ant_value_t exotic_own_get(ant_t *js, ant_value_t obj, const ant_object_t *ptr, const char *key, size_t len) {
  return exotic_has_getter(ptr) ? ptr->exotic_ops->getter(js, obj, key, len) : js_mkundef();
}

bool array_own_value(ant_t *js, ant_value_t obj, const char *key, size_t len, ant_value_t *out) {
  if (!array_obj_ptr(obj)) return false;
  if (real_array_obj_ptr(obj) && is_length_key(key, len)) {
    *out = Ant_Value_FromNumber((double)get_array_length(js, obj));
    return true;
  }

  unsigned long idx;
  ant_offset_t doff = get_dense_buf(obj);
  if (!doff || !parse_array_index(key, len, dense_iterable_length(js, obj), &idx)) return false;
  ant_value_t value = dense_get(doff, (ant_offset_t)idx);
  if (is_empty_slot(value)) return false;
  *out = value;
  return true;
}

// TODO: break up function
ant_value_t setprop_impl(ant_t *js, ant_value_t obj, ant_value_t k, ant_value_t v, bool keyed, bool *declined) {
  uint8_t ot = vtype(obj);

  if (ot == kTypeBuiltin) {
    ant_value_t promoted = js_cfunc_promote(js, obj);
    return setprop_impl(js, promoted, k, v, keyed, declined);
  }

  // TODO: reduce nesting
  if ((ot == kTypeString || ot == kTypeNumber || ot == kTypeBool || ot == kTypeSymbol || ot == kTypeBigInt) && vtype(k) == kTypeSymbol) {
    proto_overflow_guard_t guard;
    proto_overflow_guard_init(&guard);
    for (ant_value_t cur = js_primitive_prototype(js, ot); is_object_type(cur);) {
      prop_meta_t meta;
      if (lookup_symbol_prop_meta(js, cur, (ant_offset_t)vdata(k), &meta)) {
        if (meta.has_setter && (vtype(meta.setter) == kTypeFunction || vtype(meta.setter) == kTypeBuiltin))
          return call_proto_accessor(js, obj, meta.setter, true, &v, 1, true);
        break;
      }
      cur = get_proto(js, cur);
      if (proto_overflow_guard_hit_cycle(js, &guard, cur)) break;
    }
    if (sv_is_strict_context(js)) return js_mkerr_typed(js, JS_ERR_TYPE, "Cannot create property on %s", typestr(ot));
    return v;
  }

  if (ot == kTypeString || ot == kTypeNumber || ot == kTypeBool || ot == kTypeSymbol || ot == kTypeBigInt) {
    ant_offset_t klen; ant_offset_t koff = vstr(js, k, &klen);
    const char *key = (char *)(uintptr_t)(koff);
    
    ant_value_t proto = js_primitive_prototype(js, ot);
    if (is_object_type(proto)) {
      ant_value_t setter = js_mkundef();
      bool has_setter = false;
      
      lkp_with_setter(js, proto, key, klen, &setter, &has_setter);
      if (has_setter && (vtype(setter) == kTypeFunction || vtype(setter) == kTypeBuiltin))
        return call_proto_accessor(js, obj, setter, true, &v, 1, true);
    }
    
    if (sv_is_strict_context(js)) return js_mkerr_typed(js, JS_ERR_TYPE,
      "Cannot create property '%.*s' on %s",
      (int)klen, key, typestr(ot)
    );
    
    return v;
  }

  if (vtype(obj) == kTypeFunction) obj = js_func_obj(obj);
  if (vtype(k) == kTypeSymbol) {
    ant_offset_t sym_off = (ant_offset_t)vdata(k);
    ant_value_t cur = obj;
    proto_overflow_guard_t guard;
    proto_overflow_guard_init(&guard);

    bool receiver_missed = false;
    for (bool on_receiver = true; is_object_type(cur); on_receiver = false) {
      ant_value_t cur_obj = js_as_obj(cur);
      ant_object_t *cur_ptr = (ant_object_t *)vptr(cur_obj);
      prop_meta_t meta;
      bool found;
      if (!cur_ptr->flags.is_exotic) {
        int32_t slot = cur_ptr->shape ? ant_shape_lookup_symbol(cur_ptr->shape, sym_off) : -1;
        const ant_shape_prop_t *prop = slot >= 0 ? ant_shape_prop_at(cur_ptr->shape, (uint32_t)slot) : NULL;
        found = prop != NULL;
        if (found) prop_meta_from_shape(&meta, prop);
        else if (on_receiver) receiver_missed = true;
      } else if (is_proxy(cur_obj)) {
        ant_value_t done;
        if (setprop_sym_reaches_proxy(js, obj, cur_obj, k, v, keyed, declined, &done, &cur)) return done;
        continue;
      } else found = lookup_symbol_prop_meta(js, cur_obj, sym_off, &meta);
      if (found) {
        if (meta.has_setter) {
          ant_value_t setter = meta.setter;
          if (vtype(setter) == kTypeFunction || vtype(setter) == kTypeBuiltin) {
            ant_value_t result = sv_vm_call(js->vm, js, setter, obj, &v, 1, NULL, js_mkundef());
            if (is_err(result)) return result;
            return v;
          }
        }
        if (meta.has_getter && !meta.has_setter) {
          if (sv_is_strict_context(js)) return js_mkerr_typed(js, JS_ERR_TYPE, "Cannot set property which has only a getter");
          return set_declined(declined, v);
        }
        if (!meta.has_getter && !meta.has_setter && !meta.writable) {
          if (sv_is_strict_context(js)) return js_mkerr(js, "assignment to read-only property");
          return set_declined(declined, v);
        }
        break;
      }
      
      ant_value_t proto = is_object_type(cur_ptr->proto) ? cur_ptr->proto : get_proto(js, cur_obj);
      if (!is_object_type(proto)) break;
      cur = proto;
      if (proto_overflow_guard_hit_cycle(js, &guard, cur)) break;
    }

    ant_prop_loc_t existing = receiver_missed ? ANT_PROP_LOC_NONE : lkp_sym(obj, sym_off);
    
    if (existing.obj) {
      if (is_const_prop(existing)) return js_mkerr(js, "assignment to constant");
      js_prop_store(js, existing, v);
      ant_symbol_property_mutation_invalidate(js, existing.obj, sym_off);
      return v;
    }
    
    {
      ant_value_t extensibility_error = check_object_extensibility(js, obj);
      if (is_err(extensibility_error)) return extensibility_error;
      if (extensibility_error == js_false) return set_declined(declined, v);
    }
    
    return mkprop_symbol_attrs_impl(
      js, obj, k, v, 0, 
      MKPROP_USE_DEFAULT_ATTRS | (keyed ? MKPROP_KEYED_STORE : 0)
    );
  }

  ant_offset_t klen; ant_offset_t koff = vstr(js, k, &klen);
  const char *key = (char *)(uintptr_t)(koff);
  regexp_note_property_write(js, key, (size_t)klen);
  if (string_wrapper_owns_key(js, obj, key, (size_t)klen))
    return string_wrapper_write_refused(js, key, (size_t)klen, v, declined);

  if (array_obj_ptr(obj) && klen > 0 && key[0] >= '0' && key[0] <= '9') {
    ant_arguments_state_t *args_state = js_arguments_state(obj);
    if (args_state && !args_state->in_setter && js_arguments_setter(js, obj, key, (size_t)klen, v)) return v;
  }
  
  if (array_obj_ptr(obj) && !is_proxy(obj) && klen > 0 && key[0] >= '0' && key[0] <= '9') {
    ant_value_t result = js_setprop_array_fast(js, obj, k, v, klen, key, declined);
    if (result != JS_SETPROP_ARRAY_NOT_HANDLED) return result;
  }

  ant_object_t *len_obj = real_array_obj_ptr(obj);
  if (len_obj && is_length_key(key, klen)) {
    ant_value_t err = validate_array_length(js, v);
    if (is_err(err)) return err;
    
    if (
      __builtin_expect((len_obj->flags.raw & (ANT_OBJECT_FLAG_FROZEN | ANT_OBJECT_FLAG_EXOTIC)) != 0, 0) && 
      array_length_readonly_slow(js, obj)
    ) return array_length_write_ignored(js, v, declined);
    
    ant_offset_t new_len_val = (ant_offset_t) Ant_Value_AsNumberUnchecked(v);
    if (__builtin_expect(!array_length_has_guarded_elements(len_obj), 1)) {
      array_length_set_unguarded(js, obj, new_len_val);
      return v;
    }
    
    ant_offset_t final_len = new_len_val;
    ant_value_t truncated = array_truncate_length(js, obj, new_len_val, &final_len);
    if (is_err(truncated)) return truncated;
    
    if (final_len != new_len_val) return sv_is_strict_context(js)
      ? array_truncate_stopped_error(js, final_len)
      : set_declined(declined, v);
    
    return v;
  }
  
  if (is_proxy(obj)) {
    ant_value_t result = proxy_set(js, obj, key, klen, v);
    if (is_err(result)) return result;
    return result == js_false ? proxy_set_rejected(js, key, klen, v, declined) : v;
  }
  
  if (try_dynamic_setter(js, obj, key, klen, v))
    return Ant_Exception_Pending(js) ? Ant_Exception_Current(js) : v;
  ant_prop_loc_t existing = lkp(js, obj, key, klen);
  
  {
    const char *interned_key = intern_string(js, key, (size_t)klen);
    bool found_desc = false;
    bool desc_on_receiver = false;
    bool desc_has_getter = false;
    bool desc_has_setter = false;
    bool desc_writable = true;
    ant_value_t desc_setter = js_mkundef();

    ant_value_t cur = obj;
    proto_overflow_guard_t guard;
    proto_overflow_guard_init(&guard);
    bool on_receiver = true;
    while (is_object_type(cur)) {
      ant_value_t cur_obj = js_as_obj(cur);
      if (!on_receiver && is_proxy(cur_obj)) {
        ant_value_t result = proxy_set_with_receiver(js, cur_obj, key, klen, v, obj);
        if (is_err(result)) return result;
        return result == js_false ? proxy_set_rejected(js, key, klen, v, declined) : v;
      }
      ant_object_t *cur_ptr = js_obj_ptr(cur_obj);
      if (!cur_ptr) break;

      bool found_here = false;
      if (cur_ptr->shape && interned_key) {
        int32_t slot = ant_shape_lookup_interned(cur_ptr->shape, interned_key);
        if (slot >= 0) {
          const ant_shape_prop_t *prop = ant_shape_prop_at(cur_ptr->shape, (uint32_t)slot);
          if (prop) {
            found_here = true;
            desc_has_getter = prop->has_getter != 0;
            desc_has_setter = prop->has_setter != 0;
            desc_writable = (prop->attrs & ANT_PROP_ATTR_WRITABLE) != 0;
            desc_setter = prop->setter;
          }
        }
      }

      ant_value_t exotic = !found_here && !on_receiver ? exotic_own_get(js, cur_obj, cur_ptr, key, klen) : js_mkundef();
      if (exotic == T_EMPTY) return v;
      if (vtype(exotic) != kTypeUndefined) {
        found_here = true;
        desc_has_getter = desc_has_setter = false;
        desc_writable = true;
      }

      if (!found_here && cur_ptr->flags.is_exotic) {
        descriptor_entry_t *desc = lookup_descriptor(js, cur_obj, key, klen);
        if (desc) {
          found_here = true;
          desc_has_getter = desc->has_getter;
          desc_has_setter = desc->has_setter;
          desc_writable = desc->writable;
          desc_setter = desc->setter;
        }
      }

      if (found_here) {
        found_desc = true;
        desc_on_receiver = on_receiver;
        break;
      }

      ant_value_t proto = get_proto(js, cur_obj);
      if (!is_object_type(proto)) break;
      cur = proto;
      on_receiver = false;
      if (proto_overflow_guard_hit_cycle(js, &guard, cur)) break;
    }

    if (!found_desc) goto no_descriptor;
    if (!desc_on_receiver && !desc_has_setter && !desc_has_getter && desc_writable) goto no_descriptor;
    
    if (desc_has_setter) {
      ant_value_t setter = desc_setter;
      uint8_t setter_type = vtype(setter);
      if (setter_type == kTypeFunction || setter_type == kTypeBuiltin) {
        ant_value_t result = sv_vm_call(js->vm, js, setter, obj, &v, 1, NULL, js_mkundef());
        if (is_err(result)) return result;
        return v;
      }
    }
    
    if (desc_has_getter && !desc_has_setter) {
      if (sv_is_strict_context(js)) return js_mkerr_typed(js, JS_ERR_TYPE, "Cannot set property which has only a getter");
      return set_declined(declined, v);
    }
    
    if (!desc_writable) {
      if (sv_is_strict_context(js)) return js_mkerr(js, "assignment to read-only property");
      return set_declined(declined, v);
    }
    
    if (!existing.obj) goto no_descriptor;
  }
  
no_descriptor:
  if (!existing.obj) goto create_new;
  if (is_const_prop(existing)) return js_mkerr(js, "assignment to constant");

  js_prop_store(js, existing, v);
  array_define_or_set_index(js, obj, key, (size_t)klen);
  return v;

create_new:
  {
    ant_value_t extensibility_error = check_object_extensibility(js, obj);
    if (is_err(extensibility_error)) return extensibility_error;
    if (extensibility_error == js_false) return set_declined(declined, v);
  }

  const char *interned_key = intern_string(js, key, (size_t)klen);
  if (!interned_key) return js_mkerr(js, "oom");
  ant_value_t result = keyed
    ? mkprop_interned_keyed(js, obj, interned_key, v)
    : mkprop_interned_exact(js, obj, interned_key, v, 0);
  if (is_err(result)) return result;
  array_define_or_set_index(js, obj, key, (size_t)klen);
  
  return v;
}

ant_value_t js_setprop(ant_t *js, ant_value_t obj, ant_value_t k, ant_value_t v) {
  return setprop_impl(js, obj, k, v, false, NULL);
}

ant_value_t js_setprop_keyed(ant_t *js, ant_value_t obj, ant_value_t k, ant_value_t v) {
  return setprop_impl(js, obj, k, v, true, NULL);
}

static ant_value_t setprop_index_impl(ant_t *js, ant_value_t obj, uint32_t idx, ant_value_t value, bool *declined) {
  ant_arguments_state_t *args_state = js_arguments_state(obj);
  if (args_state && !args_state->in_setter) {
    char key[16];
    size_t key_len = uint_to_str(key, sizeof(key), idx);
    if (js_arguments_setter(js, obj, key, key_len, value)) return value;
  }

  ant_object_t *ptr = array_obj_ptr(obj);
  if (ptr && (ant_offset_t)idx >= get_array_length(js, obj) && array_length_readonly(js, obj)) {
    if (!array_store_reaches_proto(js, obj, ptr, idx)) return array_grow_readonly_result(js, idx, value, declined);
    goto keyed;
  }
  if (ptr && !is_proxy(obj)) {
    ant_offset_t dense = get_dense_buf(obj);
    if (dense) {
      ant_offset_t dense_len = dense_iterable_length(js, obj);
      bool missing = (ant_offset_t)idx >= dense_len || is_empty_slot(dense_get(dense, (ant_offset_t)idx));
      if (missing && array_store_reaches_proto(js, obj, ptr, idx)) goto keyed;
      
      if ((ant_offset_t)idx < dense_len) {
        if (ptr->flags.frozen) 
          return sv_is_strict_context(js) ? js_mkerr(js, "assignment to read-only array element") : set_declined(declined, value);
        if (missing) {
          ant_value_t extensibility_error = check_object_extensibility(js, obj);
          if (is_err(extensibility_error)) return extensibility_error;
          if (extensibility_error == js_false) return set_declined(declined, value);
        }
        dense_set(js, dense, (ant_offset_t)idx, value);
        return value;
      }

      uint64_t density_limit = (uint64_t)dense_len * 4u > ARRAY_DENSE_GAP ? (uint64_t)dense_len * 4u : ARRAY_DENSE_GAP;
      if ((uint64_t)idx < density_limit) {
        ant_value_t extensibility_error = check_object_extensibility(js, obj);
        if (is_err(extensibility_error)) return extensibility_error;
        if (extensibility_error == js_false) return set_declined(declined, value);
        arr_set(js, obj, (ant_offset_t)idx, value);
        return value;
      }
    }
  }

keyed:;
  char key[16];
  size_t key_len = 0;
  
  ant_object_t *plain = vtype(obj) == kTypeObject ? js_obj_ptr(obj) : NULL;
  ant_value_t stored;
  
  if (plain && plain->flags.is_exotic) {
    if (buffer_typedarray_set_index(js, obj, idx, value, &stored)) return stored;
  } else if (plain && plain->shape) {
    const char *interned = index_key_find(js, idx, key, &key_len);
    int32_t slot = interned ? ant_shape_lookup_interned(plain->shape, interned) : -1;
    
    if (slot >= 0) {
      const ant_shape_prop_t *prop = ant_shape_prop_at(plain->shape, (uint32_t)slot);
      if (prop && !prop->has_getter && !prop->has_setter && (prop->attrs & ANT_PROP_ATTR_WRITABLE)) {
        ant_object_prop_set_unchecked(plain, (uint32_t)slot, value);
        gc_write_barrier(js, plain, value);
        return value;
      }
    } else if (
      plain->flags.extensible && index_keys_absent_from_protos(plain) &&
      (!plain->extra_slots || !string_wrapper_owns_index(js, obj, idx))
    ) {
      if (!interned) interned = index_key_intern(js, idx, key, &key_len);
      if (!interned) return js_mkerr(js, "oom");
      
      ant_value_t added = mkprop_interned_keyed(js, obj, interned, value);
      return is_err(added) ? added : value;
    }
  }
  
  if (!key_len) key_len = uint_to_str(key, sizeof(key), idx);
  ant_value_t property = js_mkstr_ascii(js, key, key_len);
  if (is_err(property)) return property;
  
  return setprop_impl(js, obj, property, value, true, declined);
}

ant_value_t js_setprop_index(ant_t *js, ant_value_t obj, uint32_t idx, ant_value_t value) {
  return setprop_index_impl(js, obj, idx, value, NULL);
}

ant_value_t js_setprop_throw(ant_t *js, ant_value_t obj, ant_value_t key, bool is_index, uint32_t idx, ant_value_t value) {
  sv_vm_t *vm = js->vm;
  int saved_fp = vm->jit.mode_fp;
  bool saved_strict = vm->jit.mode_strict;
  
  vm->jit.mode_fp = vm->fp;
  vm->jit.mode_strict = true;
  
  ant_value_t stored = is_index 
    ? js_setprop_index(js, obj, idx, value) 
    : js_setprop(js, obj, key, value);
  
  vm->jit.mode_fp = saved_fp;
  vm->jit.mode_strict = saved_strict;
  
  return stored;
}

ant_value_t js_setprop_index_throw(ant_t *js, ant_value_t obj, uint32_t idx, ant_value_t value) {
  return js_setprop_throw(js, obj, js_mkundef(), true, idx, value);
}

ant_value_t setprop_cstr(ant_t *js, ant_value_t obj, const char *key, size_t len, ant_value_t v) {
  obj = js_as_obj(obj);
  const char *interned = intern_string(js, key, len);
  if (!interned) return js_mkerr(js, "oom");
  return mkprop_interned(js, obj, interned, v, 0);
}

ant_value_t setprop_interned(ant_t *js, ant_value_t obj, const char *key, size_t len, ant_value_t v) {
  ant_value_t k = js_mkstr(js, key, len);
  if (is_err(k)) return k;
  return js_setprop(js, obj, k, v);
}

ant_value_t js_setprop_readonly_nonconfigurable(ant_t *js, ant_value_t obj, const char *key, size_t keylen, ant_value_t v) {
  ant_value_t k = js_mkstr(js, key, keylen);
  if (is_err(k)) return k;
  return mkprop_exact_attrs(js, obj, k, v, 0);
}

bool descriptor_has_own_key(ant_t *js, ant_value_t desc, const char *key, size_t key_len) {
  return vtype(desc) == kTypeObject && lkp(js, desc, key, key_len).obj;
}

bool descriptor_bool_value(ant_t *js, ant_value_t desc, const char *key, size_t key_len, bool default_value) {
  if (vtype(desc) != kTypeObject) return default_value;
  ant_prop_loc_t off = lkp(js, desc, key, key_len);
  if (!off.obj) return default_value;
  return js_truthy(js, js_prop_load(off));
}

static inline bool descriptor_is_accessor(ant_t *js, ant_value_t desc) {
  return descriptor_has_own_key(js, desc, "get", 3) || descriptor_has_own_key(js, desc, "set", 3);
}

static ant_value_t own_property_descriptor(ant_t *js, ant_value_t obj, ant_value_t key) {
  ant_value_t args[2] = { obj, key };
  return builtin_object_getOwnPropertyDescriptor(js, args, 2, js_mkundef(), js_mkundef(), js_mkundef());
}

static inline ant_value_t set_receiver_write_target(ant_t *js, ant_value_t receiver) {
  return vtype(receiver) == kTypeBuiltin ? js_cfunc_promote(js, receiver) : receiver;
}

static ant_value_t set_receiver_own_target(ant_t *js, ant_value_t receiver) {
  for (int depth = 0; depth < MAX_PROTO_CHAIN_DEPTH && is_object_type(receiver); depth++) {
    ant_value_t obj = js_as_obj(receiver);
    if (!is_proxy(obj)) return obj;
    
    ant_proxy_state_t *data = get_proxy_data(obj);
    if (!data || data->revoked || data->host || !is_object_type(data->handler)) return js_mkundef();
    
    if (data->plain_handler_epoch != js->ic.epoch) {
      if (
        !chain_lacks_plain_keys(data->handler, 
        js->intern.get_own_property_descriptor, js->intern.define_property)
      ) return js_mkundef();
      
      for (ant_value_t cur = data->handler; is_object_type(cur);) {
        ant_object_t *ptr = js_obj_ptr(js_as_obj(cur));
        ant_object_guard_absence(ptr);
        cur = ptr->proto;
      }
      
      data->plain_handler_epoch = js->ic.epoch;
    }
    
    receiver = data->target;
  }
  
  return js_mkundef();
}

static ant_value_t set_with_receiver_fast(ant_t *js, ant_value_t target, ant_value_t key, ant_value_t value, ant_value_t receiver) {
  if (vtype(key) != kTypeString) return T_EMPTY;
  
  size_t key_len = 0;
  const char *key_str = js_getstr(js, key, &key_len);
  if (!key_str || is_length_key(key_str, key_len) || is_array_index(key_str, (ant_offset_t)key_len)) return T_EMPTY;
  
  const char *interned = intern_find(js, key_str, key_len);
  ant_object_t *holder = NULL;
  int32_t slot = -1;
  bool chain_end = false;
  
  ant_value_t cur = target;
  for (int depth = 0; depth < MAX_PROTO_CHAIN_DEPTH && !holder && !chain_end; depth++) {
    ant_object_t *ptr = js_obj_ptr(js_as_obj(cur));
    if (!is_plain_set_target(ptr)) return T_EMPTY;
    
    slot = interned ? ant_shape_lookup_interned(ptr->shape, interned) : -1;
    if (slot >= 0) holder = ptr;
    else if (vtype(ptr->proto) == kTypeNull) chain_end = true;
    else if (is_object_type(ptr->proto)) cur = ptr->proto;
    else return T_EMPTY;
  }
  
  if (!holder && !chain_end) return T_EMPTY;
  if (holder) {
    const ant_shape_prop_t *prop = ant_shape_prop_at(holder->shape, (uint32_t)slot);
    if (!prop || (uint32_t)slot >= holder->prop_count) return T_EMPTY;
    
    if (prop->has_getter || prop->has_setter) {
      if (!prop->has_setter || !is_callable(prop->setter)) return js_false;
      ant_value_t called = sv_vm_call(js->vm, js, prop->setter, receiver, &value, 1, NULL, js_mkundef());
      return is_err(called) ? called : js_true;
    }
    
    if (!(ant_shape_get_attrs(holder->shape, (uint32_t)slot) & ANT_PROP_ATTR_WRITABLE)) return js_false;
  }
  
  ant_value_t write = set_receiver_write_target(js, receiver);
  if (is_err(write)) return write;
  if (!is_object_type(write)) return js_false;
  ant_value_t own = set_receiver_own_target(js, write);
  ant_object_t *own_ptr = is_object_type(own) ? js_obj_ptr(own) : NULL;
  if (!is_plain_set_target(own_ptr)) return T_EMPTY;
  
  int32_t own_slot = interned ? ant_shape_lookup_interned(own_ptr->shape, interned) : -1;
  if (own_slot >= 0) {
    if ((uint32_t)own_slot >= own_ptr->prop_count) return T_EMPTY;
    if (!own_writable_data_slot(own_ptr, own_slot)) return js_false;
    store_own_data_slot(js, own_ptr, (uint32_t)own_slot, interned, value);
    return js_true;
  }
  
  if (!own_ptr->flags.extensible) return js_false;
  const char *added_key = interned ? interned : intern_string(js, key_str, key_len);
  if (!added_key) return js_mkerr(js, "oom");
  
  ant_value_t added = mkprop_interned_exact(js, own, added_key, value, 0);
  return is_err(added) ? added : js_true;
}

static ant_value_t set_with_receiver(ant_t *js, ant_value_t target, ant_value_t key, ant_value_t value, ant_value_t receiver) {
  ant_value_t cur = target;
  ant_value_t desc = js_mkundef();
  
  for (int depth = 0; depth < MAX_PROTO_CHAIN_DEPTH; depth++) {
    ant_value_t obj = js_as_obj(cur);
    if (is_proxy(obj) && vtype(key) == kTypeSymbol) {
      ant_value_t proxy_target = js_mkundef();
      ant_value_t result = proxy_set_sym(js, obj, key, value, receiver, &proxy_target);
      if (vtype(result) != kTypeUndefined) return result;
      cur = proxy_target;
      continue;
    }
    
    if (is_proxy(obj)) {
      size_t key_len = 0;
      const char *key_str = js_getstr(js, key, &key_len);
      return proxy_set_with_receiver(js, obj, key_str, key_len, value, receiver);
    }
    
    if (vtype(key) == kTypeString) {
      size_t key_len = 0;
      const char *key_str = js_getstr(js, key, &key_len);
      ant_object_t *ptr = js_obj_ptr(obj);
      if (key_str && ptr && exotic_own_get(js, obj, ptr, key_str, key_len) == T_EMPTY) return js_true;
    }
    
    desc = own_property_descriptor(js, cur, key);
    if (is_err(desc) || vtype(desc) != kTypeUndefined) break;
    
    ant_value_t proto = js_get_proto(js, cur);
    if (!is_object_type(proto)) break;
    cur = proto;
  }
  
  if (is_err(desc)) return desc;
  if (descriptor_is_accessor(js, desc)) {
    ant_value_t setter = js_get(js, desc, "set");
    if (!is_callable(setter)) return js_false;
    
    ant_value_t called = sv_vm_call(js->vm, js, setter, receiver, &value, 1, NULL, js_mkundef());
    return is_err(called) ? called : js_true;
  }
  
  if (vtype(desc) != kTypeUndefined && !descriptor_bool_value(js, desc, "writable", 8, false)) return js_false;
  ant_value_t write = set_receiver_write_target(js, receiver);
  if (is_err(write)) return write;
  if (!is_object_type(write)) return js_false;
  
  ant_value_t existing = own_property_descriptor(js, write, key);
  if (is_err(existing)) return existing;
  
  bool fresh = vtype(existing) == kTypeUndefined;
  if (!fresh && (descriptor_is_accessor(js, existing) || !descriptor_bool_value(js, existing, "writable", 8, false)))
    return js_false;
  
  ant_value_t update = Ant_Object_CreateBare(js);
  if (is_err(update)) return update;
  
  GC_ROOT_SAVE(root_mark, js);
  GC_ROOT_PIN(js, update);
  
  js_set(js, update, "value", value);
  if (fresh) {
    js_set(js, update, "writable", js_true);
    js_set(js, update, "enumerable", js_true);
    js_set(js, update, "configurable", js_true);
  }
  
  GC_ROOT_PIN(js, write);
  ant_value_t result = js_define_property(js, write, key, update, true);
  GC_ROOT_RESTORE(js, root_mark);
  
  return result;
}

ant_value_t js_set_with_receiver(ant_t *js, ant_value_t target, ant_value_t key, ant_value_t value, ant_value_t receiver) {
  if (receiver == target) return js_set_reporting(js, target, key, value);
  
  GC_ROOT_SAVE(root_mark, js);
  GC_ROOT_PIN(js, target);
  GC_ROOT_PIN(js, key);
  GC_ROOT_PIN(js, value);
  GC_ROOT_PIN(js, receiver);
  
  ant_value_t result = set_with_receiver_fast(js, target, key, value, receiver);
  if (result == T_EMPTY) result = set_with_receiver(js, target, key, value, receiver);
  GC_ROOT_RESTORE(js, root_mark);
  
  return result;
}

static void js_set_interned_key(ant_t *js, ant_value_t obj, const char *key, size_t key_len, ant_value_t val) {
  ant_value_t target = obj;
  if (vtype(obj) == kTypeFunction) target = js_func_obj(obj);
  else if (vtype(obj) != kTypeObject) return;
  
  const char *interned = intern_string(js, key, key_len);
  if (!interned) {
    js_mkerr(js, "oom");
    return;
  }
  
  ant_prop_loc_t existing = lkp_interned(target, interned);
  if (!existing.obj) {
    mkprop_interned_attrs_impl(js, target, interned, key_len, val, 0, MKPROP_USE_DEFAULT_ATTRS);
    return;
  }
  
  if (is_const_prop(existing)) {
    js_mkerr(js, "assignment to constant");
    return;
  }
  
  js_prop_store(js, existing, val);
}

void js_set(ant_t *js, ant_value_t obj, const char *key, ant_value_t val) {
  size_t key_len = strlen(key);
  val = js_expose_cfunc_for_key(js, val, key, key_len);
  if (!is_err(val)) js_set_interned_key(js, obj, key, key_len, val);
}

void js_set_exact(ant_t *js, ant_value_t obj, const char *key, ant_value_t val) {
  js_set_interned_key(js, obj, key, strlen(key), val);
}

void js_set_tostringtag(ant_t *js, ant_value_t obj, ant_value_t tag) {
  mkprop(js, obj, js->sym.toStringTag_sym, tag, ANT_PROP_ATTR_CONFIGURABLE);
}

void js_set_sym(ant_t *js, ant_value_t obj, ant_value_t sym, ant_value_t val) {
  if (vtype(sym) != kTypeSymbol) return;
  ant_offset_t sym_off = (ant_offset_t)vdata(sym);
  
  const char *desc = js_sym_desc(sym);
  val = js_cfunc_expose_property(js, val, JS_CFUNC_VALUE, desc, desc ? strlen(desc) : 0, true);
  if (is_err(val)) return;
  
  if (vtype(obj) == kTypeFunction) obj = js_func_obj(obj);
  else if (is_object_type(obj)) obj = js_as_obj(obj);
  if (vtype(obj) != kTypeObject && vtype(obj) != kTypeArray) return;
  
  ant_prop_loc_t existing = lkp_sym(obj, sym_off);
  if (existing.obj) {
    if (is_const_prop(existing)) return;
    js_prop_store(js, existing, val);
    ant_symbol_property_mutation_invalidate(js, existing.obj, sym_off);
  } else mkprop(js, obj, sym, val, 0);
}

void js_set_symbol(ant_t *js, ant_value_t obj, const char *key, ant_value_t val) {
  ant_value_t sym = js_mksym_for(js, key);
  if (is_err(sym)) return;
  js_set_sym(js, obj, sym, val);
}

void js_merge_obj(ant_t *js, ant_value_t dst, ant_value_t src) {
  if (vtype(dst) != kTypeObject || vtype(src) != kTypeObject) return;
  ant_value_t as_src = js_as_obj(src);
  ant_object_t *src_obj = js_obj_ptr(as_src);
  if (!src_obj || !src_obj->shape) return;

  uint32_t count = ant_shape_count(src_obj->shape);
  for (uint32_t i = 0; i < count; i++) {
    const ant_shape_prop_t *prop = ant_shape_prop_at(src_obj->shape, i);
    if (!prop || prop->type != ANT_SHAPE_KEY_STRING) continue;

    const char *key = prop->key.interned;
    ant_value_t val = (i < src_obj->prop_count) ? ant_object_prop_get_unchecked(src_obj, i) : js_mkundef();
    js_setprop(js, dst, js_mkstr(js, key, intern_length(key)), val);
  }
}
