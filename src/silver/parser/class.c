#include "parser_internal.h"

sv_ast_t *parse_class(sv_parser_t *p) {
  sv_ast_t *cls = mk(N_CLASS);

  sv_lexer_next(&p->lx);
  if (binding_ident_here(p) && !(p->lx.st.tlen == 7 && memcmp(tok_str(p), "extends", 7) == 0)) {
    cls->str = tok_ident_str(p, &cls->len);
    if (sv_strict_forbidden_binding_ident(cls->str, cls->len)) {
      SV_MKERR_TYPED(p->js, JS_ERR_SYNTAX, "Unexpected strict mode reserved word");
      return cls;
    }
    p->lx.st.consumed = 1;
  }

  if (sv_lexer_next(&p->lx) == TOK_IDENTIFIER && p->lx.st.tlen == 7 && memcmp(tok_str(p), "extends", 7) == 0) {
    p->lx.st.consumed = 1;
    cls->left = parse_assign(p);
  }

  expect(p, TOK_LBRACE);
  while (sv_lexer_next(&p->lx) != TOK_RBRACE && p->lx.st.tok != TOK_EOF) {
    if (p->lx.st.tok == TOK_SEMICOLON) { p->lx.st.consumed = 1; continue; }

    uint8_t flags = 0;

    if (
      p->lx.st.tok == TOK_STATIC &&
      sv_lexer_lookahead(&p->lx) != TOK_LPAREN &&
      sv_lexer_lookahead(&p->lx) != TOK_ASSIGN &&
      sv_lexer_lookahead(&p->lx) != TOK_SEMICOLON &&
      sv_lexer_lookahead(&p->lx) != TOK_RBRACE
    ) {
      flags |= FN_STATIC;
      p->lx.st.consumed = 1;
      sv_lexer_next(&p->lx);

      if (p->lx.st.tok == TOK_LBRACE) {
        bool saved_strict = p->lx.strict;
        p->lx.strict = true;
        sv_ast_t *block = parse_block(p, false);
        p->lx.strict = saved_strict;
        block->type = N_STATIC_BLOCK;
        block->flags = FN_STATIC | FN_CLASS_BODY;
        sv_ast_list_push(p->js, &cls->args, block);
        continue;
      }
    }

    sv_ast_t *method = mk(N_METHOD);
    uint32_t method_src_off = (uint32_t)p->lx.st.toff;

    if (p->lx.st.tok == TOK_ASYNC && sv_lexer_lookahead(&p->lx) != TOK_LPAREN) {
      flags |= FN_ASYNC;
      p->lx.st.consumed = 1;
      sv_lexer_next(&p->lx);
    }

    if (p->lx.st.tok == TOK_MUL) {
      flags |= FN_GENERATOR;
      p->lx.st.consumed = 1;
      sv_lexer_next(&p->lx);
    }

    if ((p->lx.st.tlen == 3 && memcmp(tok_str(p), "get", 3) == 0) ||
        (p->lx.st.tlen == 3 && memcmp(tok_str(p), "set", 3) == 0)) {
      uint8_t la = sv_lexer_lookahead(&p->lx);
      if (la != TOK_LPAREN && la != TOK_ASSIGN && la != TOK_SEMICOLON && la != TOK_RBRACE) {
        flags |= (tok_str(p)[0] == 'g') ? FN_GETTER : FN_SETTER;
        p->lx.st.consumed = 1;
        sv_lexer_next(&p->lx);
      }
    }

    if (p->lx.st.tok == TOK_LBRACKET) {
      p->lx.st.consumed = 1;
      method->left = parse_assign(p);
      expect(p, TOK_RBRACKET);
      flags |= FN_COMPUTED;
    } else if (p->lx.st.tok == TOK_HASH) {
      p->lx.st.consumed = 1;
      sv_lexer_next(&p->lx);
      if (!is_private_ident_like_tok(p->lx.st.tok)) {
        SV_MKERR_TYPED(p->js, JS_ERR_SYNTAX, "private field name expected");
        return mk(N_EMPTY);
      }
      method->left = mk_private_ident_from_tok(p);
      p->lx.st.consumed = 1;
    } else {
      method->left = mk_ident_from_tok(p);
      p->lx.st.consumed = 1;
    }

    method->flags = flags;

    if (!(flags & FN_STATIC) && (flags & FN_GENERATOR) &&
        method->left && method->left->type == N_IDENT &&
        method->left->len == 11 &&
        memcmp(method->left->str, "constructor", 11) == 0) {
      SV_MKERR_TYPED(p->js, JS_ERR_SYNTAX, "Class constructor may not be a generator");
      return cls;
    }

    if (sv_lexer_next(&p->lx) == TOK_LPAREN) {
      bool saved_strict = p->lx.strict;
      p->lx.strict = true;
      method->right = parse_func(p, (flags & FN_ASYNC) != 0);
      p->lx.strict = saved_strict;
      if (!validate_accessor_params(p, method->right, method->flags)) return cls;
      method->right->flags |= (flags & (FN_ASYNC | FN_GENERATOR)) | FN_METHOD | FN_CLASS_BODY;
      method->right->src_off = method_src_off;
    } else if (p->lx.st.tok == TOK_ASSIGN) {
      p->lx.st.consumed = 1;
      method->right = parse_assign(p);
      consume_semicolon(p);
    } else {
      method->right = mk(N_UNDEF);
      consume_semicolon(p);
    }

    sv_ast_list_push(p->js, &cls->args, method);
  }
  
  expect(p, TOK_RBRACE);
  cls->src_end = (uint32_t)(p->lx.st.toff + p->lx.st.tlen);
  
  return cls;
}
