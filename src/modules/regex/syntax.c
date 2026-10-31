#include "syntax_internal.h"

static const char *const rx_error_messages[] = {
  NULL,
#define REGEXP_SYNTAX_ERROR(id, msg) msg,
#include "modules/regex_syntax_errors.h"
  NULL,
};

static constexpr int RX_MAX_CAPTURES = (1 << 15) - 1;
static constexpr int RX_INFINITY = INT32_MAX;
static constexpr size_t RX_INLINE_UNITS = 256;

enum {
  RX_GROUP_ROOT,
  RX_GROUP_CAPTURE,
  RX_GROUP_PLAIN,
  RX_GROUP_LOOKAHEAD,
  RX_GROUP_LOOKBEHIND,
};

enum {
  RX_TERM_ATOM,
  RX_TERM_LOOKAHEAD,
  RX_TERM_LOOKBEHIND,
};

static bool rx_parse_open_paren(rx_t *p) {
  uint8_t type = RX_GROUP_CAPTURE;
  bool named = false;
  rx_advance(p);
  if (rx_cur(p) == '?') {
    bool modifiers = false, positive = true;
    unsigned seen = 0;
    do {
      uint32_t c = rx_next(p);
      if (c == '-') {
        rx_advance(p);
        modifiers = true;
        if (!positive) return rx_fail(p, RX_ERR_MULTIPLE_FLAG_DASHES);
        positive = false;
      } else if (c == 'i' || c == 'm' || c == 's') {
        rx_advance(p);
        modifiers = true;
        unsigned bit = c == 'i' ? 1u : c == 'm' ? 2u : 4u;
        if (seen & bit) return rx_fail(p, RX_ERR_REPEATED_FLAG);
        seen |= bit;
      } else if (c == ':') {
        rx_advance2(p);
        modifiers = false;
        type = RX_GROUP_PLAIN;
      } else if (!modifiers && (c == '=' || c == '!')) {
        rx_advance2(p);
        type = RX_GROUP_LOOKAHEAD;
      } else if (!modifiers && c == '<') {
        rx_advance(p);
        if (rx_next(p) == '=' || rx_next(p) == '!') {
          rx_advance2(p);
          type = RX_GROUP_LOOKBEHIND;
        } else {
          named = true;
          p->has_named = true;
          rx_advance(p);
        }
      } else return rx_fail(p, RX_ERR_INVALID_GROUP);
    } while (modifiers);
    if (!positive && !seen) return rx_fail(p, RX_ERR_INVALID_FLAG_GROUP);
  }

  rx_group_t group = { .type = type, .id = ++p->next_id };
  if (type == RX_GROUP_CAPTURE) {
    if (p->captures_started >= RX_MAX_CAPTURES) return rx_fail(p, RX_ERR_TOO_MANY_CAPTURES);
    p->captures_started++;
    if (named) {
      group.named = true;
      if (!rx_parse_group_name(p, &group.name, &group.name_len)) return false;
      group.path = (uint32_t)p->pool_len;
      for (size_t i = 0; i < p->groups_len; i++) {
        if (!rx_pool_push(p, p->groups[i].id) || !rx_pool_push(p, p->groups[i].alt)) return false;
      }
      group.path_len = (uint32_t)(p->pool_len - group.path);
      group.slot = (uint32_t)p->names_len;
      if (!rx_reserve(p, (void **)&p->names, &p->names_cap, p->names_len + 1, sizeof(*p->names))) return false;
      p->names[p->names_len++] = (rx_name_t){ group.name, group.name_len, group.path, group.path_len };
    }
  }
  if (!rx_reserve(p, (void **)&p->groups, &p->groups_cap, p->groups_len + 1, sizeof(*p->groups))) return false;
  p->groups[p->groups_len++] = group;
  return true;
}

static bool rx_close_group(rx_t *p, int *term) {
  rx_group_t group = p->groups[--p->groups_len];
  *term = group.type == RX_GROUP_LOOKAHEAD ? RX_TERM_LOOKAHEAD
    : group.type == RX_GROUP_LOOKBEHIND ? RX_TERM_LOOKBEHIND
    : RX_TERM_ATOM;
  if (!group.named) return true;
  const rx_name_t *entry = &p->names[group.slot];
  for (size_t i = 0; i < p->names_len; i++) {
    const rx_name_t *other = &p->names[i];
    if (i == group.slot || !rx_names_equal(p, other->name, other->name_len, entry->name, entry->name_len)) continue;
    if (!rx_paths_exclusive(p, other, entry)) return rx_fail(p, RX_ERR_DUPLICATE_CAPTURE_GROUP_NAME);
  }
  return true;
}

static bool rx_parse_backref_index(rx_t *p) {
  size_t start = p->pos;
  int value = (int)(rx_next(p) - '0');
  rx_advance2(p);
  while (rx_is_digit(rx_cur(p))) {
    value = value * 10 + (int)(rx_cur(p) - '0');
    if (value > RX_MAX_CAPTURES) {
      p->pos = start;
      return false;
    }
    rx_advance(p);
  }
  if (value > p->captures_started) {
    if (!p->scanned) rx_scan_captures(p, false);
    if (value > p->capture_count) {
      p->pos = start;
      return false;
    }
  }
  return true;
}

static bool rx_parse_named_backref(rx_t *p) {
  if (rx_cur(p) != '<') return rx_fail(p, RX_ERR_INVALID_NAMED_REFERENCE);
  rx_advance(p);
  rx_name_t ref = {0};
  if (!rx_parse_group_name(p, &ref.name, &ref.name_len)) return false;
  if (!rx_reserve(p, (void **)&p->refs, &p->refs_cap, p->refs_len + 1, sizeof(*p->refs))) return false;
  p->refs[p->refs_len++] = ref;
  return true;
}

static int rx_parse_decimal(rx_t *p) {
  int value = 0;
  while (rx_is_digit(rx_cur(p))) {
    int d = (int)(rx_cur(p) - '0');
    if (value > (RX_INFINITY - d) / 10) {
      do rx_advance(p); while (rx_is_digit(rx_cur(p)));
      return RX_INFINITY;
    }
    value = value * 10 + d;
    rx_advance(p);
  }
  return value;
}

static bool rx_parse_interval(rx_t *p, int *min_out, int *max_out) {
  size_t start = p->pos;
  rx_advance(p);
  if (!rx_is_digit(rx_cur(p))) {
    p->pos = start;
    return false;
  }
  int min = rx_parse_decimal(p), max;
  if (rx_cur(p) == '}') {
    max = min;
    rx_advance(p);
  } else if (rx_cur(p) == ',') {
    rx_advance(p);
    if (rx_cur(p) == '}') {
      max = RX_INFINITY;
      rx_advance(p);
    } else {
      max = rx_parse_decimal(p);
      if (rx_cur(p) != '}') {
        p->pos = start;
        return false;
      }
      rx_advance(p);
    }
  } else {
    p->pos = start;
    return false;
  }
  *min_out = min;
  *max_out = max;
  return true;
}

static bool rx_parse_atom_escape(rx_t *p, bool *assertion) {
  uint32_t next = rx_next(p);
  *assertion = false;
  switch (next) {
    case RX_END:
      return rx_fail(p, RX_ERR_ESCAPE_AT_END);
    case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
      if (rx_parse_backref_index(p)) return true;
      if (p->unicode) return rx_fail(p, RX_ERR_INVALID_ESCAPE);
      if (next == '8' || next == '9') {
        rx_advance2(p);
        return true;
      }
      [[fallthrough]];
    case '0':
      rx_advance(p);
      if (p->unicode && rx_is_digit(rx_next(p))) return rx_fail(p, RX_ERR_INVALID_DECIMAL_ESCAPE);
      rx_parse_octal(p);
      return true;
    case 'b': case 'B':
      rx_advance2(p);
      *assertion = true;
      return true;
    case 'd': case 'D': case 's': case 'S': case 'w': case 'W':
      rx_advance2(p);
      return true;
    case 'p': case 'P':
      rx_advance2(p);
      if (p->unicode) {
        bool strings;
        if (!rx_parse_property(p, next == 'P', &strings)) return rx_fail(p, RX_ERR_INVALID_PROPERTY_NAME);
      }
      return true;
    case 'k':
      if (p->unicode || rx_has_named_captures(p, false)) {
        rx_advance2(p);
        return rx_parse_named_backref(p);
      }
      [[fallthrough]];
    default:
      rx_parse_character_escape(p, false);
      return !p->err;
  }
}

static bool rx_parse_pattern(rx_t *p) {
  if (!rx_reserve(p, (void **)&p->groups, &p->groups_cap, 1, sizeof(*p->groups))) return false;
  p->groups[p->groups_len++] = (rx_group_t){ .type = RX_GROUP_ROOT };

  for (;;) {
    int term = RX_TERM_ATOM;
    uint32_t c = rx_cur(p);
    switch (c) {
      case RX_END:
        if (p->groups_len > 1) return rx_fail(p, RX_ERR_UNTERMINATED_GROUP);
        goto done;
      case ')':
        if (p->groups_len <= 1) return rx_fail(p, RX_ERR_UNMATCHED_PAREN);
        rx_advance(p);
        if (!rx_close_group(p, &term)) return false;
        break;
      case '|':
        rx_advance(p);
        p->groups[p->groups_len - 1].alt++;
        continue;
      case '*': case '+': case '?':
        return rx_fail(p, RX_ERR_NOTHING_TO_REPEAT);
      case '^': case '$':
        rx_advance(p);
        continue;
      case '(':
        if (!rx_parse_open_paren(p)) return false;
        continue;
      case '[':
        if (!(p->sets ? rx_parse_class_sets(p) : rx_parse_class_ranges(p))) return false;
        break;
      case '\\': {
        bool assertion;
        if (!rx_parse_atom_escape(p, &assertion)) return false;
        if (assertion) continue;
        break;
      }
      case '{': {
        int min, max;
        if (rx_parse_interval(p, &min, &max)) return rx_fail(p, RX_ERR_NOTHING_TO_REPEAT);
        if (p->unicode) return rx_fail(p, RX_ERR_LONE_QUANTIFIER_BRACKETS);
        rx_advance(p);
        break;
      }
      case '}': case ']':
        if (p->unicode) return rx_fail(p, RX_ERR_LONE_QUANTIFIER_BRACKETS);
        rx_advance(p);
        break;
      default:
        rx_advance(p);
        break;
    }

    int min, max;
    switch (rx_cur(p)) {
      case '*': case '+': case '?':
        rx_advance(p);
        break;
      case '{':
        if (rx_parse_interval(p, &min, &max)) {
          if (max < min) return rx_fail(p, RX_ERR_RANGE_OUT_OF_ORDER);
          break;
        }
        if (p->unicode) return rx_fail(p, RX_ERR_INCOMPLETE_QUANTIFIER);
        continue;
      default:
        continue;
    }
    if (rx_cur(p) == '?') rx_advance(p);
    if (term == RX_TERM_LOOKBEHIND || (term == RX_TERM_LOOKAHEAD && p->unicode))
      return rx_fail(p, RX_ERR_INVALID_QUANTIFIER);
  }

done:
  for (size_t i = 0; i < p->refs_len; i++) {
    const rx_name_t *ref = &p->refs[i];
    bool found = false;
    for (size_t j = 0; j < p->names_len && !found; j++)
      found = rx_names_equal(p, ref->name, ref->name_len, p->names[j].name, p->names[j].name_len);
    if (!found) return rx_fail(p, RX_ERR_INVALID_NAMED_CAPTURE_REFERENCED);
  }
  return true;
}

static size_t rx_decode(const char *src, size_t len, bool unicode, uint32_t *out) {
  const unsigned char *b = (const unsigned char *)src;
  size_t n = 0;
  bool prev_lead = false;
  for (size_t i = 0; i < len;) {
    uint32_t c = b[i];
    size_t width = 1;
    if (c >= 0x80) {
      size_t need = c >= 0xC2 && c <= 0xDF ? 2 : c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
      bool ok = need && i + need <= len;
      for (size_t k = 1; ok && k < need; k++) ok = (b[i + k] & 0xC0) == 0x80;
      if (ok) {
        c = need == 2 ? c & 0x1F : need == 3 ? c & 0x0F : c & 0x07;
        for (size_t k = 1; k < need; k++) c = (c << 6) | (b[i + k] & 0x3F);
        width = need;
      } else c = 0xFFFD;
    }
    i += width;
    if (c >= 0x10000 && !unicode) {
      out[n++] = 0xD800 + ((c - 0x10000) >> 10);
      out[n++] = 0xDC00 + ((c - 0x10000) & 0x3FF);
      prev_lead = false;
      continue;
    }
    if (unicode && prev_lead && rx_is_trail(c)) {
      out[n - 1] = rx_combine(out[n - 1], c);
      prev_lead = false;
      continue;
    }
    prev_lead = rx_is_lead(c);
    out[n++] = c;
  }
  return n;
}

const char *regexp_syntax_error(const char *src, size_t len, bool unicode, bool unicode_sets) {
  uint32_t inline_units[RX_INLINE_UNITS];
  uint32_t *units = len <= RX_INLINE_UNITS ? inline_units : malloc(len * sizeof(*units));
  if (!units) return NULL;

  rx_t p = { .unicode = unicode || unicode_sets, .sets = unicode_sets };
  p.n = rx_decode(src, len, p.unicode, units);
  p.s = units;
  rx_parse_pattern(&p);

  free(p.groups);
  free(p.names);
  free(p.refs);
  free(p.pool);
  free(p.frames);
  if (units != inline_units) free(units);
  return rx_error_messages[p.err];
}
