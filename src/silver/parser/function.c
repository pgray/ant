#include "parser_internal.h"

sv_ast_t *parse_arrow_body(sv_parser_t *p, bool is_async) {
  bool saved_async = p->in_async;
  p->in_async = is_async;
  bool saved_no_in = p->no_in;
  if (sv_lexer_next(&p->lx) == TOK_LBRACE) p->no_in = false;
  sv_ast_t *body = p->lx.st.tok == TOK_LBRACE ? parse_block(p, true) : parse_assign(p);
  p->no_in = saved_no_in;
  p->in_async = saved_async;
  return body;
}

void push_arrow_params_from_expr(sv_parser_t *p, sv_ast_t *fn, sv_ast_t *expr) {
  if (!fn || !expr) return;
  if (expr->type == N_SEQUENCE) {
    push_arrow_params_from_expr(p, fn, expr->left);
    push_arrow_params_from_expr(p, fn, expr->right);
    return;
  }
  if (expr->type == N_ASSIGN && expr->op == TOK_ASSIGN) {
    sv_ast_t *def = mk_plain(N_ASSIGN_PAT);
    def->left = expr->left;
    def->right = expr->right;
    def->src_off = expr->src_off;
    sv_ast_list_push(p->js, &fn->args, def);
    return;
  }
  if (expr->type == N_SPREAD) {
    sv_ast_t *rest = mk_plain(N_REST);
    rest->right = expr->right;
    rest->src_off = expr->src_off;
    sv_ast_list_push(p->js, &fn->args, rest);
    return;
  }
  sv_ast_list_push(p->js, &fn->args, expr);
}

sv_ast_t *try_parse_async_arrow(sv_parser_t *p) {
  uint8_t la = sv_lexer_lookahead(&p->lx);
  uint32_t async_off = (uint32_t)p->lx.st.toff;

  if (la == TOK_LPAREN) {
    sv_lexer_state_t saved;
    sv_lexer_save_state(&p->lx, &saved);
    sv_lexer_next(&p->lx); p->lx.st.consumed = 1;
    if (sv_lexer_next(&p->lx) == TOK_RPAREN) {
      p->lx.st.consumed = 1;
      if (sv_lexer_lookahead(&p->lx) == TOK_ARROW) {
        sv_lexer_next(&p->lx); p->lx.st.consumed = 1;
        sv_ast_t *fn = mk(N_FUNC);
        fn->flags = FN_ARROW | FN_ASYNC;
        fn->body = parse_arrow_body(p, true);
        fn->src_off = async_off;
        fn->src_end = node_src_end(p, fn->body);
        return finish_arrow(p, fn);
      }
      sv_lexer_restore_state(&p->lx, &saved);
      return NULL;
    }
    sv_lexer_restore_state(&p->lx, &saved);
    sv_lexer_next(&p->lx); p->lx.st.consumed = 1;
    bool saved_no_in = p->no_in;
    p->no_in = false;
    sv_ast_t *expr = parse_paren_list(p, true);
    p->no_in = saved_no_in;
    expect(p, TOK_RPAREN);
    if (sv_lexer_lookahead(&p->lx) == TOK_ARROW) {
      sv_lexer_next(&p->lx); p->lx.st.consumed = 1;
      sv_ast_t *fn = mk(N_FUNC);
      fn->flags = FN_ARROW | FN_ASYNC;
      push_arrow_params_from_expr(p, fn, expr);
      fn->body = parse_arrow_body(p, true);
      fn->src_off = async_off;
      fn->src_end = node_src_end(p, fn->body);
      return finish_arrow(p, fn);
    }
    sv_lexer_restore_state(&p->lx, &saved);
    return NULL;
  }

  if (la == TOK_IDENTIFIER) {
    sv_lexer_state_t saved;
    sv_lexer_save_state(&p->lx, &saved);
    sv_lexer_next(&p->lx); p->lx.st.consumed = 1;
    sv_ast_t *id = mk_ident_from_tok(p);
    if (sv_lexer_lookahead(&p->lx) == TOK_ARROW) {
      sv_lexer_next(&p->lx); p->lx.st.consumed = 1;
      sv_ast_t *fn = mk(N_FUNC);
      fn->flags = FN_ARROW | FN_ASYNC;
      sv_ast_list_push(p->js, &fn->args, id);
      fn->body = parse_arrow_body(p, true);
      fn->src_off = async_off;
      fn->src_end = node_src_end(p, fn->body);
      return finish_arrow(p, fn);
    }
    sv_lexer_restore_state(&p->lx, &saved);
    return NULL;
  }

  return NULL;
}

void parse_formal_params(sv_parser_t *p, sv_ast_t *fn, uint8_t close) {
  while (sv_lexer_next(&p->lx) != close && p->lx.st.tok != TOK_EOF) {
    if (p->lx.st.tok == TOK_REST) {
      p->lx.st.consumed = 1;
      sv_ast_t *rest = mk(N_REST);
      rest->right = parse_binding_pattern(p);
      sv_ast_list_push(p->js, &fn->args, rest);
      break;
    }
    sv_ast_t *param = parse_binding_pattern(p);
    if (sv_lexer_next(&p->lx) == TOK_ASSIGN) {
      p->lx.st.consumed = 1;
      sv_ast_t *def = mk(N_ASSIGN_PAT);
      def->left = param;
      def->right = parse_assign(p);
      param = def;
    }
    sv_ast_list_push(p->js, &fn->args, param);
    if (sv_lexer_next(&p->lx) == TOK_COMMA) {
      p->lx.st.consumed = 1;
      if (sv_lexer_next(&p->lx) == close) break;
    } else break;
  }
}

enum {
  SCOPE_ARGUMENTS  = 1 << 0,
  SCOPE_NEW_TARGET = 1 << 1,
  SCOPE_EVAL       = 1 << 2,
  SCOPE_YIELD      = 1 << 3,
};

static unsigned ast_scope_facts(const sv_ast_t *node, unsigned want, bool in_arrow) {
  if (!node || !want) return 0;
  unsigned found = 0;

  switch (node->type) {
    case N_IDENT:
      if (node->len == 9 && memcmp(node->str, "arguments", 9) == 0) found |= SCOPE_ARGUMENTS;
      break;
    case N_NEW_TARGET:
      found |= SCOPE_NEW_TARGET;
      break;
    case N_YIELD:
      found |= SCOPE_YIELD;
      break;
    case N_CALL:
      if (ast_is_ident(node->left, "eval", 4)) found |= SCOPE_EVAL;
      if (in_arrow && ast_is_ident(node->left, "super", 5)) found |= SCOPE_NEW_TARGET;
      break;
    case N_FUNC:
      if (!(node->flags & FN_ARROW)) {
        if (!(want & SCOPE_ARGUMENTS)) return 0;
        for (int i = 0; i < node->args.count && !found; i++)
          found |= ast_scope_facts(node->args.items[i], SCOPE_ARGUMENTS, in_arrow);
        return found;
      }
      in_arrow = true;
      want &= ~SCOPE_YIELD;
      break;
    case N_ARROW:
      want &= ~SCOPE_YIELD;
      break;
    default:
      break;
  }

  found &= want;
  if (found == want) return found;
  const sv_ast_t *every[] = { node->left, node->right, node->body, node->catch_body, node->finally_body };
  const sv_ast_t *no_args[] = { node->cond, node->catch_param, node->init, node->update };
  for (size_t i = 0; i < sizeof(every) / sizeof(every[0]) && found != want; i++)
    found |= ast_scope_facts(every[i], want & ~found, in_arrow);
  for (size_t i = 0; i < sizeof(no_args) / sizeof(no_args[0]) && found != want; i++)
    found |= ast_scope_facts(no_args[i], want & ~found & ~SCOPE_ARGUMENTS, in_arrow);
  for (int i = 0; i < node->args.count && found != want; i++)
    found |= ast_scope_facts(node->args.items[i], want & ~found, in_arrow);
  return found;
}

static constexpr unsigned SCOPE_ALL = SCOPE_ARGUMENTS | SCOPE_NEW_TARGET | SCOPE_EVAL | SCOPE_YIELD;
#define SCOPE_FACTS_FLAGS(facts) \
  (FN_SCOPE_FACTS | ((facts) & SCOPE_EVAL ? FN_DIRECT_EVAL : 0) | ((facts) & SCOPE_YIELD ? FN_OWN_YIELD : 0))

enum { WORD_ARGUMENTS, WORD_TARGET, WORD_SUPER, WORD_EVAL, WORD_YIELD, WORD_ESCAPE };
static const unsigned scope_word_facts[] = {
  [WORD_ARGUMENTS] = SCOPE_ARGUMENTS, [WORD_TARGET] = SCOPE_NEW_TARGET, [WORD_SUPER] = SCOPE_NEW_TARGET,
  [WORD_EVAL] = SCOPE_EVAL, [WORD_YIELD] = SCOPE_YIELD, [WORD_ESCAPE] = SCOPE_ALL,
};

static const uint8_t scope_word_lead[256] = {
  ['\\'] = 1, ['a'] = 1, ['t'] = 1, ['s'] = 1, ['e'] = 1, ['y'] = 1,
};

static int scope_word_at(const char *text, size_t left) {
#define SPELLS(word) (left >= sizeof(word) - 1 && text[1] == word[1] && memcmp(text, word, sizeof(word) - 1) == 0)
  switch (*text) {
    case '\\': return WORD_ESCAPE;
    case 'a': return SPELLS("arguments") ? WORD_ARGUMENTS : -1;
    case 't': return SPELLS("target") ? WORD_TARGET : -1;
    case 's': return SPELLS("super") ? WORD_SUPER : -1;
    case 'e': return SPELLS("eval") ? WORD_EVAL : -1;
    case 'y': return SPELLS("yield") ? WORD_YIELD : -1;
    default: return -1;
  }
#undef SPELLS
}

static bool index_scope_words(sv_parser_t *p, ant_offset_t to) {
  if ((uint64_t)p->lx.clen >= (UINT64_C(1) << 29) || to > p->lx.clen) return false;
  const uint8_t *code = (const uint8_t *)p->lx.code;
  for (ant_offset_t i = p->scope_words_len; i < to; i++) {
    if (!scope_word_lead[code[i]]) continue;
    int word = scope_word_at(p->lx.code + i, (size_t)(p->lx.clen - i));
    if (word < 0) continue;
    if (p->scope_word_count == p->scope_word_cap) {
      uint32_t cap = p->scope_word_cap ? p->scope_word_cap * 2 : 64;
      uint32_t *grown = parse_arena_bump(p->js, cap * sizeof(*grown));
      if (!grown) return false;
      if (p->scope_word_count) memcpy(grown, p->scope_words, p->scope_word_count * sizeof(*grown));
      p->scope_words = grown;
      p->scope_word_cap = cap;
    }
    p->scope_words[p->scope_word_count++] = (uint32_t)i << 3 | (uint32_t)word;
  }
  p->scope_words_len = to;
  return true;
}

static unsigned scope_facts_in_range(sv_parser_t *p, uint32_t from, uint32_t to) {
  if (to > p->scope_words_len && !index_scope_words(p, to)) return SCOPE_ALL;
  uint32_t lo = 0, hi = p->scope_word_count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if ((p->scope_words[mid] >> 3) < from) lo = mid + 1;
    else hi = mid;
  }
  unsigned may = 0;
  for (uint32_t i = lo; i < p->scope_word_count && (p->scope_words[i] >> 3) < to && may != SCOPE_ALL; i++)
    may |= scope_word_facts[p->scope_words[i] & 7];
  return may;
}

void finish_func(sv_parser_t *p, sv_ast_t *fn) {
  if (fn->flags & FN_ARROW) return;
  unsigned want = p && fn->src_end > fn->src_off ? scope_facts_in_range(p, fn->src_off, fn->src_end) : SCOPE_ALL;
  unsigned body = ast_scope_facts(fn->body, want, false);
  unsigned params = 0;
  for (int i = 0; i < fn->args.count && params != want; i++)
    params |= ast_scope_facts(fn->args.items[i], want & ~params, false);
  unsigned facts = body | params;

  if (body & SCOPE_ARGUMENTS) fn->flags |= FN_USES_ARGS;
  bool length_only = (facts & SCOPE_ARGUMENTS) && ast_arguments_length_only(fn->body, false, false);
  for (int i = 0; length_only && i < fn->args.count; i++)
    length_only = ast_arguments_length_only(fn->args.items[i], false, false);
  if (length_only) fn->flags |= FN_ARGS_LENGTH_ONLY;
  if (facts & (SCOPE_NEW_TARGET | SCOPE_EVAL)) fn->flags |= FN_USES_NEW_TARGET;

  fn->flags |= SCOPE_FACTS_FLAGS(facts);
}

unsigned scope_facts_eval_yield(sv_parser_t *p, const sv_ast_list_t *list, const sv_ast_t *body, uint32_t from, uint32_t to) {
  unsigned want = (p && to > from ? scope_facts_in_range(p, from, to) : SCOPE_ALL) & (SCOPE_EVAL | SCOPE_YIELD);
  unsigned found = body ? ast_scope_facts(body, want, false) : 0;
  for (int i = 0; list && i < list->count && found != want; i++)
    found |= ast_scope_facts(list->items[i], want & ~found, false);
  return SCOPE_FACTS_FLAGS(found);
}

sv_ast_t *finish_arrow(sv_parser_t *p, sv_ast_t *fn) {
  fn->flags |= scope_facts_eval_yield(p, &fn->args, fn->body, fn->src_off, fn->src_end);
  return fn;
}

sv_ast_t *parse_func(sv_parser_t *p, bool is_async) {
  sv_ast_t *fn = mk(N_FUNC);
  if (is_async) fn->flags |= FN_ASYNC;

  if (sv_lexer_next(&p->lx) == TOK_MUL) {
    p->lx.st.consumed = 1;
    fn->flags |= FN_GENERATOR;
  }

  sv_lexer_next(&p->lx);
  if (binding_ident_here(p)) {
    fn->str = tok_ident_str(p, &fn->len);
    sv_strict_check_binding_ident(p, fn->str, fn->len);
    p->lx.st.consumed = 1;
  }

  bool saved_async = p->in_async;
  bool saved_no_in = p->no_in;
  p->in_async = is_async;
  p->no_in = false;
  expect(p, TOK_LPAREN);
  parse_formal_params(p, fn, TOK_RPAREN);
  expect(p, TOK_RPAREN);
  fn->body = parse_block(p, true);
  p->no_in = saved_no_in;
  p->in_async = saved_async;

  fn->src_end = (uint32_t)(p->lx.st.toff + p->lx.st.tlen);
  finish_func(p, fn);
  return fn;
}
