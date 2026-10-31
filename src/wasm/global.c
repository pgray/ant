#include "wasm_internal.h"

ant_value_t js_wasm_global_value_getter(ant_params_t) {
  wasm_extern_handle_t *handle = wasm_extern_handle(call_this, WASM_EXTERN_WRAP_GLOBAL);
  wasm_val_t value = WASM_INIT_VAL;

  if (!handle || !handle->as.global) return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Global");

  wasm_global_get(handle->as.global, &value);
  if (handle->use_cached_value) handle->cached_value = value;
  return wasm_value_to_js(js, &value);
}

ant_value_t js_wasm_global_value_setter(ant_params_t) {
  wasm_extern_handle_t *handle = wasm_extern_handle(call_this, WASM_EXTERN_WRAP_GLOBAL);
  wasm_globaltype_t *type = NULL;
  
  const wasm_valtype_t *content = NULL;
  wasm_val_t value = WASM_INIT_VAL;

  if (!handle || !handle->as.global)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Global");
  if (nargs < 1) return js_mkundef();

  type = wasm_global_type(handle->as.global);
  if (!type) return js_mkerr(js, "Failed to inspect WebAssembly.Global");
  if (wasm_globaltype_mutability(type) != WASM_VAR) {
    wasm_globaltype_delete(type);
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Global is immutable");
  }

  content = wasm_globaltype_content(type);
  if (!js_value_to_wasm(js, args[0], wasm_valtype_kind(content), &value)) {
    wasm_globaltype_delete(type);
    return js_mkerr_typed(js, JS_ERR_TYPE, "Unsupported global value");
  }

  wasm_global_set(handle->as.global, &value);
  if (handle->use_cached_value) handle->cached_value = value;
  wasm_globaltype_delete(type);
  
  return js_mkundef();
}

ant_value_t js_wasm_global_value_of(ant_params_t) {
  return js_wasm_global_value_getter(js, NULL, 0, js_mkundef(), call_this, call_func);
}

ant_value_t js_wasm_global_ctor(ant_params_t) {
  ant_value_t descriptor;
  ant_value_t mutable_val;
  
  const char *value_type;
  ant_offset_t value_type_len = 0;
  bool ok = false;
  
  wasm_valkind_t kind;
  wasm_store_t *store = NULL;
  wasm_valtype_t *valtype = NULL;
  wasm_globaltype_t *globaltype = NULL;
  wasm_global_t *global = NULL;
  wasm_val_t initial = WASM_INIT_VAL;
  ant_value_t result;

  if (vtype(call_new_target) == kTypeUndefined)
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Global constructor requires 'new'");
  if (nargs < 1 || !is_object_type(args[0]))
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Global requires a descriptor object");
  if (!ensure_wasm_engine())
    return js_mkerr(js, "Failed to initialize WebAssembly engine");

  descriptor = args[0];
  value_type = get_str_prop(js, descriptor, "value", 5, &value_type_len);
  if (!value_type)
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Global descriptor requires a value type");

  kind = wasm_valkind_from_string(value_type, value_type_len, &ok);
  if (!ok)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Unsupported WebAssembly.Global value type");

  if (!js_value_to_wasm(js, nargs >= 2 ? args[1] : Ant_Value_FromNumber(0), kind, &initial))
    return js_mkerr_typed(js, JS_ERR_TYPE, "Unsupported WebAssembly.Global initial value");

  mutable_val = js_get(js, descriptor, "mutable");
  store = wasm_store_new(g_wasm_engine);
  if (!store) return js_mkerr(js, "Failed to create WebAssembly store");

  valtype = wasm_valtype_new(kind);
  globaltype = wasm_globaltype_new(valtype, js_truthy(js, mutable_val) ? WASM_VAR : WASM_CONST);
  global = globaltype ? wasm_global_new(store, globaltype, &initial) : NULL;

  wasm_globaltype_delete(globaltype);

  if (!global) {
    wasm_store_delete(store);
    return js_throw(js, wasm_make_runtime_error(js, "Failed to create WebAssembly.Global"));
  }

  result = wasm_wrap_extern_object(
    js, WASM_EXTERN_WRAP_GLOBAL, js->builtins.wasm_global_proto, BRAND_WASM_GLOBAL,
    store, true, global, js_mkundef()
  );
  
  if (vtype(result) == kTypeObject) {
    wasm_extern_handle_t *handle = wasm_extern_handle(result, WASM_EXTERN_WRAP_GLOBAL);
    if (handle) {
      handle->use_cached_value = true;
      handle->cached_value = initial;
    }
    js_set_finalizer(result, wasm_extern_finalize);
  }
  
  return result;
}
