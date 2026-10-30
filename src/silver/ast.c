#include "silver/ast.h"
#include "runtime.h"

sv_ast_t *sv_ast_new(ant_t *js, sv_node_type_t type) {
  sv_ast_t *n = parse_arena_bump(js, sizeof(sv_ast_t));
  if (!n) return NULL;
  memset(n, 0, sizeof(sv_ast_t));
  n->type = type;
  return n;
}

bool sv_ast_list_push(ant_t *js, sv_ast_list_t *list, sv_ast_t *node) {
  if (list->count >= list->cap) {
    int new_cap = list->cap ? list->cap * 2 : 4;
    sv_ast_t **new_items = parse_arena_bump(js, (size_t)new_cap * sizeof(sv_ast_t *));
    if (!new_items) return false;
    if (list->items)
      memcpy(new_items, list->items, (size_t)list->count * sizeof(sv_ast_t *));
    list->items = new_items;
    list->cap = new_cap;
  }
  list->items[list->count++] = node;
  return true;
}

bool sv_ast_can_be_expression_statement(const sv_ast_t *node) {
  if (!node) return false;
  if ((node->type == N_FUNC || node->type == N_CLASS) && (node->flags & FN_PAREN)) return true;

  static const uint8_t expr_stmt_nodes[N__COUNT] = {
    [N_NUMBER] = 1, [N_STRING] = 1, [N_BIGINT] = 1, [N_BOOL] = 1,
    [N_NULL] = 1, [N_UNDEF] = 1, [N_THIS] = 1, [N_GLOBAL_THIS] = 1,
    [N_TEMPLATE] = 1, [N_REGEXP] = 1, [N_IDENT] = 1, [N_BINARY] = 1,
    [N_UNARY] = 1, [N_UPDATE] = 1, [N_ASSIGN] = 1, [N_TERNARY] = 1,
    [N_CALL] = 1, [N_NEW] = 1, [N_MEMBER] = 1, [N_OPTIONAL] = 1,
    [N_ARRAY] = 1, [N_OBJECT] = 1, [N_PROPERTY] = 1, [N_SPREAD] = 1,
    [N_SEQUENCE] = 1, [N_ARROW] = 1, [N_YIELD] = 1, [N_AWAIT] = 1,
    [N_TYPEOF] = 1, [N_DELETE] = 1, [N_VOID] = 1, [N_TAGGED_TEMPLATE] = 1,
    [N_IMPORT] = 1, [N_NEW_TARGET] = 1,
  };

  if (node->type == N_FUNC)
    return !(node->str && !(node->flags & FN_ARROW));
  if ((unsigned)node->type >= N__COUNT) return false;
  return expr_stmt_nodes[node->type] != 0;
}

bool ast_pattern_binds(const sv_ast_t *node, const char *name) {
  if (!node) return false;
  switch (node->type) {
    case N_IDENT:
      return node->len == strlen(name) && memcmp(node->str, name, node->len) == 0;
    case N_ASSIGN: case N_ASSIGN_PAT:
      return ast_pattern_binds(node->left, name);
    case N_REST: case N_SPREAD: case N_PROPERTY:
      return ast_pattern_binds(node->right, name);
    case N_ARRAY: case N_ARRAY_PAT: case N_OBJECT: case N_OBJECT_PAT:
      for (int i = 0; i < node->args.count; i++)
        if (ast_pattern_binds(node->args.items[i], name)) return true;
      return false;
    default: return false;
  }
}

// TODO: move into a ast_contains
bool ast_references_arguments(const sv_ast_t *node) {
  if (!node) return false;
  if (node->type == N_IDENT && node->len == 9 && memcmp(node->str, "arguments", 9) == 0)
    return true;
    
  if (node->type == N_FUNC && !(node->flags & FN_ARROW)) {
    for (int i = 0; i < node->args.count; i++)
      if (ast_references_arguments(node->args.items[i])) return true;
    return false;
  }
  
  if (ast_references_arguments(node->left))            return true;
  if (ast_references_arguments(node->right))           return true;
  if (ast_references_arguments(node->body))            return true;
  if (ast_references_arguments(node->catch_body))      return true;
  if (ast_references_arguments(node->finally_body))    return true;
  for (int i = 0; i < node->args.count; i++)
    if (ast_references_arguments(node->args.items[i])) return true;
    
  return false;
}

bool ast_is_arguments_length(const sv_ast_t *node) {
  return
    node && node->type == N_MEMBER && !(node->flags & 1) &&
    ast_is_ident(node->left, "arguments", 9) &&
    ast_is_ident(node->right, "length", 6);
}

bool ast_arguments_length_only(const sv_ast_t *node, bool write, bool arrow) {
  if (!node) return true;

  switch (node->type) {
    case N_IDENT: return !ast_is_ident(node, "arguments", 9);
    case N_WITH:  return false;
    
    case N_ARROW:
      arrow = true;
      break;
      
    case N_FUNC:
    case N_CLASS:
      if (node->str && node->len == 9 && memcmp(node->str, "arguments", 9) == 0) return false;
      if (node->type == N_FUNC && !(node->flags & FN_ARROW)) return true;
      if (node->flags & FN_ARROW) arrow = true;
      break;
      
    case N_CALL:
      if (ast_is_ident(node->left, "eval", 4)) return false;
      break;
      
    case N_MEMBER:
      if (ast_is_arguments_length(node)) return !write && !arrow;
      return
        ast_arguments_length_only(node->left, false, arrow) &&
        (!(node->flags & 1) || ast_arguments_length_only(node->right, false, arrow));
      
    case N_ASSIGN:
      return
        ast_arguments_length_only(node->left, true, arrow) &&
        ast_arguments_length_only(node->right, false, arrow);
      
    case N_FOR_IN:
    case N_FOR_OF:
    case N_FOR_AWAIT_OF:
      return
        ast_arguments_length_only(node->left, true, arrow) &&
        ast_arguments_length_only(node->right, write, arrow) &&
        ast_arguments_length_only(node->body, write, arrow);
      
    case N_UPDATE:
    case N_DELETE:
      write = true;
      break;
      
    default: break;
  }

  const sv_ast_t *children[] = {
    node->left, node->right, node->cond, node->body, node->catch_param,
    node->catch_body, node->finally_body, node->init, node->update
  };
  
  for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); i++)
    if (!ast_arguments_length_only(children[i], write, arrow)) return false;
  for (int i = 0; i < node->args.count; i++)
    if (!ast_arguments_length_only(node->args.items[i], write, arrow)) return false;

  return true;
}

bool ast_contains_direct_suspend(const sv_ast_t *node, const sv_ast_t **out_offender) {
  if (!node) return false;

  if (node->type == N_AWAIT || node->type == N_YIELD || node->type == N_FOR_AWAIT_OF) {
    if (out_offender) *out_offender = node;
    return true;
  }

  if (node->type == N_FUNC || node->type == N_ARROW) {
    bool plain_arrow = (node->flags & FN_ARROW) && !(node->flags & FN_ASYNC);
    if (!plain_arrow) return false;
  }

  if (ast_contains_direct_suspend(node->left, out_offender))         return true;
  if (ast_contains_direct_suspend(node->right, out_offender))        return true;
  if (ast_contains_direct_suspend(node->cond, out_offender))         return true;
  if (ast_contains_direct_suspend(node->body, out_offender))         return true;
  if (ast_contains_direct_suspend(node->catch_param, out_offender))  return true;
  if (ast_contains_direct_suspend(node->catch_body, out_offender))   return true;
  if (ast_contains_direct_suspend(node->finally_body, out_offender)) return true;
  if (ast_contains_direct_suspend(node->init, out_offender))         return true;
  if (ast_contains_direct_suspend(node->update, out_offender))       return true;

  for (int i = 0; i < node->args.count; i++)
    if (ast_contains_direct_suspend(node->args.items[i], out_offender)) return true;

  return false;
}

bool ast_contains_own_yield(const sv_ast_t *node, const sv_ast_t **out_offender) {
  if (!node) return false;

  if (node->type == N_YIELD) {
    if (out_offender) *out_offender = node;
    return true;
  }

  if (node->type == N_FUNC || node->type == N_ARROW)            return false;
  if (ast_contains_own_yield(node->left, out_offender))         return true;
  if (ast_contains_own_yield(node->right, out_offender))        return true;
  if (ast_contains_own_yield(node->cond, out_offender))         return true;
  if (ast_contains_own_yield(node->body, out_offender))         return true;
  if (ast_contains_own_yield(node->catch_param, out_offender))  return true;
  if (ast_contains_own_yield(node->catch_body, out_offender))   return true;
  if (ast_contains_own_yield(node->finally_body, out_offender)) return true;
  if (ast_contains_own_yield(node->init, out_offender))         return true;
  if (ast_contains_own_yield(node->update, out_offender))       return true;

  for (int i = 0; i < node->args.count; i++)
    if (ast_contains_own_yield(node->args.items[i], out_offender)) return true;

  return false;
}

bool ast_contains_lexical_new_target(const sv_ast_t *node) {
  if (!node) return false;
  if (node->type == N_NEW_TARGET) return true;
  if (node->type == N_FUNC && !(node->flags & FN_ARROW)) return false;
  if (node->type == N_CLASS) {
    if (ast_contains_lexical_new_target(node->left)) return true;
    for (int i = 0; i < node->args.count; i++) {
      const sv_ast_t *member = node->args.items[i];
      if (member->type == N_METHOD && (member->flags & FN_COMPUTED) &&
          ast_contains_lexical_new_target(member->left)) return true;
    }
    return false;
  }
  if (ast_contains_lexical_new_target(node->left) ||
      ast_contains_lexical_new_target(node->right) ||
      ast_contains_lexical_new_target(node->cond) ||
      ast_contains_lexical_new_target(node->body) ||
      ast_contains_lexical_new_target(node->catch_body) ||
      ast_contains_lexical_new_target(node->finally_body) ||
      ast_contains_lexical_new_target(node->catch_param) ||
      ast_contains_lexical_new_target(node->init) ||
      ast_contains_lexical_new_target(node->update)) return true;
  for (int i = 0; i < node->args.count; i++)
    if (ast_contains_lexical_new_target(node->args.items[i])) return true;
  return false;
}

bool ast_contains_direct_eval(const sv_ast_t *node) {
  if (!node || (node->type == N_FUNC && !(node->flags & FN_ARROW))) return false;
  if (
    node->type == N_CALL && node->left && node->left->type == N_IDENT &&
    node->left->len == 4 && memcmp(node->left->str, "eval", 4) == 0
  ) return true;

  const sv_ast_t *children[] = {
    node->left, node->right, node->cond, node->body, node->catch_body,
    node->finally_body, node->catch_param, node->init, node->update
  };
  
  for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); i++)
    if (ast_contains_direct_eval(children[i])) return true;
  for (int i = 0; i < node->args.count; i++)
    if (ast_contains_direct_eval(node->args.items[i])) return true;
  
  return false;
}

static bool ast_closure_captures_name_impl(
  const sv_ast_t *node, const char *name, uint32_t len, bool in_closure
) {
  if (!node) return false;
  switch (node->type) {
    case N_FUNC: case N_ARROW: case N_CLASS: case N_METHOD: case N_STATIC_BLOCK:
      in_closure = true;
      break;
    case N_IDENT:
      if (in_closure && node->len == len && memcmp(node->str, name, len) == 0) return true;
      break;
    default: break;
  }
  if (
    node->type == N_CALL && node->left && node->left->type == N_IDENT &&
    node->left->len == 4 && memcmp(node->left->str, "eval", 4) == 0
  ) return true;

  const sv_ast_t *children[] = {
    node->left, node->right, node->cond, node->body, node->catch_body,
    node->finally_body, node->catch_param, node->init, node->update
  };

  for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); i++)
    if (ast_closure_captures_name_impl(children[i], name, len, in_closure)) return true;
  for (int i = 0; i < node->args.count; i++)
    if (ast_closure_captures_name_impl(node->args.items[i], name, len, in_closure)) return true;

  return false;
}

bool ast_closure_captures_name(const sv_ast_t *node, const char *name, uint32_t len) {
  return ast_closure_captures_name_impl(node, name, len, false);
}

bool ast_mentions_name(const sv_ast_t *node, const char *name, uint32_t len) {
  return ast_closure_captures_name_impl(node, name, len, true);
}

static bool ast_references_new_target_impl(const sv_ast_t *node, bool in_arrow) {
  if (!node) return false;
  if (node->type == N_NEW_TARGET) return true;
  if (node->type == N_FUNC && !(node->flags & FN_ARROW)) return false;

  bool lexical_arrow = in_arrow ||
    (node->type == N_FUNC && (node->flags & FN_ARROW));
  if (
    lexical_arrow && node->type == N_CALL &&
    node->left && node->left->type == N_IDENT &&
    node->left->len == 5 && memcmp(node->left->str, "super", 5) == 0
  ) return true;

  if (ast_references_new_target_impl(node->left, lexical_arrow))         return true;
  if (ast_references_new_target_impl(node->right, lexical_arrow))        return true;
  if (ast_references_new_target_impl(node->cond, lexical_arrow))         return true;
  if (ast_references_new_target_impl(node->body, lexical_arrow))         return true;
  if (ast_references_new_target_impl(node->catch_body, lexical_arrow))   return true;
  if (ast_references_new_target_impl(node->finally_body, lexical_arrow)) return true;
  if (ast_references_new_target_impl(node->catch_param, lexical_arrow))  return true;
  if (ast_references_new_target_impl(node->init, lexical_arrow))         return true;
  if (ast_references_new_target_impl(node->update, lexical_arrow))       return true;
  
  for (int i = 0; i < node->args.count; i++)
    if (ast_references_new_target_impl(node->args.items[i], lexical_arrow)) return true;
    
  return false;
}

bool ast_references_new_target(const sv_ast_t *node) {
  return ast_references_new_target_impl(node, false);
}
