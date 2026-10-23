#include "wasm_internal.h"

static bool wasm_parse_table_descriptor(ant_t *js, ant_value_t descriptor, wasm_limits_t *limits, wasm_valkind_t *element, ant_value_t *err) {
  ant_value_t initial_val, maximum_val;
  const char *element_name;
  ant_offset_t element_len = 0;
  uint32_t initial, maximum = wasm_limits_max_default;

  if (!is_object_type(descriptor)) {
    *err = js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table requires a descriptor object");
    return false;
  }

  element_name = get_str_prop(js, descriptor, "element", 7, &element_len);
  if (!element_name) {
    *err = js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table descriptor requires an element type");
    return false;
  }
  if (element_len == 7 && memcmp(element_name, "anyfunc", 7) == 0) {
    *element = WASM_FUNCREF;
  } else {
    bool ok = false;
    *element = wasm_valkind_from_string(element_name, element_len, &ok);
    if (!ok || (*element != WASM_FUNCREF && *element != WASM_EXTERNREF)) {
      *err = js_mkerr_typed(js, JS_ERR_TYPE, "Unsupported WebAssembly.Table element type");
      return false;
    }
  }

  initial_val = js_get(js, descriptor, "initial");
  if (vtype(initial_val) == kTypeUndefined || !wasm_to_page_count(js, initial_val, &initial)) {
    *err = js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table descriptor requires a valid initial length");
    return false;
  }

  maximum_val = js_get(js, descriptor, "maximum");
  if (vtype(maximum_val) != kTypeUndefined) {
    if (!wasm_to_page_count(js, maximum_val, &maximum)) {
      *err = js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table descriptor maximum must be a valid length");
      return false;
    }
    if (maximum < initial) {
      *err = js_mkerr_typed(js, JS_ERR_RANGE, "WebAssembly.Table maximum must be greater than or equal to initial");
      return false;
    }
  }

  limits->min = initial;
  limits->max = maximum;
  *err = js_mkundef();
  return true;
}

static bool wasm_table_value_ok(ant_t *js, wasm_valkind_t element, ant_value_t value, ant_value_t *err) {
  if (element == WASM_EXTERNREF) return true;
  if (vtype(value) == kTypeNull || vtype(value) == kTypeUndefined) return true;
  if (is_callable(value)) {
    ant_value_t state = js_get_slot(value, SLOT_DATA);
    wasm_func_handle_t *func_handle = is_object_type(state)
      ? (wasm_func_handle_t *)js_get_native(state, WASM_FUNC_STATE_TAG)
      : NULL;
    if (func_handle && func_handle->func) return true;
  }
  *err = js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table expects a WebAssembly function or null");
  return false;
}

static ant_value_t wasm_table_entry_get(ant_t *js, ant_value_t entries, uint32_t index) {
  char key[16];
  snprintf(key, sizeof(key), "%u", index);
  return js_get(js, entries, key);
}

static void wasm_table_entry_set(ant_t *js, ant_value_t entries, uint32_t index, ant_value_t value) {
  char key[16];
  snprintf(key, sizeof(key), "%u", index);
  js_set(js, entries, key, value);
}

ant_value_t js_wasm_table_length_getter(ant_params_t) {
  wasm_extern_handle_t *handle = wasm_extern_handle(js->this_val, WASM_EXTERN_WRAP_TABLE);
  if (!handle || !handle->as.table)  return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Table");
  if (handle->standalone_table && handle->standalone_table_element != WASM_FUNCREF)
    return js_mknum((double)handle->standalone_table_size);
  return js_mknum((double)wasm_table_size(handle->as.table));
}

ant_value_t js_wasm_table_get(ant_params_t) {
  wasm_extern_handle_t *handle = wasm_extern_handle(js->this_val, WASM_EXTERN_WRAP_TABLE);
  wasm_ref_t *ref;
  wasm_func_t *func;
  uint32_t index;

  if (!handle || !handle->as.table)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Table");

  if (!wasm_to_page_count(js, nargs > 0 ? args[0] : js_mknum(0), &index))
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table.get requires a valid index");

  if (handle->standalone_table && handle->standalone_table_element != WASM_FUNCREF) {
    ant_value_t entries;
    if (index >= handle->standalone_table_size)
      return js_mkerr_typed(js, JS_ERR_RANGE, "WebAssembly.Table index is out of bounds");
    entries = js_get(js, js->this_val, "__wasmTableEntries");
    return wasm_table_entry_get(js, entries, index);
  }

  {
    ant_value_t entries = js_get(js, js->this_val, "__wasmTableEntries");
    if (is_object_type(entries)) {
      ant_value_t retained = wasm_table_entry_get(js, entries, index);
      if (vtype(retained) != kTypeUndefined && vtype(retained) != kTypeNull) return retained;
    }
  }

  ref = wasm_table_get(handle->as.table, index);

  if (!ref) return js_mknull();
  func = wasm_ref_as_func(ref);
  
  if (func) {
    ant_value_t owner = wasm_find_instance_owner(js, func->inst_comm_rt);
    if (!is_object_type(owner)) owner = js_get_slot(js->this_val, SLOT_ENTRIES);
    ant_value_t wrapped = wasm_wrap_func(js, func, owner, true);
    wasm_ref_delete(ref);
    return wrapped;
  }
  
  wasm_ref_delete(ref);
  return js_mknull();
}

ant_value_t js_wasm_table_set(ant_params_t) {
  wasm_extern_handle_t *handle = wasm_extern_handle(js->this_val, WASM_EXTERN_WRAP_TABLE);
  wasm_ref_t *ref = NULL;
  uint32_t index;
  ant_value_t err;

  if (!handle || !handle->as.table) return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Table");
  if (nargs < 2) return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table.set requires 2 arguments");

  if (!wasm_to_page_count(js, args[0], &index))
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table.set requires a valid index");

  if (handle->standalone_table && handle->standalone_table_element != WASM_FUNCREF) {
    ant_value_t entries;
    if (index >= handle->standalone_table_size)
      return js_mkerr_typed(js, JS_ERR_RANGE, "WebAssembly.Table index is out of bounds");
    if (!wasm_table_value_ok(js, handle->standalone_table_element, args[1], &err))
      return err;
    entries = js_get(js, js->this_val, "__wasmTableEntries");
    wasm_table_entry_set(js, entries, index, args[1]);
    return js_mkundef();
  }

  if (!(vtype(args[1]) == kTypeNull || vtype(args[1]) == kTypeUndefined)) {
    ant_value_t state;
    wasm_func_handle_t *func_handle;
    
    if (!is_callable(args[1]))
      return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table.set expects a WebAssembly function or null");
    
    state = js_get_slot(args[1], SLOT_DATA);
    func_handle = is_object_type(state)
      ? (wasm_func_handle_t *)js_get_native(state, WASM_FUNC_STATE_TAG)
      : NULL;
    if (!func_handle || !func_handle->func)
      return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table.set expects a WebAssembly function or null");
    
    ref = wasm_func_as_ref(func_handle->func);
    if (!ref) return js_mkerr_typed(js, JS_ERR_TYPE, "Failed to materialize WebAssembly function reference");
  }

  if (!wasm_table_set(handle->as.table, index, ref)) {
    if (ref) wasm_ref_delete(ref);
    return js_mkerr_typed(js, JS_ERR_TYPE, "Failed to update WebAssembly.Table");
  }

  {
    ant_value_t entries = js_get(js, js->this_val, "__wasmTableEntries");
    if (is_object_type(entries))
      wasm_table_entry_set(js, entries, index, vtype(args[1]) == kTypeUndefined ? js_mknull() : args[1]);
  }
  
  return js_mkundef();
}

ant_value_t js_wasm_table_grow(ant_params_t) {
  wasm_extern_handle_t *handle = wasm_extern_handle(js->this_val, WASM_EXTERN_WRAP_TABLE);
  uint32_t delta, old_size, needed;
  ant_value_t init, err, entries;
  wasm_ref_t *ref = NULL;

  if (!handle || !handle->as.table)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Table");
  if (!wasm_to_page_count(js, nargs > 0 ? args[0] : js_mknum(0), &delta))
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table.grow requires a valid length");

  if (!handle->standalone_table)
    return js_mkerr_typed(js, JS_ERR_TYPE, "The current WAMR backend does not support host-side table.grow");

  init = nargs > 1 ? args[1] : js_mknull();
  if (!wasm_table_value_ok(js, handle->standalone_table_element, init, &err))
    return err;

  if (handle->standalone_table_element == WASM_FUNCREF) {
    if (!(vtype(init) == kTypeNull || vtype(init) == kTypeUndefined)) {
      ant_value_t state;
      wasm_func_handle_t *func_handle;

      state = js_get_slot(init, SLOT_DATA);
      func_handle = is_object_type(state)
        ? (wasm_func_handle_t *)js_get_native(state, WASM_FUNC_STATE_TAG)
        : NULL;
      if (!func_handle || !func_handle->func)
        return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table.grow expects a WebAssembly function or null");

      ref = wasm_func_as_ref(func_handle->func);
      if (!ref) return js_mkerr_typed(js, JS_ERR_TYPE, "Failed to materialize WebAssembly function reference");
    }

    old_size = wasm_table_size(handle->as.table);
    if (!wasm_table_grow(handle->as.table, delta, ref)) {
      if (ref) wasm_ref_delete(ref);
      return js_mkerr_typed(js, JS_ERR_RANGE, "Failed to grow WebAssembly.Table");
    }
    entries = js_get(js, js->this_val, "__wasmTableEntries");
    if (is_object_type(entries)) {
      for (uint32_t i = old_size; i < old_size + delta; i++)
        wasm_table_entry_set(js, entries, i,
                             vtype(init) == kTypeUndefined ? js_mknull() : init);
    }
    handle->standalone_table_size = wasm_table_size(handle->as.table);
    return js_mknum((double)old_size);
  }

  old_size = handle->standalone_table_size;
  if (delta == 0) return js_mknum((double)old_size);
  if (delta > UINT32_MAX - old_size)
    return js_mkerr_typed(js, JS_ERR_RANGE, "Failed to grow WebAssembly.Table");
  needed = old_size + delta;
  if (needed > handle->standalone_table_max_size)
    return js_mkerr_typed(js, JS_ERR_RANGE, "Failed to grow WebAssembly.Table");

  entries = js_get(js, js->this_val, "__wasmTableEntries");
  for (uint32_t i = old_size; i < needed; i++) wasm_table_entry_set(js, entries, i, init);
  handle->standalone_table_size = needed;
  return js_mknum((double)old_size);
}

ant_value_t js_wasm_table_ctor(ant_params_t) {
  wasm_limits_t limits;
  wasm_valkind_t element;
  wasm_store_t *store = NULL;
  wasm_valtype_t *valtype = NULL;
  wasm_tabletype_t *tabletype = NULL;
  wasm_table_t *table = NULL;
  ant_value_t err, result;

  if (vtype(call_new_target) == kTypeUndefined)
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Table constructor requires 'new'");
  if (!ensure_wasm_engine())
    return js_mkerr(js, "Failed to initialize WebAssembly engine");
  if (!wasm_parse_table_descriptor(js, nargs > 0 ? args[0] : js_mkundef(), &limits, &element, &err))
    return err;

  store = wasm_store_new(g_wasm_engine);
  if (!store) return js_mkerr(js, "Failed to create WebAssembly store");

  valtype = wasm_valtype_new(element);
  tabletype = valtype ? wasm_tabletype_new(valtype, &limits) : NULL;
  table = tabletype ? wasm_table_new(store, tabletype, NULL) : NULL;
  wasm_tabletype_delete(tabletype);

  if (!table) {
    wasm_store_delete(store);
    return js_throw(js, wasm_make_runtime_error(js, "Failed to create WebAssembly.Table"));
  }

  result = wasm_wrap_extern_object(
    js, WASM_EXTERN_WRAP_TABLE, js->builtins.wasm_table_proto, BRAND_WASM_TABLE,
    store, true, table, js_mkundef()
  );
  if (vtype(result) == kTypeObject) {
    ant_value_t entries = Ant_Object_CreateBare(js);
    wasm_extern_handle_t *handle = wasm_extern_handle(result, WASM_EXTERN_WRAP_TABLE);
    if (!handle || is_err(entries)) {
      js_clear_native(result, WASM_EXTERN_NATIVE_TAG);
      wasm_table_delete(table);
      wasm_store_delete(store);
      free(handle);
      return is_err(entries) ? entries : Ant_Error_CreateOutOfMemory(js);
    }

    handle->standalone_table = true;
    handle->standalone_table_element = element;
    handle->standalone_table_size = limits.min;
    handle->standalone_table_max_size = limits.max;
    for (uint32_t i = 0; i < limits.min; i++) wasm_table_entry_set(js, entries, i, js_mknull());
    js_set(js, result, "__wasmTableEntries", entries);
    js_set_descriptor(js, result, "__wasmTableEntries", 18, 0);
    js_set_finalizer(result, wasm_extern_finalize);
  }

  return result;
}
