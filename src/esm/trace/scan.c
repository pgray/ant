#include <compat.h> // IWYU pragma: keep

#include <string.h>

#include "tokens.h"
#include "trace_internal.h"

static void scan_node(trace_ctx_t *ctx, const sv_ast_t *node);

static bool scan_bytes_contain(const char *value, uint32_t len, const char *needle) {
  size_t n = strlen(needle);
  if (!value || len < n) return false;
  for (uint32_t i = 0; i + n <= len; i++) if (memcmp(value + i, needle, n) == 0) return true;
  return false;
}

static bool scan_mentions_native_addon(const sv_ast_t *node) {
  if (!node) return false;
  if (node->type == N_STRING &&
      (scan_bytes_contain(node->str, node->len, ".node") || scan_bytes_contain(node->aux, node->aux_len, ".node")))
    return true;

  const sv_ast_t *children[] = {
    node->left, node->right, node->cond, node->body, node->catch_param,
    node->catch_body, node->finally_body, node->init, node->update,
  };
  for (size_t i = 0; i < sizeof(children) / sizeof(*children); i++)
    if (scan_mentions_native_addon(children[i])) return true;
  for (int i = 0; i < node->args.count; i++)
    if (scan_mentions_native_addon(node->args.items[i])) return true;

  return false;
}

static void scan_collect_assigned(trace_ctx_t *ctx, const sv_ast_t *node) {
  if (!node || ctx->oom) return;
  const sv_ast_t *target = NULL;

  if (node->type == N_ASSIGN) target = node->left;
  else if (node->type == N_UPDATE) target = node->right;
  else if ((node->type == N_FOR_IN || node->type == N_FOR_OF || node->type == N_FOR_AWAIT_OF) && node->left && node->left->type == N_IDENT)
    target = node->left;

  if (target && target->type == N_IDENT && target->str && !trace_name_assigned(ctx, target->str, target->len)) {
    trace_name_t *entry = trace_alloc(ctx, sizeof(*entry));
    if (entry) {
      *entry = (trace_name_t){ .name = target->str, .len = target->len };
      HASH_ADD_KEYPTR(hh, ctx->assigned, entry->name, entry->len, entry);
    }
  }

  const sv_ast_t *children[] = {
    node->left, node->right, node->cond, node->body, node->catch_param,
    node->catch_body, node->finally_body, node->init, node->update,
  };
  for (size_t i = 0; i < sizeof(children) / sizeof(*children); i++) scan_collect_assigned(ctx, children[i]);
  for (int i = 0; i < node->args.count; i++) scan_collect_assigned(ctx, node->args.items[i]);
}

static bool scan_pattern_assigned(trace_ctx_t *ctx, const sv_ast_t *pattern) {
  if (!pattern) return false;
  if (pattern->type == N_IDENT) return pattern->str && trace_name_assigned(ctx, pattern->str, pattern->len);
  if (scan_pattern_assigned(ctx, pattern->left) || (pattern->type != N_ASSIGN_PAT && scan_pattern_assigned(ctx, pattern->right))) return true;
  for (int i = 0; i < pattern->args.count; i++) if (scan_pattern_assigned(ctx, pattern->args.items[i])) return true;
  return false;
}

static void scan_push_dep(trace_ctx_t *ctx, trace_sink_t sink, const char *spec, uint32_t offset, bool edge) {
  if (!trace_grow((void **)&ctx->deps, ctx->dep_count, &ctx->dep_cap, sizeof(*ctx->deps))) {
    ctx->oom = true;
    return;
  }
  char *copy = trace_strndup(ctx, spec, strlen(spec));
  if (!copy) return;

  ctx->deps[ctx->dep_count++] = (trace_dep_t){
    .spec = copy,
    .offset = offset,
    .sink = sink,
    .is_require = sink == TRACE_SINK_REQUIRE || sink == TRACE_SINK_RESOLVE,
    .lenient = sink != TRACE_SINK_STATIC && sink != TRACE_SINK_IMPORT && sink != TRACE_SINK_REQUIRE,
    .optional = ctx->try_depth > 0,
    .edge = edge,
  };
}

typedef struct {
  trace_sink_t sink;
  uint32_t offset;
} scan_expand_t;

static bool scan_emit_expanded(trace_ctx_t *ctx, const char *spec, bool relative, void *user) {
  scan_expand_t *e = user;
  scan_push_dep(ctx, TRACE_SINK_EXPANDED, spec, e->offset, relative);
  if (ctx->oom) return false;
  ctx->deps[ctx->dep_count - 1].is_require = e->sink == TRACE_SINK_REQUIRE || e->sink == TRACE_SINK_RESOLVE;
  return true;
}

static const char *scan_sink_label(trace_sink_t sink) {
  switch (sink) {
  case TRACE_SINK_REQUIRE: return "require() with a non-constant specifier";
  case TRACE_SINK_IMPORT: return "import() with a non-constant specifier";
  case TRACE_SINK_WORKER: return "new Worker() with a non-constant path";
  case TRACE_SINK_FORK: return "child_process.fork() with a non-constant path";
  default: return NULL;
  }
}

static void scan_warn_untraced(trace_ctx_t *ctx, trace_sink_t sink, uint32_t offset) {
  uint32_t line = trace_source_line(ctx, offset);
  const char *label = scan_sink_label(sink);
  if (!label) return;
  if (ctx->try_depth > 0) trace_warn_deferred(ctx,
    "%s:%u: %s is not traced inside try/catch; it will fail at runtime unless the target is bundled",
    ctx->file, line, label);
  else trace_warn(ctx,
    "%s:%u: %s is not traced; it will fail at runtime unless the target is bundled",
    ctx->file, line, label);
}

static bool scan_try_native(trace_ctx_t *ctx) {
  int native = trace_register_native_package(ctx, ctx->file, false);
  if (native < 0) {
    ctx->oom = true;
    return false;
  }
  if (native > 0) ctx->native_fallback_available = true;
  return native > 0;
}

static void scan_sink(trace_ctx_t *ctx, trace_sink_t sink, trace_value_t value, const sv_ast_t *arg, uint32_t offset) {
  bool loader = sink == TRACE_SINK_REQUIRE || sink == TRACE_SINK_RESOLVE || sink == TRACE_SINK_IMPORT;
  bool resolved = value.kind == TRACE_VALUE_STRING || value.kind == TRACE_VALUE_URL;
  bool native = false;
  bool unresolved = !resolved || value.partial;

  for (uint8_t i = 0; resolved && i < value.count; i++) {
    const char *alt = value.alts[i];
    bool computed_path = value.kind == TRACE_VALUE_URL || value.located;

    if (!trace_alt_has_hole(alt)) {
      scan_push_dep(ctx, sink, alt, offset, !computed_path);
      continue;
    }
    if (loader && trace_has_suffix(alt, ".node")) {
      native = true;
      continue;
    }

    scan_expand_t expand = { sink, offset };
    if (trace_expand_pattern(ctx, alt, scan_emit_expanded, &expand) <= 0) unresolved = true;
  }

  if (!native && unresolved && loader && scan_mentions_native_addon(arg)) native = true;
  if (native && scan_try_native(ctx)) return;
  if (unresolved || native) scan_warn_untraced(ctx, sink, offset);
}

static trace_value_t scan_eval_arg(trace_ctx_t *ctx, const sv_ast_t *call, int index) {
  return call->args.count > index ? trace_eval(ctx, call->args.items[index]) : trace_unknown();
}

static void scan_asset(trace_ctx_t *ctx, const sv_ast_t *node) {
  if (node->args.count < 2) return;
  trace_value_t url = trace_eval(ctx, node);
  if (url.kind != TRACE_VALUE_URL || !url.located) return;

  const sv_ast_t *spec = node->args.items[0];
  if (spec && spec->type == N_STRING && spec->str) {
    trace_value_t literal = trace_string(ctx, spec->str, spec->len);
    scan_sink(ctx, TRACE_SINK_ASSET, literal, spec, node->src_off);
  } else scan_sink(ctx, TRACE_SINK_ASSET, url, spec, node->src_off);
}

static void scan_call_sinks(trace_ctx_t *ctx, const sv_ast_t *node, bool construct) {
  trace_value_t callee = trace_eval(ctx, node->left);
  if (callee.kind != TRACE_VALUE_FUNC) return;
  trace_sink_t sink = trace_func_sink(callee.id);

  switch (sink) {
  case TRACE_SINK_REQUIRE:
  case TRACE_SINK_RESOLVE:
  case TRACE_SINK_FORK:
    if (!construct && node->args.count) scan_sink(ctx, sink, scan_eval_arg(ctx, node, 0), node->args.items[0], node->src_off);
    break;
  case TRACE_SINK_WORKER:
    if (construct && node->args.count) scan_sink(ctx, sink, scan_eval_arg(ctx, node, 0), node->args.items[0], node->src_off);
    break;
  case TRACE_SINK_ASSET:
    if (construct) scan_asset(ctx, node);
    break;
  default:
    break;
  }
}

static void scan_list(trace_ctx_t *ctx, const sv_ast_list_t *list) {
  for (int i = 0; i < list->count; i++) scan_node(ctx, list->items[i]);
}

static void scan_children(trace_ctx_t *ctx, const sv_ast_t *node) {
  scan_node(ctx, node->left);
  scan_node(ctx, node->right);
  scan_node(ctx, node->cond);
  scan_node(ctx, node->body);
  scan_node(ctx, node->init);
  scan_node(ctx, node->update);
  scan_list(ctx, &node->args);
}

static trace_scope_t *scan_var_scope(trace_ctx_t *ctx, const sv_ast_t *var) {
  return var->var_kind == SV_VAR_VAR ? trace_function_scope(ctx) : ctx->scope;
}

static void scan_bind_var(trace_ctx_t *ctx, const sv_ast_t *var) {
  trace_scope_t *scope = scan_var_scope(ctx, var);
  for (int i = 0; i < var->args.count; i++) {
    const sv_ast_t *decl = var->args.items[i];
    if (!decl || decl->type != N_VARDECL) continue;
    bool stable = var->var_kind == SV_VAR_CONST || !scan_pattern_assigned(ctx, decl->left);
    trace_value_t value = stable && decl->right ? trace_eval(ctx, decl->right) : trace_unknown();
    trace_bind_pattern(ctx, scope, decl->left, value);
  }
}

static void scan_bind_import(trace_ctx_t *ctx, const sv_ast_t *decl) {
  const sv_ast_t *source = decl->right;
  trace_value_t module = source && source->type == N_STRING && source->str
    ? trace_import_value(ctx, source->str, source->len)
    : trace_unknown();

  for (int i = 0; i < decl->args.count; i++) {
    const sv_ast_t *spec = decl->args.items[i];
    if (!spec || !spec->right) continue;
    trace_value_t value = trace_unknown();
    if (spec->flags & 3) value = module;
    else if (spec->left && spec->left->str) value = trace_member(ctx, module, spec->left->str, spec->left->len);
    trace_bind_pattern(ctx, ctx->scope, spec->right, value);
  }
}

static const sv_ast_t *scan_declaration(const sv_ast_t *stmt) {
  if (stmt && stmt->type == N_EXPORT && (stmt->flags & (EX_DECL | EX_DEFAULT))) return stmt->left;
  return stmt;
}

static void scan_hoist_vars(trace_ctx_t *ctx, trace_scope_t *scope, const sv_ast_t *node) {
  if (!node || node->type == N_FUNC || node->type == N_ARROW || node->type == N_CLASS) return;
  if (node->type == N_VAR && node->var_kind == SV_VAR_VAR)
    for (int i = 0; i < node->args.count; i++) {
      const sv_ast_t *decl = node->args.items[i];
      if (decl && decl->type == N_VARDECL) trace_bind_pattern(ctx, scope, decl->left, trace_unknown());
    }

  const sv_ast_t *children[] = { node->left, node->right, node->body, node->init, node->catch_body, node->finally_body };
  for (size_t i = 0; i < sizeof(children) / sizeof(*children); i++) scan_hoist_vars(ctx, scope, children[i]);
  if (node->type != N_CALL && node->type != N_NEW) for (int i = 0; i < node->args.count; i++) scan_hoist_vars(ctx, scope, node->args.items[i]);
}

static void scan_predeclare(trace_ctx_t *ctx, const sv_ast_list_t *body) {
  for (int i = 0; i < body->count; i++) {
    const sv_ast_t *stmt = scan_declaration(body->items[i]);
    if (!stmt) continue;
    if ((stmt->type == N_FUNC || stmt->type == N_CLASS) && stmt->str && stmt->len)
      trace_declare(ctx, ctx->scope, stmt->str, stmt->len, trace_unknown());
    else if (stmt->type == N_VAR && stmt->var_kind != SV_VAR_VAR)
      for (int j = 0; j < stmt->args.count; j++) {
        const sv_ast_t *decl = stmt->args.items[j];
        if (decl && decl->type == N_VARDECL) trace_bind_pattern(ctx, ctx->scope, decl->left, trace_unknown());
      }
  }

  for (int i = 0; i < body->count; i++) {
    const sv_ast_t *stmt = scan_declaration(body->items[i]);
    if (!stmt) continue;
    if (stmt->type == N_VAR) scan_bind_var(ctx, stmt);
    else if (stmt->type == N_IMPORT_DECL) scan_bind_import(ctx, stmt);
  }
}

static void scan_body(trace_ctx_t *ctx, const sv_ast_list_t *body, bool function) {
  if (function) for (int i = 0; i < body->count; i++) scan_hoist_vars(ctx, ctx->scope, body->items[i]);
  scan_predeclare(ctx, body);
  scan_list(ctx, body);
}

static void scan_block(trace_ctx_t *ctx, const sv_ast_list_t *body) {
  trace_scope_push(ctx, false);
  scan_body(ctx, body, false);
  trace_scope_pop(ctx);
}

static void scan_function(trace_ctx_t *ctx, const sv_ast_t *fn) {
  trace_scope_push(ctx, true);
  if (fn->str && fn->len && !(fn->flags & FN_ARROW)) trace_declare(ctx, ctx->scope, fn->str, fn->len, trace_unknown());

  for (int i = 0; i < fn->args.count; i++) {
    const sv_ast_t *param = fn->args.items[i];
    trace_bind_pattern(ctx, ctx->scope, param, trace_unknown());
    if (param && param->type == N_ASSIGN_PAT) scan_node(ctx, param->right);
  }

  if (fn->body && fn->body->type == N_BLOCK) scan_body(ctx, &fn->body->args, true);
  else scan_node(ctx, fn->body);

  trace_scope_pop(ctx);
}

static void scan_branch(trace_ctx_t *ctx, const sv_ast_t *cond, const sv_ast_t *then, const sv_ast_t *otherwise) {
  scan_node(ctx, cond);
  trace_value_t v = trace_eval(ctx, cond);
  bool truth;

  if (trace_value_truthy(&v, &truth)) {
    scan_node(ctx, truth ? then : otherwise);
    return;
  }

  scan_node(ctx, then);
  scan_node(ctx, otherwise);
}

static void scan_logical(trace_ctx_t *ctx, const sv_ast_t *node) {
  scan_node(ctx, node->left);
  if (node->op == TOK_LAND || node->op == TOK_LOR) {
    trace_value_t left = trace_eval(ctx, node->left);
    bool truth;
    if (trace_value_truthy(&left, &truth) && truth == (node->op == TOK_LOR)) return;
  }
  scan_node(ctx, node->right);
}

static void scan_for_of(trace_ctx_t *ctx, const sv_ast_t *node) {
  trace_scope_push(ctx, false);
  scan_node(ctx, node->right);

  trace_value_t items = trace_eval(ctx, node->right);
  trace_value_t item = items.kind == TRACE_VALUE_LIST ? items : trace_unknown();
  if (item.kind == TRACE_VALUE_LIST) item.kind = TRACE_VALUE_STRING;

  const sv_ast_t *left = node->left;
  if (left && left->type == N_VAR) {
    bool stable = left->var_kind == SV_VAR_CONST;
    for (int i = 0; i < left->args.count; i++) {
      const sv_ast_t *decl = left->args.items[i];
      if (decl && decl->type == N_VARDECL)
        trace_bind_pattern(ctx, scan_var_scope(ctx, left), decl->left, node->type == N_FOR_OF && stable ? item : trace_unknown());
    }
  }

  scan_node(ctx, node->body);
  trace_scope_pop(ctx);
}

static void scan_try(trace_ctx_t *ctx, const sv_ast_t *node) {
  ctx->try_depth++;
  scan_node(ctx, node->body);
  ctx->try_depth--;

  trace_scope_push(ctx, false);
  trace_bind_pattern(ctx, ctx->scope, node->catch_param, trace_unknown());
  scan_node(ctx, node->catch_body);
  trace_scope_pop(ctx);

  scan_node(ctx, node->finally_body);
}

static void scan_class(trace_ctx_t *ctx, const sv_ast_t *node) {
  scan_node(ctx, node->left);
  for (int i = 0; i < node->args.count; i++) {
    const sv_ast_t *member = node->args.items[i];
    if (!member) continue;
    if (member->type == N_STATIC_BLOCK) scan_block(ctx, &member->args);
    else {
      if (member->flags & FN_COMPUTED) scan_node(ctx, member->left);
      scan_node(ctx, member->right);
    }
  }
}

static void scan_static_source(trace_ctx_t *ctx, const sv_ast_t *source, uint32_t offset) {
  if (source && source->type == N_STRING && source->str) {
    trace_value_t literal = trace_string(ctx, source->str, source->len);
    const char *text;
    if (trace_value_exact(&literal, &text)) scan_push_dep(ctx, TRACE_SINK_STATIC, text, offset, true);
  }
}

static void scan_node(trace_ctx_t *ctx, const sv_ast_t *node) {
  if (!node || ctx->oom) return;

  switch (node->type) {
  case N_FUNC:
  case N_ARROW:
    scan_function(ctx, node);
    return;
  case N_CLASS:
    scan_class(ctx, node);
    return;
  case N_BLOCK:
    scan_block(ctx, &node->args);
    return;
  case N_VAR:
    scan_bind_var(ctx, node);
    for (int i = 0; i < node->args.count; i++) if (node->args.items[i]) scan_node(ctx, node->args.items[i]->right);
    return;
  case N_IF:
    scan_branch(ctx, node->cond, node->left, node->right);
    return;
  case N_TERNARY:
    scan_branch(ctx, node->cond, node->left, node->right);
    return;
  case N_BINARY:
    scan_logical(ctx, node);
    return;
  case N_TRY:
    scan_try(ctx, node);
    return;
  case N_FOR_IN:
  case N_FOR_OF:
  case N_FOR_AWAIT_OF:
    scan_for_of(ctx, node);
    return;
  case N_FOR:
    trace_scope_push(ctx, false);
    scan_children(ctx, node);
    trace_scope_pop(ctx);
    return;
  case N_SWITCH:
    scan_node(ctx, node->cond);
    trace_scope_push(ctx, false);
    for (int i = 0; i < node->args.count; i++) {
      const sv_ast_t *c = node->args.items[i];
      if (!c) continue;
      scan_node(ctx, c->left);
      scan_body(ctx, &c->args, false);
    }
    trace_scope_pop(ctx);
    return;
  case N_IMPORT_DECL:
    scan_static_source(ctx, node->right, node->src_off);
    return;
  case N_EXPORT:
    if (node->flags & EX_FROM) scan_static_source(ctx, node->right, node->src_off);
    scan_node(ctx, node->left);
    return;
  case N_IMPORT:
    if (node->right) scan_sink(ctx, TRACE_SINK_IMPORT, trace_eval(ctx, node->right), node->right, node->src_off);
    scan_children(ctx, node);
    return;
  case N_CALL:
    scan_call_sinks(ctx, node, false);
    scan_children(ctx, node);
    return;
  case N_NEW:
    scan_call_sinks(ctx, node, true);
    scan_children(ctx, node);
    return;
  default:
    scan_children(ctx, node);
    scan_node(ctx, node->catch_param);
    scan_node(ctx, node->catch_body);
    scan_node(ctx, node->finally_body);
    return;
  }
}

static const sv_ast_list_t *scan_program_body(const sv_ast_t *program, bool cjs_wrapped) {
  if (!cjs_wrapped || program->args.count < 1) return &program->args;
  const sv_ast_t *wrapper = program->args.items[0];
  if (wrapper && wrapper->type == N_FUNC && wrapper->body && wrapper->body->type == N_BLOCK) return &wrapper->body->args;
  return &program->args;
}

bool trace_scan_module(trace_ctx_t *ctx, const sv_ast_t *program, bool cjs_wrapped) {
  ctx->scope = NULL;
  ctx->assigned = NULL;
  ctx->try_depth = 0;

  scan_collect_assigned(ctx, program);
  trace_scope_push(ctx, true);
  scan_body(ctx, scan_program_body(program, cjs_wrapped), true);
  trace_scope_pop(ctx);

  HASH_CLEAR(hh, ctx->assigned);
  ctx->scope = NULL;

  return !ctx->oom;
}
