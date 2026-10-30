#include <compat.h> // IWYU pragma: keep

#include "../silver/ops/using.h"

#include "modules/disposable.h"

static ant_value_t disposable_stack_init(ant_t *js, ant_value_t obj, int brand, ant_value_t proto) {
  if (vtype(obj) != kTypeObject) obj = Ant_Object_CreateBare(js);
  if (is_err(obj)) return obj;
  if (is_object_type(proto)) js_set_proto_init(obj, proto);

  ant_value_t entries = js_mkarr(js);
  if (is_err(entries)) return entries;
  set_slot(obj, SLOT_BRAND, Ant_Value_FromNumber((double)brand));
  set_slot(obj, SLOT_ENTRIES, entries);
  set_slot(obj, SLOT_SETTLED, js_false);
  
  return obj;
}

static ant_value_t builtin_DisposableStack(ant_params_t) {
  if (vtype(call_new_target) == kTypeUndefined)
    return js_mkerr_typed(js, JS_ERR_TYPE, "DisposableStack constructor requires 'new'");

  ant_value_t instance_proto = js_instance_proto_from_new_target(js, js->builtins.disposable_stack_proto, call_new_target);
  return disposable_stack_init(js, js->this_val, BRAND_DISPOSABLE_STACK, instance_proto);
}

static ant_value_t builtin_AsyncDisposableStack(ant_params_t) {
  if (vtype(call_new_target) == kTypeUndefined)
    return js_mkerr_typed(js, JS_ERR_TYPE, "AsyncDisposableStack constructor requires 'new'");

  ant_value_t instance_proto = js_instance_proto_from_new_target(js, js->builtins.async_disposable_stack_proto, call_new_target);
  return disposable_stack_init(js, js->this_val, BRAND_ASYNC_DISPOSABLE_STACK, instance_proto);
}

static bool disposable_stack_has_brand(ant_value_t obj, int brand) {
  if (!is_object_type(obj)) return false;
  ant_value_t actual = get_slot(js_as_obj(obj), SLOT_BRAND);
  return vtype(actual) == kTypeNumber && (int)Ant_Value_AsNumberUnchecked(actual) == brand;
}

static ant_value_t disposable_stack_entries_checked(ant_t *js, ant_value_t stack, int brand, const char *name) {
  if (!disposable_stack_has_brand(stack, brand))
    return js_mkerr_typed(js, JS_ERR_TYPE, "%s method called on incompatible receiver", name);

  if (get_slot(js_as_obj(stack), SLOT_SETTLED) == js_true)
    return js_mkerr_typed(js, JS_ERR_TYPE, "%s is already disposed", name);

  ant_value_t entries = get_slot(js_as_obj(stack), SLOT_ENTRIES);
  if (vtype(entries) != kTypeArray) return js_mkerr_typed(js, JS_ERR_TYPE, "Invalid %s", name);
  return entries;
}

static ant_value_t disposable_stack_push_record(
  ant_t *js, ant_value_t stack, int brand, const char *name,
  sv_disposal_record_kind_t kind, ant_value_t value, ant_value_t method
) {
  if (!is_callable(method))
    return js_mkerr_typed(js, JS_ERR_TYPE, "%s requires a callable disposer", name);

  GC_ROOT_SAVE(root_mark, js);
  GC_ROOT_PIN(js, stack);
  GC_ROOT_PIN(js, value);
  GC_ROOT_PIN(js, method);

  ant_value_t entries = disposable_stack_entries_checked(js, stack, brand, name);
  GC_ROOT_PIN(js, entries);
  if (is_err(entries)) {
    GC_ROOT_RESTORE(js, root_mark);
    return entries;
  }

  ant_value_t record = js_mkarr(js);
  GC_ROOT_PIN(js, record);
  if (is_err(record)) {
    GC_ROOT_RESTORE(js, root_mark);
    return record;
  }

  js_arr_push(js, record, Ant_Value_FromNumber((double)kind));
  js_arr_push(js, record, value);
  js_arr_push(js, record, method);
  js_arr_push(js, entries, record);

  GC_ROOT_RESTORE(js, root_mark);
  return js_mkundef();
}

static ant_value_t builtin_DisposableStack_defer(ant_params_t) {
  ant_value_t fn = nargs > 0 ? args[0] : js_mkundef();
  ant_value_t result = disposable_stack_push_record(
    js, js->this_val, BRAND_DISPOSABLE_STACK, 
    "DisposableStack", SV_DISPOSAL_RECORD_DEFER, js_mkundef(), fn
  );
  return is_err(result) ? result : js_mkundef();
}

static ant_value_t builtin_DisposableStack_adopt(ant_params_t) {
  ant_value_t value = nargs > 0 ? args[0] : js_mkundef();
  ant_value_t fn = nargs > 1 ? args[1] : js_mkundef();
  ant_value_t result = disposable_stack_push_record(
    js, js->this_val, BRAND_DISPOSABLE_STACK, 
    "DisposableStack", SV_DISPOSAL_RECORD_ADOPT, value, fn
  );
  return is_err(result) ? result : value;
}

static ant_value_t builtin_AsyncDisposableStack_defer(ant_params_t) {
  ant_value_t fn = nargs > 0 ? args[0] : js_mkundef();
  ant_value_t result = disposable_stack_push_record(
    js, js->this_val, BRAND_ASYNC_DISPOSABLE_STACK, 
    "AsyncDisposableStack", SV_DISPOSAL_RECORD_DEFER, js_mkundef(), fn
  );
  return is_err(result) ? result : js_mkundef();
}

static ant_value_t builtin_AsyncDisposableStack_adopt(ant_params_t) {
  ant_value_t value = nargs > 0 ? args[0] : js_mkundef();
  ant_value_t fn = nargs > 1 ? args[1] : js_mkundef();
  ant_value_t result = disposable_stack_push_record(
    js, js->this_val, BRAND_ASYNC_DISPOSABLE_STACK, 
    "AsyncDisposableStack", SV_DISPOSAL_RECORD_ADOPT, value, fn
  );
  return is_err(result) ? result : value;
}

static ant_value_t disposable_stack_use(
  ant_t *js, ant_value_t stack, int brand, const char *name,
  ant_value_t resource, ant_value_t dispose_sym, ant_value_t fallback_sym
) {
  ant_value_t entries = disposable_stack_entries_checked(js, stack, brand, name);
  if (is_err(entries)) return entries;
  if (vtype(resource) == kTypeNull || vtype(resource) == kTypeUndefined) return resource;

  ant_value_t method = js_get_sym(js, resource, dispose_sym);
  if (is_err(method)) return method;

  if ((vtype(method) == kTypeUndefined || vtype(method) == kTypeNull) && vtype(fallback_sym) == kTypeSymbol) {
    method = js_get_sym(js, resource, fallback_sym);
    if (is_err(method)) return method;
  }

  if (!is_callable(method))
    return js_mkerr_typed(js, JS_ERR_TYPE, "%s resource is not disposable", name);

  ant_value_t result = disposable_stack_push_record(
    js, stack, brand, name,
    SV_DISPOSAL_RECORD_USE, resource, method
  );

  return is_err(result) ? result : resource;
}

static ant_value_t builtin_DisposableStack_use(ant_params_t) {
  ant_value_t resource = nargs > 0 ? args[0] : js_mkundef();
  return disposable_stack_use(
    js, js->this_val, BRAND_DISPOSABLE_STACK, "DisposableStack", resource, js->sym.dispose_sym, js_mkundef()
  );
}

static ant_value_t builtin_AsyncDisposableStack_use(ant_params_t) {
  ant_value_t resource = nargs > 0 ? args[0] : js_mkundef();
  return disposable_stack_use(
    js, js->this_val, BRAND_ASYNC_DISPOSABLE_STACK, "AsyncDisposableStack",
    resource, js->sym.asyncDispose_sym, js->sym.dispose_sym
  );
}

static ant_value_t disposable_stack_move(ant_t *js, int brand, const char *name, ant_value_t proto) {
  GC_ROOT_SAVE(root_mark, js);
  ant_value_t stack = js->this_val;
  GC_ROOT_PIN(js, stack);

  ant_value_t entries = disposable_stack_entries_checked(js, stack, brand, name);
  GC_ROOT_PIN(js, entries);
  if (is_err(entries)) {
    GC_ROOT_RESTORE(js, root_mark);
    return entries;
  }

  ant_value_t moved = disposable_stack_init(js, js_mkundef(), brand, proto);
  GC_ROOT_PIN(js, moved);
  if (is_err(moved)) {
    GC_ROOT_RESTORE(js, root_mark);
    return moved;
  }

  set_slot(moved, SLOT_ENTRIES, entries);
  set_slot(js_as_obj(stack), SLOT_ENTRIES, js_mkarr(js));
  set_slot(js_as_obj(stack), SLOT_SETTLED, js_true);

  GC_ROOT_RESTORE(js, root_mark);
  return moved;
}

static ant_value_t builtin_DisposableStack_move(ant_params_t) {
  (void)args; (void)nargs;
  return disposable_stack_move(js, BRAND_DISPOSABLE_STACK, "DisposableStack", js->builtins.disposable_stack_proto);
}

static ant_value_t builtin_AsyncDisposableStack_move(ant_params_t) {
  (void)args; (void)nargs;
  return disposable_stack_move(js, BRAND_ASYNC_DISPOSABLE_STACK, "AsyncDisposableStack", js->builtins.async_disposable_stack_proto);
}

static ant_value_t builtin_DisposableStack_dispose(ant_params_t) {
  ant_value_t stack = js->this_val;
  if (!disposable_stack_has_brand(stack, BRAND_DISPOSABLE_STACK)) {
    return js_mkerr_typed(js, JS_ERR_TYPE, "DisposableStack method called on incompatible receiver");
  }
  if (get_slot(js_as_obj(stack), SLOT_SETTLED) == js_true) return js_mkundef();

  GC_ROOT_SAVE(root_mark, js);
  GC_ROOT_PIN(js, stack);
  ant_value_t entries = get_slot(js_as_obj(stack), SLOT_ENTRIES);
  
  GC_ROOT_PIN(js, entries);
  ant_value_t completion = js_mkundef();
  
  GC_ROOT_PIN(js, completion);
  set_slot(js_as_obj(stack), SLOT_SETTLED, js_true);

  ant_offset_t len = vtype(entries) == kTypeArray ? js_arr_len(js, entries) : 0;
  bool has_completion = false;
  if (len > 0) completion = sv_dispose_records_sync(js, entries, len, &completion, &has_completion, false);

  if (is_err(completion)) {
    GC_ROOT_RESTORE(js, root_mark);
    return completion;
  }

  set_slot(js_as_obj(stack), SLOT_ENTRIES, js_mkarr(js));
  if (has_completion) {
    ant_value_t thrown = js_throw(js, completion);
    GC_ROOT_RESTORE(js, root_mark);
    return thrown;
  }

  GC_ROOT_RESTORE(js, root_mark);
  return js_mkundef();
}

static ant_value_t builtin_AsyncDisposableStack_disposeAsync(ant_params_t) {
  ant_value_t stack = js->this_val;
  ant_value_t result_promise = js_mkpromise(js);
  if (is_err(result_promise)) return result_promise;

  if (!disposable_stack_has_brand(stack, BRAND_ASYNC_DISPOSABLE_STACK)) {
    ant_value_t error = Ant_Error_Create(
      js, JS_ERR_TYPE, "AsyncDisposableStack method called on incompatible receiver"
    );
    js_reject_promise(js, result_promise, error);
    return result_promise;
  }
  
  if (get_slot(js_as_obj(stack), SLOT_SETTLED) == js_true) {
    js_resolve_promise(js, result_promise, js_mkundef());
    return result_promise;
  }

  GC_ROOT_SAVE(root_mark, js);
  GC_ROOT_PIN(js, stack);
  GC_ROOT_PIN(js, result_promise);

  ant_value_t entries = get_slot(js_as_obj(stack), SLOT_ENTRIES);
  GC_ROOT_PIN(js, entries);
  set_slot(js_as_obj(stack), SLOT_SETTLED, js_true);

  ant_value_t state = Ant_Object_CreateBare(js);
  GC_ROOT_PIN(js, state);
  if (is_err(state)) {
    GC_ROOT_RESTORE(js, root_mark);
    return state;
  }
  
  set_slot(state, SLOT_ENTRIES, entries);
  set_slot(state, SLOT_DATA, result_promise);
  set_slot(state, SLOT_AUX, js_mkundef());
  set_slot(state, SLOT_ITER_STATE, Ant_Value_FromNumber((double)(vtype(entries) == kTypeArray ? js_arr_len(js, entries) : 0)));
  set_slot(js_as_obj(stack), SLOT_ENTRIES, js_mkarr(js));

  ant_value_t result = sv_async_dispose_continue(js, state, false, js_mkundef());
  GC_ROOT_RESTORE(js, root_mark);
  
  return result;
}

void js_disposable_stack_init(ant_t *js, ant_value_t object_proto, ant_value_t function_proto) {
  ant_value_t disposable_stack_proto = Ant_Object_CreateBare(js);
  set_proto(js, disposable_stack_proto, object_proto);
  js->builtins.disposable_stack_proto = disposable_stack_proto;
  defmethod(js, disposable_stack_proto, "use", 3, Ant_Function_CreateWithArity(builtin_DisposableStack_use, 1));
  defmethod(js, disposable_stack_proto, "adopt", 5, Ant_Function_CreateWithArity(builtin_DisposableStack_adopt, 2));
  defmethod(js, disposable_stack_proto, "defer", 5, Ant_Function_CreateWithArity(builtin_DisposableStack_defer, 1));
  defmethod(js, disposable_stack_proto, "move", 4, Ant_Function_Create(builtin_DisposableStack_move));
  defmethod(js, disposable_stack_proto, "dispose", 7, Ant_Function_Create(builtin_DisposableStack_dispose));

  ant_value_t disposable_stack_ctor = mkobj(js, 0);
  set_proto(js, disposable_stack_ctor, function_proto);
  set_slot(disposable_stack_ctor, SLOT_CFUNC, Ant_Function_Create(builtin_DisposableStack));
  js_setprop_readonly_nonconfigurable(js, disposable_stack_ctor, "prototype", 9, disposable_stack_proto);
  js_set_function_name(js, disposable_stack_ctor, "DisposableStack", sizeof("DisposableStack") - 1);
  
  ant_value_t disposable_stack_ctor_func = js_obj_to_func(js, disposable_stack_ctor);
  defhidden(js, disposable_stack_proto, "constructor", 11, disposable_stack_ctor_func);
  js_set_global_builtin(js, "DisposableStack", disposable_stack_ctor_func);

  ant_value_t async_disposable_stack_proto = Ant_Object_CreateBare(js);
  set_proto(js, async_disposable_stack_proto, object_proto);
  js->builtins.async_disposable_stack_proto = async_disposable_stack_proto;
  defmethod(js, async_disposable_stack_proto, "use", 3, Ant_Function_CreateWithArity(builtin_AsyncDisposableStack_use, 1));
  defmethod(js, async_disposable_stack_proto, "adopt", 5, Ant_Function_CreateWithArity(builtin_AsyncDisposableStack_adopt, 2));
  defmethod(js, async_disposable_stack_proto, "defer", 5, Ant_Function_CreateWithArity(builtin_AsyncDisposableStack_defer, 1));
  defmethod(js, async_disposable_stack_proto, "move", 4, Ant_Function_Create(builtin_AsyncDisposableStack_move));
  defmethod(js, async_disposable_stack_proto, "disposeAsync", 12, Ant_Function_Create(builtin_AsyncDisposableStack_disposeAsync));

  ant_value_t async_disposable_stack_ctor = mkobj(js, 0);
  set_proto(js, async_disposable_stack_ctor, function_proto);
  set_slot(async_disposable_stack_ctor, SLOT_CFUNC, Ant_Function_Create(builtin_AsyncDisposableStack));
  js_setprop_readonly_nonconfigurable(js, async_disposable_stack_ctor, "prototype", 9, async_disposable_stack_proto);
  js_set_function_name(js, async_disposable_stack_ctor, "AsyncDisposableStack", sizeof("AsyncDisposableStack") - 1);
  
  ant_value_t async_disposable_stack_ctor_func = js_obj_to_func(js, async_disposable_stack_ctor);
  defhidden(js, async_disposable_stack_proto, "constructor", 11, async_disposable_stack_ctor_func);
  js_set_global_builtin(js, "AsyncDisposableStack", async_disposable_stack_ctor_func);
}
