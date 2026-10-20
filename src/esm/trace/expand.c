#include <compat.h> // IWYU pragma: keep

#include <stdio.h>
#include <string.h>
#include <uv.h>

#include "trace_internal.h"

static constexpr uint32_t TRACE_EXPAND_MAX_MATCHES = 256;
static constexpr int TRACE_EXPAND_MAX_DEPTH = 8;

static const char *const trace_expand_extensions[] = {
  ".js", ".mjs", ".cjs", ".ts", ".mts", ".cts", ".jsx", ".tsx", ".json",
};

typedef struct {
  const char *glob;
  const char *spec_base;
  trace_expand_fn emit;
  void *user;
  bool relative;
  uint32_t matches;
  bool overflow;
} trace_expand_t;

static bool trace_glob_match(const char *glob, const char *text) {
  if (*glob == '\0') return *text == '\0';
  if (*glob == TRACE_HOLE) {
    for (const char *t = text;; t++) {
      if (trace_glob_match(glob + 1, t)) return true;
      if (*t == '\0') return false;
    }
  }
  return *glob == *text && trace_glob_match(glob + 1, text + 1);
}

static bool trace_glob_literal_tail(const char *glob) {
  const char *last = strrchr(glob, TRACE_HOLE);
  return last && last[1] != '\0';
}

static bool trace_expand_emit(trace_ctx_t *ctx, trace_expand_t *x, const char *rel) {
  if (x->matches >= TRACE_EXPAND_MAX_MATCHES) {
    x->overflow = true;
    return true;
  }

  char spec[PATH_MAX];
  if ((size_t)snprintf(spec, sizeof(spec), "%s%s", x->spec_base, rel) >= sizeof(spec)) return true;
  x->matches++;
  if (!x->emit(ctx, spec, x->relative, x->user)) return false;
  if (trace_glob_literal_tail(x->glob)) return true;

  for (size_t i = 0; i < sizeof(trace_expand_extensions) / sizeof(*trace_expand_extensions); i++) {
    if (!trace_has_suffix(spec, trace_expand_extensions[i])) continue;
    spec[strlen(spec) - strlen(trace_expand_extensions[i])] = '\0';
    return x->emit(ctx, spec, x->relative, x->user);
  }

  return true;
}

static bool trace_expand_walk(trace_ctx_t *ctx, trace_expand_t *x, const char *dir, const char *rel, int depth) {
  uv_fs_t req;
  if (uv_fs_scandir(NULL, &req, dir, 0, NULL) < 0) {
    uv_fs_req_cleanup(&req);
    return true;
  }

  bool ok = true;
  uv_dirent_t entry;
  while (ok && !x->overflow && uv_fs_scandir_next(&req, &entry) != UV_EOF) {
    if (entry.name[0] == '.' || strcmp(entry.name, "node_modules") == 0) continue;

    char path[PATH_MAX], child[PATH_MAX];
    if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, entry.name) >= sizeof(path)) continue;
    if ((size_t)snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "/" : "", entry.name) >= sizeof(child)) continue;

    if (entry.type == UV_DIRENT_DIR) {
      if (depth < TRACE_EXPAND_MAX_DEPTH) ok = trace_expand_walk(ctx, x, path, child, depth + 1);
    } else if (entry.type == UV_DIRENT_FILE && trace_glob_match(x->glob, child)) {
      ok = trace_expand_emit(ctx, x, child);
    }
  }

  uv_fs_req_cleanup(&req);
  return ok;
}

int trace_expand_pattern(trace_ctx_t *ctx, const char *pattern, trace_expand_fn emit, void *user) {
  const char *hole = strchr(pattern, TRACE_HOLE);
  if (!hole) return 0;

  const char *slash = NULL;
  for (const char *p = pattern; p < hole; p++) if (*p == '/') slash = p;
  if (!slash) return 0;

  bool absolute = pattern[0] == '/';
  bool relative = strncmp(pattern, "./", 2) == 0 || strncmp(pattern, "../", 3) == 0;
  if (!absolute && !relative) return 0;

  char *base_text = trace_strndup(ctx, pattern, (size_t)(slash - pattern + 1));
  if (!base_text) return -1;

  char *base_dir = absolute ? trace_normalize_path(ctx, base_text) : trace_join_path(ctx, ctx->dir, base_text);
  if (!base_dir) return -1;

  const char *glob = slash + 1;
  bool own_dir = strcmp(base_dir, ctx->dir) == 0;
  if (own_dir && !trace_glob_literal_tail(glob)) return 0;

  trace_expand_t x = {
    .glob = glob,
    .spec_base = base_text,
    .emit = emit,
    .user = user,
    .relative = relative,
  };
  if (!trace_expand_walk(ctx, &x, base_dir, "", 0)) return -1;

  if (x.overflow) trace_warn(ctx,
    "%s: dynamic specifier \"%s*\" matches more than %u files; only the first %u were bundled",
    ctx->file, base_text, TRACE_EXPAND_MAX_MATCHES, TRACE_EXPAND_MAX_MATCHES);

  return (int)x.matches;
}
