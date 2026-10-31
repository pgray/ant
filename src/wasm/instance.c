#include "wasm_internal.h"

static void wasm_import_func_env_finalizer(void *env_ptr) {
  wasm_import_func_env_t *env = (wasm_import_func_env_t *)env_ptr;
  Ant_List_Remove(env);
  free(env->result_kinds);
  free(env);
}

static void wasm_delete_owned_globals(wasm_global_t **globals, size_t count) {
  if (!globals) return;
  for (size_t i = 0; i < count; i++)
    if (globals[i]) wasm_global_delete(globals[i]);
}

static void wasm_delete_owned_imports(
  wasm_func_t **funcs, size_t func_count, wasm_global_t **globals, size_t global_count
) {
  if (funcs) for (size_t i = 0; i < func_count; i++) 
    if (funcs[i]) wasm_func_delete(funcs[i]);
  
  wasm_delete_owned_globals(globals, global_count);
  free(funcs);
  free(globals);
}

static ant_value_t wasm_wrap_instance(ant_t *js, wasm_instance_handle_t *handle, ant_value_t module_ref) {
  ant_value_t obj = Ant_Object_CreateBare(js);
  js_set_proto_init(obj, js->builtins.wasm_instance_proto);
  js_set_slot(obj, SLOT_BRAND, Ant_Value_FromNumber(BRAND_WASM_INSTANCE));
  js_set_native(obj, handle, WASM_INSTANCE_NATIVE_TAG);
  js_set_slot_wb(js, obj, SLOT_CTOR, module_ref);
  return obj;
}

static void wasm_instance_finalize(ant_t *js, ant_object_t *obj) {
  ant_value_t value = js_obj_from_ptr(obj);
  wasm_instance_handle_t *handle = (wasm_instance_handle_t *)js_get_native(value, WASM_INSTANCE_NATIVE_TAG);
  if (!handle) return;
  
  wasm_unregister_instance_owner(js, handle->instance);
  for (size_t j = 0; j < handle->host_func_count; j++) 
    if (handle->host_funcs[j]) wasm_func_delete(handle->host_funcs[j]);
  
  for (size_t j = 0; j < handle->host_global_count; j++)
    if (handle->host_globals[j]) wasm_global_delete(handle->host_globals[j]);
  
  free(handle->host_funcs);
  free(handle->host_globals);
  wasm_extern_vec_delete(&handle->exports);
  
  free(handle);
  js_clear_native(value, WASM_INSTANCE_NATIVE_TAG);
}

static ant_value_t wasm_wrap_export_value(ant_t *js, ant_value_t instance_obj, const wasm_exporttype_t *export_type, wasm_extern_t *external) {
  switch (wasm_extern_kind(external)) {
    case WASM_EXTERN_FUNC: {
      return wasm_wrap_func(js, wasm_extern_as_func(external), instance_obj, false);
    }
    case WASM_EXTERN_GLOBAL: {
      ant_value_t obj = wasm_wrap_extern_object(
        js, WASM_EXTERN_WRAP_GLOBAL, js->builtins.wasm_global_proto, BRAND_WASM_GLOBAL,
        NULL, false, wasm_extern_as_global(external), instance_obj
      );
      if (vtype(obj) == kTypeObject) js_set_finalizer(obj, wasm_extern_finalize);
      return obj;
    }
    case WASM_EXTERN_MEMORY: {
      ant_value_t obj = wasm_wrap_extern_object(
        js, WASM_EXTERN_WRAP_MEMORY, js->builtins.wasm_memory_proto, BRAND_WASM_MEMORY,
        NULL, false, wasm_extern_as_memory(external), instance_obj
      );
      if (vtype(obj) == kTypeObject) js_set_finalizer(obj, wasm_extern_finalize);
      return obj;
    }
    case WASM_EXTERN_TABLE: {
      ant_value_t obj = wasm_wrap_extern_object(
        js, WASM_EXTERN_WRAP_TABLE, js->builtins.wasm_table_proto, BRAND_WASM_TABLE,
        NULL, false, wasm_extern_as_table(external), instance_obj
      );
      if (vtype(obj) == kTypeObject) js_set_finalizer(obj, wasm_extern_finalize);
      return obj;
    }
    default:
      (void)export_type;
      return Ant_Value_Undefined;
  }
}

ant_value_t js_wasm_instance_exports_getter(ant_params_t) {
  if (!js_check_brand(call_this, BRAND_WASM_INSTANCE)) return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Instance");
  return js_get_slot(call_this, SLOT_ENTRIES);
}

static ant_value_t wasm_property_get_nested(ant_t *js, ant_value_t base, const wasm_name_t *name) {
  if (!is_object_type(base)) return Ant_Value_Undefined;
  return js_getprop_fallback(js, base, name && name->data ? name->data : "");
}

static wasm_trap_t *wasm_import_trap(wasm_import_func_env_t *env, const char *message) {
  wasm_message_t trap_msg = WASM_EMPTY_VEC;
  wasm_name_new_from_string_nt(&trap_msg, message);
  wasm_trap_t *trap = wasm_trap_new(env->store, &trap_msg);
  wasm_byte_vec_delete(&trap_msg);
  return trap;
}

static wasm_trap_t *wasm_import_func_callback(void *env_ptr, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
  wasm_import_func_env_t *env = (wasm_import_func_env_t *)env_ptr;
  ant_t *js = env->js;
  
  ant_value_t *js_args = NULL;
  ant_value_t result = Ant_Value_Undefined;
  wasm_message_t trap_msg = WASM_EMPTY_VEC;

  wasm_sync_memory_views(js);
  if (args && args->size > 0) {
    js_args = calloc(args->size, sizeof(*js_args));
    if (!js_args) {
      wasm_name_new_from_string_nt(&trap_msg, "Out of memory");
      wasm_trap_t *trap = wasm_trap_new(env->store, &trap_msg);
      wasm_byte_vec_delete(&trap_msg);
      return trap;
    }
  }

  for (size_t i = 0; args && i < args->size; i++) {
    js_args[i] = wasm_value_to_js(js, &args->data[i]);
  }

  result = sv_vm_call(
    js->vm, js, env->fn, Ant_Value_Undefined, 
    js_args, args ? (int)args->size : 0, NULL, Ant_Value_Undefined
  );
  free(js_args);

  if (is_err(result)) {
    ant_value_t thrown = Ant_Exception_Value(js, result);
    wasm_set_pending_import_throw(js, thrown);
    
    const char *msg = "WebAssembly import threw";
    if (vtype(Ant_Exception_Value(js, Ant_Exception_Peek(js))) == kTypeObject) {
      const char *message = get_str_prop(js, Ant_Exception_Value(js, Ant_Exception_Peek(js)), "message", 7, NULL);
      if (message && *message) msg = message;
    }
    
    wasm_name_new_from_string_nt(&trap_msg, msg);
    wasm_trap_t *trap = wasm_trap_new(env->store, &trap_msg);
    wasm_byte_vec_delete(&trap_msg);
    
    return trap;
  }

  if (!results || results->size == 0) return NULL;

  if (results->size != env->result_count)
    return wasm_import_trap(env, "Mismatched import result arity");

  if (results->size == 1) {
    if (!js_value_to_wasm(js, result, env->result_kinds[0], &results->data[0]))
      return wasm_import_trap(env, "Unsupported import return value");
    return NULL;
  }

  if (vtype(result) != kTypeArray)
    return wasm_import_trap(env, "Expected an array for multi-value return");

  for (size_t i = 0; i < results->size; i++) {
    ant_value_t item = js_arr_get(js, result, (ant_offset_t)i);
    if (!js_value_to_wasm(js, item, env->result_kinds[i], &results->data[i]))
      return wasm_import_trap(env, "Unsupported import return value");
  }

  return NULL;
}

ant_value_t wasm_instantiate_module(ant_t *js, ant_value_t module_obj, ant_value_t import_obj, ant_value_t *out_instance) {
  wasm_module_handle_t *module_handle = wasm_module_handle(module_obj);
  wasm_importtype_vec_t import_types = WASM_EMPTY_VEC;
  wasm_exporttype_vec_t export_types = WASM_EMPTY_VEC;
  
  wasm_extern_t **imports = NULL;
  wasm_func_t **owned_host_funcs = NULL;
  wasm_global_t **owned_host_globals = NULL;
  
  size_t owned_host_func_count = 0;
  size_t owned_host_global_count = 0;
  wasm_extern_vec_t exports = WASM_EMPTY_VEC;
  
  wasm_trap_t *trap = NULL;
  wasm_instance_t *instance = NULL;
  ant_value_t import_error = Ant_Value_Undefined;
  ant_value_t instance_obj = Ant_Value_Undefined;
  ant_value_t exports_obj = Ant_Object_CreateBare(js);
  *out_instance = Ant_Value_Undefined;

  if (!module_handle) 
    return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Module");

  if (wasi_module_has_wasi_imports(module_handle->module)
      && wasi_module_is_command_or_reactor(module_handle->module)) {
    ant_value_t wasi_opts = is_object_type(import_obj) ? js_get(js, import_obj, "wasi") : Ant_Value_Undefined;
    if (is_err(wasi_opts)) return wasi_opts;

    if (!is_object_type(import_obj) || is_object_type(wasi_opts)) {
      ant_value_t bytes_src = js_get_slot(module_obj, SLOT_MAP);
      wasm_byte_vec_t binary = WASM_EMPTY_VEC;
      char wasi_err[128] = {0};
      if (!wasm_buffer_source_to_vec(js, bytes_src, &binary, wasi_err, sizeof(wasi_err)))
        return js_mkerr(js, "WASI: cannot extract module bytes");
      *out_instance = wasi_instantiate(js, (const uint8_t *)binary.data, binary.size, module_obj, wasi_opts);
      wasm_byte_vec_delete(&binary);
      return is_err(*out_instance) ? *out_instance : Ant_Value_Undefined;
    }
  }

  wasm_module_imports(module_handle->module, &import_types);
  if (import_types.size > 0) {
    imports = calloc(import_types.size, sizeof(*imports));
    owned_host_funcs = calloc(import_types.size, sizeof(*owned_host_funcs));
    owned_host_globals = calloc(import_types.size, sizeof(*owned_host_globals));
    if (!imports || !owned_host_funcs || !owned_host_globals) {
      import_error = Ant_Error_CreateOutOfMemory(js);
      goto import_setup_failed;
    }
  }

  for (size_t i = 0; i < import_types.size; i++) {
    const wasm_importtype_t *import_type = import_types.data[i];
    const wasm_name_t *module_name = wasm_importtype_module(import_type);
    const wasm_name_t *field_name = wasm_importtype_name(import_type);
    const wasm_externtype_t *extern_type = wasm_importtype_type(import_type);
    
    ant_value_t namespace_obj = wasm_property_get_nested(js, import_obj, module_name);
    if (is_err(namespace_obj)) {
      import_error = namespace_obj;
      goto import_setup_failed;
    }

    ant_value_t value = wasm_property_get_nested(js, namespace_obj, field_name);
    if (is_err(value)) {
      import_error = value;
      goto import_setup_failed;
    }

    wasm_externkind_t kind = wasm_externtype_kind(extern_type);
    if (kind == WASM_EXTERN_FUNC) {
      const wasm_functype_t *func_type = wasm_externtype_as_functype_const(extern_type);
      const wasm_valtype_vec_t *result_types = wasm_functype_results(func_type);
      wasm_import_func_env_t *env = NULL;

      if (!is_callable(value)) {
        import_error = wasm_make_link_error(js, "Missing function import");
        goto import_setup_failed;
      }

      env = calloc(1, sizeof(*env));
      if (!env) {
        import_error = Ant_Error_CreateOutOfMemory(js);
        goto import_setup_failed;
      }

      env->js = js;
      env->store = module_handle->store;
      env->owner = module_obj;
      env->fn = value;
      env->result_count = result_types ? result_types->size : 0;
      if (env->result_count > 0) {
        env->result_kinds = calloc(env->result_count, sizeof(*env->result_kinds));
        if (!env->result_kinds) {
          free(env);
          import_error = Ant_Error_CreateOutOfMemory(js);
          goto import_setup_failed;
        }
        for (size_t j = 0; j < env->result_count; j++)
          env->result_kinds[j] = wasm_valtype_kind(result_types->data[j]);
      }
      
      Ant_List_Insert(&env->js->wasm_import_envs, env);

      owned_host_funcs[owned_host_func_count] = wasm_func_new_with_env(
        module_handle->store, func_type,
        wasm_import_func_callback, env,
        wasm_import_func_env_finalizer
      );

      if (!owned_host_funcs[owned_host_func_count]) {
        wasm_import_func_env_finalizer(env);
        import_error = wasm_make_link_error(js, "Failed to create function import");
        goto import_setup_failed;
      }

      imports[i] = wasm_func_as_extern(owned_host_funcs[owned_host_func_count]);
      owned_host_func_count++;
      continue;
    }

    if (kind == WASM_EXTERN_MEMORY) {
      wasm_extern_handle_t *handle = wasm_extern_handle(value, WASM_EXTERN_WRAP_MEMORY);
      if (!handle || !handle->as.memory) {
        import_error = wasm_make_link_error(js, "Missing memory import");
        goto import_setup_failed;
      }
      imports[i] = wasm_memory_as_extern(handle->as.memory);
      continue;
    }

    if (kind == WASM_EXTERN_TABLE) {
      wasm_extern_handle_t *handle = wasm_extern_handle(value, WASM_EXTERN_WRAP_TABLE);
      if (!handle || !handle->as.table) {
        import_error = wasm_make_link_error(js, "Missing table import");
        goto import_setup_failed;
      }
      imports[i] = wasm_table_as_extern(handle->as.table);
      continue;
    }

    if (kind == WASM_EXTERN_GLOBAL) {
      wasm_extern_handle_t *handle = wasm_extern_handle(value, WASM_EXTERN_WRAP_GLOBAL);
      if (!handle || !handle->as.global) {
        const wasm_globaltype_t *global_type = wasm_externtype_as_globaltype_const(extern_type);
        const wasm_valtype_t *content = global_type ? wasm_globaltype_content(global_type) : NULL;
        wasm_val_t initial = WASM_INIT_VAL;
        wasm_global_t *owned_global = NULL;
        if (global_type && content
            && wasm_globaltype_mutability(global_type) == WASM_CONST
            && js_value_to_wasm(js, value, wasm_valtype_kind(content), &initial)) {
          owned_global = wasm_global_new(module_handle->store, global_type, &initial);
        }
        if (owned_global) {
          owned_host_globals[owned_host_global_count++] = owned_global;
          imports[i] = wasm_global_as_extern(owned_global);
          continue;
        }
        char msg[256];
        snprintf(msg, sizeof(msg), "Missing global import %.*s.%.*s",
          (int)wasm_name_len(module_name),
          module_name && module_name->data ? module_name->data : "",
          (int)wasm_name_len(field_name),
          field_name && field_name->data ? field_name->data : "");
        import_error = wasm_make_link_error(js, msg);
        goto import_setup_failed;
      }
      imports[i] = wasm_global_as_extern(handle->as.global);
      continue;
    }
  }

  {
    wasm_extern_vec_t import_vec = WASM_EMPTY_VEC;
    if (imports && import_types.size > 0)
      import_vec = (wasm_extern_vec_t){ import_types.size, imports, import_types.size, sizeof(*imports), NULL };

    wasm_clear_pending_import_throw(js);
    instance = wasm_instance_new_with_args(module_handle->store, module_handle->module, &import_vec, &trap, KILOBYTE(1024), 0);
    wasm_sync_memory_views(js);
  }

  free(imports);
  wasm_importtype_vec_delete(&import_types);

  if (!instance) {
    wasm_delete_owned_imports(
      owned_host_funcs,
      owned_host_func_count,
      owned_host_globals,
      owned_host_global_count
    );
    
    if (trap) {
      if (js->wasm_pending_import_throw) {
        ant_value_t thrown = wasm_consume_pending_import_throw(js);
        js_mark_errorlike_no_stack(js, thrown);
        wasm_trap_delete(trap);
        return js_throw(js, thrown);
      }
      return wasm_trap_to_error(js, trap);
    }
    
    return wasm_make_link_error(js, "Failed to instantiate WebAssembly module");
  }
  
  wasm_clear_pending_import_throw(js);
  wasm_instance_exports(instance, &exports);
  wasm_module_exports(module_handle->module, &export_types);

  {
    wasm_instance_handle_t *inst_handle = calloc(1, sizeof(*inst_handle));
    if (!inst_handle) {
      wasm_extern_vec_delete(&exports);
      wasm_exporttype_vec_delete(&export_types);
      wasm_delete_owned_globals(owned_host_globals, owned_host_global_count);
      free(owned_host_globals);
      return Ant_Error_CreateOutOfMemory(js);
    }
    inst_handle->instance = instance;
    inst_handle->exports = exports;
    inst_handle->host_funcs = owned_host_funcs;
    inst_handle->host_func_count = owned_host_func_count;
    inst_handle->host_globals = owned_host_globals;
    inst_handle->host_global_count = owned_host_global_count;

    instance_obj = wasm_wrap_instance(js, inst_handle, module_obj);
  }
  
  if (vtype(instance_obj) != kTypeObject) {
    wasm_exporttype_vec_delete(&export_types);
    return instance_obj;
  }
  
  js_set_finalizer(instance_obj, wasm_instance_finalize);
  if (!wasm_register_instance_owner(js, instance, instance_obj)) {
    wasm_exporttype_vec_delete(&export_types);
    return Ant_Error_CreateOutOfMemory(js);
  }

  for (size_t i = 0; i < export_types.size && i < exports.size; i++) {
    const wasm_exporttype_t *export_type = export_types.data[i];
    const wasm_name_t *name = wasm_exporttype_name(export_type);
    ant_value_t export_value = wasm_wrap_export_value(js, instance_obj, export_type, exports.data[i]);
    js_setprop(js, exports_obj, js_mkstr(js, name->data, wasm_name_len(name)), export_value);
  }
  
  wasm_exporttype_vec_delete(&export_types);
  js_set_slot_wb(js, instance_obj, SLOT_ENTRIES, exports_obj);
  if (is_object_type(import_obj)) js_set_slot_wb(js, instance_obj, SLOT_MAP, import_obj);

  *out_instance = instance_obj;
  return Ant_Value_Undefined;

import_setup_failed:
  free(imports);
  wasm_delete_owned_imports(
    owned_host_funcs,
    owned_host_func_count,
    owned_host_globals,
    owned_host_global_count
  );
  wasm_importtype_vec_delete(&import_types);
  return import_error;
}

ant_value_t js_wasm_instance_ctor(ant_params_t) {
  ant_value_t instance = Ant_Value_Undefined;
  ant_value_t import_obj = (nargs >= 2 && is_object_type(args[1])) ? args[1] : Ant_Value_Undefined;
  ant_value_t err;

  if (vtype(call_new_target) == kTypeUndefined) return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Instance constructor requires 'new'");
  if (nargs < 1) return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Instance requires a module");

  err = wasm_instantiate_module(js, args[0], import_obj, &instance);
  if (is_err(err)) return err;
  if (vtype(instance) != kTypeObject) return js_throw(js, wasm_error_value(js, err));
  
  return instance;
}

ant_value_t wasm_instantiate_result(ant_t *js, ant_value_t module, ant_value_t instance) {
  ant_value_t result = Ant_Object_Create(js);
  js_set(js, result, "module", module);
  js_set(js, result, "instance", instance);
  return result;
}

ant_value_t js_wasm_instantiate(ant_params_t) {
  ant_value_t promise = js_mkpromise(js);
  ant_value_t module = Ant_Value_Undefined;
  ant_value_t instance = Ant_Value_Undefined;
  ant_value_t import_obj = (nargs >= 2 && is_object_type(args[1])) ? args[1] : Ant_Value_Undefined;
  ant_value_t err;

  if (nargs < 1) {
    wasm_reject_with_error(js, promise, js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.instantiate requires 1 argument"));
    return promise;
  }

  if (wasm_module_handle(args[0])) {
    err = wasm_instantiate_module(js, args[0], import_obj, &instance);
    if (is_err(err) || vtype(instance) != kTypeObject) {
      wasm_reject_with_error(js, promise, err);
      return promise;
    }
    js_resolve_promise(js, promise, instance);
    return promise;
  }

  err = wasm_module_from_bytes(js, args[0], &module, "WebAssembly.instantiate()");
  if (is_err(err) || vtype(module) != kTypeObject) {
    wasm_reject_with_error(js, promise, err);
    return promise;
  }

  err = wasm_instantiate_module(js, module, import_obj, &instance);
  if (is_err(err) || vtype(instance) != kTypeObject) {
    wasm_reject_with_error(js, promise, err);
    return promise;
  }

  js_resolve_promise(js, promise, wasm_instantiate_result(js, module, instance));
  return promise;
}
