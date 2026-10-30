#include <compat.h> // IWYU pragma: keep
#include <string.h>

#include "ant.h"
#include "cfunc.h"
#include "descriptors.h"
#include "errors.h"
#include "internal.h"
#include "worker_internal.h"
#include "modules/events.h"
#include "modules/timer.h"
#include "modules/worker_threads.h"


static ant_value_t web_worker_dispatch(ant_t *js, ant_worker_t *w, ant_value_t event) {
  if (!is_object_type(w->obj) || !is_object_type(event)) return event;
  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, event);
  Ant_EventTarget_Dispatch(js, w->obj, event);
  GC_ROOT_RESTORE(js, mark);
  process_microtasks(js);
  return event;
}

static ant_value_t web_worker_event(ant_t *js, ant_worker_t *w, ant_value_t proto, ant_event_kind_t kind, const char *type) {
  if (!is_object_type(w->obj)) return js_mkundef();
  return Ant_EventTarget_CreateEvent(js, w->obj, proto, kind, type);
}

static void web_worker_on_message(ant_t *js, ant_value_t target, ant_value_t value, ant_value_t ports) {
  ant_worker_t *w = worker_data(target);
  if (!w) return;

  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, value);
  worker_flush_online(js, w);

  bool failed = vtype(ports) == kTypeNull;
  ant_value_t event = ant_message_event_create(js, failed ? "messageerror" : "message", value, failed ? js_mkundef() : ports);
  if (!is_err(event)) web_worker_dispatch(js, w, event);

  GC_ROOT_RESTORE(js, mark);
}

static void web_worker_online(ant_t *js, ant_worker_t *w) {
  web_worker_dispatch(js, w, web_worker_event(js, w, js->builtins.event_proto, ANT_EVENT_BASE, "open"));
}

static ant_value_t web_worker_error_message(ant_t *js, ant_value_t error) {
  ant_value_t text = js_tostring_val(js, error);
  if (!is_err(text)) return text;
  js_take_thrown(js, text);
  return js_mkstr(js, "Uncaught exception in worker", 28);
}

static void web_worker_error(ant_t *js, ant_worker_t *w, ant_value_t error) {
  ant_value_t event = Ant_Event_Create(
    js, js->builtins.errorevent_proto, ANT_EVENT_ERROR,
    js_mkstr(js, "error", 5), ANT_EVENT_CANCELABLE | ANT_EVENT_TRUSTED
  );
  if (is_err(event)) {
    js_take_thrown(js, event);
    return;
  }

  GC_ROOT_SAVE(mark, js);
  GC_ROOT_PIN(js, event);
  bool inline_source = !w->entry || strncmp(w->entry, "data:", 5) == 0;

  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_ERROR_message, web_worker_error_message(js, error));
  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_ERROR_filename, inline_source ? js_mkstr(js, "", 0) : js_mkstr(js, w->entry, strlen(w->entry)));
  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_ERROR_lineno, Ant_Value_FromNumber(0));
  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_ERROR_colno, Ant_Value_FromNumber(0));
  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_ERROR_error, error);

  web_worker_dispatch(js, w, event);
  if (!Ant_Event_Canceled(event)) print_error_value(js, error, js_mkundef(), "Uncaught (in worker) ");
  GC_ROOT_RESTORE(js, mark);
}

static void web_worker_exit(ant_t *js, ant_worker_t *w, int code) {
  ant_value_t event = web_worker_event(js, w, js->builtins.close_event_proto, ANT_EVENT_CLOSE, "close");
  if (!is_object_type(event)) return;

  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_CLOSE_code, Ant_Value_FromNumber(code));
  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_CLOSE_reason, js_mkstr(js, "", 0));
  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_CLOSE_wasClean, js_bool(code == 0));

  web_worker_dispatch(js, w, event);
}

static ant_value_t web_scope_post_message(ant_params_t) {
  ant_value_t port = worker_parent_port(js);
  if (!is_object_type(port)) return js_mkundef();
  return ant_message_port_post(js, port, nargs > 0 ? args[0] : js_mkundef(), worker_transfer_arg(js, args, nargs));
}

static ant_value_t web_scope_close(ant_params_t) {
  if (js->worker) worker_request_stop(js->worker, 0);
  return js_mkundef();
}

#define WEB_SCOPE_HANDLER(type)                                                 \
  static ant_value_t web_scope_get_on##type(ant_params_t) {                     \
    return Ant_GlobalEvents_GetHandler(js, #type, sizeof(#type) - 1);           \
  }                                                                             \
  static ant_value_t web_scope_set_on##type(ant_params_t) {                     \
    Ant_GlobalEvents_SetHandler(js, #type, sizeof(#type) - 1,                   \
      nargs > 0 ? args[0] : js_mknull());                                       \
    return js_mkundef();                                                        \
  }

WEB_SCOPE_HANDLER(message)
WEB_SCOPE_HANDLER(messageerror)

#define WEB_SCOPE_HANDLER_DEFINE(global, type)                                  \
  js_set_accessor_desc(js, global, "on" #type, sizeof("on" #type) - 1,          \
    Ant_Function_Create(web_scope_get_on##type),                                \
    Ant_Function_CreateWithArity(web_scope_set_on##type, 1),                    \
    JS_DESC_E | JS_DESC_C)

static void web_worker_child_init(ant_t *js, ant_worker_t *w) {
  ant_value_t global = js_glob(js);
  ant_value_t port = worker_parent_port(js);
  if (is_object_type(port)) ant_message_port_mirror_global(js, port);

  js_set(js, global, "postMessage", Ant_Function_CreateWithArity(web_scope_post_message, 1));
  js_set(js, global, "close", Ant_Function_Create(web_scope_close));
  if (w->name) js_set(js, global, "name", js_mkstr(js, w->name, strlen(w->name)));

  WEB_SCOPE_HANDLER_DEFINE(global, message);
  WEB_SCOPE_HANDLER_DEFINE(global, messageerror);
}

static const worker_ops_t web_worker_ops = {
  .on_message = web_worker_on_message,
  .online = web_worker_online,
  .error = web_worker_error,
  .exit = web_worker_exit,
  .child_init = web_worker_child_init,
};

static ant_value_t web_worker_check_this(ant_t *js, ant_value_t self) {
  return worker_data(self) ? 0 : js_mkerr_typed(js, JS_ERR_TYPE, "Illegal invocation");
}

ANT_EVENT_HANDLER_ACCESSORS(web_worker, message, web_worker_check_this)
ANT_EVENT_HANDLER_ACCESSORS(web_worker, messageerror, web_worker_check_this)
ANT_EVENT_HANDLER_ACCESSORS(web_worker, error, web_worker_check_this)

static ant_value_t js_web_worker_ctor(ant_params_t) {
  if (vtype(call_new_target) == kTypeUndefined)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Constructor Worker requires 'new'");
  if (nargs < 1) return js_mkerr_typed(js, JS_ERR_TYPE, "Not enough arguments");

  ant_value_t this_obj = js_getthis(js);
  ant_value_t proto = js_instance_proto_from_new_target(js, js->builtins.web_worker_proto, call_new_target);
  if (is_object_type(proto)) js_set_proto_init(this_obj, proto);
  js_set_slot(this_obj, SLOT_BRAND, Ant_Value_FromNumber(BRAND_EVENTTARGET));

  ant_value_t options = nargs > 1 && is_object_type(args[1]) ? args[1] : js_mkundef();
  ant_worker_t *w = worker_new(js, this_obj, &web_worker_ops);
  if (!w) return Ant_Error_CreateOutOfMemory(js);

  ant_value_t error = js_mkundef();
  w->entry = worker_resolve_entry(js, args[0], WORKER_ENTRY_WEB, &error);
  if (!w->entry) {
    worker_release(w);
    return is_err(error) ? error : Ant_Error_CreateOutOfMemory(js);
  }

  ant_value_t r = worker_read_options(js, w, options);
  if (!is_err(r)) r = worker_start(js, w);
  if (is_err(r)) {
    worker_release(w);
    return r;
  }

  ant_value_t ref = is_object_type(options) ? js_get(js, options, "ref") : js_mkundef();
  if (ref == js_false) worker_set_ref(w, false);

  return this_obj;
}

static ant_worker_t *web_worker_this(ant_t *js) {
  return worker_data(js_getthis(js));
}

static ant_value_t js_web_worker_post_message(ant_params_t) {
  ant_worker_t *w = web_worker_this(js);
  if (!w) return js_mkerr_typed(js, JS_ERR_TYPE, "Illegal invocation");
  if (w->exited || !w->binding) return js_mkundef();
  return ant_port_binding_post(js, w->binding, nargs > 0 ? args[0] : js_mkundef(), worker_transfer_arg(js, args, nargs));
}

static ant_value_t js_web_worker_terminate(ant_params_t) {
  ant_worker_t *w = web_worker_this(js);
  if (!w) return js_mkerr_typed(js, JS_ERR_TYPE, "Illegal invocation");
  if (!w->exited) worker_request_stop(w, 0);
  return js_mkundef();
}

static ant_value_t js_web_worker_ref(ant_params_t) {
  ant_worker_t *w = web_worker_this(js);
  if (w) worker_set_ref(w, true);
  return js_mkundef();
}

static ant_value_t js_web_worker_unref(ant_params_t) {
  ant_worker_t *w = web_worker_this(js);
  if (w) worker_set_ref(w, false);
  return js_mkundef();
}

static ant_value_t js_web_worker_thread_id(ant_params_t) {
  ant_worker_t *w = web_worker_this(js);
  return Ant_Value_FromNumber(w && !w->exited ? (double)w->thread_id : -1);
}

void init_web_worker_module(ant_t *js) {
  ant_value_t proto = Ant_Object_CreateBare(js);
  js_set_proto_init(proto, js->builtins.eventtarget_proto);

  js_set(js, proto, "postMessage", Ant_Function_CreateWithArity(js_web_worker_post_message, 1));
  js_set(js, proto, "terminate", Ant_Function_Create(js_web_worker_terminate));
  js_set(js, proto, "ref", Ant_Function_Create(js_web_worker_ref));
  js_set(js, proto, "unref", Ant_Function_Create(js_web_worker_unref));
  js_set_getter_desc(js, proto, "threadId", 8, Ant_Function_Create(js_web_worker_thread_id), JS_DESC_C);

  ANT_EVENT_HANDLER_DEFINE(proto, web_worker, message);
  ANT_EVENT_HANDLER_DEFINE(proto, web_worker, messageerror);
  ANT_EVENT_HANDLER_DEFINE(proto, web_worker, error);
  js_set_tostringtag(js, proto, js_mkstr(js, "Worker", 6));

  js->builtins.web_worker_proto = proto;
  js->builtins.web_worker_ctor = Ant_Function_CreateConstructor(js, js_web_worker_ctor, proto, "Worker", 1);
  js_set_global_builtin(js, "Worker", js->builtins.web_worker_ctor);
}
