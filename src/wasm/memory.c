#include "wasm_internal.h"

static WASMMemoryInstance *wasm_memory_instance(wasm_memory_t *memory) {
  if (memory->host_memory) return memory->host_memory;
  WASMModuleInstanceCommon *runtime = memory->inst_comm_rt;
  if (!runtime || runtime->module_type != Wasm_Module_Bytecode) return NULL;
  return ((WASMModuleInstance *)runtime)->memories[memory->memory_idx_rt];
}

__attribute__((noinline, cold)) void wasm_detach_moved_views(ant_t *js) {
  for (wasm_extern_handle_t *handle = js->wasm_memory_views, *next; handle; handle = next) {
    next = handle->next;
    if (!wasm_view_moved(handle)) continue;
    ArrayBufferData *cached = buffer_get_arraybuffer_data(handle->memory_buffer);
    if (cached && !cached->is_detached) buffer_detach(js, cached);
    handle->memory_buffer = js_mkundef();
    Ant_List_Remove(handle);
  }
}

static bool wasm_parse_memory_descriptor(ant_t *js, ant_value_t descriptor, wasm_limits_t *limits, ant_value_t *err) {
  ant_value_t initial_val, maximum_val, shared_val;
  uint32_t initial_pages, maximum_pages = wasm_limits_max_default;

  if (!is_object_type(descriptor)) {
    *err = js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Memory requires a descriptor object");
    return false;
  }

  initial_val = js_get(js, descriptor, "initial");
  if (vtype(initial_val) == kTypeUndefined || !wasm_to_page_count(js, initial_val, &initial_pages)) {
    *err = js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Memory descriptor requires a valid initial page count");
    return false;
  }

  maximum_val = js_get(js, descriptor, "maximum");
  if (vtype(maximum_val) != kTypeUndefined) {
    if (!wasm_to_page_count(js, maximum_val, &maximum_pages)) {
      *err = js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Memory descriptor maximum must be a valid page count");
      return false;
    }
    if (maximum_pages < initial_pages) {
      *err = js_mkerr_typed(js, JS_ERR_RANGE, "WebAssembly.Memory maximum must be greater than or equal to initial");
      return false;
    }
  }

  shared_val = js_get(js, descriptor, "shared");
  if (js_truthy(js, shared_val)) {
    *err = js_mkerr_typed(js, JS_ERR_TYPE, "Shared WebAssembly.Memory is not supported");
    return false;
  }

  limits->min = initial_pages;
  limits->max = maximum_pages;
  *err = js_mkundef();
  return true;
}

static void wasm_detach_external_arraybuffer(ant_t *js, ant_value_t buffer) {
  ArrayBufferData *data = buffer_get_arraybuffer_data(buffer);
  if (data && !data->is_detached) buffer_detach(js, data);
}

static ant_value_t wasm_make_external_arraybuffer(ant_t *js, uint8_t *data, size_t len, ant_value_t owner) {
  ArrayBufferData *buffer = create_external_array_buffer_data(data, len);
  if (!buffer) return Ant_Error_CreateOutOfMemory(js);
  ant_value_t result = create_arraybuffer_obj(js, buffer);
  if (is_object_type(owner) && is_object_type(result)) js_set_slot_wb(js, result, SLOT_ENTRIES, owner);
  return result;
}

ant_value_t js_wasm_memory_buffer_getter(ant_params_t) {
  wasm_extern_handle_t *handle = wasm_extern_handle(call_this, WASM_EXTERN_WRAP_MEMORY);
  byte_t *data; size_t len;

  if (!handle || !handle->as.memory)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Memory");

  data = wasm_memory_data(handle->as.memory);
  len = wasm_memory_data_size(handle->as.memory);
  ArrayBufferData *cached = buffer_get_arraybuffer_data(handle->memory_buffer);
  if (cached && !cached->is_detached && cached->data == (uint8_t *)data && cached->length == len)
    return handle->memory_buffer;
  if (cached) wasm_detach_external_arraybuffer(js, handle->memory_buffer);

  ant_value_t buffer = wasm_make_external_arraybuffer(js, (uint8_t *)data, len, call_this);
  if (is_err(buffer)) return buffer;

  handle->memory_buffer = buffer;
  handle->view_memory = wasm_memory_instance(handle->as.memory);
  if (handle->view_memory) {
    handle->view_data = handle->view_memory->memory_data;
    handle->view_pages = handle->view_memory->cur_page_count;
    Ant_List_Insert(&js->wasm_memory_views, handle);
  }
  js_set_slot_wb(js, call_this, SLOT_AUX, buffer);
  return buffer;
}

ant_value_t js_wasm_memory_grow(ant_params_t) {
  wasm_extern_handle_t *handle = wasm_extern_handle(call_this, WASM_EXTERN_WRAP_MEMORY);
  wasm_memory_pages_t old_size;
  uint32_t delta;

  if (!handle || !handle->as.memory)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Expected a WebAssembly.Memory");

  if (!wasm_to_page_count(js, nargs > 0 ? args[0] : Ant_Value_FromNumber(0), &delta))
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Memory.grow requires a valid page count");

  old_size = wasm_memory_size(handle->as.memory);

  if (delta == 0) return Ant_Value_FromNumber((double)old_size);

  if (!wasm_memory_grow(handle->as.memory, delta))
    return js_mkerr_typed(js, JS_ERR_RANGE, "Failed to grow memory by %u pages", delta);

  wasm_detach_external_arraybuffer(js, handle->memory_buffer);
  handle->memory_buffer = js_mkundef();
  Ant_List_Remove(handle);
  js_set_slot_wb(js, call_this, SLOT_AUX, js_mkundef());

  return Ant_Value_FromNumber((double)old_size);
}

ant_value_t js_wasm_memory_ctor(ant_params_t) {
  wasm_limits_t limits;
  wasm_store_t *store = NULL;
  wasm_memorytype_t *memorytype = NULL;
  wasm_memory_t *memory = NULL;
  ant_value_t err, result;

  if (vtype(call_new_target) == kTypeUndefined)
    return js_mkerr_typed(js, JS_ERR_TYPE, "WebAssembly.Memory constructor requires 'new'");
  if (!ensure_wasm_engine())
    return js_mkerr(js, "Failed to initialize WebAssembly engine");
  if (!wasm_parse_memory_descriptor(js, nargs > 0 ? args[0] : js_mkundef(), &limits, &err))
    return err;

  store = wasm_store_new(g_wasm_engine);
  if (!store) return js_mkerr(js, "Failed to create WebAssembly store");

  memorytype = wasm_memorytype_new(&limits);
  memory = memorytype ? wasm_memory_new(store, memorytype) : NULL;
  wasm_memorytype_delete(memorytype);

  if (!memory) {
    wasm_store_delete(store);
    return js_throw(js, wasm_make_runtime_error(js, "Failed to create WebAssembly.Memory"));
  }

  result = wasm_wrap_extern_object(
    js, WASM_EXTERN_WRAP_MEMORY, js->builtins.wasm_memory_proto, BRAND_WASM_MEMORY,
    store, true, memory, js_mkundef()
  );
  if (vtype(result) != kTypeObject) {
    wasm_memory_delete(memory);
    wasm_store_delete(store);
    return result;
  }

  wasm_extern_handle_t *handle = wasm_extern_handle(result, WASM_EXTERN_WRAP_MEMORY);
  if (!handle) {
    wasm_memory_delete(memory);
    wasm_store_delete(store);
    return Ant_Error_CreateOutOfMemory(js);
  }

  js_set_finalizer(result, wasm_extern_finalize);
  return result;
}

ant_value_t js_wasm_memory_to_fixed_length_buffer(ant_params_t) {
  return js_wasm_memory_buffer_getter(js, args, nargs, call_new_target, call_this, call_func);
}
