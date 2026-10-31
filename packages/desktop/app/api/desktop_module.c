#include "../platform/platform.h"
#include "cfunc.h"
#include "desktop_core.h"
#include "renderer_bridge.h"

#include <stdlib.h>
#include <string.h>

#ifndef ANT_DESKTOP_VERSION
#define ANT_DESKTOP_VERSION "unknown"
#endif
#ifndef ANT_DESKTOP_CHROMIUM_VERSION
#define ANT_DESKTOP_CHROMIUM_VERSION "unknown"
#endif

static void SetVersion(ant_t *js, ant_value_t versions, const char *name, const char *value) {
  js_set(js, versions, name, js_mkstr(js, value, strlen(value)));
}

static void DesktopStateFinalize(ant_t *js, ant_object_t *object) {
  ant_value_t value = js_obj_from_ptr(object);
  ant_desktop_state_t *state = ant_desktop_state_from(value);
  js_clear_native(value, ANT_DESKTOP_STATE_TAG);
  free(state);
}

ant_value_t DesktopLibrary(ant_t *js) {
  ant_desktop_state_t *state = calloc(1, sizeof(*state));
  if (!state) return js_mkerr(js, "failed to allocate desktop state");
  state->js = js;
  state->ready = true;
  state->window_objects = Ant_Object_CreateBare(js);
  state->application_menu = Ant_Value_Undefined;

  ant_value_t app = Ant_Object_CreateBare(js);
  state->app = app;
  ant_desktop_state_attach(app, state);

  js_set(js, app, "ready", Ant_Function_Create(DesktopAppReady));
  js_set(js, app, "quit", Ant_Function_Create(DesktopAppQuit));
  js_set(js, app, "getPath", Ant_Function_Create(DesktopAppGetPath));
  js_set(js, app, "setApplicationMenu", Ant_Function_Create(DesktopSetApplicationMenu));
  js_set(js, app, "getApplicationMenu", Ant_Function_Create(DesktopGetApplicationMenu));
  const char *resources_path = ant_desktop_platform_resources_path();
  js_set(js, app, "resourcesPath", js_mkstr(js, resources_path, strlen(resources_path)));

  ant_value_t versions = Ant_Object_CreateBare(js);
  SetVersion(js, versions, "ant", ANT_VERSION);
  SetVersion(js, versions, "desktop", ANT_DESKTOP_VERSION);
  SetVersion(js, versions, "chrome", ANT_DESKTOP_CHROMIUM_VERSION);
  js_set(js, app, "versions", versions);

  ant_value_t process = js_get(js, js_glob(js), "process");
  ant_value_t process_versions = js_get(js, process, "versions");
  if (is_object_type(process_versions)) {
    SetVersion(js, process_versions, "ant-desktop", ANT_DESKTOP_VERSION);
    SetVersion(js, process_versions, "chrome", ANT_DESKTOP_CHROMIUM_VERSION);
  }

  state->ipc_handlers = Ant_Object_CreateBare(js);
  state->ipc_listeners = Ant_Object_CreateBare(js);

  ant_value_t bridge_factory =
    js_eval_bytecode_eval(js, (const char *)ant_desktop_ipc_bridge_source, ant_desktop_ipc_bridge_source_len);
  if (is_err(bridge_factory)) {
    return js_mkerr(js, "desktop IPC bridge compile failed: %s", js_str(js, bridge_factory));
  }
  ant_value_t bindings = Ant_Object_CreateBare(js);
  ant_desktop_state_attach(bindings, state);
  js_set(js, bindings, "windows", state->window_objects);
  js_set(js, bindings, "handlers", state->ipc_handlers);
  js_set(js, bindings, "listeners", state->ipc_listeners);
  js_set(js, bindings, "reply", Ant_Function_Create(DesktopNativeIpcReply));
  ant_value_t bridge = ant_desktop_call_function(js, bridge_factory, Ant_Value_Undefined, &bindings, 1);
  if (is_err(bridge)) { return js_mkerr(js, "desktop IPC bridge initialization failed: %s", js_str(js, bridge)); }
  state->encode = js_get(js, bridge, "encode");
  state->dispatch = js_get(js, bridge, "dispatch");
  if (!is_callable(state->encode) || !is_callable(state->dispatch)) {
    return js_mkerr(js, "desktop IPC bridge did not initialize");
  }

  ant_value_t ipc_main = Ant_Object_CreateBare(js);
  ant_desktop_state_attach(ipc_main, state);
  js_set_slot_wb(js, ipc_main, SLOT_DEFAULT, bridge);
  js_set(js, ipc_main, "handle", Ant_Function_Create(DesktopIpcMainHandle));
  js_set(js, ipc_main, "removeHandler", Ant_Function_Create(DesktopIpcMainRemoveHandler));
  js_set(js, ipc_main, "on", Ant_Function_Create(DesktopIpcMainOn));

  state->browser_window_proto = Ant_Object_CreateBare(js);
  ant_desktop_state_attach(state->browser_window_proto, state);
  js_set(js, state->browser_window_proto, "loadURL", Ant_Function_Create(DesktopBrowserWindowLoadURL));
  js_set(js, state->browser_window_proto, "loadFile", Ant_Function_Create(DesktopBrowserWindowLoadFile));
  js_set(js, state->browser_window_proto, "close", Ant_Function_Create(DesktopBrowserWindowClose));
  js_set(js, state->browser_window_proto, "on", Ant_Function_Create(DesktopBrowserWindowOn));
  js_set(js, state->browser_window_proto, "getBounds", Ant_Function_Create(DesktopBrowserWindowGetBounds));
  js_set(js, state->browser_window_proto, "show", Ant_Function_Create(DesktopBrowserWindowShow));
  js_set(js, state->browser_window_proto, "hide", Ant_Function_Create(DesktopBrowserWindowHide));
  js_set(js, state->browser_window_proto, "minimize", Ant_Function_Create(DesktopBrowserWindowMinimize));
  js_set(js, state->browser_window_proto, "restore", Ant_Function_Create(DesktopBrowserWindowRestore));
  js_set(js, state->browser_window_proto, "maximize", Ant_Function_Create(DesktopBrowserWindowMaximize));
  js_set(js, state->browser_window_proto, "setAlwaysOnTop", Ant_Function_Create(DesktopBrowserWindowSetAlwaysOnTop));
  js_set(js, state->browser_window_proto, "setTitle", Ant_Function_Create(DesktopBrowserWindowSetTitle));
  js_set(js, state->browser_window_proto, "setFullScreen", Ant_Function_Create(DesktopBrowserWindowSetFullScreen));
  state->browser_window_ctor =
    Ant_Function_CreateConstructor(js, DesktopBrowserWindowCtor, state->browser_window_proto, "BrowserWindow", 13);
  ant_desktop_state_attach(state->browser_window_ctor, state);

  ant_value_t menu = Ant_Object_CreateBare(js);
  ant_desktop_state_attach(menu, state);
  js_set(js, menu, "buildFromTemplate", Ant_Function_Create(DesktopMenuBuildFromTemplate));
  js_set(js, menu, "setApplicationMenu", Ant_Function_Create(DesktopSetApplicationMenu));
  js_set(js, menu, "getApplicationMenu", Ant_Function_Create(DesktopGetApplicationMenu));
  ant_value_t menu_item_proto = Ant_Object_CreateBare(js);
  ant_value_t menu_item = Ant_Function_CreateConstructor(js, DesktopMenuItemCtor, menu_item_proto, "MenuItem", 8);
  ant_value_t exports = Ant_Object_CreateBare(js);
  js_set(js, exports, "app", app);
  ant_desktop_state_attach(exports, state);
  js_set_finalizer(exports, DesktopStateFinalize);
  js_set(js, exports, "BrowserWindow", state->browser_window_ctor);
  js_set(js, exports, "ipcMain", ipc_main);
  js_set(js, exports, "Menu", menu);
  js_set(js, exports, "MenuItem", menu_item);
  js_set(js, exports, "versions", versions);
  return exports;
}
