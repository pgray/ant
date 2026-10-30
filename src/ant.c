#include <compat.h> // IWYU pragma: keep

#include <float.h>
#include <limits.h>
#include <uv.h>
#include <assert.h>
#include <pcre2.h>
#include <stdio.h>
#include <time.h>
#include <utarray.h>
#include <uthash.h>
#include <tlsuv/tlsuv.h>
#include <tlsuv/http.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <string.h>

#include "ant.h"
#include "ptr.h"
#include "cfunc.h"
#include "debug.h"
#include "common.h"
#include "sugar.h"
#include "runtime.h"
#include "utf8.h"
#include "output.h"
#include "utils.h"
#include "builder.h"
#include "list.h"
#include "loop.h"
#include "props.h"
#include "internal.h"
#include "watch.h"
#include "interrupt.h"
#include "errors.h"
#include "descriptors.h"
#include "shapes.h"
#include "gc.h"
#include "gc/objects.h"
#include "gc/roots.h"
#include "realm.h"
#include "gc/weak.h"
#include "esm/loader.h"
#include "wasm/wasm.h"
#include "gc/bigints.h"
#include "esm/exports.h"

#include "silver/compiler.h"
#include "silver/call.h"
#include "silver/jit.h"
#include "silver/ops/eval_env.h"
#include "silver/ops/async.h"

#include "modules/regex.h"
#include "modules/json.h"
#include "modules/timer.h"
#include "modules/promise.h"
#include "modules/buffer.h"
#include "modules/regex.h"
#include "modules/globals.h"
#include "modules/process.h"
#include "modules/abort.h"
#include "modules/eventsource.h"
#include "modules/websocket.h"
#include "modules/dns.h"
#include "modules/function.h"
#include "modules/atomics.h"
#include "modules/symbol.h"
#include "modules/iterator.h"
#include "modules/events.h"
#include "modules/cron.h"
#include "modules/lmdb.h"
#include "modules/rpc.h"
#include "modules/navigator.h"
#include "modules/isolate.h"
#include "modules/worker_threads.h"
#include "modules/message_port.h"
#include "modules/performance.h"
#include "modules/napi.h"
#include "modules/readline.h"
#include "modules/server.h"
#include "modules/fs.h"
#include "modules/child_process.h"
#include "modules/fetch.h"
#include "modules/http.h"
#include "modules/net.h"
#include "modules/tls.h"

static_assert(sizeof(double) == 8, "NaN-boxing requires 64-bit IEEE 754 doubles");
static_assert(sizeof(uint64_t) == 8, "NaN-boxing requires 64-bit integers");
static_assert(sizeof(double) == sizeof(uint64_t), "double and uint64_t must have same size");

#if defined(__STDC_IEC_559__) || defined(__GCC_IEC_559)
#elif defined(__FAST_MATH__)
  #error "NaN-boxing is incompatible with -ffast-math"
#elif DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
  #error "NaN-boxing requires IEEE 754 binary64 doubles"
#endif

ant_value_t js_codegen_refused(ant_t *js) {
  return js_mkerr_typed(js, JS_ERR_EVAL, "Code generation from strings disallowed for this context");
}

static ant_value_t builtin_eval(ant_params_t) {
  if (nargs == 0) return js_mkundef();
  ant_value_t code = args[0];
  
  if (vtype(code) != kTypeString) return code;
  if (__builtin_expect(js->codegen_blocked & ANT_CODEGEN_NO_STRINGS, 0)) return js_codegen_refused(js);
  
  ant_offset_t code_len = 0;
  ant_offset_t code_off = vstr(js, code, &code_len);
  const char *code_str = (const char *)(uintptr_t)(code_off);
  
  return js_eval_bytecode_eval_with_strict(js, code_str, (size_t)code_len, false);
}

ant_value_t js_builtin_eval(ant_t *js) {
  if (vtype(js->sym.eval_fn) == kTypeBuiltin) return js->sym.eval_fn;
  ant_value_t eval_fn = js_cfunc_expose_named(js, Ant_Function_CreateWithArity(builtin_eval, 1), "eval", 4);
  if (vtype(eval_fn) == kTypeBuiltin) js->sym.eval_fn = eval_fn;
  return eval_fn;
}

static inline void isolate_arenas_destroy(ant_t *js) {
  #define ISOLATE_ARENA(field, elem_size, epoch_offset, divisor) fixed_arena_destroy(&js->field);
  #include "gc/isolate_arenas.h"
}

static inline size_t isolate_object_arena_size(const ant_isolate_options_t *options) {
  size_t size = options && options->object_arena_bytes ? options->object_arena_bytes : ANT_ARENA_MAX;
  if (size < ANT_ARENA_MIN) size = ANT_ARENA_MIN;
  if (size > ANT_ARENA_MAX) size = ANT_ARENA_MAX;
  return (size + ARENA_GROW_INCREMENT - 1) / ARENA_GROW_INCREMENT * ARENA_GROW_INCREMENT;
}

static inline bool isolate_arenas_init(ant_t *js, const ant_isolate_options_t *options) {
  size_t objects = isolate_object_arena_size(options);

  #define ISOLATE_ARENA(field, elem_size, epoch_offset, divisor)                       \
    if (!fixed_arena_init(&js->field, elem_size, epoch_offset, objects / (divisor))) { \
      isolate_arenas_destroy(js);                                                      \
      return false;                                                                    \
    }
  #include "gc/isolate_arenas.h"

  return true;
}

static ant_t *isolate_init(void *buf, size_t len, const ant_isolate_options_t *options) {
  #if UINTPTR_MAX > 9007199254740991ULL
  ANT_ASSERT(
    (uintptr_t)buf <= ((1ULL << 53) - 1),
    "pointer exceeds 53-bit double-precision integer limit"
  );
  #endif
  
  ant_t *js = NULL;
  
  if (len < sizeof(*js)) return js;
  memset(buf, 0, len);
  
  js = (ant_t *)buf;
  js->stringify_root.max_depth = ANT_INSPECT_MAX_DEPTH;
  Ant_List_Insert(&js->stringify, &js->stringify_root);
  
  js->exception = js_mkundef();
  js->exception_oom = js_mkundef();
  js_init_intern_cache(js);
  
  js->pool.rope.block_size = ANT_POOL_ROPE_BLOCK_SIZE;
  js->rope_gc.young.block_size = ANT_POOL_ROPE_BLOCK_SIZE;
  js->rope_gc.old.block_size = ANT_POOL_ROPE_BLOCK_SIZE;
  js->gc_use_nursery_major_floor = true;
  
  js->ic.epoch = 1;
  js->ic.obj_epoch = 1;
  gc_state_init(js);
  
  js->defaults.stream_high_water_mark = 65536.0;
  js->defaults.stream_object_high_water_mark = 16.0;
  js->defaults.net_auto_select_family_timeout = 250.0;
  js->defaults.net_auto_select_family = true;
  
  if (!isolate_arenas_init(js, options)) return NULL;
  if (!Ant_Loop_Init(js)) return NULL;

  js->young_closure_trigger = GC_CLOSURE_NURSERY_THRESHOLD;
  js->c_root_cap = 64;
  js->c_roots = calloc(js->c_root_cap, sizeof(*js->c_roots));
  
  if (!js->c_roots) {
    isolate_arenas_destroy(js);
    return NULL;
  }

  ant_realm_t *main_realm = calloc(1, sizeof(ant_realm_t));
  if (!main_realm) {
    free(js->c_roots);
    isolate_arenas_destroy(js);
    return NULL;
  }
  
  main_realm->is_main = true;
  Ant_List_Insert(&js->realms, main_realm);
  js->realm = js->main_realm = main_realm;

  js->modules.cjs.cache = js_mkundef();
  js->modules.cjs.parent = js_mkundef();
  js->modules.cjs.main = js_mkundef();
  js->modules.cjs.constructor = js_mkundef();
  js->modules.cjs.generation = 0;
  js->modules.hooks = js_mkundef();
  js->modules.import_meta = js_mkundef();
  js->modules.state = NULL;
  js->length_str = ANT_STRING("length");
  
  realm_init_core(js);
  
  js->this_val = js->global;
  js->owns_mem = false;
  js->max_size = 0;
  
  return js;
}

ant_t *ant_create(const ant_isolate_options_t *options) {
  ant_t *js = (ant_t *)calloc(1, sizeof(*js));
  if (js == NULL) return NULL;
  
  if (isolate_init(js, sizeof(*js), options) == NULL) {
    free(js);
    return NULL;
  }
  
  js->owns_mem = true;
  js->vm = sv_vm_create(js);

  if (!js->vm) {
    js_destroy(js);
    return NULL;
  }

  ant_value_t oom = Ant_Error_Create(
    js, JS_ERR_INTERNAL | JS_ERR_NO_STACK,
    "out of memory creating exception"
  );

  if (is_err(oom)) {
    js_destroy(js);
    return NULL;
  }

  js->exception_oom = Ant_Exception_CreateRecord(js, oom, js_mkundef());
  if (!is_err(js->exception_oom) || vdata(js->exception_oom) < 2) {
    js_destroy(js);
    return NULL;
  }

  return js;
}

void ant_thread_cleanup(void) {
  ant_output_thread_cleanup();
  utf8_thread_cleanup();
  ts_strip_thread_cleanup();
}

void js_destroy(ant_t *js) {
  if (js == NULL) return;
  js->destroying = true;
  
  cleanup_isolate_module(js);
  cleanup_worker_threads_module(js);
  cleanup_timer_module(js);
  json_layout_cache_clear(js);
  
  if (js->index_layout_shape) ant_shape_release(js->index_layout_shape);
  js->index_layout_shape = NULL;
  
  cleanup_cron_module(js);
  cleanup_abort_module(js);
  cleanup_eventsource_module(js);
  cleanup_websocket_module(js);
  cleanup_dns_module(js);
  cleanup_net_module(js);
  cleanup_tls_module(js);
  cleanup_server_module(js);
  cleanup_fetch_module(js);
  
  ant_http_pool_free(js->http_pool);
  js->http_pool = NULL;
  
  cleanup_child_process_module(js);
  cleanup_fs_module(js);
  cleanup_readline_module(js);
  cleanup_napi_module(js);
  cleanup_message_port_module(js);
  cleanup_navigator_module(js);
  cleanup_performance_module(js);
  cleanup_process_module(js);

  Ant_Loop_Drain(js);
  cleanup_timer_module(js);
  gc_weak_cleanup(js);

  if (js->vm) {
    sv_vm_destroy(js->vm);
    js->vm = NULL;
  }
  
  js_esm_cleanup_module_cache(js);
  sv_jit_destroy(js);
  
  sv_ic_shape_refs_cleanup(js);
  sv_keyed_put_destroy(js);
  sv_code_units_destroy(js);
  sv_ic_polys_cleanup(js);
  
  free(js->ic.gf_mega);
  free(js->ic.gf_mega_keyed);
  free(js->gc.mark_stack);
  free(js->gc.func_stack);
  free(js->gc.fb_funcs);
  
  js->ic.gf_mega = NULL;
  js->ic.gf_mega_keyed = NULL;
  
  code_arenas_destroy(js);
  cleanup_rpc_module(js);
  cleanup_lmdb_module(js);
  js_descriptor_registry_cleanup(js);
  cleanup_iterator_module(js);

  sym_registry_entry_t *entry, *tmp;
  HASH_ITER(hh, js->sym.registry, entry, tmp) {
    HASH_DEL(js->sym.registry, entry);
    free(entry);
  }

  while (js->objects || js->objects_old || js->permanent_objects) {
    ant_object_t *lists[] = { js->objects, js->objects_old, js->permanent_objects };
    js->objects = NULL;
    js->objects_old = NULL;
    js->permanent_objects = NULL;
    
    for (int i = 0; i < 3; i++) for (ant_object_t *obj = lists[i]; obj;) {
      ant_object_t *next = obj->next;
      gc_object_free(js, obj);
      obj = next;
    }
  }
  
  cleanup_buffer_module(js);
  cleanup_atomics_module(js);
  cleanup_events_module(js);
  cleanup_regex_module(js);
  free_process_module(js);
  free_napi_module(js);
  
  Ant_Loop_Destroy(js);
  gc_array_storage_cache_destroy(js);
  
  coroutine_pool_destroy(js);
  gc_closures_destroy(js);
  isolate_arenas_destroy(js);
  
  free(js->c_roots);
  js->c_roots = NULL;
  js->c_root_count = js->c_root_cap = 0;

  ant_realm_destroy_all(js);
  if (!js->process_exiting) ant_shapes_destroy(js);
  free(js->global_lexicals);
  
  js->global_lexicals = NULL;
  js->global_lexical_count = js->global_lexical_cap = 0;
  free(js->global_lexical_index);
  
  js->global_lexical_index = NULL;
  js->global_lexical_index_cap = 0;

  free(js->permanent_roots);
  js->permanent_roots = NULL;
  js->permanent_root_len = 0;
  js->permanent_root_cap = 0;
  js->permanent_root_traced = 0;

  free(js->remembered_upvalues);
  js->remembered_upvalues = NULL;
  js->remembered_upvalue_len = js->remembered_upvalue_cap = 0;

  free(js->remembered_closures);
  js->remembered_closures = NULL;
  free(js->remembered_coroutines);
  js->remembered_coroutines = NULL;
  js->remembered_coroutine_len = js->remembered_coroutine_cap = 0;
  js->remembered_closure_len = js->remembered_closure_cap = 0;

  free(js->rope_gc.remembered_builders);
  js->rope_gc.remembered_builders = NULL;
  js->rope_gc.remembered_builder_len = js->rope_gc.remembered_builder_cap = 0;

  free(js->rope_gc.marks);
  js->rope_gc.marks = NULL;
  js->rope_gc.last_mark = NULL;
  js->rope_gc.mark_count = js->rope_gc.mark_cap = 0;

  free(js->young_closures);
  js->young_closures = NULL;
  js->young_closure_len = js->young_closure_cap = 0;

  free(js->young_upvalues);
  js->young_upvalues = NULL;
  js->young_upvalue_len = js->young_upvalue_cap = 0;

  free(js->remember_set);
  js->remember_set = NULL;
  js->remember_set_len = js->remember_set_cap = 0;

  free(js->pending_rejections.items);
  js->pending_rejections.items = NULL;
  js->pending_rejections.len = js->pending_rejections.cap = 0;

  ant_ptr_pair_free(&js->cfunc_promoted);
  ant_ptr_pair_free(&js->cfunc_named);
  wasm_isolate_destroy(js);

  js_pool_destroy(&js->pool.rope);
  js_pool_destroy(&js->rope_gc.young);
  js_pool_destroy(&js->rope_gc.old);
  js_pool_destroy(&js->pool.symbol);
  js_pool_destroy(&js->pool.permanent);
  
  js_class_pool_destroy(&js->pool.bigint);
  js_string_pool_destroy(&js->pool.string);
  gc_strings_epoch_bump();
  gc_strings_destroy(js);
  gc_bigints_destroy(js);
  intern_table_free(js);

  if (js->owns_mem) free(js);
}

bool js_chkargs(ant_args_t, const char *spec) {
  int i = 0, ok = 1;
  for (; ok && i < nargs && spec[i]; i++) {
    uint8_t t = vtype(args[i]), c = (uint8_t) spec[i];
    ok = (c == 'b' && t == kTypeBool) || (c == 'd' && t == kTypeNumber) ||
         (c == 's' && t == kTypeString) || (c == 'j');
  }
  if (spec[i] != '\0' || i != nargs) ok = 0;
  return ok;
}

sv_func_t *js_compile_parsed_bytecode(
  ant_t *js, sv_ast_t *program, const char *buf,
  size_t len, int mode_value, ant_value_t eval_env
) {
  sv_compile_mode_t mode = (sv_compile_mode_t)mode_value;

  if (mode != SV_COMPILE_MODULE && mode != SV_COMPILE_REPL && (program->flags & FN_MODULE_SYNTAX)) {
    js_mkerr_typed(js, JS_ERR_SYNTAX, "Cannot use import/export syntax outside a module");
    return NULL;
  }

  if (mode == SV_COMPILE_MODULE) {
    ant_value_t ns = js_module_eval_active_ns(js);
    if (is_object_type(ns)) esm_predeclare_exports(js, program, ns);
  }

  sv_func_t *func = sv_compile(js, program, mode, buf, (ant_offset_t)len, eval_env);
  if (!func) {
    if (!Ant_Exception_Pending(js)) js_mkerr_typed(js, JS_ERR_INTERNAL | JS_ERR_NO_STACK, "Unexpected compile error");
    return NULL;
  }

  return func;
}

static ant_value_t execute_top_level(
  ant_t *js, sv_func_t *func,
  ant_value_t this_val, coroutine_t **async_coro_out
) {
  if (async_coro_out) *async_coro_out = NULL;
  if (sv_debug_unlikely(SV_DEBUG_DUMP_BC)) sv_disasm(js, func, js->filename);
  if (func->is_tla) return sv_start_tla(js, func, this_val, async_coro_out);
  return sv_execute_entry(js->vm, func, this_val, NULL, 0);
}

ant_value_t js_execute_compiled_bytecode(ant_t *js, sv_func_t *func, coroutine_t **async_coro_out) {
  return execute_top_level(js, func, js->global_this, async_coro_out);
}

ant_value_t js_execute_compiled_module(ant_t *js, sv_func_t *func) {
  return execute_top_level(js, func, js_mkundef(), NULL);
}

static ant_value_t js_execute_compiled_repl_bytecode(
  ant_t *js, sv_func_t *func, coroutine_t **async_coro_out
) {
  if (async_coro_out) *async_coro_out = NULL;
  GC_ROOT_SAVE(mark, js);

  ant_value_t compiled = mkref(kTypeFunctionInfo, func);
  GC_ROOT_PIN(js, compiled);

  ant_value_t result = sv_global_declare(js, func);
  if (!is_err(result))
    result = js_execute_compiled_bytecode(js, func, async_coro_out);

  GC_ROOT_RESTORE(js, mark);
  return result;
}

static ant_value_t js_execute_compiled_eval_bytecode(
  ant_t *js, sv_func_t *func,
  ant_value_t this_val, ant_value_t eval_env, ant_value_t new_target
) {
  if (sv_debug_unlikely(SV_DEBUG_DUMP_BC)) sv_disasm(js, func, js->filename);
  GC_ROOT_SAVE(mark, js);

  ant_value_t compiled = mkref(kTypeFunctionInfo, func);
  GC_ROOT_PIN(js, compiled);

  ant_value_t result = sv_eval_declare_vars(js, func, eval_env, true);
  if (!is_err(result))
    result = sv_execute_eval_entry(js->vm, func, this_val, eval_env, new_target);

  GC_ROOT_RESTORE(js, mark);
  return result;
}

static inline js_eval_result_t js_eval_bytecode_mode_result(
  ant_t *js, const char *buf, size_t len, 
  sv_compile_mode_t mode, bool parse_strict,
  ant_value_t eval_this, ant_value_t eval_env, ant_value_t new_target
) {
  if (len == (size_t)~0U) len = strlen(buf);

  code_arena_mark_t parse_mark = parse_arena_mark(js);
  sv_parse_goal_t goal =
    mode == SV_COMPILE_MODULE ? SV_PARSE_MODULE :
    mode == SV_COMPILE_REPL ? SV_PARSE_REPL : SV_PARSE_SCRIPT;
  sv_ast_t *program = sv_parse(js, buf, (ant_offset_t)len, goal, parse_strict);

  if (!program) {
    parse_arena_rewind(js, parse_mark);
    ant_value_t value = Ant_Exception_Pending(js)
      ? Ant_Exception_Current(js)
      : js_mkerr_typed(js, JS_ERR_INTERNAL | JS_ERR_NO_STACK, "Unexpected parse error");
    
    return (js_eval_result_t){
      .value = value,
      .async_coro = NULL,
      .kind = JS_EVAL_COMPLETE,
    };
  }

  sv_func_t *func = js_compile_parsed_bytecode(js, program, buf, len, mode, eval_env);
  parse_arena_rewind(js, parse_mark);

  if (!func) {
    ant_value_t value = Ant_Exception_Pending(js)
      ? Ant_Exception_Current(js)
      : js_mkerr_typed(js, JS_ERR_INTERNAL | JS_ERR_NO_STACK, "Unexpected compile error");
    
    return (js_eval_result_t){
      .value = value,
      .async_coro = NULL,
      .kind = JS_EVAL_COMPLETE,
    };
  }

  coroutine_t *async_coro = NULL;
  ant_value_t value;
  
  switch (mode) {
    case SV_COMPILE_EVAL:
    case SV_COMPILE_EVAL_FUNCTION:
      value = js_execute_compiled_eval_bytecode(js, func, eval_this, eval_env, new_target);
      break;
    case SV_COMPILE_REPL:
      value = js_execute_compiled_repl_bytecode(js, func, &async_coro);
      break;
    case SV_COMPILE_MODULE:
      value = js_execute_compiled_module(js, func);
      break;
    case SV_COMPILE_SCRIPT:
    default:
      value = js_execute_compiled_bytecode(js, func, NULL);
      break;
  }

  sv_code_unit_unpin(func->unit);
  
  return (js_eval_result_t){
    .value = value,
    .async_coro = async_coro,
    .kind = async_coro ? JS_EVAL_ASYNC_ENTRY : JS_EVAL_COMPLETE,
  };
}

static inline ant_value_t js_eval_bytecode_mode(
  ant_t *js, const char *buf, size_t len,
  sv_compile_mode_t mode, bool parse_strict,
  ant_value_t eval_this, ant_value_t eval_env, ant_value_t new_target
) {
  return js_eval_bytecode_mode_result(
    js, buf, len, mode, 
    parse_strict, eval_this, eval_env, new_target
  ).value;
}

ant_value_t js_eval_bytecode(ant_t *js, const char *buf, size_t len) {
  return js_eval_bytecode_mode(
    js, buf, len, SV_COMPILE_SCRIPT, 
    false, js_mkundef(), js_mkundef(), js_mkundef()
  );
}

ant_value_t js_eval_bytecode_module(ant_t *js, const char *buf, size_t len) {
  return js_eval_bytecode_mode(
    js, buf, len, SV_COMPILE_MODULE, 
    false, js_mkundef(), js_mkundef(), js_mkundef()
  );
}

ant_value_t js_eval_bytecode_eval(ant_t *js, const char *buf, size_t len) {
  return js_eval_bytecode_mode(
    js, buf, len, SV_COMPILE_EVAL, 
    false, js->global, js->global, js_mkundef()
  );
}

ant_value_t js_eval_bytecode_eval_with_strict(ant_t *js, const char *buf, size_t len, bool inherit_strict) {
  return js_eval_bytecode_mode(
    js, buf, len, SV_COMPILE_EVAL, 
    inherit_strict, js->global, js->global, js_mkundef()
  );
}

ant_value_t js_eval_bytecode_eval_in_env_with_strict(
  ant_t *js, const char *buf, size_t len,
  bool inherit_strict, ant_value_t this_val, ant_value_t eval_env,
  ant_value_t new_target, bool allows_new_target
) {
  return js_eval_bytecode_mode(
    js, buf, len, allows_new_target ? SV_COMPILE_EVAL_FUNCTION : SV_COMPILE_EVAL,
    inherit_strict, this_val, eval_env, new_target
  );
}

ant_value_t js_vm_script_compile(ant_t *js, const char *buf, size_t len) {
  code_arena_mark_t parse_mark = parse_arena_mark(js);
  sv_ast_t *program = sv_parse(js, buf, (ant_offset_t)len, SV_PARSE_SCRIPT, false);
  if (program && (program->flags & FN_MODULE_SYNTAX)) {
    js_mkerr_typed(js, JS_ERR_SYNTAX, "Cannot use import/export syntax outside a module");
    program = NULL;
  }
  
  sv_func_t *func = program
    ? js_compile_parsed_bytecode(js, program, buf, len, SV_COMPILE_REPL, js_mkundef())
    : NULL;
  
  parse_arena_rewind(js, parse_mark);

  if (!func) return Ant_Exception_Pending(js)
    ? Ant_Exception_Current(js)
    : js_mkerr_typed(js, JS_ERR_SYNTAX | JS_ERR_NO_STACK, "Unexpected parse error");

  if (func->debug) func->debug->name = func->debug->display_name = "<script>";

  ant_value_t compiled = mkref(kTypeFunctionInfo, func);
  sv_code_unit_unpin(func->unit);
  
  return compiled;
}

ant_value_t js_vm_script_run(ant_t *js, ant_value_t compiled) {
  return js_execute_compiled_repl_bytecode(js, (sv_func_t *)vptr(compiled), NULL);
}

ant_value_t js_function_from_source(ant_t *js, ant_value_t *params_and_body, int nargs) {
  return build_dynamic_function(js, params_and_body, nargs, js_mkundef(), false, false);
}

js_eval_result_t js_eval_bytecode_repl(
  ant_t *js, const char *buf, size_t len
) {
  return js_eval_bytecode_mode_result(
    js, buf, len, SV_COMPILE_REPL,
    false, js_mkundef(), js_mkundef(), js_mkundef()
  );
}

static inline ant_value_t sv_call_object_builtin(
  ant_t *js, ant_value_t this_val, ant_call_params_t
) {
  ant_value_t saved_this = js->this_val;
  js->this_val = this_val;
  
  ant_value_t res = sv_invoke_native(js, builtin_Object, args, nargs, new_target);
  js->this_val = saved_this;
  
  return res;
}

ant_value_t sv_call_native(ant_t *js, ant_value_t func, ant_value_t this_val, ant_call_params_t) {
  if (vtype(func) == kTypeBuiltin) return sv_call_builtin_this(js, func, this_val, args, nargs, new_target);
  
  if (vtype(func) == kTypeFunction) {
    ant_object_t *func_obj = js_obj_ptr(js_func_obj(func));
    ant_value_t cfunc_slot = obj_extra_get(func_obj, SLOT_CFUNC);
    
    if (vtype(cfunc_slot) == kTypeBuiltin)
      return sv_call_native_closure(js, func, cfunc_slot, this_val, args, nargs, new_target);
      
    ant_value_t builtin_slot = obj_extra_get(func_obj, SLOT_BUILTIN);
    if (vtype(builtin_slot) == kTypeNumber && (int)Ant_Value_AsNumberUnchecked(builtin_slot) == BUILTIN_OBJECT)
      return sv_call_object_builtin(js, this_val, args, nargs, new_target);
  }

  return js_mkerr_typed(js, JS_ERR_TYPE, "%s is not a function", typestr(vtype(func)));
}

static void report_unhandled_rejection(ant_t *js, ant_value_t promise, ant_value_t reason) {
  if (process_has_event_listeners(js, "unhandledRejection")) {
    ant_value_t event_args[2] = { reason, promise };
    emit_process_event(js, "unhandledRejection", event_args, 2);
    process_report_uncaught_exception_if_pending(js);
    return;
  }

  if (js_fire_unhandled_rejection(js, promise, reason)) return;

  if (process_has_event_listeners(js, "uncaughtException")) {
    ant_value_t event_args[2] = { reason, js_mkstr(js, "unhandledRejection", 18) };
    emit_process_event(js, "uncaughtException", event_args, 2);
    
    if (Ant_Exception_Pending(js)) {
      ant_value_t thrown = js_take_thrown(js, js_mkundef());
      if (js->worker) ant_worker_report_uncaught(js, thrown);
      else if (ant_watch_supervises(js)) ant_watch_exit(js);
      else if (!js->uncaught_nonfatal) exit(7);
    }
    
    return;
  }

  if (js->worker) {
    ant_worker_report_uncaught(js, reason);
    return;
  }

  print_unhandled_promise_rejection(js, reason);
  if (!js->uncaught_nonfatal) process_exit_with(js, EXIT_FAILURE);
}

void js_check_unhandled_rejections(ant_t *js) {
  size_t keep = 0;
  
  for (size_t i = 0; i < js->pending_rejections.len; i++) {
    ant_value_t p = js->pending_rejections.items[i];
    ant_promise_state_t *pd = js_promise_data(js, p, false);
    if (!pd || pd->has_rejection_handler || pd->unhandled_reported) continue;
    
    if (vtype(pd->trigger_parent) == kTypePromise) {
      ant_promise_state_t *parent = js_promise_data(js, pd->trigger_parent, false);
      if (parent && parent->has_rejection_handler) continue;
    }
    
    if (js->fatal_error) {
      js_throw(js, pd->value);
      print_uncaught_throw(js);
      int status = process_run_exit_handlers(js, EXIT_FAILURE);
      js->process_exiting = true;
      js_destroy(js); exit(status);
    }

    if (js_is_termination(pd->value)) continue;

    GC_ROOT_SAVE(root_mark, js);
    ant_value_t reason = pd->value;
    GC_ROOT_PIN(js, p); GC_ROOT_PIN(js, reason);
    
    pd->unhandled_reported = true;
    report_unhandled_rejection(js, p, reason);
    GC_ROOT_RESTORE(js, root_mark);
  }
  
  js->pending_rejections.len = keep;
}
