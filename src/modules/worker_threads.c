#include <compat.h> // IWYU pragma: keep

#include <libgen.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <uthash.h>
#include <uv.h>

#include "ant.h"
#include "arena.h"
#include "bootstrap.h"
#include "cfunc.h"
#include "errors.h"
#include "internal.h"
#include "interrupt.h"
#include "list.h"
#include "ptr.h"
#include "reactor.h"
#include "runtime.h"
#include "vfs_bundle.h"
#include "sync.h"
#include "silver/call.h"

#include "esm/commonjs.h"
#include "esm/loader.h"
#include "esm/remote.h"
#include "gc/modules.h"
#include "modules/events.h"
#include "modules/atomics.h"
#include "modules/buffer.h"
#include "modules/message_port.h"
#include "modules/performance.h"
#include "modules/process.h"
#include "modules/stream.h"
#include "output.h"
#include "modules/timer.h"
#include "modules/wire.h"
#include "modules/worker_threads.h"

typedef enum {
  WORKER_EVENT_ONLINE,
  WORKER_EVENT_ERROR,
  WORKER_EVENT_STDOUT,
  WORKER_EVENT_STDERR,
  WORKER_EVENT_EXIT,
} worker_event_kind_t;

typedef enum {
  WORKER_ERROR_THROWN,
  WORKER_ERROR_INIT,
  WORKER_ERROR_OOM,
} worker_error_kind_t;

typedef struct worker_event {
  struct worker_event *next;
  worker_event_kind_t kind;
  int code;
  ant_wire_t *wire;
  size_t len;
  char bytes[];
} worker_event_t;

enum {
  WORKER_LIMIT_YOUNG,
  WORKER_LIMIT_OLD,
  WORKER_LIMIT_CODE,
  WORKER_LIMIT_STACK,
  WORKER_LIMIT_COUNT,
};

struct ant_worker {
  _Atomic int refs;
  ant_mutex_t lock;
  uint64_t thread_id;

  ant_t *parent;
  const ant_bundle_t *bundle;
  
  ant_value_t obj;
  ant_value_t exit_promise;
  uv_async_t parent_async;
  
  bool parent_async_open;
  bool refed;
  bool exited;
  bool pipe_stdout;
  bool pipe_stderr;
  bool stdin_enabled;
  bool stdin_ended;
  
  int exit_code;
  ant_value_t stdin_obj;
  ant_value_t stdout_obj;
  ant_value_t stderr_obj;
  ant_port_binding_t *binding;
  
  worker_event_t *events_head;
  worker_event_t *events_tail;
  worker_event_t *stdin_head;
  worker_event_t *stdin_tail;
  ant_worker_t *next;
  ant_worker_t **pprev;

  uv_thread_t thread;
  bool thread_started;
  ant_t *child;
  _Atomic uint64_t loop_start_ns;
  uv_async_t child_async;
  bool child_async_open;
  
  ant_value_t child_stdin;
  bool child_stdin_reading;
  bool child_stdin_done;
  
  bool terminate_requested;
  int requested_exit_code;
  ant_port_end_t *child_end;

  char *entry;
  bool is_eval;
  int argc;
  char **argv;
  char *name;
  char **env;
  bool share_env;
  
  ant_wire_t *worker_data;
  ant_wire_t *environment_data;
  double limits[WORKER_LIMIT_COUNT];
  double requested_limits[WORKER_LIMIT_COUNT];
};

static constexpr uint32_t WORKER_NATIVE_TAG = Ant_NativeTag("WRKR");
static _Atomic uint64_t next_thread_id = 1;
static constexpr double WORKER_DEFAULT_STACK_MB = 4.0;
static constexpr double WORKER_DEFAULT_OLD_MB = 512.0;

static void worker_install_stdin(ant_t *js, ant_worker_t *w);

static void worker_release(ant_worker_t *w) {
  if (atomic_fetch_sub_explicit(&w->refs, 1, memory_order_acq_rel) != 1) return;
  for (worker_event_t *e = w->events_head, *next; e; e = next) {
    next = e->next;
    ant_wire_release(e->wire);
    free(e);
  }
  for (worker_event_t *e = w->stdin_head, *next; e; e = next) {
    next = e->next;
    free(e);
  }
  
  if (w->child_end) ant_port_end_release(w->child_end);
  ant_wire_release(w->worker_data);
  ant_wire_release(w->environment_data);
  for (int i = 0; i < w->argc; i++) free(w->argv[i]);
  
  free(w->argv);
  for (char **e = w->env; e && *e; e++) free(*e);
  
  free(w->env);
  free(w->entry);
  free(w->name);
  free(w);
}

static void worker_post_event(ant_worker_t *w, worker_event_t *e) {
  ant_mutex_lock(&w->lock);
  if (w->events_tail) w->events_tail->next = e;
  else w->events_head = e;
  w->events_tail = e;
  if (w->parent_async_open) uv_async_send(&w->parent_async);
  ant_mutex_unlock(&w->lock);
}

static void worker_post(ant_worker_t *w, worker_event_kind_t kind, int code, ant_wire_t *wire) {
  worker_event_t *e = calloc(1, sizeof(*e));
  if (!e) {
    ant_wire_release(wire);
    return;
  }
  e->kind = kind;
  e->code = code;
  e->wire = wire;
  worker_post_event(w, e);
}

static bool worker_stdio_writer(FILE *stream, const void *data, size_t len, void *user) {
  worker_event_t *e = malloc(sizeof(*e) + len);
  if (!e) return false;
  *e = (worker_event_t){ .kind = stream == stderr ? WORKER_EVENT_STDERR : WORKER_EVENT_STDOUT, .len = len };
  memcpy(e->bytes, data, len);
  worker_post_event(user, e);
  return true;
}

static void worker_drain_stdin(ant_t *js, ant_worker_t *w) {
  if (!is_object_type(w->child_stdin) || w->child_stdin_done) return;
  ant_mutex_lock(&w->lock);
  
  worker_event_t *e = w->stdin_head;
  bool ended = w->stdin_ended || !w->stdin_enabled;
  w->stdin_head = w->stdin_tail = NULL;
  ant_mutex_unlock(&w->lock);

  GC_ROOT_SAVE(mark, js);
  ant_value_t stream = w->child_stdin;
  GC_ROOT_PIN(js, stream);
  
  for (worker_event_t *next; e; e = next) {
    next = e->next;
    ArrayBufferData *ab = create_array_buffer_data(js, e->len);
    if (ab && e->len) memcpy(ab->data, e->bytes, e->len);
    ant_value_t chunk = ab ? create_typed_array(js, TYPED_ARRAY_UINT8, ab, 0, e->len, js->builtins.buffer_proto) : js_mkundef();
    free(e);
    if (is_object_type(chunk)) stream_readable_push(js, stream, chunk, js_mkundef());
  }
  
  if (ended) {
    w->child_stdin_done = true;
    if (w->child_stdin_reading) uv_unref((uv_handle_t *)&w->child_async);
    w->child_stdin_reading = false;
    stream_readable_push(js, stream, js_mknull(), js_mkundef());
  }
  
  GC_ROOT_RESTORE(js, mark);
  process_microtasks(js);
}

static void worker_child_wake(uv_async_t *handle) {
  ant_worker_t *w = handle->data;
  ant_t *js = w->child;
  if (!js) return;
  
  ant_mutex_lock(&w->lock);
  bool stop = w->terminate_requested;
  ant_mutex_unlock(&w->lock);
  
  if (!stop) {
    worker_drain_stdin(js, w);
    return;
  }
  js->terminating = true;
  atomic_store_explicit(&js->interrupt_pending, 1, memory_order_relaxed);
  uv_stop(js->loop);
}

static void worker_request_stop(ant_worker_t *w, int code) {
  ant_mutex_lock(&w->lock);
  if (!w->terminate_requested) {
    w->terminate_requested = true;
    w->requested_exit_code = code;
  }
  if (w->child) {
    atomic_store_explicit(&w->child->interrupt_pending, 1, memory_order_relaxed);
    atomics_wake_isolate(w->child);
    if (w->child_async_open) uv_async_send(&w->child_async);
  }
  ant_mutex_unlock(&w->lock);
}

static bool worker_stop_requested(ant_worker_t *w, int *code) {
  ant_mutex_lock(&w->lock);
  bool requested = w->terminate_requested;
  if (requested && code) *code = w->requested_exit_code;
  ant_mutex_unlock(&w->lock);
  return requested;
}

static ant_wire_t *worker_serialize_error(ant_t *js, ant_value_t error) {
  ant_wire_t *wire = NULL;
  ant_value_t r = ant_wire_serialize(js, error, js_mkundef(), js_mkundef(), &wire);
  if (!is_err(r)) return wire;

  js_take_thrown(js, r);
  ant_value_t text = js_tostring_val(js, error);
  if (is_err(text)) {
    js_take_thrown(js, text);
    text = js_mkstr(js, "Uncaught exception in worker", 28);
  }
  r = ant_wire_serialize(js, text, js_mkundef(), js_mkundef(), &wire);
  if (is_err(r)) {
    js_take_thrown(js, r);
    return NULL;
  }
  return wire;
}

bool ant_worker_report_uncaught(ant_t *js, ant_value_t reason) {
  ant_worker_t *w = js->worker;
  if (!w) return false;
  int code = 0;
  if (worker_stop_requested(w, &code)) return true;

  bool oom = is_err(js->exception_oom) && reason == Ant_Exception_Value(js, js->exception_oom);
  ant_wire_t *wire = NULL;
  if (!oom) {
    GC_ROOT_SAVE(mark, js);
    GC_ROOT_PIN(js, reason);
    wire = worker_serialize_error(js, reason);
    GC_ROOT_RESTORE(js, mark);
  }

  worker_post(w, WORKER_EVENT_ERROR, oom ? WORKER_ERROR_OOM : WORKER_ERROR_THROWN, wire);
  worker_request_stop(w, 1);
  js->terminating = true;
  return true;
}

ant_value_t ant_worker_exit(ant_t *js, int code) {
  ant_worker_t *w = js->worker;
  if (w) worker_request_stop(w, code);
  return js_interrupt_raise(js);
}

bool ant_worker_is_worker(ant_t *js) {
  return js->worker != NULL;
}

static ant_value_t worker_run_entry(ant_t *js, ant_worker_t *w) {
  if (w->is_eval) {
    const char *tag = "[worker eval]";
    js_set_filename(js, tag);
    js_setup_import_meta(js, tag);
    ant_value_t ns = Ant_Object_CreateBare(js);
    if (is_err(ns)) return ns;
    return esm_load_commonjs_module(js, tag, w->entry, strlen(w->entry), ns, js_mkundef());
  }

  ant_value_t specifier;
  if (strncmp(w->entry, "data:", 5) == 0) {
    js_set_global_builtin(js, "__dirname", js_mkundef());
    specifier = js_mkstr(js, w->entry, strlen(w->entry));
    js_set_filename(js, w->entry);
    js_setup_import_meta(js, w->entry);
  } else {
    char *copy = strdup(w->entry);
    if (copy) {
      char *dir = dirname(copy);
      js_set_global_builtin(js, "__dirname", js_mkstr(js, dir, strlen(dir)));
      free(copy);
    }
    
    const char *interned = intern_string(js, w->entry, strlen(w->entry));
    js_set_filename(js, interned ? interned : w->entry);
    js_setup_import_meta(js, interned ? interned : w->entry);
    specifier = js_esm_make_file_url(js, w->entry);
  }
  
  js_set_global_builtin(js, "__filename", js_mkstr(js, w->entry, strlen(w->entry)));
  return js_esm_import_sync(js, specifier);
}

static void worker_report_pending(ant_t *js) {
  if (!Ant_Exception_Pending(js)) return;
  ant_value_t thrown = js_take_thrown(js, js_mkundef());
  if (js_is_termination(thrown)) return;
  ant_worker_report_uncaught(js, thrown);
}

static void worker_thread_main(void *arg) {
  ant_worker_t *w = arg;
  volatile char stack_base;
  int exit_code = 1;

  ant_t *js = ant_create(&(ant_isolate_options_t){
    .object_arena_bytes = (size_t)(w->limits[WORKER_LIMIT_OLD] * 1024 * 1024),
  });
  
  if (!js) {
    worker_post(w, WORKER_EVENT_ERROR, WORKER_ERROR_INIT, NULL);
    worker_post(w, WORKER_EVENT_EXIT, 1, NULL);
    worker_release(w);
    return;
  }

  js->worker = w;
  js->interrupts_armed = true;
  uv_loop_configure(js->loop, UV_METRICS_IDLE_TIME);
  js_setstackbase(js, (void *)&stack_base);
  js_setstacklimit(js, (size_t)(w->limits[WORKER_LIMIT_STACK] * 1024 * 1024) * 3 / 4);

  if (uv_async_init(js->loop, &w->child_async, worker_child_wake) == 0) {
    w->child_async.data = w;
    uv_unref((uv_handle_t *)&w->child_async);
    ant_mutex_lock(&w->lock);
    w->child = js;
    w->child_async_open = true;
    if (w->terminate_requested) atomic_store_explicit(&js->interrupt_pending, 1, memory_order_relaxed);
    ant_mutex_unlock(&w->lock);
  }

  ant_output_set_thread_writer(worker_stdio_writer, w);
  ant_runtime_init(js, w->argc, w->argv, NULL);
  
  if (!w->share_env) process_set_private_env(js, w->env);
  ant_bootstrap_modules(js);
  worker_install_stdin(js, w);
  if (w->bundle && !js_esm_bundle_activate(js, w->bundle)) {
    worker_post(w, WORKER_EVENT_ERROR, WORKER_ERROR_INIT, NULL);
    worker_request_stop(w, 1);
  }

  int stop_code = 0;
  bool started = !worker_stop_requested(w, &stop_code);
  if (started) {
    worker_post(w, WORKER_EVENT_ONLINE, 0, NULL);
    ant_value_t result = worker_run_entry(js, w);
    if (is_err(result) && !Ant_Exception_Pending(js)) ant_worker_report_uncaught(js, result);
    worker_report_pending(js);
    if (!worker_stop_requested(w, NULL)) {
      atomic_store_explicit(&w->loop_start_ns, uv_hrtime(), memory_order_release);
      js_run_event_loop(js);
      worker_report_pending(js);
    }
  }

  if (!started) exit_code = 0;
  else if (worker_stop_requested(w, &stop_code)) exit_code = stop_code;
  else {
    exit_code = process_run_exit_handlers(js, process_exit_code(js));
    if (worker_stop_requested(w, &stop_code)) exit_code = stop_code;
  }

  ant_mutex_lock(&w->lock);
  w->child = NULL;
  bool close_async = w->child_async_open;
  w->child_async_open = false;
  ant_mutex_unlock(&w->lock);
  if (close_async) uv_close((uv_handle_t *)&w->child_async, NULL);

  js->terminating = false;
  atomic_store_explicit(&js->interrupt_pending, 0, memory_order_relaxed);
  js_destroy(js);
  ant_output_set_thread_writer(NULL, NULL);
  ant_thread_cleanup();

  worker_post(w, WORKER_EVENT_EXIT, exit_code, NULL);
  worker_release(w);
}

static ant_worker_t *worker_data(ant_value_t obj) {
  return is_object_type(obj) ? (ant_worker_t *)js_get_native(obj, WORKER_NATIVE_TAG) : NULL;
}

static void worker_finalize(ant_t *js, ant_object_t *obj) {
  ant_value_t value = js_obj_from_ptr(obj);
  ant_worker_t *w = worker_data(value);
  if (!w) return;
  js_clear_native(value, WORKER_NATIVE_TAG);
  w->obj = js_mkundef();
  if (w->binding) {
    ant_port_binding_free(w->binding);
    w->binding = NULL;
  }
  worker_release(w);
}

static void worker_emit(ant_t *js, ant_worker_t *w, const char *event, ant_value_t *args, int nargs) {
  if (!is_object_type(w->obj)) return;
  eventemitter_emit_args(js, w->obj, event, args, nargs);
  process_microtasks(js);
}

static void worker_flush_online(ant_t *js, ant_worker_t *w) {
  for (;;) {
    ant_mutex_lock(&w->lock);
    worker_event_t *e = w->events_head;
    bool online = e && e->kind == WORKER_EVENT_ONLINE;
    if (online) {
      w->events_head = e->next;
      if (!w->events_head) w->events_tail = NULL;
    }
    ant_mutex_unlock(&w->lock);
    if (!online) return;
    free(e);
    worker_emit(js, w, "online", NULL, 0);
  }
}

static void worker_on_message(ant_t *js, ant_value_t target, ant_value_t value, ant_value_t ports) {
  ant_worker_t *w = worker_data(target);
  if (!w) return;
  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, value);
  worker_flush_online(js, w);
  worker_emit(js, w, vtype(ports) == kTypeNull ? "messageerror" : "message", &value, 1);
  GC_ROOT_RESTORE(js, mark);
}

static ant_value_t worker_stdio_noop(ant_params_t) {
  return js_mkundef();
}

static ant_value_t worker_make_readable(ant_t *js) {
  ant_value_t obj = Ant_Object_CreateBare(js);
  if (is_err(obj)) return obj;
  js_set_proto_init(obj, stream_readable_prototype(js));
  stream_init_readable_object(js, obj, js_mkundef());
  js_set(js, obj, "_read", Ant_Function_Create(worker_stdio_noop));
  return obj;
}

static ant_value_t worker_stdin_read(ant_params_t) {
  ant_worker_t *w = js->worker;
  if (!w || w->child_stdin_done || w->child_stdin_reading || !w->child_async_open) return js_mkundef();
  w->child_stdin_reading = true;
  uv_ref((uv_handle_t *)&w->child_async);
  return js_mkundef();
}

static void worker_install_stdin(ant_t *js, ant_worker_t *w) {
  ant_value_t stream = worker_make_readable(js);
  if (is_err(stream)) {
    js_take_thrown(js, stream);
    return;
  }
  js_set(js, stream, "_read", Ant_Function_Create(worker_stdin_read));
  w->child_stdin = stream;
  js_set(js, js_get(js, js_glob(js), "process"), "stdin", stream);
  worker_drain_stdin(js, w);
}

static void worker_stdin_call(ant_t *js, ant_value_t callback) {
  if (!is_callable(callback)) return;
  ant_value_t r = sv_vm_call(js->vm, js, callback, js_mkundef(), NULL, 0, NULL, js_mkundef());
  if (is_err(r)) js_take_thrown(js, r);
}

static void worker_stdin_enqueue(ant_worker_t *w, worker_event_t *e) {
  ant_mutex_lock(&w->lock);
  if (e && !w->stdin_ended) {
    if (w->stdin_tail) w->stdin_tail->next = e;
    else w->stdin_head = e;
    w->stdin_tail = e;
    e = NULL;
  } else if (!e) w->stdin_ended = true;
  if (w->child_async_open) uv_async_send(&w->child_async);
  ant_mutex_unlock(&w->lock);
  free(e);
}

static ant_value_t worker_stdin_write(ant_params_t) {
  ant_worker_t *w = worker_data(js_get_slot(js_getcurrentfunc(js), SLOT_DATA));
  ant_value_t chunk = nargs > 0 ? args[0] : js_mkundef();
  const uint8_t *bytes = NULL;
  size_t len = 0;
  if (vtype(chunk) == kTypeString) bytes = (const uint8_t *)js_getstr(js, chunk, &len);
  else if (!buffer_source_get_bytes(js, chunk, &bytes, &len))
    return Ant_Error_CreateInvalidArgType(js, "chunk", "of type string or an instance of Buffer or Uint8Array", chunk);

  if (w && !w->exited) {
    worker_event_t *e = malloc(sizeof(*e) + len);
    if (!e) return js_mkerr(js, "out of memory");
    *e = (worker_event_t){ .kind = WORKER_EVENT_STDOUT, .len = len };
    if (len) memcpy(e->bytes, bytes, len);
    worker_stdin_enqueue(w, e);
  }
  worker_stdin_call(js, nargs > 2 ? args[2] : js_mkundef());
  return js_mkundef();
}

static ant_value_t worker_stdin_final(ant_params_t) {
  ant_worker_t *w = worker_data(js_get_slot(js_getcurrentfunc(js), SLOT_DATA));
  if (w) worker_stdin_enqueue(w, NULL);
  worker_stdin_call(js, nargs > 0 ? args[0] : js_mkundef());
  return js_mkundef();
}

static ant_value_t worker_make_stdin(ant_t *js, ant_value_t worker_obj) {
  ant_value_t obj = Ant_Object_CreateBare(js);
  if (is_err(obj)) return obj;
  js_set_proto_init(obj, stream_writable_prototype(js));
  stream_init_writable_object(js, obj, js_mkundef());
  js_set(js, obj, "_write", js_heavy_mkfun(js, worker_stdin_write, worker_obj));
  js_set(js, obj, "_final", js_heavy_mkfun(js, worker_stdin_final, worker_obj));
  return obj;
}

static void worker_forward_stdio(ant_t *js, ant_worker_t *w, worker_event_t *e) {
  bool is_stderr = e->kind == WORKER_EVENT_STDERR;
  ant_value_t stream = is_stderr ? w->stderr_obj : w->stdout_obj;
  bool pipe = is_stderr ? w->pipe_stderr : w->pipe_stdout;

  if (pipe) {
    ant_output_stream_t *out = ant_output_stream(is_stderr ? stderr : stdout);
    ant_output_stream_begin(out);
    if (ant_output_stream_append(out, e->bytes, e->len)) ant_output_stream_flush(out);
  }

  if (!is_object_type(stream) || (pipe && eventemitter_listener_count(js, stream, "data") == 0)) return;
  ArrayBufferData *ab = create_array_buffer_data(js, e->len);
  if (!ab) return;
  if (e->len) memcpy(ab->data, e->bytes, e->len);
  ant_value_t chunk = create_typed_array(js, TYPED_ARRAY_UINT8, ab, 0, e->len, js->builtins.buffer_proto);
  if (is_err(chunk)) return;
  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, chunk);
  stream_readable_push(js, stream, chunk, js_mkundef());
  GC_ROOT_RESTORE(js, mark);
  process_microtasks(js);
}

static void worker_end_stdio(ant_t *js, ant_worker_t *w) {
  if (is_object_type(w->stdout_obj)) stream_readable_push(js, w->stdout_obj, js_mknull(), js_mkundef());
  if (is_object_type(w->stderr_obj)) stream_readable_push(js, w->stderr_obj, js_mknull(), js_mkundef());
}

static void worker_parent_async_closed(uv_handle_t *handle) {
  worker_release(handle->data);
}

static void worker_finish(ant_t *js, ant_worker_t *w, int code) {
  if (w->exited) return;
  w->exited = true;
  w->exit_code = code;

  if (w->binding) {
    ant_port_binding_drain(w->binding);
    ant_port_binding_free(w->binding);
    w->binding = NULL;
  }

  if (w->thread_started) {
    uv_thread_join(&w->thread);
    w->thread_started = false;
  }

  ant_mutex_lock(&w->lock);
  bool close_async = w->parent_async_open;
  w->parent_async_open = false;
  ant_mutex_unlock(&w->lock);
  if (close_async) uv_close((uv_handle_t *)&w->parent_async, worker_parent_async_closed);

  GC_ROOT_SAVE(mark, js);
  ant_value_t obj = w->obj;
  ant_value_t promise = w->exit_promise;
  GC_ROOT_PIN(js, obj);
  GC_ROOT_PIN(js, promise);
  Ant_List_Remove(w);

  worker_end_stdio(js, w);
  ant_value_t code_val = js_mknum(code);
  if (is_object_type(obj)) {
    js_set(js, obj, "threadId", js_mknum(-1));
    worker_emit(js, w, "exit", &code_val, 1);
  }
  if (vtype(promise) == kTypePromise) js_resolve_promise(js, promise, code_val);
  w->exit_promise = js_mkundef();
  GC_ROOT_RESTORE(js, mark);
}

static void worker_event_free(worker_event_t *e) {
  ant_wire_release(e->wire);
  free(e);
}

static ant_value_t worker_error_value(ant_t *js, worker_event_t *e) {
  if (e->wire) return ant_wire_deserialize(js, e->wire, NULL);
  if (e->code == WORKER_ERROR_OOM) return js_mkerr_code(
    js, JS_ERR_GENERIC, "ERR_WORKER_OUT_OF_MEMORY",
    "Worker terminated due to reaching memory limit: JS heap out of memory"
  );
  return js_mkerr_code(js, JS_ERR_GENERIC, "ERR_WORKER_INIT_FAILED", "Worker initialization failure: out of memory");
}

static void worker_dispatch_error(ant_t *js, ant_worker_t *w, worker_event_t *e) {
  ant_value_t error = worker_error_value(js, e);
  if (is_err(error)) error = js_take_thrown(js, error);

  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, error);
  if (is_object_type(w->obj) && eventemitter_listener_count(js, w->obj, "error") > 0)
    worker_emit(js, w, "error", &error, 1);
  else {
    js_throw(js, error);
    process_report_uncaught_exception(js);
  }
  
  GC_ROOT_RESTORE(js, mark);
}

static void worker_parent_wake(uv_async_t *handle) {
  ant_worker_t *w = handle->data;
  ant_t *js = w->parent;

  for (;;) {
    ant_mutex_lock(&w->lock);
    worker_event_t *e = w->events_head;
    if (e) {
      w->events_head = e->next;
      if (!w->events_head) w->events_tail = NULL;
    }
    ant_mutex_unlock(&w->lock);
    if (!e) break;

    if (e->kind == WORKER_EVENT_EXIT) {
      int code = e->code;
      worker_event_free(e);
      worker_finish(js, w, code);
      return;
    }

    switch (e->kind) {
      case WORKER_EVENT_ONLINE: worker_emit(js, w, "online", NULL, 0); break;
      case WORKER_EVENT_STDOUT:
      case WORKER_EVENT_STDERR: worker_forward_stdio(js, w, e); break;
      case WORKER_EVENT_ERROR: worker_dispatch_error(js, w, e); break;
      default: break;
    }
    worker_event_free(e);
  }
}

static char *worker_resolve_entry(ant_t *js, ant_value_t spec, bool is_eval, ant_value_t *error) {
  *error = js_mkundef();
  if (is_eval) {
    if (vtype(spec) != kTypeString) {
      *error = js_mkerr_typed(js, JS_ERR_TYPE, "The \"filename\" argument must be of type string when eval is true");
      return NULL;
    }
    size_t len = 0;
    const char *code = js_getstr(js, spec, &len);
    return strndup(code, len);
  }

  ant_value_t href = spec;
  if (is_object_type(spec)) {
    href = js_get(js, spec, "href");
    if (vtype(href) != kTypeString) {
      *error = js_mkerr_typed(js, JS_ERR_TYPE, "The \"filename\" argument must be of type string or an instance of URL");
      return NULL;
    }
  } else if (vtype(spec) != kTypeString) {
    *error = js_mkerr_typed(js, JS_ERR_TYPE, "The \"filename\" argument must be of type string or an instance of URL");
    return NULL;
  }

  size_t len = 0;
  const char *str = js_getstr(js, href, &len);
  char *path = strndup(str, len);
  if (!path) return NULL;
  if (strncmp(path, "data:", 5) == 0) return path;
  if (strncmp(path, "file:", 5) == 0) {
    char *file = esm_file_url_to_path(js, path);
    free(path);
    if (!file) *error = js_mkerr_typed(js, JS_ERR_TYPE, "Invalid file URL");
    return file;
  }
  if (esm_is_url(path)) {
    *error = js_mkerr_typed(js, JS_ERR_TYPE, "The URL must be of scheme file or data. Received \"%s\"", path);
    free(path);
    return NULL;
  }

  bool relative = strncmp(path, "./", 2) == 0 || strncmp(path, "../", 3) == 0;
  const ant_bundle_t *vfs = js_esm_bundle(js);
  if (vfs && (relative || path[0] == '/')) {
    const char *key = NULL;
    if (!relative) key = ant_bundle_has_key(vfs, path) ? path : NULL;
    else {
      const char *parent = js_module_eval_active_filename(js);
      const ant_bundle_module_t *entry = ant_bundle_entry(vfs);
      if (parent) key = ant_bundle_resolve(vfs, parent, path, false);
      if (!key && entry) key = ant_bundle_resolve(vfs, entry->key, path, false);
    }
    if (key) {
      char *copy = strdup(key);
      free(path);
      return copy;
    }
  }

  if (path[0] != '/' && !relative) {
    *error = js_mkerr_typed(
      js, JS_ERR_TYPE, 
      "The worker script or module filename must be an absolute path or a relative path starting with './' or '../'. Received \"%s\"", path
    );
    free(path);
    return NULL;
  }

  char *resolved = realpath(path, NULL);
  if (resolved) {
    free(path);
    return resolved;
  }
  return path;
}

static char **worker_env_from_object(ant_t *js, ant_value_t env_obj) {
  ant_value_t keys = js_own_property_keys(js, env_obj, false, true);
  if (is_err(keys) || vtype(keys) != kTypeArray) return NULL;
  
  ant_offset_t len = js_arr_len(js, keys);
  char **env = calloc((size_t)len + 1, sizeof(char *));
  if (!env) return NULL;

  size_t out = 0;
  for (ant_offset_t i = 0; i < len; i++) {
    ant_value_t key = js_arr_get(js, keys, i);
    if (vtype(key) != kTypeString) continue;
    ant_value_t value = js_tostring_val(js, js_getprop_fallback(js, env_obj, js_getstr(js, key, NULL)));
    
    if (is_err(value)) {
      js_take_thrown(js, value);
      continue;
    }
    
    size_t klen = 0, vlen = 0;
    const char *k = js_getstr(js, key, &klen);
    const char *v = js_getstr(js, value, &vlen);
    
    char *entry = malloc(klen + vlen + 2);
    if (!entry) continue;
    
    memcpy(entry, k, klen);
    entry[klen] = '=';
    memcpy(entry + klen + 1, v, vlen);
    entry[klen + vlen + 1] = '\0';
    env[out++] = entry;
  }
  
  return env;
}

static ant_value_t worker_environment_map(ant_t *js) {
  if (!is_object_type(js->mutable_roots.worker_environment_data)) {
    ant_value_t ctor = js_get(js, js->global, "Map");
    ant_value_t map = sv_vm_call(js->vm, js, ctor, js_mkundef(), NULL, 0, NULL, ctor);
    if (is_err(map)) return map;
    js->mutable_roots.worker_environment_data = map;
  }
  return js->mutable_roots.worker_environment_data;
}

static const char *worker_limit_names[WORKER_LIMIT_COUNT] = {
  "maxYoungGenerationSizeMb", "maxOldGenerationSizeMb", "codeRangeSizeMb", "stackSizeMb",
};

static ant_value_t worker_read_limits(ant_t *js, ant_worker_t *w, ant_value_t limits) {
  for (int i = 0; i < WORKER_LIMIT_COUNT; i++) w->limits[i] = w->requested_limits[i] = -1;
  w->requested_limits[WORKER_LIMIT_STACK] = WORKER_DEFAULT_STACK_MB;

  for (int i = 0; i < WORKER_LIMIT_COUNT && is_object_type(limits); i++) {
    ant_value_t v = js_get(js, limits, worker_limit_names[i]);
    if (vtype(v) == kTypeUndefined) continue;
    if (vtype(v) != kTypeNumber) return js_mkerr_typed(js, JS_ERR_TYPE, "The \"options.resourceLimits.%s\" property must be of type number", worker_limit_names[i]);
    if (js_getnum(v) > 0) w->limits[i] = w->requested_limits[i] = js_getnum(v);
  }

  if (w->limits[WORKER_LIMIT_OLD] <= 0) w->limits[WORKER_LIMIT_OLD] = WORKER_DEFAULT_OLD_MB;
  if (w->limits[WORKER_LIMIT_STACK] <= 0) w->limits[WORKER_LIMIT_STACK] = WORKER_DEFAULT_STACK_MB;

  double old_min = (double)(ANT_ARENA_MIN >> 20), old_max = (double)(ANT_ARENA_MAX >> 20);
  if (w->limits[WORKER_LIMIT_OLD] < old_min) w->limits[WORKER_LIMIT_OLD] = old_min;
  if (w->limits[WORKER_LIMIT_OLD] > old_max) w->limits[WORKER_LIMIT_OLD] = old_max;
  return js_mkundef();
}

static ant_value_t worker_limits_object(ant_t *js, const double limits[WORKER_LIMIT_COUNT], double unset) {
  ant_value_t obj = Ant_Object_Create(js);
  for (int i = 0; i < WORKER_LIMIT_COUNT; i++)
    js_set(js, obj, worker_limit_names[i], js_mknum(limits[i] > 0 ? limits[i] : unset));
  return obj;
}

static ant_value_t worker_build_argv(ant_t *js, ant_worker_t *w, ant_value_t argv_opt) {
  ant_offset_t extra = vtype(argv_opt) == kTypeArray ? js_arr_len(js, argv_opt) : 0;
  w->argv = calloc((size_t)extra + 3, sizeof(char *));
  if (!w->argv) return js_mkerr(js, "out of memory");

  const char *exe = js->runtime.argc > 0 && js->runtime.argv ? js->runtime.argv[0] : "ant";
  w->argv[w->argc++] = strdup(exe);
  w->argv[w->argc++] = strdup(w->is_eval ? "[worker eval]" : w->entry);
  
  for (ant_offset_t i = 0; i < extra; i++) {
    ant_value_t s = js_tostring_val(js, js_arr_get(js, argv_opt, i));
    if (is_err(s)) return s;
    size_t len = 0;
    const char *str = js_getstr(js, s, &len);
    w->argv[w->argc++] = strndup(str, len);
  }
  
  return js_mkundef();
}

static ant_value_t js_worker_elu(ant_params_t) {
  ant_worker_t *w = worker_data(js_get_slot(js_getcurrentfunc(js), SLOT_DATA));
  if (!w || w->exited) return ant_performance_elu_zero(js);

  ant_value_t pair;
  if (ant_performance_elu_pair(js, args, nargs, &pair)) return pair;

  ant_mutex_lock(&w->lock);
  uint64_t start = atomic_load_explicit(&w->loop_start_ns, memory_order_acquire);
  uint64_t idle_ns = w->child && start ? uv_metrics_idle_time(w->child->loop) : 0;
  ant_mutex_unlock(&w->lock);
  if (!start) return ant_performance_elu_zero(js);

  double idle = (double)idle_ns / 1e6;
  double active = (double)(uv_hrtime() - start) / 1e6 - idle;
  return ant_performance_elu_from(js, idle, active, args, nargs);
}

static ant_value_t js_worker_ctor(ant_params_t) {
  if (vtype(call_new_target) == kTypeUndefined)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Class constructor Worker cannot be invoked without 'new'");
  if (nargs < 1) return js_mkerr_typed(js, JS_ERR_TYPE, "The \"filename\" argument must be of type string or an instance of URL");

  ant_value_t this_obj = js_getthis(js);
  ant_value_t proto = js_instance_proto_from_new_target(js, js->builtins.worker_proto, call_new_target);
  if (is_object_type(proto)) js_set_proto_init(this_obj, proto);

  ant_value_t options = nargs > 1 && is_object_type(args[1]) ? args[1] : js_mkundef();
  bool is_eval = is_object_type(options) && js_truthy(js, js_get(js, options, "eval"));

  ant_worker_t *w = calloc(1, sizeof(*w));
  if (!w) return js_mkerr(js, "out of memory");
  w->lock = (ant_mutex_t)ANT_MUTEX_INIT;
  atomic_init(&w->refs, 1);
  w->obj = this_obj;
  w->exit_promise = js_mkundef();
  w->stdin_obj = js_mkundef();
  w->child_stdin = js_mkundef();
  w->stdout_obj = js_mkundef();
  w->stderr_obj = js_mkundef();
  w->stdin_enabled = is_object_type(options) && js_truthy(js, js_get(js, options, "stdin"));
  w->pipe_stdout = !(is_object_type(options) && js_truthy(js, js_get(js, options, "stdout")));
  w->pipe_stderr = !(is_object_type(options) && js_truthy(js, js_get(js, options, "stderr")));
  w->is_eval = is_eval;
  w->parent = js;
  w->bundle = js_esm_bundle(js);

  ant_value_t error = js_mkundef();
  w->entry = worker_resolve_entry(js, args[0], is_eval, &error);
  if (!w->entry) {
    worker_release(w);
    return is_err(error) ? error : js_mkerr(js, "out of memory");
  }

  ant_value_t r = worker_read_limits(js, w, is_object_type(options) ? js_get(js, options, "resourceLimits") : js_mkundef());
  if (!is_err(r)) r = worker_build_argv(js, w, is_object_type(options) ? js_get(js, options, "argv") : js_mkundef());
  if (is_err(r)) {
    worker_release(w);
    return r;
  }

  ant_value_t env_opt = is_object_type(options) ? js_get(js, options, "env") : js_mkundef();
  if (vtype(env_opt) == kTypeSymbol && env_opt == js->builtins.worker_share_env) w->share_env = true;
  else if (is_object_type(env_opt)) w->env = worker_env_from_object(js, env_opt);
  else w->env = process_env_snapshot(js);

  ant_value_t name = is_object_type(options) ? js_get(js, options, "name") : js_mkundef();
  if (vtype(name) == kTypeString) {
    size_t len = 0;
    const char *s = js_getstr(js, name, &len);
    w->name = strndup(s, len);
  }

  ant_value_t data = is_object_type(options) ? js_get(js, options, "workerData") : js_mkundef();
  ant_value_t transfer = is_object_type(options) ? js_get(js, options, "transferList") : js_mkundef();
  r = ant_wire_serialize(js, data, transfer, js_mkundef(), &w->worker_data);
  if (!is_err(r)) {
    ant_value_t map = worker_environment_map(js);
    r = is_err(map) ? map : ant_wire_serialize(js, map, js_mkundef(), js_mkundef(), &w->environment_data);
  }
  if (is_err(r)) {
    worker_release(w);
    return r;
  }

  ant_port_end_t *parent_end = NULL;
  if (!ant_port_end_pair(&parent_end, &w->child_end)) {
    worker_release(w);
    return js_mkerr(js, "out of memory");
  }
  w->binding = ant_port_end_bind(js, parent_end, this_obj, worker_on_message);
  if (!w->binding) {
    ant_port_end_release(parent_end);
    worker_release(w);
    return js_mkerr(js, "out of memory");
  }

  if (uv_async_init(js->loop, &w->parent_async, worker_parent_wake) != 0) {
    worker_release(w);
    return js_mkerr(js, "failed to initialize Worker");
  }
  w->parent_async.data = w;
  w->parent_async_open = true;
  w->refed = true;
  atomic_fetch_add_explicit(&w->refs, 1, memory_order_relaxed);
  w->thread_id = atomic_fetch_add_explicit(&next_thread_id, 1, memory_order_relaxed);

  js_set_native(this_obj, w, WORKER_NATIVE_TAG);
  js_set_finalizer(this_obj, worker_finalize);
  js_set(js, this_obj, "threadId", js_mknum((double)w->thread_id));
  js_set(js, this_obj, "resourceLimits", worker_limits_object(js, w->requested_limits, -1));
  w->stdout_obj = worker_make_readable(js);
  w->stderr_obj = worker_make_readable(js);
  if (w->stdin_enabled) w->stdin_obj = worker_make_stdin(js, this_obj);
  js_set(js, this_obj, "stdin", w->stdin_enabled ? w->stdin_obj : js_mknull());
  ant_value_t perf = Ant_Object_Create(js);
  js_set(js, perf, "eventLoopUtilization", js_heavy_mkfun(js, js_worker_elu, this_obj));
  js_set(js, this_obj, "performance", perf);
  js_set(js, this_obj, "stdout", w->stdout_obj);
  js_set(js, this_obj, "stderr", w->stderr_obj);
  Ant_List_Insert(&js->workers, w);

  uv_thread_options_t opts = { .flags = UV_THREAD_HAS_STACK_SIZE, .stack_size = (size_t)(w->limits[WORKER_LIMIT_STACK] * 1024 * 1024) };
  atomic_fetch_add_explicit(&w->refs, 1, memory_order_relaxed);
  if (uv_thread_create_ex(&w->thread, &opts, worker_thread_main, w) != 0) {
    atomic_fetch_sub_explicit(&w->refs, 1, memory_order_relaxed);
    worker_post(w, WORKER_EVENT_EXIT, 1, NULL);
    return this_obj;
  }
  w->thread_started = true;
  return this_obj;
}

static ant_worker_t *worker_this(ant_t *js) {
  return worker_data(js_getthis(js));
}

static ant_value_t js_worker_post_message(ant_params_t) {
  ant_worker_t *w = worker_this(js);
  if (!w) return js_mkerr_typed(js, JS_ERR_TYPE, "Illegal invocation");
  if (w->exited || !w->binding) return js_mkundef();

  ant_value_t transfer = js_mkundef();
  if (nargs > 1) {
    ant_value_t opt = args[1];
    if (is_object_type(opt) && vtype(opt) != kTypeArray) {
      transfer = js_get(js, opt, "transfer");
      if (vtype(transfer) == kTypeUndefined) transfer = js_get(js, opt, "transferList");
    } else transfer = opt;
  }
  return ant_port_binding_post(js, w->binding, nargs > 0 ? args[0] : js_mkundef(), transfer);
}

static ant_value_t js_worker_terminate(ant_params_t) {
  ant_worker_t *w = worker_this(js);
  if (!w) return js_mkerr_typed(js, JS_ERR_TYPE, "Illegal invocation");

  ant_value_t promise = js_mkpromise(js);
  if (w->exited) {
    js_resolve_promise(js, promise, js_mkundef());
    return promise;
  }
  if (vtype(w->exit_promise) == kTypePromise) return w->exit_promise;
  w->exit_promise = promise;
  worker_request_stop(w, 1);
  return promise;
}

static ant_value_t js_worker_ref(ant_params_t) {
  ant_worker_t *w = worker_this(js);
  if (w && w->parent_async_open) {
    w->refed = true;
    uv_ref((uv_handle_t *)&w->parent_async);
  }
  return js_mkundef();
}

static ant_value_t js_worker_unref(ant_params_t) {
  ant_worker_t *w = worker_this(js);
  if (w && w->parent_async_open) {
    w->refed = false;
    uv_unref((uv_handle_t *)&w->parent_async);
  }
  return js_mkundef();
}

static ant_value_t js_worker_has_ref(ant_params_t) {
  ant_worker_t *w = worker_this(js);
  return js_bool(w && !w->exited && w->refed);
}

static ant_value_t js_worker_get_heap_snapshot(ant_params_t) {
  ant_value_t promise = js_mkpromise(js);
  js_reject_promise(js, promise, js_mkerr(js, "Worker.getHeapSnapshot is not supported"));
  return promise;
}

static ant_value_t js_mark_as_untransferable(ant_params_t) {
  if (nargs < 1) return js_mkundef();
  return ant_wire_mark_untransferable(js, args[0]);
}

static ant_value_t js_mark_as_uncloneable(ant_params_t) {
  if (nargs < 1) return js_mkundef();
  return ant_wire_mark_uncloneable(js, args[0]);
}

static ant_value_t js_is_marked_as_untransferable(ant_params_t) {
  return js_bool(nargs > 0 && ant_wire_is_untransferable(js, args[0]));
}

static ant_value_t js_receive_message_on_port(ant_params_t) {
  if (nargs < 1 || !ant_message_port_is(args[0]))
    return js_mkerr_typed(js, JS_ERR_TYPE, "The \"port\" argument must be a MessagePort instance");
  return ant_message_port_receive(js, args[0]);
}

static ant_value_t js_move_message_port_to_context(ant_params_t) {
  if (nargs < 1 || !ant_message_port_is(args[0]))
    return js_mkerr_typed(js, JS_ERR_TYPE, "The \"port\" argument must be a MessagePort instance");
  return args[0];
}

static ant_value_t js_set_environment_data(ant_params_t) {
  if (nargs < 1) return js_mkundef();
  ant_value_t map = worker_environment_map(js);
  if (is_err(map)) return map;
  bool remove = nargs < 2 || vtype(args[1]) == kTypeUndefined;
  ant_value_t method = js_get(js, map, remove ? "delete" : "set");
  return sv_vm_call(js->vm, js, method, map, args, remove ? 1 : 2, NULL, js_mkundef());
}

static ant_value_t js_get_environment_data(ant_params_t) {
  if (nargs < 1) return js_mkundef();
  ant_value_t map = worker_environment_map(js);
  if (is_err(map)) return map;
  ant_value_t method = js_get(js, map, "get");
  return sv_vm_call(js->vm, js, method, map, args, 1, NULL, js_mkundef());
}

typedef enum {
  THREAD_MSG_REQUEST,
  THREAD_MSG_REPLY,
} thread_msg_kind_t;

typedef enum {
  THREAD_MSG_DELIVERED,
  THREAD_MSG_FAILED,
  THREAD_MSG_ERRORED,
} thread_msg_status_t;

typedef struct thread_msg {
  struct thread_msg *next;
  thread_msg_kind_t kind;
  thread_msg_status_t status;
  uint32_t request_id;
  uint64_t source;
  ant_thread_mailbox_t *reply_to;
  ant_wire_t *wire;
} thread_msg_t;

typedef struct thread_request {
  struct thread_request *next;
  struct thread_request **pprev;
  ant_thread_mailbox_t *mailbox;
  uint32_t id;
  ant_value_t promise;
  uv_timer_t timer;
  bool has_timer;
} thread_request_t;

struct ant_thread_mailbox {
  _Atomic int refs;
  ant_mutex_t lock;
  uint64_t thread_id;
  ant_t *js;
  bool closed;
  thread_msg_t *head;
  thread_msg_t *tail;
  uv_async_t async;
  thread_request_t *requests;
  uint32_t next_request_id;
  uint32_t request_count;
  UT_hash_handle hh;
};

static ant_mutex_t mailbox_registry_lock = ANT_MUTEX_INIT;
static ant_thread_mailbox_t *mailbox_registry = NULL;

uint64_t ant_worker_thread_id(ant_t *js) {
  return js->worker ? js->worker->thread_id : 0;
}

static ant_thread_mailbox_t *mailbox_retain(ant_thread_mailbox_t *mb) {
  atomic_fetch_add_explicit(&mb->refs, 1, memory_order_relaxed);
  return mb;
}

static void thread_msg_free(thread_msg_t *msg);

static void mailbox_release(ant_thread_mailbox_t *mb) {
  if (atomic_fetch_sub_explicit(&mb->refs, 1, memory_order_acq_rel) != 1) return;
  while (mb->head) {
    thread_msg_t *msg = mb->head;
    mb->head = msg->next;
    thread_msg_free(msg);
  }
  free(mb);
}

static void thread_msg_free(thread_msg_t *msg) {
  if (msg->reply_to) mailbox_release(msg->reply_to);
  ant_wire_release(msg->wire);
  free(msg);
}

static bool mailbox_push(ant_thread_mailbox_t *mb, thread_msg_t *msg) {
  ant_mutex_lock(&mb->lock);
  bool open = !mb->closed;
  if (open) {
    msg->next = NULL;
    if (mb->tail) mb->tail->next = msg;
    else mb->head = msg;
    mb->tail = msg;
    uv_async_send(&mb->async);
  }
  ant_mutex_unlock(&mb->lock);
  return open;
}

static void mailbox_reply(ant_thread_mailbox_t *to, uint32_t id, thread_msg_status_t status) {
  thread_msg_t *msg = calloc(1, sizeof(*msg));
  if (!msg) return;
  msg->kind = THREAD_MSG_REPLY;
  msg->request_id = id;
  msg->status = status;
  if (!mailbox_push(to, msg)) free(msg);
}

static ant_value_t mailbox_error(ant_t *js, thread_msg_status_t status) {
  ant_value_t err = status == THREAD_MSG_ERRORED
    ? js_mkerr_code(js, JS_ERR_GENERIC, "ERR_WORKER_MESSAGING_ERRORED", "The destination thread threw an error while processing the message")
    : js_mkerr_code(js, JS_ERR_GENERIC, "ERR_WORKER_MESSAGING_FAILED", "Cannot find the destination thread or listener");
  return js_take_thrown(js, err);
}

static ant_value_t mailbox_rejected(ant_t *js, ant_value_t err) {
  ant_value_t promise = js_mkpromise(js);
  if (is_err(err)) err = js_take_thrown(js, err);
  js_reject_promise(js, promise, err);
  return promise;
}

static void thread_request_timer_closed(uv_handle_t *handle) {
  free(handle->data);
}

static void thread_request_free(thread_request_t *req) {
  ant_thread_mailbox_t *mb = req->mailbox;
  Ant_List_Remove(req);
  req->promise = js_mkundef();
  if (--mb->request_count == 0) uv_unref((uv_handle_t *)&mb->async);
  if (!req->has_timer) {
    free(req);
    return;
  }
  uv_timer_stop(&req->timer);
  uv_close((uv_handle_t *)&req->timer, thread_request_timer_closed);
}

static void thread_request_settle(ant_t *js, thread_request_t *req, ant_value_t err) {
  GC_ROOT_SAVE(mark, js);
  ant_value_t promise = req->promise;
  GC_ROOT_PIN(js, promise);
  thread_request_free(req);
  if (vtype(err) == kTypeUndefined) js_resolve_promise(js, promise, err);
  else {
    GC_ROOT_PIN(js, err);
    js_reject_promise(js, promise, err);
  }
  GC_ROOT_RESTORE(js, mark);
}

static void thread_request_timeout(uv_timer_t *timer) {
  thread_request_t *req = timer->data;
  ant_t *js = req->mailbox->js;
  thread_request_settle(js, req, js_take_thrown(js, js_mkerr_code(
    js, JS_ERR_GENERIC, "ERR_WORKER_MESSAGING_TIMEOUT", "Sending a message to another thread timed out"
  )));
  process_microtasks(js);
}

static thread_msg_status_t mailbox_deliver(ant_t *js, thread_msg_t *msg) {
  if (!process_has_event_listeners(js, "workerMessage")) return THREAD_MSG_FAILED;
  ant_value_t value = ant_wire_deserialize(js, msg->wire, NULL);
  if (is_err(value)) {
    js_take_thrown(js, value);
    return THREAD_MSG_ERRORED;
  }

  GC_ROOT_SAVE(mark, js);
  ant_value_t argv[2] = { value, js_mknum((double)msg->source) };
  GC_ROOT_PIN(js, argv[0]);
  emit_process_event(js, "workerMessage", argv, 2);
  GC_ROOT_RESTORE(js, mark);
  if (!Ant_Exception_Pending(js)) return THREAD_MSG_DELIVERED;
  if (js_is_termination(Ant_Exception_Peek(js))) return THREAD_MSG_FAILED;
  js_take_thrown(js, js_mkundef());
  return THREAD_MSG_ERRORED;
}

static void mailbox_wake(uv_async_t *handle) {
  ant_thread_mailbox_t *mb = handle->data;
  ant_t *js = mb->js;

  while (!js->destroying && !Ant_Exception_Pending(js)) {
    ant_mutex_lock(&mb->lock);
    thread_msg_t *msg = mb->head;
    if (msg) {
      mb->head = msg->next;
      if (!mb->head) mb->tail = NULL;
    }
    ant_mutex_unlock(&mb->lock);
    if (!msg) break;

    if (msg->kind == THREAD_MSG_REQUEST)
      mailbox_reply(msg->reply_to, msg->request_id, mailbox_deliver(js, msg));
    else {
      thread_request_t *req = mb->requests;
      while (req && req->id != msg->request_id) req = req->next;
      if (req) thread_request_settle(js, req, msg->status == THREAD_MSG_DELIVERED
        ? js_mkundef() : mailbox_error(js, msg->status));
    }
    thread_msg_free(msg);
    process_microtasks(js);
  }
}

static ant_thread_mailbox_t *worker_mailbox(ant_t *js) {
  if (js->thread_mailbox) return js->thread_mailbox;
  ant_thread_mailbox_t *mb = calloc(1, sizeof(*mb));
  if (!mb) return NULL;
  if (uv_async_init(js->loop, &mb->async, mailbox_wake) != 0) {
    free(mb);
    return NULL;
  }
  mb->async.data = mb;
  uv_unref((uv_handle_t *)&mb->async);
  atomic_init(&mb->refs, 1);
  mb->lock = (ant_mutex_t)ANT_MUTEX_INIT;
  mb->thread_id = ant_worker_thread_id(js);
  mb->js = js;
  js->thread_mailbox = mb;

  ant_mutex_lock(&mailbox_registry_lock);
  HASH_ADD(hh, mailbox_registry, thread_id, sizeof(mb->thread_id), mb);
  ant_mutex_unlock(&mailbox_registry_lock);
  return mb;
}

void ant_worker_messaging_listen(ant_t *js) {
  if (!js->destroying) worker_mailbox(js);
}

static void mailbox_async_closed(uv_handle_t *handle) {
  mailbox_release(handle->data);
}

static void worker_mailbox_close(ant_t *js) {
  ant_thread_mailbox_t *mb = js->thread_mailbox;
  if (!mb) return;
  js->thread_mailbox = NULL;

  ant_mutex_lock(&mailbox_registry_lock);
  HASH_DELETE(hh, mailbox_registry, mb);
  ant_mutex_unlock(&mailbox_registry_lock);

  ant_mutex_lock(&mb->lock);
  mb->closed = true;
  thread_msg_t *msg = mb->head;
  mb->head = mb->tail = NULL;
  ant_mutex_unlock(&mb->lock);

  while (msg) {
    thread_msg_t *next = msg->next;
    if (msg->kind == THREAD_MSG_REQUEST) mailbox_reply(msg->reply_to, msg->request_id, THREAD_MSG_FAILED);
    thread_msg_free(msg);
    msg = next;
  }

  while (mb->requests) thread_request_free(mb->requests);
  uv_close((uv_handle_t *)&mb->async, mailbox_async_closed);
}

static ant_thread_mailbox_t *mailbox_find(uint64_t thread_id) {
  ant_thread_mailbox_t *mb = NULL;
  ant_mutex_lock(&mailbox_registry_lock);
  HASH_FIND(hh, mailbox_registry, &thread_id, sizeof(thread_id), mb);
  if (mb) mailbox_retain(mb);
  ant_mutex_unlock(&mailbox_registry_lock);
  return mb;
}

static ant_value_t js_post_message_to_thread(ant_params_t) {
  ant_value_t id = nargs > 0 ? args[0] : js_mkundef();
  ant_value_t value = nargs > 1 ? args[1] : js_mkundef();
  ant_value_t transfer = nargs > 2 ? args[2] : js_mkundef();
  ant_value_t timeout = nargs > 3 ? args[3] : js_mkundef();

  if (vtype(transfer) == kTypeNumber && vtype(timeout) == kTypeUndefined) {
    timeout = transfer;
    transfer = js_mkundef();
  }
  
  if (vtype(timeout) != kTypeUndefined) {
    if (vtype(timeout) != kTypeNumber)
      return mailbox_rejected(js, Ant_Error_CreateInvalidArgType(js, "timeout", "of type number", timeout));
    if (!(js_getnum(timeout) >= 0))
      return mailbox_rejected(js, Ant_Error_CreateOutOfRange(js, "timeout", ">= 0", timeout));
  }

  uint64_t self = ant_worker_thread_id(js);
  double num = vtype(id) == kTypeNumber ? js_getnum(id) : -1;
  
  if (num == (double)self) return mailbox_rejected(js, js_mkerr_code(
    js, JS_ERR_GENERIC, 
    "ERR_WORKER_MESSAGING_SAME_THREAD", 
    "Cannot sent a message to the same thread"
  ));

  ant_thread_mailbox_t *own = worker_mailbox(js);
  if (!own) return mailbox_rejected(js, js_mkerr(js, "out of memory"));

  ant_wire_t *wire = NULL;
  ant_value_t ser = ant_wire_serialize(js, value, transfer, js_mkundef(), &wire);
  if (is_err(ser)) return mailbox_rejected(js, ser);

  bool valid = num >= 0 && num <= 9007199254740991.0 && num == (double)(uint64_t)num;
  ant_thread_mailbox_t *dest = valid ? mailbox_find((uint64_t)num) : NULL;
  
  if (!dest) {
    ant_wire_release(wire);
    return mailbox_rejected(js, mailbox_error(js, THREAD_MSG_FAILED));
  }

  thread_request_t *req = calloc(1, sizeof(*req));
  thread_msg_t *msg = calloc(1, sizeof(*msg));
  
  if (!req || !msg) {
    free(req);
    free(msg);
    ant_wire_release(wire);
    mailbox_release(dest);
    return mailbox_rejected(js, js_mkerr(js, "out of memory"));
  }

  ant_value_t promise = js_mkpromise(js);
  req->mailbox = own;
  req->id = ++own->next_request_id;
  req->promise = promise;
  
  Ant_List_Insert(&own->requests, req);
  if (own->request_count++ == 0) uv_ref((uv_handle_t *)&own->async);

  if (vtype(timeout) == kTypeNumber && uv_timer_init(js->loop, &req->timer) == 0) {
    req->has_timer = true;
    req->timer.data = req;
    uv_timer_start(&req->timer, thread_request_timeout, (uint64_t)js_getnum(timeout), 0);
  }

  msg->kind = THREAD_MSG_REQUEST;
  msg->request_id = req->id;
  msg->source = self;
  msg->reply_to = mailbox_retain(own);
  msg->wire = wire;
  bool sent = mailbox_push(dest, msg);
  
  mailbox_release(dest);
  if (!sent) {
    thread_msg_free(msg);
    thread_request_settle(js, req, mailbox_error(js, THREAD_MSG_FAILED));
  }
  
  return promise;
}

static void worker_setup_child_state(ant_t *js, ant_value_t lib) {
  ant_worker_t *w = js->worker;

  ant_value_t data = w->worker_data ? ant_wire_deserialize(js, w->worker_data, NULL) : js_mkundef();
  if (is_err(data)) data = js_take_thrown(js, data);
  js_set(js, lib, "workerData", data);

  if (w->environment_data) {
    ant_value_t map = ant_wire_deserialize(js, w->environment_data, NULL);
    if (is_err(map)) js_take_thrown(js, map);
    else js->mutable_roots.worker_environment_data = map;
  }

  ant_port_end_t *end = w->child_end;
  w->child_end = NULL;
  ant_value_t port = end ? ant_message_port_adopt(js, end) : js_mknull();
  if (is_err(port)) port = js_take_thrown(js, port);
  
  js_set(js, lib, "parentPort", port);
  js_set(js, lib, "threadId", js_mknum((double)w->thread_id));
  js_set(js, lib, "resourceLimits", worker_limits_object(js, w->limits, 0));
  js_set(js, lib, "threadName", w->name ? js_mkstr(js, w->name, strlen(w->name)) : js_mkstr(js, "", 0));
}

ant_value_t worker_threads_library(ant_t *js) {
  ant_value_t lib = Ant_Object_CreateBare(js);
  bool is_worker = js->worker != NULL;
  if (!js->builtins.worker_share_env) js->builtins.worker_share_env = js_mksym(js, "nodejs.worker_threads.SHARE_ENV");

  if (!is_object_type(js->builtins.worker_proto)) {
    ant_value_t proto = Ant_Object_CreateBare(js);
    js_set_proto_init(proto, js->builtins.eventemitter_proto);
    js_set(js, proto, "postMessage", Ant_Function_CreateWithArity(js_worker_post_message, 1));
    js_set(js, proto, "terminate", Ant_Function_Create(js_worker_terminate));
    js_set(js, proto, "ref", Ant_Function_Create(js_worker_ref));
    js_set(js, proto, "unref", Ant_Function_Create(js_worker_unref));
    js_set(js, proto, "hasRef", Ant_Function_Create(js_worker_has_ref));
    js_set(js, proto, "getHeapSnapshot", Ant_Function_Create(js_worker_get_heap_snapshot));
    js_set_tostringtag(js, proto, js_mkstr(js, "Worker", 6));
    js->builtins.worker_proto = proto;
    js->builtins.worker_ctor = Ant_Function_CreateConstructor(js, js_worker_ctor, proto, "Worker", 1);
  }

  js_set(js, lib, "Worker", js->builtins.worker_ctor);
  js_set(js, lib, "MessageChannel", js_get(js, js->global, "MessageChannel"));
  js_set(js, lib, "MessagePort", js_get(js, js->global, "MessagePort"));
  js_set(js, lib, "BroadcastChannel", js_get(js, js->global, "BroadcastChannel"));
  js_set(js, lib, "isMainThread", js_bool(!is_worker));
  js_set(js, lib, "SHARE_ENV", js->builtins.worker_share_env);
  js_set(js, lib, "markAsUntransferable", Ant_Function_CreateWithArity(js_mark_as_untransferable, 1));
  js_set(js, lib, "isMarkedAsUntransferable", Ant_Function_CreateWithArity(js_is_marked_as_untransferable, 1));
  js_set(js, lib, "markAsUncloneable", Ant_Function_CreateWithArity(js_mark_as_uncloneable, 1));
  js_set(js, lib, "isInternalThread", js_false);
  js_set(js, lib, "receiveMessageOnPort", Ant_Function_CreateWithArity(js_receive_message_on_port, 1));
  js_set(js, lib, "moveMessagePortToContext", Ant_Function_CreateWithArity(js_move_message_port_to_context, 2));
  js_set(js, lib, "setEnvironmentData", Ant_Function_CreateWithArity(js_set_environment_data, 2));
  js_set(js, lib, "getEnvironmentData", Ant_Function_CreateWithArity(js_get_environment_data, 1));
  js_set(js, lib, "postMessageToThread", Ant_Function_CreateWithArity(js_post_message_to_thread, 4));

  if (is_worker) worker_setup_child_state(js, lib);
  else {
    js_set(js, lib, "parentPort", js_mknull());
    js_set(js, lib, "workerData", js_mknull());
    js_set(js, lib, "threadId", js_mknum(0));
    js_set(js, lib, "threadName", js_mkstr(js, "", 0));
    js_set(js, lib, "resourceLimits", Ant_Object_Create(js));
  }

  js_set(js, lib, "default", lib);
  js_set_slot_wb(js, lib, SLOT_DEFAULT, lib);
  js_set_tostringtag(js, lib, js_mkstr(js, "worker_threads", 14));
  
  return lib;
}

void cleanup_worker_threads_module(ant_t *js) {
  while (js->workers) {
    ant_worker_t *w = js->workers;
    Ant_List_Remove(w);
    worker_request_stop(w, 1);

    ant_mutex_lock(&w->lock);
    bool close_async = w->parent_async_open;
    w->parent_async_open = false;
    ant_mutex_unlock(&w->lock);

    if (w->thread_started) {
      uv_thread_join(&w->thread);
      w->thread_started = false;
    }
    w->exited = true;
    w->exit_promise = js_mkundef();
    if (w->binding) {
      ant_port_binding_free(w->binding);
      w->binding = NULL;
    }
    if (close_async) uv_close((uv_handle_t *)&w->parent_async, worker_parent_async_closed);
  }
  worker_mailbox_close(js);
}

void gc_mark_worker_threads(ant_t *js, gc_mark_fn mark) {
  for (ant_worker_t *w = js->workers; w; w = w->next) {
    if (is_object_type(w->obj)) mark(js, w->obj);
    if (vtype(w->exit_promise) == kTypePromise) mark(js, w->exit_promise);
    if (is_object_type(w->stdout_obj)) mark(js, w->stdout_obj);
    if (is_object_type(w->stderr_obj)) mark(js, w->stderr_obj);
    if (is_object_type(w->stdin_obj)) mark(js, w->stdin_obj);
  }
  
  if (js->worker && is_object_type(js->worker->child_stdin)) mark(js, js->worker->child_stdin);
  if (js->thread_mailbox) for (thread_request_t *req = js->thread_mailbox->requests; req; req = req->next)
    if (vtype(req->promise) == kTypePromise) mark(js, req->promise);
}
