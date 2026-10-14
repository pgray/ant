#ifndef EVENTS_H
#define EVENTS_H

#include <stdbool.h>
#include "types.h"

#define EVENTS_DEFAULT_MAX_LISTENERS 10

typedef void (*eventemitter_listener_change_fn)(
  ant_t *js,
  ant_value_t target,
  ant_value_t key,
  ant_offset_t listener_count,
  void *context
);

ant_value_t events_library(ant_t *js);
ant_value_t eventemitter_prototype(ant_t *js);
ant_offset_t eventemitter_listener_count(ant_t *js, ant_value_t target, const char *event_type);

void init_events_module(ant_t *js);
void cleanup_events_module(ant_t *js);
void js_dispatch_global_event(ant_t *js, ant_value_t event_obj);

bool eventemitter_add_listener(
  ant_t *js,
  ant_value_t target, const char *event_type,
  ant_value_t listener, bool once
);

bool eventemitter_add_listener_val(
  ant_t *js,
  ant_value_t target, ant_value_t key,
  ant_value_t listener, bool once
);

bool eventemitter_set_listener_change_hook(
  ant_t *js,
  ant_value_t target,
  eventemitter_listener_change_fn hook,
  void *context
);

bool eventemitter_emit_args(
  ant_t *js,
  ant_value_t target, const char *event_type,
  ant_value_t *args, int nargs
);

bool eventemitter_remove_listener(
  ant_t *js,
  ant_value_t target, const char *event_type,
  ant_value_t listener
);

bool eventemitter_remove_listener_val(
  ant_t *js,
  ant_value_t target, ant_value_t key,
  ant_value_t listener
);


typedef enum: uint8_t {
  ANT_EVENT_BASE,
  ANT_EVENT_CUSTOM,
  ANT_EVENT_ERROR,
  ANT_EVENT_PROMISE_REJECTION,
  ANT_EVENT_MESSAGE,
  ANT_EVENT_CLOSE,
} ant_event_kind_t;

enum {
#define ANT_EVENT_FIELD(k, name, index) ANT_EVENT_FIELD_##k##_##name = index,
#include "events/fields.h"
};

ant_value_t Ant_Event_Create(
  ant_t *js, ant_value_t proto, ant_event_kind_t kind,
  ant_value_t type, bool bubbles, bool cancelable
);

ant_value_t Ant_EventTarget_GetHandler(ant_t *js, ant_value_t target, ant_value_t type);

void Ant_Event_BeginDispatch(ant_t *js, ant_value_t event, ant_value_t target);
void Ant_Event_EndDispatch(ant_t *js, ant_value_t event);
void Ant_Event_SetField(ant_t *js, ant_value_t event, unsigned int index, ant_value_t value);
void Ant_Event_DefineFields(ant_t *js, ant_value_t proto, ant_event_kind_t kind);
void Ant_EventTarget_Dispatch(ant_t *js, ant_value_t target, ant_value_t event);
void Ant_EventTarget_SetHandler(ant_t *js, ant_value_t target, ant_value_t type, ant_value_t handler);

bool Ant_EventTarget_AddListener(ant_t *js, ant_value_t target, ant_value_t type, ant_value_t listener);
bool Ant_EventTarget_RemoveListener(ant_t *js, ant_value_t target, ant_value_t type, ant_value_t listener);
bool Ant_EventTarget_HasListeners(ant_t *js, ant_value_t target, ant_value_t type);

#endif
