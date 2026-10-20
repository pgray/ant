#include <compat.h> // IWYU pragma: keep

#include <stdio.h>
#include <string.h>

#include "tokens.h"
#include "trace_internal.h"

typedef struct {
  const char *specifier;
  trace_module_id_t id;
} trace_specifier_entry_t;

typedef struct {
  uint16_t owner;
  const char *name;
  trace_func_id_t func;
} trace_member_entry_t;

typedef struct {
  const char *name;
  trace_value_kind_t kind;
  uint16_t id;
} trace_global_entry_t;

static const trace_specifier_entry_t trace_specifiers[] = {
#define TRACE_SPECIFIER(spec, id) { spec, TRACE_MODULE_##id },
#include "builtins.h"
};

static const trace_member_entry_t trace_module_members[] = {
#define TRACE_MODULE_MEMBER(owner, name, func) { TRACE_MODULE_##owner, name, TRACE_FUNC_##func },
#include "builtins.h"
};

static const trace_member_entry_t trace_func_members[] = {
#define TRACE_FUNC_MEMBER(owner, name, func) { TRACE_FUNC_##owner, name, TRACE_FUNC_##func },
#include "builtins.h"
};

static const trace_global_entry_t trace_globals[] = {
#define TRACE_GLOBAL(name, kind, id) { name, TRACE_VALUE_##kind, TRACE_##kind##_##id },
#include "builtins.h"
};

static const trace_sink_t trace_func_sinks[TRACE_FUNC_COUNT] = {
#define TRACE_FUNC(id, sink) [TRACE_FUNC_##id] = TRACE_SINK_##sink,
#include "builtins.h"
};

trace_sink_t trace_func_sink(uint16_t id) {
  return id < TRACE_FUNC_COUNT ? trace_func_sinks[id] : TRACE_SINK_NONE;
}

static bool trace_name_is(const char *name, size_t len, const char *literal) {
  return strlen(literal) == len && memcmp(name, literal, len) == 0;
}

static bool trace_node_is_ident(const sv_ast_t *node, const char *name) {
  return node && node->type == N_IDENT && node->str && trace_name_is(node->str, node->len, name);
}

void trace_scope_push(trace_ctx_t *ctx, bool function) {
  trace_scope_t *scope = trace_alloc(ctx, sizeof(*scope));
  if (!scope) return;
  *scope = (trace_scope_t){ .parent = ctx->scope, .function = function };
  ctx->scope = scope;
}

void trace_scope_pop(trace_ctx_t *ctx) {
  if (ctx->scope) ctx->scope = ctx->scope->parent;
}

trace_scope_t *trace_function_scope(trace_ctx_t *ctx) {
  trace_scope_t *scope = ctx->scope;
  while (scope && !scope->function && scope->parent) scope = scope->parent;
  return scope;
}

void trace_declare(trace_ctx_t *ctx, trace_scope_t *scope, const char *name, uint32_t len, trace_value_t value) {
  if (!scope || !name) return;
  for (uint32_t i = 0; i < scope->count; i++) {
    trace_binding_t *b = &scope->items[i];
    if (b->len == len && memcmp(b->name, name, len) == 0) {
      b->value = value;
      return;
    }
  }

  if (scope->count == scope->cap) {
    uint32_t cap = scope->cap ? scope->cap * 2 : 8;
    trace_binding_t *items = trace_alloc(ctx, cap * sizeof(*items));
    if (!items) return;
    if (scope->count) memcpy(items, scope->items, scope->count * sizeof(*items));
    scope->items = items;
    scope->cap = cap;
  }

  scope->items[scope->count++] = (trace_binding_t){ name, len, value };
}

static trace_binding_t *trace_lookup(trace_ctx_t *ctx, const char *name, uint32_t len) {
  for (trace_scope_t *scope = ctx->scope; scope; scope = scope->parent)
    for (uint32_t i = 0; i < scope->count; i++) {
      trace_binding_t *b = &scope->items[i];
      if (b->len == len && memcmp(b->name, name, len) == 0) return b;
    }
  return NULL;
}

bool trace_name_assigned(trace_ctx_t *ctx, const char *name, uint32_t len) {
  trace_name_t *entry = NULL;
  HASH_FIND(hh, ctx->assigned, name, len, entry);
  return entry != NULL;
}

static trace_value_t trace_located(trace_ctx_t *ctx, const char *path) {
  trace_value_t v = trace_string(ctx, path, strlen(path));
  v.located = true;
  return v;
}

static trace_value_t trace_global(trace_ctx_t *ctx, const char *name, uint32_t len) {
  if (trace_name_is(name, len, "__dirname")) return trace_located(ctx, ctx->dir);
  if (trace_name_is(name, len, "__filename")) return trace_located(ctx, ctx->file);
  for (size_t i = 0; i < sizeof(trace_globals) / sizeof(*trace_globals); i++)
    if (trace_name_is(name, len, trace_globals[i].name)) return trace_ref(trace_globals[i].kind, trace_globals[i].id);
  return trace_unknown();
}

trace_value_t trace_import_value(trace_ctx_t *ctx, const char *spec, size_t len) {
  if (len > 5 && memcmp(spec, "node:", 5) == 0) {
    spec += 5;
    len -= 5;
  }
  for (size_t i = 0; i < sizeof(trace_specifiers) / sizeof(*trace_specifiers); i++)
    if (trace_name_is(spec, len, trace_specifiers[i].specifier)) return trace_ref(TRACE_VALUE_MODULE, (uint16_t)trace_specifiers[i].id);
  return trace_unknown();
}

static trace_value_t trace_lookup_member(const trace_member_entry_t *table, size_t count, uint16_t owner, const char *name, size_t len) {
  for (size_t i = 0; i < count; i++)
    if (table[i].owner == owner && trace_name_is(name, len, table[i].name)) return trace_ref(TRACE_VALUE_FUNC, (uint16_t)table[i].func);
  return trace_unknown();
}

static char *trace_file_url_text(trace_ctx_t *ctx, const char *path, void *user) {
  size_t len = strlen(path);
  char *out = trace_alloc(ctx, len + 8);
  if (!out) return NULL;
  memcpy(out, "file://", 7);
  memcpy(out + 7, path, len + 1);
  return out;
}

static char *trace_identity_text(trace_ctx_t *ctx, const char *text, void *user) {
  return trace_strndup(ctx, text, strlen(text));
}

static trace_value_t trace_process_member(trace_ctx_t *ctx, const char *name, size_t len) {
  if (trace_name_is(name, len, "platform")) return trace_string(ctx, trace_target_platform(), strlen(trace_target_platform()));
  if (trace_name_is(name, len, "arch")) return trace_string(ctx, trace_target_arch(), strlen(trace_target_arch()));
  return trace_unknown();
}

static trace_value_t trace_meta_member(trace_ctx_t *ctx, const char *name, size_t len) {
  if (trace_name_is(name, len, "dirname")) return trace_located(ctx, ctx->dir);
  if (trace_name_is(name, len, "filename")) return trace_located(ctx, ctx->file);
  if (!trace_name_is(name, len, "url")) return trace_unknown();
  trace_value_t url = trace_located(ctx, ctx->file);
  url.kind = TRACE_VALUE_URL;
  return url;
}

trace_value_t trace_member(trace_ctx_t *ctx, trace_value_t base, const char *name, size_t len) {
  switch (base.kind) {
  case TRACE_VALUE_META:
    return trace_meta_member(ctx, name, len);
  case TRACE_VALUE_MODULE:
    if (trace_name_is(name, len, "default")) return base;
    if (base.id == TRACE_MODULE_PROCESS) return trace_process_member(ctx, name, len);
    return trace_lookup_member(trace_module_members, sizeof(trace_module_members) / sizeof(*trace_module_members), base.id, name, len);
  case TRACE_VALUE_FUNC:
    return trace_lookup_member(trace_func_members, sizeof(trace_func_members) / sizeof(*trace_func_members), base.id, name, len);
  case TRACE_VALUE_URL:
    if (trace_name_is(name, len, "pathname")) return trace_map_alts(ctx, base, TRACE_VALUE_STRING, trace_identity_text, NULL);
    if (trace_name_is(name, len, "href")) return trace_map_alts(ctx, base, TRACE_VALUE_STRING, trace_file_url_text, NULL);
    return trace_unknown();
  default:
    return trace_unknown();
  }
}

static bool trace_member_key(trace_ctx_t *ctx, const sv_ast_t *node, const char **name, size_t *len) {
  const sv_ast_t *key = node->right;
  if (!key) return false;
  if (!node->flags) {
    if (!key->str) return false;
    *name = key->str;
    *len = key->len;
    return true;
  }

  trace_value_t v = trace_eval(ctx, key);
  const char *text;
  if (!trace_value_exact(&v, &text)) return false;
  *name = text;
  *len = strlen(text);

  return true;
}

static bool trace_node_is_import_meta(const sv_ast_t *node) {
  return node && node->type == N_MEMBER && !node->flags && trace_node_is_ident(node->left, "import") &&
    node->right && node->right->str && trace_name_is(node->right->str, node->right->len, "meta");
}

static trace_value_t trace_eval_member(trace_ctx_t *ctx, const sv_ast_t *node) {
  if (trace_node_is_import_meta(node)) return trace_ref(TRACE_VALUE_META, 0);
  const char *name;
  size_t len;
  if (!trace_member_key(ctx, node, &name, &len)) return trace_unknown();
  return trace_member(ctx, trace_eval(ctx, node->left), name, len);
}

static char *trace_strip_file_url(trace_ctx_t *ctx, const char *text, void *user) {
  if (strncmp(text, "file://", 7) == 0) return trace_strndup(ctx, text + 7, strlen(text + 7));
  return text[0] == '/' ? trace_strndup(ctx, text, strlen(text)) : NULL;
}

static char *trace_dirname_text(trace_ctx_t *ctx, const char *path, void *user) {
  const char *slash = strrchr(path, '/');
  if (!slash) return trace_strndup(ctx, ".", 1);
  return trace_strndup(ctx, path, slash == path ? 1 : (size_t)(slash - path));
}

static char *trace_resolve_against(trace_ctx_t *ctx, const char *spec, void *user) {
  if (strncmp(spec, "file://", 7) == 0) return trace_normalize_path(ctx, spec + 7);
  return trace_join_path(ctx, user, spec);
}

static char *trace_normalize_text(trace_ctx_t *ctx, const char *path, void *user) {
  return trace_normalize_path(ctx, path);
}

static bool trace_value_absolute(const trace_value_t *v) {
  if (v->kind != TRACE_VALUE_STRING || v->partial || !v->count) return false;
  for (uint8_t i = 0; i < v->count; i++) if (v->alts[i][0] != '/') return false;
  return true;
}

static trace_value_t trace_path_join(trace_ctx_t *ctx, const sv_ast_t *call, bool resolve) {
  int first = 0;
  if (resolve) {
    first = -1;
    for (int i = call->args.count - 1; i >= 0 && first < 0; i--) {
      trace_value_t arg = trace_eval(ctx, call->args.items[i]);
      if (trace_value_absolute(&arg)) first = i;
    }
    if (first < 0) return trace_unknown();
  }
  if (first >= call->args.count) return trace_unknown();

  trace_value_t acc = trace_eval(ctx, call->args.items[first]);
  if (acc.kind != TRACE_VALUE_STRING) return trace_unknown();
  
  for (int i = first + 1; i < call->args.count; i++) {
    trace_value_t part = trace_eval(ctx, call->args.items[i]);
    if (part.kind != TRACE_VALUE_STRING) part = trace_unknown();
    acc = trace_concat(ctx, trace_concat(ctx, acc, trace_string(ctx, "/", 1)), part);
    if (acc.kind != TRACE_VALUE_STRING) return trace_unknown();
  }
  
  return trace_map_alts(ctx, acc, TRACE_VALUE_STRING, trace_normalize_text, NULL);
}

static trace_value_t trace_url_ctor(trace_ctx_t *ctx, const sv_ast_t *call) {
  if (call->args.count < 1) return trace_unknown();
  trace_value_t spec = trace_eval(ctx, call->args.items[0]);
  if (spec.kind != TRACE_VALUE_STRING) return trace_unknown();

  if (call->args.count < 2) {
    trace_value_t url = trace_map_alts(ctx, spec, TRACE_VALUE_URL, trace_strip_file_url, NULL);
    return url.kind == TRACE_VALUE_URL ? url : trace_unknown();
  }

  trace_value_t base = trace_eval(ctx, call->args.items[1]);
  if (base.kind == TRACE_VALUE_STRING) base = trace_map_alts(ctx, base, TRACE_VALUE_URL, trace_strip_file_url, NULL);
  const char *base_path;
  if (base.kind != TRACE_VALUE_URL || base.count != 1 || trace_alt_has_hole(base.alts[0])) return trace_unknown();
  base_path = base.alts[0];

  char *dir = trace_dirname_text(ctx, base_path, NULL);
  if (!dir) return trace_unknown();
  trace_value_t url = trace_map_alts(ctx, spec, TRACE_VALUE_URL, trace_resolve_against, dir);
  url.located = url.located || base.located;

  return url;
}

static trace_value_t trace_eval_call(trace_ctx_t *ctx, const sv_ast_t *node, bool construct) {
  trace_value_t callee = trace_eval(ctx, node->left);
  if (callee.kind != TRACE_VALUE_FUNC) return trace_unknown();
  trace_value_t arg = node->args.count ? trace_eval(ctx, node->args.items[0]) : trace_unknown();
  const char *text;

  switch ((trace_func_id_t)callee.id) {
  case TRACE_FUNC_REQUIRE:
    return trace_value_exact(&arg, &text) ? trace_import_value(ctx, text, strlen(text)) : trace_unknown();
  case TRACE_FUNC_CREATE_REQUIRE:
    return trace_ref(TRACE_VALUE_FUNC, TRACE_FUNC_REQUIRE);
  case TRACE_FUNC_PATH_JOIN:
    return trace_path_join(ctx, node, false);
  case TRACE_FUNC_PATH_RESOLVE:
    return trace_path_join(ctx, node, true);
  case TRACE_FUNC_PATH_DIRNAME:
    return trace_map_alts(ctx, arg, TRACE_VALUE_STRING, trace_dirname_text, NULL);
  case TRACE_FUNC_FILE_URL_TO_PATH:
    return arg.kind == TRACE_VALUE_URL
      ? trace_map_alts(ctx, arg, TRACE_VALUE_STRING, trace_identity_text, NULL)
      : trace_map_alts(ctx, arg, TRACE_VALUE_STRING, trace_strip_file_url, NULL);
  case TRACE_FUNC_PATH_TO_FILE_URL:
    return trace_map_alts(ctx, arg, TRACE_VALUE_URL, trace_identity_text, NULL);
  case TRACE_FUNC_URL:
    return construct ? trace_url_ctor(ctx, node) : trace_unknown();
  default:
    return trace_unknown();
  }
}

static trace_value_t trace_eval_number(trace_ctx_t *ctx, double num) {
  char buf[32];
  if (num != (double)(long long)num) return trace_unknown();
  int len = snprintf(buf, sizeof(buf), "%lld", (long long)num);
  return trace_string(ctx, buf, (size_t)len);
}

static trace_value_t trace_eval_template(trace_ctx_t *ctx, const sv_ast_t *node) {
  trace_value_t acc = trace_string(ctx, "", 0);
  for (int i = 0; i < node->args.count; i++) {
    const sv_ast_t *part = node->args.items[i];
    trace_value_t v;
    if ((i & 1) == 0) v = part->flags & FN_INVALID_COOKED ? trace_unknown() : trace_string(ctx, part->str ? part->str : "", part->len);
    else v = trace_eval(ctx, part);
    acc = trace_concat(ctx, acc, v);
  }
  return acc;
}

static trace_value_t trace_eval_array(trace_ctx_t *ctx, const sv_ast_t *node) {
  trace_value_t acc = { .kind = TRACE_VALUE_UNKNOWN };
  for (int i = 0; i < node->args.count; i++) {
    trace_value_t item = trace_eval(ctx, node->args.items[i]);
    if (item.kind != TRACE_VALUE_STRING) item = trace_unknown();
    acc = i == 0 ? item : trace_union(ctx, acc, item);
  }
  if (acc.kind == TRACE_VALUE_STRING) acc.kind = TRACE_VALUE_LIST;
  return acc;
}

static trace_value_t trace_eval_logical(trace_ctx_t *ctx, const sv_ast_t *node) {
  trace_value_t left = trace_eval(ctx, node->left);
  bool truth;
  bool known = trace_value_truthy(&left, &truth);

  if (node->op == TOK_NULLISH) {
    if (left.kind == TRACE_VALUE_STRING && !left.partial) return left;
    return trace_union(ctx, left, trace_eval(ctx, node->right));
  }

  if (known && truth == (node->op == TOK_LOR)) return left;
  if (known) return trace_eval(ctx, node->right);
  return trace_union(ctx, left, trace_eval(ctx, node->right));
}

static trace_value_t trace_eval_binary(trace_ctx_t *ctx, const sv_ast_t *node) {
  switch (node->op) {
  case TOK_PLUS:
    return trace_concat(ctx, trace_eval(ctx, node->left), trace_eval(ctx, node->right));
  case TOK_LAND:
  case TOK_LOR:
  case TOK_NULLISH:
    return trace_eval_logical(ctx, node);
  case TOK_EQ:
  case TOK_NE:
  case TOK_SEQ:
  case TOK_SNE: {
    trace_value_t a = trace_eval(ctx, node->left), b = trace_eval(ctx, node->right);
    bool equal;
    if (!trace_value_equal(&a, &b, &equal)) return trace_unknown();
    return trace_bool((node->op == TOK_EQ || node->op == TOK_SEQ) ? equal : !equal);
  }
  default:
    return trace_unknown();
  }
}

static trace_value_t trace_eval_typeof(trace_ctx_t *ctx, const sv_ast_t *node) {
  trace_value_t v = trace_eval(ctx, node->right);
  const char *type = NULL;
  switch (v.kind) {
  case TRACE_VALUE_STRING: type = v.partial ? NULL : "string"; break;
  case TRACE_VALUE_BOOL: type = "boolean"; break;
  case TRACE_VALUE_FUNC: type = "function"; break;
  case TRACE_VALUE_MODULE:
  case TRACE_VALUE_URL:
  case TRACE_VALUE_LIST:
  case TRACE_VALUE_META: type = "object"; break;
  default: break;
  }
  return type ? trace_string(ctx, type, strlen(type)) : trace_unknown();
}

static trace_value_t trace_eval_ident(trace_ctx_t *ctx, const sv_ast_t *node) {
  if (!node->str) return trace_unknown();
  trace_binding_t *b = trace_lookup(ctx, node->str, node->len);
  return b ? b->value : trace_global(ctx, node->str, node->len);
}

trace_value_t trace_eval(trace_ctx_t *ctx, const sv_ast_t *node) {
  if (!node || ctx->oom) return trace_unknown();

  switch (node->type) {
  case N_STRING: return trace_string(ctx, node->str ? node->str : "", node->len);
  case N_NUMBER: return trace_eval_number(ctx, node->num);
  case N_BOOL: return trace_bool(node->num != 0);
  case N_TEMPLATE: return trace_eval_template(ctx, node);
  case N_IDENT: return trace_eval_ident(ctx, node);
  case N_MEMBER:
  case N_OPTIONAL: return trace_eval_member(ctx, node);
  case N_CALL: return trace_eval_call(ctx, node, false);
  case N_NEW: return trace_eval_call(ctx, node, true);
  case N_BINARY: return trace_eval_binary(ctx, node);
  case N_TYPEOF: return trace_eval_typeof(ctx, node);
  case N_ARRAY: return trace_eval_array(ctx, node);
  case N_AWAIT: return trace_eval(ctx, node->right);

  case N_UNARY: {
    if (node->op != TOK_NOT) return trace_unknown();
    trace_value_t v = trace_eval(ctx, node->right ? node->right : node->left);
    bool truth;
    return trace_value_truthy(&v, &truth) ? trace_bool(!truth) : trace_unknown();
  }

  case N_TERNARY: {
    trace_value_t cond = trace_eval(ctx, node->cond);
    bool truth;
    if (trace_value_truthy(&cond, &truth)) return trace_eval(ctx, truth ? node->left : node->right);
    return trace_union(ctx, trace_eval(ctx, node->left), trace_eval(ctx, node->right));
  }

  case N_IMPORT: {
    trace_value_t spec = trace_eval(ctx, node->right);
    const char *text;
    return trace_value_exact(&spec, &text) ? trace_import_value(ctx, text, strlen(text)) : trace_unknown();
  }

  default:
    return trace_unknown();
  }
}

void trace_bind_pattern(trace_ctx_t *ctx, trace_scope_t *scope, const sv_ast_t *pattern, trace_value_t value) {
  if (!pattern) return;
  switch (pattern->type) {
  case N_IDENT:
    trace_declare(ctx, scope, pattern->str, pattern->len, value);
    return;
  case N_ASSIGN_PAT:
    trace_bind_pattern(ctx, scope, pattern->left, value.kind == TRACE_VALUE_UNKNOWN ? trace_eval(ctx, pattern->right) : value);
    return;
  case N_REST:
  case N_SPREAD:
    trace_bind_pattern(ctx, scope, pattern->right, trace_unknown());
    return;
  case N_ARRAY_PAT:
  case N_ARRAY:
    for (int i = 0; i < pattern->args.count; i++) trace_bind_pattern(ctx, scope, pattern->args.items[i], trace_unknown());
    return;
  case N_OBJECT_PAT:
  case N_OBJECT:
    for (int i = 0; i < pattern->args.count; i++) {
      const sv_ast_t *prop = pattern->args.items[i];
      if (!prop) continue;
      if (prop->type != N_PROPERTY) {
        trace_bind_pattern(ctx, scope, prop, trace_unknown());
        continue;
      }
      bool computed = (prop->flags & FN_COMPUTED) != 0;
      const sv_ast_t *key = prop->left;
      trace_value_t member = !computed && key && key->str
        ? trace_member(ctx, value, key->str, key->len)
        : trace_unknown();
      trace_bind_pattern(ctx, scope, prop->right ? prop->right : prop->left, member);
    }
    return;
  default:
    return;
  }
}
