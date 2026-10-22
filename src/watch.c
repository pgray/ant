#include <compat.h> // IWYU pragma: keep

#include "watch.h"
#include "internal.h"
#include "interrupt.h"
#include "messages.h"
#include "esm/loader.h"
#include "modules/atomics.h"

#include <uv.h>
#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <crprintf.h>

static constexpr uint64_t WATCH_DEBOUNCE_MS = 75;
static constexpr uint64_t WATCH_SETTLE_MS = 250;
static constexpr uint64_t WATCH_SHUTDOWN_GRACE_MS = 500;

typedef struct {
  uint64_t size;
  time_t mtime_sec;
  long mtime_nsec;
  bool present;
} watch_stat_t;

typedef struct watch_file {
  struct watch_file *next;
  char *name;
  watch_stat_t stat;
} watch_file_t;

typedef struct {
  char *path;
  watch_stat_t stat;
} watch_pending_t;

typedef struct watch_dir {
  uv_fs_event_t event;
  struct watch_dir *next;
  char *path;
  watch_file_t *files;
} watch_dir_t;

struct ant_watch {
  uv_mutex_t lock;
  uv_cond_t cond;
  uv_thread_t thread;

  uv_loop_t loop;
  uv_async_t wake;
  uv_timer_t debounce;
  uv_timer_t settle;
  uv_timer_t grace;
  uv_signal_t sigint;
  uv_signal_t sigterm;
  watch_dir_t *dirs;

  watch_pending_t *pending;
  size_t pending_len;
  size_t pending_cap;

  ant_t *js;
  uv_async_t *js_wake;
  char *cwd;

  uint32_t generation;
  int shutdown_code;

  bool hot;
  bool clear_screen;
  bool changed;
  bool shutdown;
  bool shutdown_deferred;
  bool closing;
};

static void watch_clear_screen(void) {
  fputs("\033[3J\033[2J\033[H", stdout);
  fflush(stdout);
}

char *ant_watch_resolve_path(const char *path) {
#ifdef _WIN32
  char *resolved = _fullpath(NULL, path, 0);
  if (resolved) return resolved;
  return _strdup(path);
#else
  char abs_path[PATH_MAX];
  const char *resolved = realpath(path, abs_path);
  return strdup(resolved ? resolved : path);
#endif
}

int ant_watch_start(
  uv_loop_t *loop,
  uv_fs_event_t *event,
  const char *path,
  uv_fs_event_cb callback,
  void *data,
  unsigned int flags,
  char **resolved_path_out
) {
  char *watch_path = NULL;
  int rc = 0;

  if (!loop || !event || !path || !callback) return UV_EINVAL;

  watch_path = ant_watch_resolve_path(path);
  if (!watch_path) return UV_ENOMEM;

  rc = uv_fs_event_init(loop, event);
  if (rc != 0) goto cleanup;

  event->data = data;
  rc = uv_fs_event_start(event, callback, watch_path, flags);
  if (rc != 0) goto cleanup;

  if (resolved_path_out) *resolved_path_out = watch_path;
  else free(watch_path);
  return 0;

cleanup:
  free(watch_path);
  return rc;
}

void ant_watch_stop(uv_fs_event_t *event) {
  if (!event) return;
  uv_fs_event_stop(event);
}

static long watch_stat_mtime_nsec(const struct stat *st) {
#ifdef _WIN32
  (void)st;
  return 0;
#elif defined(__APPLE__)
  return st->st_mtimespec.tv_nsec;
#else
  return st->st_mtim.tv_nsec;
#endif
}

static watch_stat_t watch_stat_path(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) return (watch_stat_t){ 0 };

  return (watch_stat_t){
    .size = (uint64_t)st.st_size,
    .mtime_sec = st.st_mtime,
    .mtime_nsec = watch_stat_mtime_nsec(&st),
    .present = true,
  };
}

static bool watch_file_refresh(watch_file_t *file, const char *dir) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", dir, file->name);

  watch_stat_t next = watch_stat_path(path);
  bool changed =
    next.present != file->stat.present ||
    next.size != file->stat.size ||
    next.mtime_sec != file->stat.mtime_sec ||
    next.mtime_nsec != file->stat.mtime_nsec;
  file->stat = next;
  return changed;
}

static bool watch_is_sep(char ch) {
  return ch == '/' || ch == '\\';
}

static char *watch_last_sep(char *path) {
  char *sep = NULL;
  for (char *p = path; *p; p++) if (watch_is_sep(*p)) sep = p;
  return sep;
}

static bool watch_ignored(const char *path) {
  return strstr(path, "/node_modules/") || strstr(path, "\\node_modules\\");
}

static void watch_wake_js(ant_watch_t *w, bool interrupt) {
  if (!w->js_wake) return;
  if (interrupt) {
    atomic_store_explicit(&w->js->interrupt_pending, 1, memory_order_relaxed);
    atomics_wake_isolate(w->js);
  }
  uv_async_send(w->js_wake);
}

static void watch_notify_change(ant_watch_t *w) {
  uv_mutex_lock(&w->lock);
  w->changed = true;
  uv_cond_signal(&w->cond);
  watch_wake_js(w, !w->hot);
  uv_mutex_unlock(&w->lock);
}

static void watch_on_debounce(uv_timer_t *timer) {
  watch_notify_change(timer->data);
}

static void watch_on_fs_event(uv_fs_event_t *handle, const char *filename, int events, int status) {
  watch_dir_t *dir = handle->data;
  ant_watch_t *w = handle->loop->data;

  if (status < 0) {
    crfprintf(stderr, msg.watch_warn_normal, dir->path, uv_strerror(status));
    return;
  }

  if ((events & (UV_CHANGE | UV_RENAME)) == 0) return;

  bool changed = false;
  for (watch_file_t *file = dir->files; file; file = file->next) {
    if (filename && strcmp(file->name, filename) != 0) continue;
    if (watch_file_refresh(file, dir->path)) changed = true;
  }

  if (changed) uv_timer_start(&w->debounce, watch_on_debounce, WATCH_DEBOUNCE_MS, 0);
}

static void watch_on_settle(uv_timer_t *timer) {
  ant_watch_t *w = timer->data;
  bool changed = false;

  for (watch_dir_t *dir = w->dirs; dir; dir = dir->next)
  for (watch_file_t *file = dir->files; file; file = file->next)
    if (watch_file_refresh(file, dir->path)) changed = true;

  if (changed) uv_timer_start(&w->debounce, watch_on_debounce, WATCH_DEBOUNCE_MS, 0);
}

static void watch_add_path(ant_watch_t *w, char *path, const watch_stat_t *seen) {
  char *sep = watch_last_sep(path);
  if (!sep || sep[1] == '\0' || watch_ignored(path)) return;

  char root[2] = { *sep, '\0' };
  *sep = '\0';
  const char *dir_path = sep == path ? root : path;
  const char *name = sep + 1;

  watch_dir_t *dir = w->dirs;
  while (dir && strcmp(dir->path, dir_path) != 0) dir = dir->next;

  if (!dir) {
    dir = calloc(1, sizeof(*dir));
    if (!dir || !(dir->path = strdup(dir_path))) {
      free(dir);
      return;
    }

    int rc = uv_fs_event_init(&w->loop, &dir->event);
    if (rc == 0) rc = uv_fs_event_start(&dir->event, watch_on_fs_event, dir->path, 0);
    if (rc != 0) {
      crfprintf(stderr, msg.watch_file_warn, dir->path, uv_strerror(rc));
      free(dir->path);
      free(dir);
      return;
    }

    dir->event.data = dir;
    dir->next = w->dirs;
    w->dirs = dir;
  }

  for (watch_file_t *file = dir->files; file; file = file->next)
    if (strcmp(file->name, name) == 0) return;

  watch_file_t *file = calloc(1, sizeof(*file));
  if (!file || !(file->name = strdup(name))) {
    free(file);
    return;
  }

  file->stat = *seen;
  file->next = dir->files;
  dir->files = file;
  uv_timer_start(&w->settle, watch_on_settle, WATCH_SETTLE_MS, 0);
}

static void watch_on_dir_closed(uv_handle_t *handle) {
  watch_dir_t *dir = handle->data;
  for (watch_file_t *file = dir->files, *next; file; file = next) {
    next = file->next;
    free(file->name);
    free(file);
  }
  free(dir->path);
  free(dir);
}

static void watch_close_loop_handles(ant_watch_t *w) {
  for (watch_dir_t *dir = w->dirs, *next; dir; dir = next) {
    next = dir->next;
    uv_close((uv_handle_t *)&dir->event, watch_on_dir_closed);
  }
  w->dirs = NULL;

  uv_close((uv_handle_t *)&w->wake, NULL);
  uv_close((uv_handle_t *)&w->debounce, NULL);
  uv_close((uv_handle_t *)&w->settle, NULL);
  uv_close((uv_handle_t *)&w->grace, NULL);
  uv_close((uv_handle_t *)&w->sigint, NULL);
  uv_close((uv_handle_t *)&w->sigterm, NULL);
}

static void watch_on_wake(uv_async_t *handle) {
  ant_watch_t *w = handle->data;

  uv_mutex_lock(&w->lock);
  watch_pending_t *pending = w->pending;
  size_t len = w->pending_len;
  bool closing = w->closing;
  w->pending = NULL;
  w->pending_len = w->pending_cap = 0;
  uv_mutex_unlock(&w->lock);

  for (size_t i = 0; i < len; i++) {
    if (!closing) watch_add_path(w, pending[i].path, &pending[i].stat);
    free(pending[i].path);
  }
  free(pending);

  if (closing) watch_close_loop_handles(w);
}

static void watch_on_grace(uv_timer_t *timer) {
  ant_watch_t *w = timer->data;
  uv_mutex_lock(&w->lock);
  watch_wake_js(w, true);
  uv_mutex_unlock(&w->lock);
}

static void watch_on_signal(uv_signal_t *handle, int signum) {
  ant_watch_t *w = handle->data;

  uv_mutex_lock(&w->lock);
  bool again = w->shutdown;
  w->shutdown = true;
  w->shutdown_code = 128 + signum;
  uv_cond_signal(&w->cond);
  watch_wake_js(w, false);
  uv_mutex_unlock(&w->lock);

  if (again) _exit(128 + signum);
  uv_timer_start(&w->grace, watch_on_grace, WATCH_SHUTDOWN_GRACE_MS, 0);
}

static void watch_thread_main(void *arg) {
  ant_watch_t *w = arg;
  uv_run(&w->loop, UV_RUN_DEFAULT);
}

static int watch_loop_init(ant_watch_t *w) {
  int rc = uv_loop_init(&w->loop);
  if (rc != 0) return rc;
  w->loop.data = w;

  if ((rc = uv_async_init(&w->loop, &w->wake, watch_on_wake)) != 0) return rc;
  if ((rc = uv_timer_init(&w->loop, &w->debounce)) != 0) return rc;
  if ((rc = uv_timer_init(&w->loop, &w->settle)) != 0) return rc;
  if ((rc = uv_timer_init(&w->loop, &w->grace)) != 0) return rc;
  if ((rc = uv_signal_init(&w->loop, &w->sigint)) != 0) return rc;
  if ((rc = uv_signal_init(&w->loop, &w->sigterm)) != 0) return rc;

  w->wake.data = w;
  w->debounce.data = w;
  w->settle.data = w;
  w->grace.data = w;
  w->sigint.data = w;
  w->sigterm.data = w;

  if ((rc = uv_signal_start(&w->sigint, watch_on_signal, SIGINT)) != 0) return rc;
  rc = uv_signal_start(&w->sigterm, watch_on_signal, SIGTERM);
  return rc == UV_ENOSYS || rc == UV_EINVAL ? 0 : rc;
}

static void watch_queue_path(ant_watch_t *w, const char *path) {
  char *copy = strdup(path);
  if (!copy) return;

  watch_stat_t stat = watch_stat_path(path);

  uv_mutex_lock(&w->lock);
  if (w->pending_len == w->pending_cap) {
    size_t cap = w->pending_cap ? w->pending_cap * 2 : 16;
    watch_pending_t *next = realloc(w->pending, cap * sizeof(*next));
    if (!next) {
      uv_mutex_unlock(&w->lock);
      free(copy);
      return;
    }
    w->pending = next;
    w->pending_cap = cap;
  }
  w->pending[w->pending_len++] = (watch_pending_t){ copy, stat };
  uv_mutex_unlock(&w->lock);

  uv_async_send(&w->wake);
}

ant_watch_t *ant_watch_open(const char *entry, bool hot, bool clear_screen) {
  ant_watch_t *w = calloc(1, sizeof(*w));
  if (!w) return NULL;

  w->hot = hot;
  w->clear_screen = clear_screen && isatty(STDOUT_FILENO);
  w->generation = hot ? 1 : 0;

  if (uv_mutex_init(&w->lock) != 0) {
    free(w);
    return NULL;
  }

  if (uv_cond_init(&w->cond) != 0) {
    uv_mutex_destroy(&w->lock);
    free(w);
    return NULL;
  }

  char cwd[PATH_MAX];
  size_t cwd_len = sizeof(cwd);
  if (uv_cwd(cwd, &cwd_len) == 0) w->cwd = strdup(cwd);

  int rc = watch_loop_init(w);
  if (rc == 0) rc = uv_thread_create(&w->thread, watch_thread_main, w);
  if (rc != 0) {
    crfprintf(stderr, msg.watch_loop_fatal, uv_strerror(rc));
    uv_cond_destroy(&w->cond);
    uv_mutex_destroy(&w->lock);
    free(w->cwd);
    free(w);
    return NULL;
  }

  char *path = ant_watch_resolve_path(entry);
  if (path) watch_queue_path(w, path);
  free(path);

  return w;
}

void ant_watch_close(ant_watch_t *w) {
  if (!w) return;

  uv_mutex_lock(&w->lock);
  w->closing = true;
  uv_mutex_unlock(&w->lock);

  uv_async_send(&w->wake);
  uv_thread_join(&w->thread);
  uv_loop_close(&w->loop);

  for (size_t i = 0; i < w->pending_len; i++) free(w->pending[i].path);
  free(w->pending);
  free(w->cwd);

  uv_cond_destroy(&w->cond);
  uv_mutex_destroy(&w->lock);
  free(w);
}

static void watch_on_js_wake(uv_async_t *handle) {
  ant_watch_t *w = handle->data;
  ant_t *js = handle->loop->data;

  uv_mutex_lock(&w->lock);
  bool shutdown = w->shutdown;
  bool stop = shutdown || (!w->hot && w->changed);
  bool reload = w->hot && w->changed;
  uv_mutex_unlock(&w->lock);

  if (shutdown && !w->shutdown_deferred) {
    w->shutdown_deferred = true;
    uv_async_send(handle);
  } else if (stop) {
    js->terminating = true;
    atomic_store_explicit(&js->interrupt_pending, 1, memory_order_relaxed);
    uv_stop(js->loop);
  } else if (reload) {
    js->loop_yield = true;
    uv_stop(js->loop);
  }
}

static void watch_free_handle(uv_handle_t *handle) {
  free(handle);
}

void ant_watch_attach(ant_watch_t *w, ant_t *js) {
  if (w->cwd) uv_chdir(w->cwd);

  uv_async_t *wake = calloc(1, sizeof(*wake));
  if (wake && uv_async_init(js->loop, wake, watch_on_js_wake) != 0) {
    free(wake);
    wake = NULL;
  }

  if (wake) {
    wake->data = w;
    if (!w->hot) uv_unref((uv_handle_t *)wake);
  }

  js->watch = w;
  if (w->hot) js->uncaught_nonfatal = true;
  sv_jit_arm_interrupts(js);

  uv_mutex_lock(&w->lock);
  w->js = js;
  w->js_wake = wake;
  w->changed = false;
  if (w->shutdown) {
    js->terminating = true;
    atomic_store_explicit(&js->interrupt_pending, 1, memory_order_relaxed);
  }
  uv_mutex_unlock(&w->lock);
}

void ant_watch_detach(ant_watch_t *w, ant_t *js) {
  uv_mutex_lock(&w->lock);
  uv_async_t *wake = w->js_wake;
  w->js = NULL;
  w->js_wake = NULL;
  uv_mutex_unlock(&w->lock);

  if (wake) uv_close((uv_handle_t *)wake, watch_free_handle);
  js->watch = NULL;
  js->terminating = false;
  js->loop_yield = false;
  atomic_store_explicit(&js->interrupt_pending, 0, memory_order_relaxed);
}

bool ant_watch_hot(const ant_watch_t *w) {
  return w && w->hot;
}

bool ant_watch_wait(ant_watch_t *w) {
  uv_mutex_lock(&w->lock);
  while (!w->changed && !w->shutdown) uv_cond_wait(&w->cond, &w->lock);
  bool go = !w->shutdown;
  w->changed = false;
  uv_mutex_unlock(&w->lock);

  if (go && w->clear_screen) watch_clear_screen();
  return go;
}

bool ant_watch_begin_reload(ant_watch_t *w, ant_t *js) {
  if (!ant_watch_wait(w)) return false;
  w->generation++;
  js_esm_invalidate_files(js);
  return true;
}

int ant_watch_shutdown_code(ant_watch_t *w) {
  uv_mutex_lock(&w->lock);
  int code = w->shutdown_code;
  uv_mutex_unlock(&w->lock);
  return code;
}

void ant_watch_track(ant_t *js, const char *path) {
  if (js->watch && path && !watch_ignored(path)) watch_queue_path(js->watch, path);
}

uint32_t ant_watch_generation(ant_t *js) {
  return js->watch ? js->watch->generation : 0;
}

bool ant_watch_supervises(ant_t *js) {
  return js->watch && !js->watch->hot;
}

ant_value_t ant_watch_exit(ant_t *js) {
  atomic_store_explicit(&js->interrupt_pending, 1, memory_order_relaxed);
  uv_stop(js->loop);
  return js_interrupt_raise(js);
}
