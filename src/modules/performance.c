#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include <uv.h>

#include "ant.h"
#include "cfunc.h"
#include "internal.h"
#include "modules/performance.h"

static double get_current_time_ms(void) {
#ifdef _WIN32
  LARGE_INTEGER freq, count;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&count);
  return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
#endif
}

static double perf_now(ant_t *js) {
  return get_current_time_ms() - js->perf_time_origin_ms;
}

// performance.now()
static ant_value_t js_performance_now(ant_params_t) {
  return js_mknum(perf_now(js));
}

void Ant_Performance_MarkLoopStart(ant_t *js) {
  if (js->perf_loop_start_ms < 0) js->perf_loop_start_ms = perf_now(js);
}

typedef struct {
  double idle;
  double active;
} perf_elu_t;

static bool perf_elu_read(ant_t *js, ant_value_t value, perf_elu_t *out) {
  if (!is_object_type(value)) return false;
  ant_value_t idle = js_get(js, value, "idle");
  ant_value_t active = js_get(js, value, "active");
  out->idle = vtype(idle) == kTypeNumber ? js_getnum(idle) : 0;
  out->active = vtype(active) == kTypeNumber ? js_getnum(active) : 0;
  return true;
}

static ant_value_t perf_elu_result(ant_t *js, double idle, double active, double utilization) {
  ant_value_t obj = Ant_Object_Create(js);
  js_set(js, obj, "idle", js_mknum(idle));
  js_set(js, obj, "active", js_mknum(active));
  js_set(js, obj, "utilization", js_mknum(utilization));
  return obj;
}

static ant_value_t perf_elu_delta(ant_t *js, perf_elu_t now, perf_elu_t prev) {
  double idle = now.idle - prev.idle;
  double active = now.active - prev.active;
  return perf_elu_result(js, idle, active, active / (idle + active));
}

// performance.eventLoopUtilization([utilization1[, utilization2]])
static ant_value_t js_performance_elu(ant_params_t) {
  if (js->perf_loop_start_ms < 0) return perf_elu_result(js, 0, 0, 0);

  perf_elu_t first, second;
  bool has_first = nargs > 0 && perf_elu_read(js, args[0], &first);
  if (has_first && nargs > 1 && perf_elu_read(js, args[1], &second)) return perf_elu_delta(js, first, second);

  if (js->perf_elu_start_ms < 0) {
    uv_loop_configure(js->loop, UV_METRICS_IDLE_TIME);
    js->perf_elu_start_ms = perf_now(js);
  }

  double since = js->perf_elu_start_ms > js->perf_loop_start_ms ? js->perf_elu_start_ms : js->perf_loop_start_ms;
  double idle = (double)uv_metrics_idle_time(js->loop) / 1e6;
  perf_elu_t current = { idle, perf_now(js) - since - idle };
  
  if (has_first) return perf_elu_delta(js, current, first);
  return perf_elu_result(js, current.idle, current.active, current.active / (current.idle + current.active));
}

// performance.toJSON()
static ant_value_t js_performance_to_json(ant_params_t) {
  ant_value_t obj = Ant_Object_Create(js);
  js_set(js, obj, "timeOrigin", js_mknum(js->perf_time_origin_ms));
  return obj;
}

ant_value_t perf_hooks_library(ant_t *js) {
  ant_value_t lib = Ant_Object_CreateBare(js);
  js_set(js, lib, "performance", js_get(js, js_glob(js), "performance"));
  js_set_tostringtag(js, lib, ANT_STRING("perf_hooks"));
  return lib;
}

void init_performance_module(ant_t *js) {
  ant_value_t glob = js_glob(js);
  ant_value_t perf_obj = Ant_Object_Create(js);

  js->perf_time_origin_ms = get_current_time_ms();
  js->perf_loop_start_ms = -1;
  js->perf_elu_start_ms = -1;
  
  js_set(js, perf_obj, "now", Ant_Function_Create(js_performance_now));
  js_set(js, perf_obj, "timeOrigin", js_mknum(js->perf_time_origin_ms));
  js_set(js, perf_obj, "toJSON", Ant_Function_Create(js_performance_to_json));
  js_set(js, perf_obj, "eventLoopUtilization", Ant_Function_CreateWithArity(js_performance_elu, 2));
  
  js_set_tostringtag(js, perf_obj, ANT_STRING("Performance"));
  js_set(js, glob, "performance", perf_obj);
}
