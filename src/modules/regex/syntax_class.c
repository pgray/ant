#include "syntax_internal.h"

static bool rx_parse_class_escape(rx_t *p, uint32_t *value, bool *is_class) {
  *is_class = false;
  if (rx_cur(p) != '\\') {
    *value = rx_cur(p);
    rx_advance(p);
    return true;
  }
  uint32_t next = rx_next(p);
  if (next == 'b') {
    *value = '\b';
    rx_advance2(p);
    return true;
  }
  if (next == '-' && p->unicode) {
    *value = '-';
    rx_advance2(p);
    return true;
  }
  if (next == RX_END) return rx_fail(p, RX_ERR_ESCAPE_AT_END);
  if (rx_is_class_escape_char(next)) {
    *is_class = true;
    rx_advance2(p);
    return true;
  }
  if ((next == 'p' || next == 'P') && p->unicode) {
    *is_class = true;
    rx_advance2(p);
    bool strings;
    if (!rx_parse_property(p, next == 'P', &strings)) return rx_fail(p, RX_ERR_INVALID_CLASS_PROPERTY_NAME);
    return true;
  }
  *value = rx_parse_character_escape(p, true);
  return !p->err;
}

bool rx_parse_class_ranges(rx_t *p) {
  rx_advance(p);
  if (rx_cur(p) == '^') rx_advance(p);
  if (rx_cur(p) == ']') {
    rx_advance(p);
    return true;
  }
  while (rx_has_more(p) && rx_cur(p) != ']') {
    uint32_t first, second;
    bool first_class, second_class;
    if (!rx_parse_class_escape(p, &first, &first_class)) return false;
    if (rx_cur(p) != '-') continue;
    rx_advance(p);
    if (rx_cur(p) == RX_END || rx_cur(p) == ']') break;
    if (!rx_parse_class_escape(p, &second, &second_class)) return false;
    if (first_class || second_class) {
      if (p->unicode) return rx_fail(p, RX_ERR_INVALID_CHARACTER_CLASS);
      continue;
    }
    if (first > second) return rx_fail(p, RX_ERR_OUT_OF_ORDER_CHARACTER_CLASS);
  }
  if (!rx_has_more(p)) return rx_fail(p, RX_ERR_UNTERMINATED_CHARACTER_CLASS);
  rx_advance(p);
  return true;
}

static uint32_t rx_parse_class_set_character(rx_t *p) {
  uint32_t c = rx_cur(p);
  if (c == '\\') {
    uint32_t next = rx_next(p);
    if (next == 'b') {
      rx_advance2(p);
      return '\b';
    }
    if (next == RX_END) {
      rx_fail(p, RX_ERR_ESCAPE_AT_END);
      return 0;
    }
    return rx_parse_character_escape(p, true);
  }
  if (rx_is_class_set_syntax_char(c)) {
    rx_fail(p, RX_ERR_INVALID_CHARACTER_IN_CLASS);
    return 0;
  }
  if (rx_is_class_set_double_punctuator(p, c)) {
    rx_fail(p, RX_ERR_INVALID_CLASS_SET_OPERATION);
    return 0;
  }
  rx_advance(p);
  return c;
}

static bool rx_parse_class_strings(rx_t *p, bool *strings) {
  rx_advance2(p);
  if (rx_cur(p) != '{') return rx_fail(p, RX_ERR_INVALID_ESCAPE);
  rx_advance(p);
  size_t length = 0;
  *strings = false;
  while (rx_has_more(p) && rx_cur(p) != '}') {
    if (rx_cur(p) == '|') {
      if (length != 1) *strings = true;
      length = 0;
      rx_advance(p);
      continue;
    }
    rx_parse_class_set_character(p);
    if (p->err) return false;
    length++;
  }
  if (length != 1) *strings = true;
  rx_advance(p);
  return true;
}

static bool rx_parse_set_operand(rx_t *p, rx_operand_t *op) {
  *op = (rx_operand_t){ .type = RX_OPERAND_CHAR };
  if (rx_cur(p) == '\\') {
    uint32_t next = rx_next(p);
    if (next == 'q') {
      op->type = RX_OPERAND_STRINGS;
      return rx_parse_class_strings(p, &op->strings);
    }
    if (rx_is_class_escape_char(next)) {
      op->type = RX_OPERAND_ESCAPE;
      rx_advance2(p);
      return true;
    }
    if (next == 'p' || next == 'P') {
      op->type = RX_OPERAND_ESCAPE;
      rx_advance2(p);
      if (!rx_parse_property(p, next == 'P', &op->strings)) return rx_fail(p, RX_ERR_INVALID_CLASS_PROPERTY_NAME);
      return true;
    }
  }
  op->value = rx_parse_class_set_character(p);
  return !p->err;
}

bool rx_parse_class_sets(rx_t *p) {
  size_t depth = 0;
  rx_operand_t op;
  rx_class_frame_t *f = NULL;

open:
  rx_advance(p);
  {
    bool negated = rx_cur(p) == '^';
    if (negated) rx_advance(p);
    if (rx_cur(p) == ']') {
      rx_advance(p);
      op = (rx_operand_t){ .type = RX_OPERAND_NESTED };
      goto deliver;
    }
    if (!rx_reserve(p, (void **)&p->frames, &p->frames_cap, depth + 1, sizeof(*p->frames))) return false;
    p->frames[depth++] = (rx_class_frame_t){ .negated = negated, .next = RX_NEXT_FIRST };
    f = &p->frames[depth - 1];
  }

operand:
  if (rx_cur(p) == '[') goto open;
  if (!rx_parse_set_operand(p, &op)) return false;

handle:
  switch (f->next) {
    case RX_NEXT_FIRST:
      f->strings = op.strings;
      if (rx_cur(p) == '-' && rx_next(p) == '-') {
          if (f->negated && f->strings) return rx_fail(p, RX_ERR_NEGATED_CLASS_WITH_STRINGS);
        goto subtraction;
      }
      if (rx_cur(p) == '&' && rx_next(p) == '&') {
          goto intersection;
      }
      f->last = op.type;
      f->from = op.value;
      goto set_union;
    case RX_NEXT_UNION:
      f->strings |= op.strings;
      f->last = op.type;
      f->from = op.value;
      goto set_union;
    case RX_NEXT_RANGE:
      if (op.type != RX_OPERAND_CHAR) return rx_fail(p, RX_ERR_INVALID_CHARACTER_CLASS);
      if (f->from > op.value) return rx_fail(p, RX_ERR_OUT_OF_ORDER_CHARACTER_CLASS);
      f->last = RX_OPERAND_RANGE;
      goto set_union;
    case RX_NEXT_INTERSECTION:
      if (!op.strings) f->strings = false;
      goto intersection;
    default:
      goto subtraction;
  }

set_union:
  if (!rx_has_more(p) || rx_cur(p) == ']') goto close;
  if (rx_cur(p) == '-') {
    if (rx_next(p) == '-') return rx_fail(p, RX_ERR_INVALID_CLASS_SET_OPERATION);
    rx_advance(p);
    if (!rx_has_more(p)) goto close;
    if (f->last != RX_OPERAND_CHAR) return rx_fail(p, RX_ERR_INVALID_CHARACTER_CLASS);
    f->next = RX_NEXT_RANGE;
    goto operand;
  }
  if (rx_cur(p) == '&' && rx_next(p) == '&') return rx_fail(p, RX_ERR_INVALID_CLASS_SET_OPERATION);
  f->next = RX_NEXT_UNION;
  goto operand;

intersection:
  if (!rx_has_more(p) || rx_cur(p) == ']') goto close;
  if (rx_cur(p) != '&' || rx_next(p) != '&') return rx_fail(p, RX_ERR_INVALID_CLASS_SET_OPERATION);
  rx_advance2(p);
  if (rx_cur(p) == '&') return rx_fail(p, RX_ERR_INVALID_CHARACTER_IN_CLASS);
  f->next = RX_NEXT_INTERSECTION;
  goto operand;

subtraction:
  if (!rx_has_more(p) || rx_cur(p) == ']') goto close;
  if (rx_cur(p) != '-' || rx_next(p) != '-') return rx_fail(p, RX_ERR_INVALID_CLASS_SET_OPERATION);
  rx_advance2(p);
  f->next = RX_NEXT_SUBTRACTION;
  goto operand;

close:
  if (!rx_has_more(p)) return rx_fail(p, RX_ERR_UNTERMINATED_CHARACTER_CLASS);
  rx_advance(p);
  if (f->negated && f->strings) return rx_fail(p, RX_ERR_NEGATED_CLASS_WITH_STRINGS);
  op = (rx_operand_t){ .type = RX_OPERAND_NESTED, .strings = f->strings };
  depth--;
  f = depth ? &p->frames[depth - 1] : NULL;

deliver:
  if (!depth) return true;
  op.type = RX_OPERAND_NESTED;
  goto handle;
}
