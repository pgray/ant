#include "wasm_internal.h"

static ant_value_t js_wasm_exported_func_call(ant_params_t) {
  ant_value_t state = js_get_slot(call_func, SLOT_DATA);
  wasm_func_handle_t *handle;
  wasm_func_t *func;

  handle = is_object_type(state) 
    ? (wasm_func_handle_t *)js_get_native(state, WASM_FUNC_STATE_TAG)
    : NULL;
  
  func = handle ? handle->func : NULL;
  if (!func) return js_mkerr(js, "Invalid WebAssembly function");

  wasm_functype_t *type = wasm_func_type(func);
  if (!type) return js_mkerr(js, "Failed to inspect WebAssembly function");

  const wasm_valtype_vec_t *params = wasm_functype_params(type);
  const wasm_valtype_vec_t *results_t = wasm_functype_results(type);

  wasm_val_vec_t wasm_args = WASM_EMPTY_VEC;
  wasm_val_vec_t wasm_results = WASM_EMPTY_VEC;
  wasm_trap_t *trap = NULL;
  ant_value_t result = Ant_Value_Undefined;

  wasm_val_vec_new_uninitialized(&wasm_args, params ? params->size : 0);
  wasm_val_vec_new_uninitialized(&wasm_results, results_t ? results_t->size : 0);

  for (size_t i = 0; params && i < params->size; i++) {
    if ((int)i >= nargs) {
      wasm_val_vec_delete(&wasm_args);
      wasm_val_vec_delete(&wasm_results);
      wasm_functype_delete(type);
      return js_mkerr_typed(js, JS_ERR_TYPE, "Missing WebAssembly argument");
    }

    if (!js_value_to_wasm(js, args[i], wasm_valtype_kind(params->data[i]), &wasm_args.data[i])) {
      wasm_val_vec_delete(&wasm_args);
      wasm_val_vec_delete(&wasm_results);
      wasm_functype_delete(type);
      return js_mkerr_typed(js, JS_ERR_TYPE, "Unsupported WebAssembly argument type");
    }
  }

  wasm_clear_pending_import_throw(js);
  trap = wasm_func_call(func, &wasm_args, &wasm_results);
  wasm_sync_memory_views(js);
  
  if (trap) {
    if (js->wasm_pending_import_throw) {
      result = wasm_consume_pending_import_throw(js);
      js_mark_errorlike_no_stack(js, result);
      wasm_val_vec_delete(&wasm_args);
      wasm_val_vec_delete(&wasm_results);
      wasm_functype_delete(type);
      wasm_trap_delete(trap);
      return js_throw(js, result);
    }
    
    result = wasm_trap_to_error(js, trap);
    wasm_val_vec_delete(&wasm_args);
    wasm_val_vec_delete(&wasm_results);
    wasm_functype_delete(type);
    return js_throw(js, result);
  }
  
  wasm_clear_pending_import_throw(js);
  result = wasm_js_from_result_vec(js, &wasm_results);

  wasm_val_vec_delete(&wasm_args);
  wasm_val_vec_delete(&wasm_results);
  wasm_functype_delete(type);
  
  return result;
}

static void wasm_func_state_finalize(ant_t *js, ant_object_t *obj) {
  ant_value_t value = js_obj_from_ptr(obj);
  wasm_func_handle_t *handle = (wasm_func_handle_t *)js_get_native(value, WASM_FUNC_STATE_TAG);
  if (!handle) return;

  if (handle->own_func && handle->func) wasm_func_delete(handle->func);
  free(handle);
  js_clear_native(value, WASM_FUNC_STATE_TAG);
}

ant_value_t wasm_wrap_func(ant_t *js, wasm_func_t *func, ant_value_t owner, bool own_func) {
  wasm_func_handle_t *handle;
  ant_value_t state;

  if (!func) return Ant_Value_Undefined;

  handle = calloc(1, sizeof(*handle));
  if (!handle) {
    if (own_func) wasm_func_delete(func);
    return Ant_Error_CreateOutOfMemory(js);
  }

  handle->func = func;
  handle->own_func = own_func;

  state = Ant_Object_CreateBare(js);
  js_set_native(state, handle, WASM_FUNC_STATE_TAG);
  
  if (is_object_type(owner)) js_set_slot_wb(js, state, SLOT_ENTRIES, owner);
  js_set_finalizer(state, wasm_func_state_finalize);

  return js_heavy_mkfun(js, js_wasm_exported_func_call, state);
}
