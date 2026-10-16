#include <compat.h> // IWYU pragma: keep

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define ant_getcwd _getcwd
#else
#include <unistd.h>
#define ant_getcwd getcwd
#endif

#include "ant.h"
#include "cfunc.h"
#include "errors.h"
#include "internal.h"
#include "descriptors.h"
#include "silver/call.h"

#include "modules/events.h"
#include "modules/globals.h"
#include "modules/url.h"

#ifdef _WIN32
static bool is_windows_drive_path(const char *path) {
  return path &&
    ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
    path[1] == ':';
}
#endif

static char *location_file_href(void) {
  char *cwd = ant_getcwd(NULL, 0);
  const char *path = cwd && cwd[0] ? cwd : "/";

#ifdef _WIN32
  for (char *ch = cwd; ch && *ch; ch++) if (*ch == '\\') *ch = '/';
  const char *scheme = is_windows_drive_path(path) ? "file:///" : "file://";
#else
  const char *scheme = "file://";
#endif

  size_t path_len = strlen(path);
  const char *slash = path_len > 0 && path[path_len - 1] == '/' ? "" : "/";
  size_t len = strlen(scheme) + path_len + strlen(slash);
  
  char *href = malloc(len + 1);
  if (href) snprintf(href, len + 1, "%s%s%s", scheme, path, slash);
  
  free(cwd);
  return href;
}

static void location_set(ant_t *js, ant_value_t location, const char *name, const char *value, const char *empty_if) {
  if (empty_if && strcmp(value, empty_if) == 0) value = "";
  js_set(js, location, name, js_mkstr(js, value, strlen(value)));
}

static ant_value_t make_global_location(ant_t *js, const char *href) {
  url_state_t url;
  bool http = Ant_URL_ParseLocation(href, &url) == 0;
  if (!http && parse_url_to_state(href, NULL, &url) != 0) return js_mkundef();

  const char *colon = url.port[0] ? ":" : "";
  size_t host_len = strlen(url.hostname) + strlen(colon) + strlen(url.port);
  size_t origin_len = strlen(url.protocol) + 2 + host_len;
  
  char *host = malloc(host_len + 1);
  char *origin = malloc(origin_len + 1);
  ant_value_t location = js_mkundef();

  if (host && origin) {
    snprintf(host, host_len + 1, "%s%s%s", url.hostname, colon, url.port);
    snprintf(origin, origin_len + 1, "%s//%s", url.protocol, host);

    location = Ant_Object_Create(js);
    js->builtins.location_href = js_mkstr(js, url.href, strlen(url.href));
    js_set(js, location, "href", js->builtins.location_href);
    
    location_set(js, location, "origin", http ? origin : "null", NULL);
    location_set(js, location, "protocol", url.protocol, NULL);
    location_set(js, location, "host", host, NULL);
    location_set(js, location, "hostname", url.hostname, NULL);
    location_set(js, location, "port", url.port, NULL);
    location_set(js, location, "pathname", url.pathname, NULL);
    location_set(js, location, "search", url.search, "?");
    location_set(js, location, "hash", url.hash, "#");
  }

  free(host);
  free(origin);
  url_state_clear(&url);
  
  return location;
}

ant_value_t js_report_error(ant_params_t) {
  if (nargs < 1) return js_mkerr_typed(js, JS_ERR_TYPE, "reportError requires 1 argument");
  
  ant_value_t error  = args[0];
  ant_value_t global = js_glob(js);

  const char *msg = "";
  uint8_t et = vtype(error);
  
  if (et == kTypeString) {
    msg = js_str(js, error);
  } else if (et == kTypeObject) {
    const char *m = get_str_prop(js, error, "message", 7, NULL);
    if (m && *m) msg = m;
  }
  
  if (!*msg) {
    ant_value_t str = js_tostring_val(js, error);
    if (vtype(str) == kTypeString) { const char *s = js_str(js, str); if (s) msg = s; }
  }

  const char *filename = "";
  int lineno = 1, colno = 1;
  js_get_call_location(js, &filename, &lineno, &colno);
  if (!filename) filename = "";

  ant_value_t loc = js_get(js, global, "location");
  if (vtype(loc) == kTypeObject) {
    const char *href = get_str_prop(js, loc, "href", 4, NULL);
    if (href && *href) filename = href;
  }

  ant_value_t event = Ant_Object_CreateBare(js);
  js_set(js, event, "type",     js_mkstr(js, "error", 5));
  js_set(js, event, "message",  js_mkstr(js, msg, strlen(msg)));
  js_set(js, event, "filename", js_mkstr(js, filename, strlen(filename)));
  js_set(js, event, "lineno",   js_mknum(lineno));
  js_set(js, event, "colno",    js_mknum(colno));
  js_set(js, event, "error",    error);

  js_dispatch_global_event(js, event);
  ant_value_t handler = js_get(js, global, "onerror");
  
  uint8_t ht = vtype(handler);
  if (ht == kTypeFunction || ht == kTypeBuiltin) {
    ant_value_t msg_str  = js_mkstr(js, msg, strlen(msg));
    ant_value_t file_str = js_mkstr(js, filename, strlen(filename));
    ant_value_t call_args[5] = { msg_str, file_str, js_mknum(lineno), js_mknum(colno), error };
    sv_vm_call(js->vm, js, handler, global, call_args, 5, NULL, js_mkundef());
  }

  return js_mkundef();
}

static ant_value_t globals_rejection_event(
  ant_t *js, ant_value_t type, ant_value_t promise, ant_value_t reason, bool cancelable
) {
  ant_value_t event = Ant_Event_Create(
    js, js->builtins.promiserejectionevent_proto, ANT_EVENT_PROMISE_REJECTION, 
    type, ANT_EVENT_TRUSTED | (cancelable ? ANT_EVENT_CANCELABLE : 0)
  );
  
  if (is_err(event)) return event;
  
  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_PROMISE_REJECTION_promise, promise);
  Ant_Event_SetField(js, event, ANT_EVENT_FIELD_PROMISE_REJECTION_reason, reason);
  
  return event;
}

bool js_fire_unhandled_rejection(ant_t *js, ant_value_t promise_val, ant_value_t reason) {
  ant_value_t global  = js_glob(js);
  ant_value_t handler = js_get(js, global, "onunhandledrejection");
  
  uint8_t ht = vtype(handler);
  if (ht != kTypeFunction && ht != kTypeBuiltin) return false;

  ant_value_t event = globals_rejection_event(js, ANT_STRING("unhandledrejection"), promise_val, reason, true);
  if (is_err(event)) return false;
  
  ant_value_t call_args[1] = { event };
  sv_vm_call(js->vm, js, handler, global, call_args, 1, NULL, js_mkundef());
  
  return true;
}

void js_fire_rejection_handled(ant_t *js, ant_value_t promise_val, ant_value_t reason) {
  ant_value_t global  = js_glob(js);
  ant_value_t handler = js_get(js, global, "onrejectionhandled");
  
  uint8_t ht = vtype(handler);
  if (ht != kTypeFunction && ht != kTypeBuiltin) return;

  ant_value_t event = globals_rejection_event(js, ANT_STRING("rejectionhandled"), promise_val, reason, false);
  if (is_err(event)) return;
  
  ant_value_t call_args[1] = { event };
  sv_vm_call(js->vm, js, handler, global, call_args, 1, NULL, js_mkundef());
}

void init_globals_module(ant_t *js) {
  ant_value_t global = js_glob(js);

  char *file_href = NULL;
  const char *href = js->runtime.location;
  if (!href && (js->runtime.flags & ANT_RUNTIME_WEB_COMPAT)) href = file_href = location_file_href();

  ant_value_t location = href ? make_global_location(js, href) : js_mkundef();
  free(file_href);
  
  if (is_object_type(location)) {
    js_set(js, global, "location", location);
    js_set_descriptor(js, global, "location", 8, JS_DESC_W | JS_DESC_C);
  }

  js_set(js, global, "reportError", Ant_Function_Create(js_report_error));
  js_set_descriptor(js, global, "reportError", 11, JS_DESC_W | JS_DESC_C);
}
