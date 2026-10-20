#include <compat.h> // IWYU pragma: keep

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "esm/remote.h"
#include "vfs_bundle.h"
#include "../loader_internal.h"
#include "trace_internal.h"

static bool trace_file_exists(const char *path) {
  uv_fs_t req;
  int rc = uv_fs_stat(NULL, &req, path, NULL);
  uv_fs_req_cleanup(&req);
  return rc == 0;
}

static bool trace_is_shared_library(const char *path) {
  if (trace_has_suffix(path, ".dylib") || trace_has_suffix(path, ".dll")) return true;
  const char *so = strstr(path, ".so");
  return so && (so[3] == '\0' || so[3] == '.');
}

static bool trace_skip_entry(const char *name) {
  return strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
    strcmp(name, "node_modules") == 0 || strcmp(name, ".git") == 0;
}

bool trace_path_is_materialized(trace_ctx_t *ctx, const char *path) {
  for (uint32_t i = 0; i < ctx->materialize_root_count; i++)
    if (trace_path_under(path, ctx->materialize_roots[i])) return true;
  return false;
}

static bool trace_find_package_root(const char *file, bool fallback_to_dir, char *out, size_t out_len) {
  char dir[PATH_MAX];
  if ((size_t)snprintf(dir, sizeof(dir), "%s", file) >= sizeof(dir)) return false;
  char *slash = strrchr(dir, '/');
  if (!slash) return false;
  *slash = '\0';

  char fallback[PATH_MAX];
  if ((size_t)snprintf(fallback, sizeof(fallback), "%s", dir) >= sizeof(fallback)) return false;

  for (;;) {
    char package_json[PATH_MAX];
    if ((size_t)snprintf(package_json, sizeof(package_json), "%s/package.json", dir) < sizeof(package_json) &&
        trace_file_exists(package_json))
      return (size_t)snprintf(out, out_len, "%s", dir) < out_len;

    char *parent = strrchr(dir, '/');
    if (!parent || parent == dir) break;
    *parent = '\0';
  }

  return fallback_to_dir && (size_t)snprintf(out, out_len, "%s", fallback) < out_len;
}

typedef int (*trace_tree_visit_fn)(trace_ctx_t *ctx, const char *path, uv_dirent_type_t type);

static int trace_walk_tree(trace_ctx_t *ctx, const char *dir, trace_tree_visit_fn visit) {
  uv_fs_t req;
  int rc = uv_fs_scandir(NULL, &req, dir, 0, NULL);
  if (rc < 0) {
    trace_set_error(ctx->result, "cannot scan native package %s: %s", dir, uv_strerror(rc));
    uv_fs_req_cleanup(&req);
    return -1;
  }

  int result = 0;
  uv_dirent_t entry;
  while (result == 0 && uv_fs_scandir_next(&req, &entry) != UV_EOF) {
    if (trace_skip_entry(entry.name)) continue;

    char path[PATH_MAX];
    if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, entry.name) >= sizeof(path)) {
      trace_set_error(ctx->result, "native package path is too long: %s/%s", dir, entry.name);
      result = -1;
      break;
    }

    if (entry.type == UV_DIRENT_DIR) result = trace_walk_tree(ctx, path, visit);
    else result = visit(ctx, path, entry.type);
  }

  uv_fs_req_cleanup(&req);
  return result;
}

static int trace_visit_find_native(trace_ctx_t *ctx, const char *path, uv_dirent_type_t type) {
  return type == UV_DIRENT_FILE && trace_has_suffix(path, ".node") ? 1 : 0;
}

static int trace_add_asset(trace_ctx_t *ctx, const char *abs_path, bool executable) {
  int idx = trace_find_module(ctx, abs_path);
  bool existing = idx >= 0;
  if (!existing) idx = trace_add_module(ctx, abs_path, false);
  if (idx < 0) return -1;

  ant_trace_module_t *module = &ctx->result->modules[idx];
  module->bundle_flags |= ANT_BUNDLE_MODULE_MATERIALIZE;
  if (executable) module->bundle_flags |= ANT_BUNDLE_MODULE_EXECUTABLE;
  if (!existing) module->kind = (uint8_t)ESM_MODULE_KIND_ASSET;

  return idx;
}

static int trace_visit_add_native(trace_ctx_t *ctx, const char *path, uv_dirent_type_t type) {
  if (type != UV_DIRENT_FILE) return 0;

  if (trace_has_suffix(path, ".node")) {
    int idx = trace_add_module(ctx, path, false);
    if (idx < 0) return -1;
    ctx->result->modules[idx].kind = (uint8_t)ESM_MODULE_KIND_NATIVE;
    ctx->result->modules[idx].bundle_flags |= ANT_BUNDLE_MODULE_MATERIALIZE;
    return 0;
  }

  uv_fs_t stat_req;
  int stat_rc = uv_fs_stat(NULL, &stat_req, path, NULL);
  bool executable = stat_rc == 0 && (stat_req.statbuf.st_mode & 0111u) != 0;
  uv_fs_req_cleanup(&stat_req);

  if ((executable || trace_is_shared_library(path)) && trace_add_asset(ctx, path, executable) < 0) return -1;
  return 0;
}

int trace_register_native_package(trace_ctx_t *ctx, const char *file, bool fallback_to_dir) {
  char root[PATH_MAX];
  if (!trace_find_package_root(file, fallback_to_dir, root, sizeof(root))) return 0;
  for (uint32_t i = 0; i < ctx->materialize_root_count; i++)
    if (strcmp(ctx->materialize_roots[i], root) == 0) return 1;

  int has_native = trace_walk_tree(ctx, root, trace_visit_find_native);
  if (has_native <= 0) return has_native;

  if (!trace_grow((void **)&ctx->materialize_roots, ctx->materialize_root_count,
        &ctx->materialize_root_cap, sizeof(*ctx->materialize_roots))) return -1;
  char *root_copy = strdup(root);
  if (!root_copy) return -1;
  ctx->materialize_roots[ctx->materialize_root_count++] = root_copy;

  for (uint32_t i = 0; i < ctx->result->module_count; i++)
    if (trace_path_under(ctx->result->modules[i].abs_path, root))
      ctx->result->modules[i].bundle_flags |= ANT_BUNDLE_MODULE_MATERIALIZE;

  return trace_walk_tree(ctx, root, trace_visit_add_native) != 0 ? -1 : 1;
}

static char *trace_darwin_arch_spec(const char *spec) {
  if (strcmp(trace_target_platform(), "darwin") != 0) return NULL;

  static const char package_marker[] = "-darwin-universal";
  static const char file_suffix[] = ".darwin-universal.node";
  const char *marker = NULL;
  const char *tail = NULL;
  char replacement[64];

  const char *package = strstr(spec, package_marker);
  const char *package_tail = package ? package + strlen(package_marker) : NULL;
  if (package && (*package_tail == '\0' || *package_tail == '/')) {
    marker = package;
    tail = package_tail;
    snprintf(replacement, sizeof(replacement), "-darwin-%s", trace_target_arch());
  } else if (trace_has_suffix(spec, file_suffix)) {
    marker = spec + strlen(spec) - strlen(file_suffix);
    tail = spec + strlen(spec);
    snprintf(replacement, sizeof(replacement), ".darwin-%s.node", trace_target_arch());
  } else return NULL;

  size_t prefix_len = (size_t)(marker - spec);
  size_t replacement_len = strlen(replacement);
  size_t tail_len = strlen(tail);
  char *target = malloc(prefix_len + replacement_len + tail_len + 1);
  if (!target) return NULL;

  memcpy(target, spec, prefix_len);
  memcpy(target + prefix_len, replacement, replacement_len);
  memcpy(target + prefix_len + replacement_len, tail, tail_len + 1);

  return target;
}

static bool trace_dep_resolves_native(trace_ctx_t *ctx, const char *parent_path, const trace_dep_t *dep) {
  char *resolved = esm_resolve(ctx->js, dep->spec, parent_path, dep->is_require ? esm_resolve_path_require : esm_resolve_path);
  if (!resolved) return false;
  bool native = trace_has_suffix(resolved, ".node");
  free(resolved);
  return native;
}

static bool trace_matching_dep_resolves_native(trace_ctx_t *ctx, const char *parent_path, const char *spec, bool is_require) {
  for (uint32_t i = 0; i < ctx->dep_count; i++) {
    trace_dep_t *candidate = &ctx->deps[i];
    if (candidate->is_require == is_require && strcmp(candidate->spec, spec) == 0)
      return trace_dep_resolves_native(ctx, parent_path, candidate);
  }
  return false;
}

void trace_exclude_redundant_universal_native(trace_ctx_t *ctx, uint32_t parent_idx) {
  static const char package_json_suffix[] = "/package.json";
  const char *parent_path = ctx->result->modules[parent_idx].abs_path;

  for (uint32_t i = 0; i < ctx->dep_count; i++) {
    trace_dep_t *universal = &ctx->deps[i];
    if (!universal->optional) continue;

    char *target_spec = trace_darwin_arch_spec(universal->spec);
    if (!target_spec) continue;
    universal->excluded = trace_matching_dep_resolves_native(ctx, parent_path, target_spec, universal->is_require);

    if (!universal->excluded && trace_has_suffix(target_spec, package_json_suffix)) {
      char *target_base = strndup(target_spec, strlen(target_spec) - strlen(package_json_suffix));
      if (target_base) {
        universal->excluded = trace_matching_dep_resolves_native(ctx, parent_path, target_base, universal->is_require);
        free(target_base);
      }
    }

    free(target_spec);
  }
}

void trace_native_free(trace_ctx_t *ctx) {
  for (uint32_t i = 0; i < ctx->materialize_root_count; i++) free(ctx->materialize_roots[i]);
  free(ctx->materialize_roots);
  ctx->materialize_roots = NULL;
  ctx->materialize_root_count = ctx->materialize_root_cap = 0;
}
