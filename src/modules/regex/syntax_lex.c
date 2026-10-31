#include "syntax_internal.h"

enum {
  RX_PROP_GENERAL_CATEGORY,
  RX_PROP_BINARY,
  RX_PROP_STRING,
  RX_PROP_SCRIPT,
};

typedef struct {
  uint8_t kind;
  const char *name;
} rx_property_t;

static const rx_property_t rx_properties[] = {
#define REGEXP_UNICODE_PROPERTY(kind, name) { RX_PROP_##kind, name },
#include "modules/regex_unicode_properties.h"
};

bool rx_reserve(rx_t *p, void **buf, size_t *cap, size_t need, size_t size) {
  if (need <= *cap) return true;
  size_t next = *cap ? *cap * 2 : 16;
  while (next < need) next *= 2;
  void *grown = realloc(*buf, next * size);
  if (!grown) {
    p->err = RX_ERR_OOM;
    return false;
  }
  *buf = grown;
  *cap = next;
  return true;
}

static inline bool rx_has_next(const rx_t *p) {
  return p->pos + 1 < p->n;
}

static inline int rx_hex_value(uint32_t c) {
  if (c >= '0' && c <= '9') return (int)(c - '0');
  if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') return (int)((c | 0x20) - 'a' + 10);
  return -1;
}

static inline bool rx_is_syntax_char(uint32_t c) {
  return rx_in_set(c, "^$\\.*+?()[]{}|/");
}

static inline bool rx_is_class_set_reserved_punctuator(uint32_t c) {
  return rx_in_set(c, "&-!#%,:;<=>@`~");
}

static const uint32_t rx_id_start_extra[][2] = {
  { 0x88F, 0x88F }, { 0xC5C, 0xC5C }, { 0xCDC, 0xCDC }, { 0xA7CE, 0xA7CF },
  { 0xA7D2, 0xA7D2 }, { 0xA7D4, 0xA7D4 }, { 0xA7F1, 0xA7F1 }, { 0x10940, 0x10959 },
  { 0x10EC5, 0x10EC7 }, { 0x11DB0, 0x11DDB }, { 0x16EA0, 0x16EB8 }, { 0x16EBB, 0x16ED3 },
  { 0x16FF2, 0x16FF6 }, { 0x187F8, 0x187FF }, { 0x18D09, 0x18D1E }, { 0x18D80, 0x18DF2 },
  { 0x1E6C0, 0x1E6DE }, { 0x1E6E0, 0x1E6E2 }, { 0x1E6E4, 0x1E6E5 }, { 0x1E6E7, 0x1E6ED },
  { 0x1E6F0, 0x1E6F4 }, { 0x1E6FE, 0x1E6FF }, { 0x2B73A, 0x2B73F }, { 0x2CEA2, 0x2CEAD },
  { 0x323B0, 0x33479 },
};

static const uint32_t rx_id_part_extra[][2] = {
  { 0x1ACF, 0x1ADD }, { 0x1AE0, 0x1AEB }, { 0x10EFA, 0x10EFB }, { 0x11B60, 0x11B67 },
  { 0x11DE0, 0x11DE9 }, { 0x1E6E3, 0x1E6E3 }, { 0x1E6E6, 0x1E6E6 }, { 0x1E6EE, 0x1E6EF },
  { 0x1E6F5, 0x1E6F5 },
};

static bool rx_in_ranges(uint32_t c, const uint32_t (*ranges)[2], size_t count) {
  size_t lo = 0, hi = count;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (c < ranges[mid][0]) hi = mid;
    else if (c > ranges[mid][1]) lo = mid + 1;
    else return true;
  }
  return false;
}

static bool rx_id_start(uint32_t c) {
  if (c < 0x80) return c == '$' || c == '_' || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z');
  if (c > 0x10FFFF) return false;
  switch (c) {
    case 0x1885: case 0x1886: case 0x2118: case 0x212E: case 0x309B: case 0x309C: return true;
    case 0x2E2F: return false;
  }
  if (rx_in_ranges(c, rx_id_start_extra, sizeof(rx_id_start_extra) / sizeof(rx_id_start_extra[0]))) return true;
  switch (unicode_category((utf8proc_int32_t)c)) {
    case UTF8PROC_CATEGORY_LU: case UTF8PROC_CATEGORY_LL: case UTF8PROC_CATEGORY_LT:
    case UTF8PROC_CATEGORY_LM: case UTF8PROC_CATEGORY_LO: case UTF8PROC_CATEGORY_NL:
      return true;
    default:
      return false;
  }
}

static bool rx_id_part(uint32_t c) {
  if (c < 0x80) return rx_id_start(c) || rx_is_digit(c);
  if (rx_id_start(c)) return true;
  if (c > 0x10FFFF) return false;
  switch (c) {
    case 0x00B7: case 0x0387: case 0x19DA: case 0x200C: case 0x200D: case 0x30FB: case 0xFF65: return true;
  }
  if (c >= 0x1369 && c <= 0x1371) return true;
  if (rx_in_ranges(c, rx_id_part_extra, sizeof(rx_id_part_extra) / sizeof(rx_id_part_extra[0]))) return true;
  switch (unicode_category((utf8proc_int32_t)c)) {
    case UTF8PROC_CATEGORY_MN: case UTF8PROC_CATEGORY_MC:
    case UTF8PROC_CATEGORY_ND: case UTF8PROC_CATEGORY_PC:
      return true;
    default:
      return false;
  }
}

bool rx_pool_push(rx_t *p, uint32_t value) {
  if (!rx_reserve(p, (void **)&p->pool, &p->pool_cap, p->pool_len + 1, sizeof(*p->pool))) return false;
  p->pool[p->pool_len++] = value;
  return true;
}

void rx_scan_captures(rx_t *p, bool in_class) {
  size_t saved = p->pos;
  int count = p->captures_started;
  uint32_t c;
  if (in_class) {
    while ((c = rx_cur(p)) != RX_END) {
      rx_advance(p);
      if (c == '\\') rx_advance(p);
      else if (c == ']') break;
    }
  }
  while ((c = rx_cur(p)) != RX_END) {
    rx_advance(p);
    if (c == '\\') rx_advance(p);
    else if (c == '[') {
      uint32_t d;
      size_t depth = 1;
      while ((d = rx_cur(p)) != RX_END) {
        rx_advance(p);
        if (d == '\\') rx_advance(p);
        else if (d == '[' && p->sets) depth++;
        else if (d == ']' && --depth == 0) break;
      }
    } else if (c == '(') {
      if (rx_cur(p) == '?') {
        rx_advance(p);
        if (rx_cur(p) != '<') continue;
        rx_advance(p);
        if (rx_cur(p) == '=' || rx_cur(p) == '!') continue;
        p->has_named = true;
      }
      count++;
    }
  }
  p->capture_count = count;
  p->scanned = true;
  p->pos = saved;
}

bool rx_has_named_captures(rx_t *p, bool in_class) {
  if (p->has_named || p->scanned) return p->has_named;
  rx_scan_captures(p, in_class);
  return p->has_named;
}

static bool rx_parse_hex(rx_t *p, int digits, uint32_t *out) {
  size_t start = p->pos;
  uint32_t value = 0;
  for (int i = 0; i < digits; i++) {
    int d = rx_hex_value(rx_cur(p));
    if (d < 0) {
      p->pos = start;
      return false;
    }
    value = value * 16 + (uint32_t)d;
    rx_advance(p);
  }
  *out = value;
  return true;
}

static bool rx_parse_unicode_escape(rx_t *p, uint32_t *out, bool force_unicode) {
  bool unicode = p->unicode || force_unicode;
  if (rx_cur(p) == '{' && unicode) {
    size_t start = p->pos;
    rx_advance(p);
    int d = rx_hex_value(rx_cur(p));
    uint32_t value = 0;
    bool ok = d >= 0;
    while (ok && d >= 0) {
      value = value * 16 + (uint32_t)d;
      if (value > 0x10FFFF) ok = false;
      else {
        rx_advance(p);
        d = rx_hex_value(rx_cur(p));
      }
    }
    if (ok && rx_cur(p) == '}') {
      rx_advance(p);
      *out = value;
      return true;
    }
    p->pos = start;
    return false;
  }
  if (!rx_parse_hex(p, 4, out)) return false;
  if (unicode && rx_is_lead(*out) && rx_cur(p) == '\\') {
    size_t start = p->pos;
    if (rx_next(p) == 'u') {
      rx_advance2(p);
      uint32_t trail;
      if (rx_parse_hex(p, 4, &trail) && rx_is_trail(trail)) {
        *out = rx_combine(*out, trail);
        return true;
      }
    }
    p->pos = start;
  }
  return true;
}

uint32_t rx_parse_octal(rx_t *p) {
  uint32_t value = rx_cur(p) - '0';
  rx_advance(p);
  if (rx_cur(p) >= '0' && rx_cur(p) <= '7') {
    value = value * 8 + rx_cur(p) - '0';
    rx_advance(p);
    if (value < 32 && rx_cur(p) >= '0' && rx_cur(p) <= '7') {
      value = value * 8 + rx_cur(p) - '0';
      rx_advance(p);
    }
  }
  return value;
}

uint32_t rx_parse_character_escape(rx_t *p, bool in_class) {
  rx_advance(p);
  uint32_t c = rx_cur(p);
  switch (c) {
    case 'f': rx_advance(p); return '\f';
    case 'n': rx_advance(p); return '\n';
    case 'r': rx_advance(p); return '\r';
    case 't': rx_advance(p); return '\t';
    case 'v': rx_advance(p); return '\v';
    case 'c': {
      uint32_t control = rx_next(p);
      uint32_t letter = control & ~(uint32_t)0x20;
      if (letter >= 'A' && letter <= 'Z') {
        rx_advance2(p);
        return control & 0x1F;
      }
      if (p->unicode) {
        rx_fail(p, RX_ERR_INVALID_UNICODE_ESCAPE);
        return 0;
      }
      if (in_class && (rx_is_digit(control) || control == '_')) {
        rx_advance2(p);
        return control & 0x1F;
      }
      return '\\';
    }
    case '0':
      if (!rx_is_digit(rx_next(p))) {
        rx_advance(p);
        return 0;
      }
      [[fallthrough]];
    case '1': case '2': case '3': case '4': case '5': case '6': case '7':
      if (p->unicode) {
        rx_fail(p, RX_ERR_INVALID_DECIMAL_ESCAPE);
        return 0;
      }
      return rx_parse_octal(p);
    case 'x': {
      rx_advance(p);
      uint32_t value;
      if (rx_parse_hex(p, 2, &value)) return value;
      if (p->unicode) {
        rx_fail(p, RX_ERR_INVALID_ESCAPE);
        return 0;
      }
      return 'x';
    }
    case 'u': {
      rx_advance(p);
      uint32_t value;
      if (rx_parse_unicode_escape(p, &value, false)) return value;
      if (p->unicode) {
        rx_fail(p, RX_ERR_INVALID_UNICODE_ESCAPE);
        return 0;
      }
      return 'u';
    }
    default:
      break;
  }
  if (!p->unicode) {
    if (c == 'k' && in_class && rx_has_named_captures(p, true)) {
      rx_fail(p, RX_ERR_INVALID_ESCAPE);
      return 0;
    }
    rx_advance(p);
    return c;
  }
  if (rx_is_syntax_char(c) || (p->sets && in_class && rx_is_class_set_reserved_punctuator(c))) {
    rx_advance(p);
    return c;
  }
  rx_fail(p, RX_ERR_INVALID_ESCAPE);
  return 0;
}

static int rx_property_cmp(const void *key, const void *entry) {
  const rx_property_t *a = key, *b = entry;
  if (a->kind != b->kind) return (int)a->kind - (int)b->kind;
  return strcmp(a->name, b->name);
}

static bool rx_property_known(uint8_t kind, const char *name) {
  rx_property_t key = { kind, name };
  return bsearch(&key, rx_properties, sizeof(rx_properties) / sizeof(rx_properties[0]), sizeof(rx_properties[0]), rx_property_cmp) != NULL;
}

static inline bool rx_is_property_char(uint32_t c) {
  return c < 0x80 && (c == '_' || rx_is_digit(c) || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z'));
}

bool rx_parse_property(rx_t *p, bool negated, bool *strings) {
  char name[64], value[64];
  size_t name_len = 0, value_len = 0;
  bool has_value = false;
  *strings = false;
  if (rx_cur(p) != '{') return false;
  for (rx_advance(p); rx_cur(p) != '}' && rx_cur(p) != '='; rx_advance(p)) {
    if (!rx_is_property_char(rx_cur(p)) || !rx_has_next(p)) return false;
    if (name_len + 1 < sizeof(name)) name[name_len] = (char)rx_cur(p);
    name_len++;
  }
  if (rx_cur(p) == '=') {
    has_value = true;
    for (rx_advance(p); rx_cur(p) != '}'; rx_advance(p)) {
      if (!rx_is_property_char(rx_cur(p)) || !rx_has_next(p)) return false;
      if (value_len + 1 < sizeof(value)) value[value_len] = (char)rx_cur(p);
      value_len++;
    }
  }
  rx_advance(p);
  if (name_len >= sizeof(name) || value_len >= sizeof(value)) return false;
  name[name_len] = '\0';
  value[value_len] = '\0';
  if (!has_value) {
    if (rx_property_known(RX_PROP_GENERAL_CATEGORY, name)) return true;
    if (rx_property_known(RX_PROP_BINARY, name)) return true;
    if (p->sets && !negated && rx_property_known(RX_PROP_STRING, name)) {
      *strings = true;
      return true;
    }
    return false;
  }
  if (!strcmp(name, "General_Category") || !strcmp(name, "gc"))
    return rx_property_known(RX_PROP_GENERAL_CATEGORY, value);
  if (!strcmp(name, "Script") || !strcmp(name, "sc") || !strcmp(name, "Script_Extensions") || !strcmp(name, "scx"))
    return rx_property_known(RX_PROP_SCRIPT, value);
  return false;
}

bool rx_parse_group_name(rx_t *p, uint32_t *name, uint32_t *name_len) {
  *name = (uint32_t)p->pool_len;
  *name_len = 0;
  bool at_start = true;
  for (;;) {
    uint32_t c = rx_cur(p);
    rx_advance(p);
    if (!p->unicode && rx_is_lead(c) && rx_is_trail(rx_cur(p))) {
      c = rx_combine(c, rx_cur(p));
      rx_advance(p);
    }
    if (c == '\\' && rx_cur(p) == 'u') {
      rx_advance(p);
      if (!rx_parse_unicode_escape(p, &c, true)) return rx_fail(p, RX_ERR_INVALID_UNICODE_ESCAPE);
    }
    if (c == '\\') return rx_fail(p, RX_ERR_INVALID_CAPTURE_GROUP_NAME);
    if (at_start) {
      if (!rx_id_start(c)) return rx_fail(p, RX_ERR_INVALID_CAPTURE_GROUP_NAME);
      at_start = false;
    } else if (c == '>') break;
    else if (!rx_id_part(c)) return rx_fail(p, RX_ERR_INVALID_CAPTURE_GROUP_NAME);
    if (!rx_pool_push(p, c)) return false;
    (*name_len)++;
  }
  return true;
}

bool rx_names_equal(const rx_t *p, uint32_t a, uint32_t a_len, uint32_t b, uint32_t b_len) {
  return a_len == b_len && memcmp(p->pool + a, p->pool + b, a_len * sizeof(uint32_t)) == 0;
}

bool rx_paths_exclusive(const rx_t *p, const rx_name_t *a, const rx_name_t *b) {
  uint32_t len = a->path_len < b->path_len ? a->path_len : b->path_len;
  for (uint32_t i = 0; i < len; i += 2) {
    const uint32_t *x = p->pool + a->path + i, *y = p->pool + b->path + i;
    if (x[0] != y[0]) return false;
    if (x[1] != y[1]) return true;
  }
  return false;
}
