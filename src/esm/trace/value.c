#include <compat.h> // IWYU pragma: keep

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#include "trace_internal.h"

static constexpr size_t TRACE_ARENA_CHUNK = 16 * 1024;

struct trace_arena_chunk {
  trace_arena_chunk_t *next;
  size_t used;
  size_t cap;
  alignas(max_align_t) char data[];
};

void *trace_alloc(trace_ctx_t *ctx, size_t size) {
  size = (size + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1);
  trace_arena_chunk_t *chunk = ctx->arena;

  if (!chunk || chunk->cap - chunk->used < size) {
    size_t cap = size > TRACE_ARENA_CHUNK ? size : TRACE_ARENA_CHUNK;
    chunk = malloc(sizeof(*chunk) + cap);
    if (!chunk) {
      ctx->oom = true;
      return NULL;
    }
    chunk->next = ctx->arena;
    chunk->used = 0;
    chunk->cap = cap;
    ctx->arena = chunk;
  }

  void *ptr = chunk->data + chunk->used;
  chunk->used += size;

  return ptr;
}

char *trace_strndup(trace_ctx_t *ctx, const char *s, size_t len) {
  char *copy = trace_alloc(ctx, len + 1);
  if (!copy) return NULL;
  memcpy(copy, s, len);
  copy[len] = '\0';
  return copy;
}

void trace_arena_free(trace_ctx_t *ctx) {
  while (ctx->arena) {
    trace_arena_chunk_t *next = ctx->arena->next;
    free(ctx->arena);
    ctx->arena = next;
  }
}

static char *trace_concat_text(trace_ctx_t *ctx, const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  bool merge = la && lb && a[la - 1] == TRACE_HOLE && b[0] == TRACE_HOLE;
  char *out = trace_alloc(ctx, la + lb + 1);
  if (!out) return NULL;
  memcpy(out, a, la);
  memcpy(out + la, b + merge, lb - merge + 1);
  return out;
}

static bool trace_segment_is(const char *seg, size_t len, const char *literal) {
  return strlen(literal) == len && memcmp(seg, literal, len) == 0;
}

char *trace_normalize_path(trace_ctx_t *ctx, const char *path) {
  size_t len = strlen(path);
  bool absolute = path[0] == '/';
  bool trailing = len > 1 && path[len - 1] == '/';
  
  const char **segs = trace_alloc(ctx, (len / 2 + 2) * sizeof(*segs));
  size_t *lens = trace_alloc(ctx, (len / 2 + 2) * sizeof(*lens));
  char *out = trace_alloc(ctx, len + 3);
  if (!segs || !lens || !out) return NULL;

  size_t n = 0;
  for (const char *seg = path;;) {
    const char *end = strchr(seg, '/');
    size_t sl = end ? (size_t)(end - seg) : strlen(seg);
    
    if (trace_segment_is(seg, sl, "..")) {
      if (n && !trace_segment_is(segs[n - 1], lens[n - 1], "..")) n--;
      else if (!absolute) {
        segs[n] = seg;
        lens[n++] = sl;
      }
    } else if (sl && !trace_segment_is(seg, sl, ".")) {
      segs[n] = seg;
      lens[n++] = sl;
    }
    
    if (!end) break;
    seg = end + 1;
  }

  size_t w = 0;
  if (absolute) out[w++] = '/';
  for (size_t i = 0; i < n; i++) {
    if (i) out[w++] = '/';
    memcpy(out + w, segs[i], lens[i]);
    w += lens[i];
  }
  
  if (w == 0) out[w++] = '.';
  if (trailing && out[w - 1] != '/') out[w++] = '/';
  out[w] = '\0';
  
  return out;
}

char *trace_join_path(trace_ctx_t *ctx, const char *dir, const char *rel) {
  if (rel[0] == '/') return trace_normalize_path(ctx, rel);
  size_t dl = strlen(dir), rl = strlen(rel);
  char *joined = trace_alloc(ctx, dl + rl + 2);
  if (!joined) return NULL;
  memcpy(joined, dir, dl);
  joined[dl] = '/';
  memcpy(joined + dl + 1, rel, rl + 1);
  return trace_normalize_path(ctx, joined);
}

trace_value_t trace_unknown(void) {
  return (trace_value_t){ .kind = TRACE_VALUE_UNKNOWN };
}

trace_value_t trace_bool(bool truth) {
  return (trace_value_t){ .kind = TRACE_VALUE_BOOL, .truth = truth };
}

trace_value_t trace_ref(trace_value_kind_t kind, uint16_t id) {
  return (trace_value_t){ .kind = kind, .id = id };
}

trace_value_t trace_string(trace_ctx_t *ctx, const char *s, size_t len) {
  const char *copy = trace_strndup(ctx, s, len);
  if (!copy) return trace_unknown();
  return (trace_value_t){ .kind = TRACE_VALUE_STRING, .count = 1, .alts = { copy } };
}

bool trace_alt_has_hole(const char *alt) {
  return strchr(alt, TRACE_HOLE) != NULL;
}

bool trace_value_exact(const trace_value_t *v, const char **out) {
  if (v->kind != TRACE_VALUE_STRING || v->count != 1 || v->partial) return false;
  if (trace_alt_has_hole(v->alts[0])) return false;
  *out = v->alts[0];
  return true;
}

static bool trace_add_alt(trace_value_t *v, const char *alt) {
  for (uint8_t i = 0; i < v->count; i++) if (strcmp(v->alts[i], alt) == 0) return true;
  if (v->count == TRACE_MAX_ALTS) return false;
  v->alts[v->count++] = alt;
  return true;
}

static uint8_t trace_value_texts(const trace_value_t *v, const char **out) {
  static const char hole[] = { TRACE_HOLE, '\0' };
  if (v->kind != TRACE_VALUE_STRING) {
    out[0] = v->kind == TRACE_VALUE_BOOL ? (v->truth ? "true" : "false") : hole;
    return 1;
  }
  
  uint8_t n = v->count;
  memcpy(out, v->alts, n * sizeof(*out));
  if (v->partial && n < TRACE_MAX_ALTS) out[n++] = hole;
  
  return n;
}

static bool trace_value_is_hole(const trace_value_t *v) {
  for (uint8_t i = 0; i < v->count; i++)
    if (!(v->alts[i][0] == TRACE_HOLE && v->alts[i][1] == '\0')) return false;
  return true;
}

trace_value_t trace_concat(trace_ctx_t *ctx, trace_value_t a, trace_value_t b) {
  const char *left[TRACE_MAX_ALTS], *right[TRACE_MAX_ALTS];
  uint8_t nl = trace_value_texts(&a, left), nr = trace_value_texts(&b, right);
  trace_value_t out = {
    .kind = TRACE_VALUE_STRING,
    .partial = (a.partial && a.count == TRACE_MAX_ALTS) || (b.partial && b.count == TRACE_MAX_ALTS),
    .located = a.located || b.located,
  };

  for (uint8_t i = 0; i < nl; i++) for (uint8_t j = 0; j < nr; j++) {
    const char *joined = trace_concat_text(ctx, left[i], right[j]);
    if (!joined || !trace_add_alt(&out, joined)) return trace_unknown();
  }

  return trace_value_is_hole(&out) ? trace_unknown() : out;
}

trace_value_t trace_union(trace_ctx_t *ctx, trace_value_t a, trace_value_t b) {
  if (a.kind == TRACE_VALUE_UNKNOWN && b.kind == TRACE_VALUE_UNKNOWN) return a;
  if (a.kind == TRACE_VALUE_UNKNOWN || b.kind == TRACE_VALUE_UNKNOWN) {
    trace_value_t known = a.kind == TRACE_VALUE_UNKNOWN ? b : a;
    if (known.kind != TRACE_VALUE_STRING && known.kind != TRACE_VALUE_URL) return trace_unknown();
    known.partial = true;
    return known;
  }

  if (a.kind != b.kind) return trace_unknown();
  if (a.kind == TRACE_VALUE_MODULE || a.kind == TRACE_VALUE_FUNC || a.kind == TRACE_VALUE_META)
    return a.id == b.id ? a : trace_unknown();
  if (a.kind == TRACE_VALUE_BOOL)
    return a.truth == b.truth ? a : trace_unknown();

  trace_value_t out = a;
  out.partial = a.partial || b.partial;
  out.located = a.located || b.located;
  for (uint8_t i = 0; i < b.count; i++) if (!trace_add_alt(&out, b.alts[i])) out.partial = true;

  return out;
}

trace_value_t trace_map_alts(
  trace_ctx_t *ctx, trace_value_t v, trace_value_kind_t kind,
  char *(*fn)(trace_ctx_t *, const char *, void *), void *user
) {
  if (v.kind != TRACE_VALUE_STRING && v.kind != TRACE_VALUE_URL) return trace_unknown();
  trace_value_t out = { .kind = kind, .partial = v.partial, .located = v.located };

  for (uint8_t i = 0; i < v.count; i++) {
    char *mapped = fn(ctx, v.alts[i], user);
    if (!mapped) out.partial = true;
    else if (!trace_add_alt(&out, mapped)) out.partial = true;
  }

  return out.count ? out : trace_unknown();
}

bool trace_value_truthy(const trace_value_t *v, bool *out) {
  switch (v->kind) {
  case TRACE_VALUE_BOOL:
    *out = v->truth;
    return true;
  case TRACE_VALUE_URL:
  case TRACE_VALUE_LIST:
  case TRACE_VALUE_MODULE:
  case TRACE_VALUE_FUNC:
  case TRACE_VALUE_META:
    *out = true;
    return true;
  case TRACE_VALUE_STRING: {
    if (v->partial) return false;
    bool any_empty = false, any_full = false;
    for (uint8_t i = 0; i < v->count; i++) {
      const char *alt = v->alts[i];
      bool literal = false;
      for (const char *p = alt; *p; p++) if (*p != TRACE_HOLE) literal = true;
      if (literal) any_full = true;
      else if (!alt[0]) any_empty = true;
      else return false;
    }
    if (any_empty == any_full) return false;
    *out = any_full;
    return true;
  }
  default:
    return false;
  }
}

bool trace_value_equal(const trace_value_t *a, const trace_value_t *b, bool *out) {
  if (a->kind == TRACE_VALUE_BOOL && b->kind == TRACE_VALUE_BOOL) {
    *out = a->truth == b->truth;
    return true;
  }

  const char *x, *y;
  if (!trace_value_exact(a, &x) || !trace_value_exact(b, &y)) return false;
  *out = strcmp(x, y) == 0;

  return true;
}

const char *trace_target_platform(void) {
#if defined(__APPLE__)
  return "darwin";
#elif defined(__linux__)
  return "linux";
#elif defined(_WIN32)
  return "win32";
#elif defined(__FreeBSD__)
  return "freebsd";
#else
  return "unknown";
#endif
}

const char *trace_target_arch(void) {
#if defined(__x86_64__) || defined(_M_X64)
  return "x64";
#elif defined(__i386__) || defined(_M_IX86)
  return "ia32";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
  return "arm";
#else
  return "unknown";
#endif
}
