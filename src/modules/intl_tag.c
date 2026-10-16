#include <stdlib.h>
#include <string.h>

#include "ascii.h"
#include "modules/intl_tag.h"

typedef struct {
  const char *p;
  size_t len;
} intl_span_t;

typedef struct {
  const char *from;
  const char *to;
} intl_language_alias_t;

typedef struct {
  const char *language;
  const char *variant;
  const char *to;
} intl_variant_alias_t;

static const intl_language_alias_t intl_language_aliases[] = {
#define INTL_LANGUAGE_ALIAS(from, to) { from, to },
#include "modules/intl_aliases.h"
};

static const intl_variant_alias_t intl_variant_aliases[] = {
#define INTL_LANGUAGE_VARIANT_ALIAS(language, variant, to) { language, variant, to },
#include "modules/intl_aliases.h"
};

typedef struct {
  intl_span_t language;
  intl_span_t script;
  intl_span_t region;
  intl_span_t *variants;
  size_t variant_count;
  intl_span_t *extensions;
  size_t extension_count;
  intl_span_t private_use;
  bool va_posix;
} intl_locale_t;

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
  bool oom;
} intl_out_t;

static intl_span_t intl_span(const char *p) {
  return (intl_span_t){ p, p ? strlen(p) : 0 };
}

static bool intl_span_eq(intl_span_t a, intl_span_t b) {
  return a.len == b.len && memcmp(a.p, b.p, a.len) == 0;
}

static bool intl_span_eq_str(intl_span_t a, const char *s) {
  return intl_span_eq(a, intl_span(s));
}

static int intl_span_cmp(const void *a, const void *b) {
  const intl_span_t *x = a, *y = b;
  size_t n = x->len < y->len ? x->len : y->len;
  int c = memcmp(x->p, y->p, n);
  return c ? c : (x->len > y->len) - (x->len < y->len);
}

static bool intl_span_all(intl_span_t s, bool (*pred)(int)) {
  for (size_t i = 0; i < s.len; i++) if (!pred((unsigned char)s.p[i])) return false;
  return s.len > 0;
}

static bool intl_is_language(intl_span_t s) {
  return ((s.len >= 2 && s.len <= 3) || (s.len >= 5 && s.len <= 8)) && intl_span_all(s, ant_ascii_is_alpha);
}

static bool intl_is_script(intl_span_t s) {
  return s.len == 4 && intl_span_all(s, ant_ascii_is_alpha);
}

static bool intl_is_region(intl_span_t s) {
  return (s.len == 2 && intl_span_all(s, ant_ascii_is_alpha)) || (s.len == 3 && intl_span_all(s, ant_ascii_is_digit));
}

static bool intl_is_variant(intl_span_t s) {
  if (!intl_span_all(s, ant_ascii_is_alnum)) return false;
  return (s.len >= 5 && s.len <= 8) || (s.len == 4 && ant_ascii_is_digit((unsigned char)s.p[0]));
}

static bool intl_is_extension_subtag(intl_span_t s, char singleton) {
  if (s.len < 2 || s.len > 8 || !intl_span_all(s, ant_ascii_is_alnum)) return false;
  return singleton != 'u' || s.len != 2 || ant_ascii_is_alpha((unsigned char)s.p[1]);
}

static size_t intl_split(char *lower, size_t len, intl_span_t *subtags) {
  size_t count = 0, start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i < len && lower[i] != '-') continue;
    subtags[count++] = (intl_span_t){ lower + start, i - start };
    start = i + 1;
  }
  return count;
}

static bool intl_parse(intl_span_t *subtags, size_t count, intl_locale_t *loc) {
  size_t i = 0;
  if (!intl_is_language(subtags[i])) return false;
  loc->language = subtags[i++];
  if (i < count && intl_is_script(subtags[i])) loc->script = subtags[i++];
  if (i < count && intl_is_region(subtags[i])) loc->region = subtags[i++];

  for (; i < count && intl_is_variant(subtags[i]); i++) {
    for (size_t v = 0; v < loc->variant_count; v++) if (intl_span_eq(loc->variants[v], subtags[i])) return false;
    loc->variants[loc->variant_count++] = subtags[i];
  }

  while (i < count) {
    if (subtags[i].len != 1 || !ant_ascii_is_alnum((unsigned char)subtags[i].p[0])) return false;
    char singleton = subtags[i].p[0];
    size_t end = i + 1;

    if (singleton == 'x') {
      for (; end < count; end++)
        if (subtags[end].len > 8 || !intl_span_all(subtags[end], ant_ascii_is_alnum)) return false;
    } else {
      for (; end < count && subtags[end].len != 1; end++)
        if (!intl_is_extension_subtag(subtags[end], singleton)) return false;
      for (size_t e = 0; e < loc->extension_count; e++) if (loc->extensions[e].p[0] == singleton) return false;
    }

    if (end == i + 1) return false;
    const char *last_end = subtags[end - 1].p + subtags[end - 1].len;
    intl_span_t whole = { subtags[i].p, (size_t)(last_end - subtags[i].p) };
    if (singleton == 'x') loc->private_use = whole;
    else loc->extensions[loc->extension_count++] = whole;
    i = end;
  }

  return true;
}

static void intl_remove_variant(intl_locale_t *loc, size_t index) {
  memmove(&loc->variants[index], &loc->variants[index + 1], (loc->variant_count - index - 1) * sizeof(intl_span_t));
  loc->variant_count--;
}

static int intl_find_variant(const intl_locale_t *loc, const char *name) {
  for (size_t v = 0; v < loc->variant_count; v++) if (intl_span_eq_str(loc->variants[v], name)) return (int)v;
  return -1;
}

static int intl_language_alias_cmp(const void *key, const void *entry) {
  const intl_span_t *span = key;
  const char *from = ((const intl_language_alias_t *)entry)->from;
  size_t from_len = strlen(from);
  size_t n = span->len < from_len ? span->len : from_len;
  int c = memcmp(span->p, from, n);
  return c ? c : (span->len > from_len) - (span->len < from_len);
}

static void intl_apply_replacement(intl_locale_t *loc, const char *to) {
  const char *dash = strchr(to, '-');
  loc->language = (intl_span_t){ to, dash ? (size_t)(dash - to) : strlen(to) };
  while (dash) {
    const char *part = dash + 1;
    dash = strchr(part, '-');
    intl_span_t span = { part, dash ? (size_t)(dash - part) : strlen(part) };
    if (span.len == 4 && !loc->script.len) loc->script = span;
    else if (span.len != 4 && !loc->region.len) loc->region = span;
  }
}

static void intl_apply_aliases(intl_locale_t *loc) {
  for (size_t a = 0; a < sizeof(intl_variant_aliases) / sizeof(intl_variant_aliases[0]); a++) {
    if (!intl_span_eq_str(loc->language, intl_variant_aliases[a].language)) continue;
    int v = intl_find_variant(loc, intl_variant_aliases[a].variant);
    if (v < 0) continue;
    intl_remove_variant(loc, (size_t)v);
    loc->language = intl_span(intl_variant_aliases[a].to);
    break;
  }

  const intl_language_alias_t *alias = bsearch(
    &loc->language, intl_language_aliases,
    sizeof(intl_language_aliases) / sizeof(intl_language_aliases[0]),
    sizeof(intl_language_aliases[0]), intl_language_alias_cmp
  );
  if (alias) intl_apply_replacement(loc, alias->to);

  int v = intl_find_variant(loc, "heploc");
  if (v >= 0) {
    loc->variants[v] = intl_span("alalc97");
    int hepburn = intl_find_variant(loc, "hepburn");
    if (hepburn >= 0) intl_remove_variant(loc, (size_t)hepburn);
  }
  if ((v = intl_find_variant(loc, "polytoni")) >= 0) loc->variants[v] = intl_span("polyton");
  if ((v = intl_find_variant(loc, "aaland")) >= 0 && !loc->region.len) {
    loc->region = intl_span("ax");
    intl_remove_variant(loc, (size_t)v);
  }
  if (loc->variant_count == 1 && intl_span_eq_str(loc->variants[0], "posix")) {
    loc->va_posix = true;
    loc->variant_count = 0;
  }

  for (size_t a = 0; a < loc->variant_count; a++)
    for (size_t b = a + 1; b < loc->variant_count;)
      if (intl_span_eq(loc->variants[a], loc->variants[b])) intl_remove_variant(loc, b);
      else b++;
}

static void intl_put(intl_out_t *out, const char *p, size_t n, int (*map)(int), bool title) {
  if (out->oom) return;
  if (out->len + n + 1 > out->cap) {
    size_t cap = (out->len + n + 1) * 2;
    char *grown = realloc(out->buf, cap);
    if (!grown) {
      out->oom = true;
      return;
    }
    out->buf = grown;
    out->cap = cap;
  }
  for (size_t i = 0; i < n; i++) {
    int c = (unsigned char)p[i];
    out->buf[out->len++] = (char)(title ? (i == 0 ? ant_ascii_to_upper(c) : ant_ascii_to_lower(c)) : map(c));
  }
}

static void intl_put_subtag(intl_out_t *out, intl_span_t s, int (*map)(int), bool title) {
  intl_put(out, "-", 1, ant_ascii_to_lower, false);
  intl_put(out, s.p, s.len, map, title);
}

typedef struct {
  intl_span_t key;
  intl_span_t type;
} intl_keyword_t;

static int intl_keyword_cmp(const void *a, const void *b) {
  return intl_span_cmp(&((const intl_keyword_t *)a)->key, &((const intl_keyword_t *)b)->key);
}

static bool intl_put_unicode_extension(intl_out_t *out, intl_span_t ext, bool va_posix) {
  size_t cap = ext.len / 2 + 2;
  intl_span_t *parts = calloc(cap, sizeof(*parts));
  intl_keyword_t *keywords = calloc(cap, sizeof(*keywords));
  if (!parts || !keywords) {
    free(parts);
    free(keywords);
    return false;
  }

  size_t count = 0, attr_count = 0, key_count = 0;
  for (size_t start = ext.len > 2 ? 2 : ext.len, i = start; i <= ext.len; i++) {
    if (i < ext.len && ext.p[i] != '-') continue;
    if (i > start) parts[count++] = (intl_span_t){ ext.p + start, i - start };
    start = i + 1;
  }

  for (size_t i = 0; i < count; i++) {
    if (parts[i].len != 2) {
      if (key_count == 0) parts[attr_count++] = parts[i];
      continue;
    }
    size_t j = i + 1;
    while (j < count && parts[j].len != 2) j++;
    intl_span_t type = { NULL, 0 };
    if (j > i + 1) type = (intl_span_t){ parts[i + 1].p, (size_t)(parts[j - 1].p + parts[j - 1].len - parts[i + 1].p) };
    if (intl_span_eq_str(type, "true")) type = (intl_span_t){ NULL, 0 };
    bool duplicate = false;
    for (size_t k = 0; k < key_count; k++) duplicate |= intl_span_eq(keywords[k].key, parts[i]);
    if (!duplicate) keywords[key_count++] = (intl_keyword_t){ parts[i], type };
    i = j - 1;
  }

  bool has_va = false;
  for (size_t k = 0; k < key_count; k++) has_va |= intl_span_eq_str(keywords[k].key, "va");
  if (va_posix && !has_va) keywords[key_count++] = (intl_keyword_t){ intl_span("va"), intl_span("posix") };

  qsort(parts, attr_count, sizeof(*parts), intl_span_cmp);
  qsort(keywords, key_count, sizeof(*keywords), intl_keyword_cmp);

  intl_put(out, "-u", 2, ant_ascii_to_lower, false);
  for (size_t a = 0; a < attr_count; a++)
    if (a == 0 || !intl_span_eq(parts[a], parts[a - 1])) intl_put_subtag(out, parts[a], ant_ascii_to_lower, false);
  for (size_t k = 0; k < key_count; k++) {
    intl_put_subtag(out, keywords[k].key, ant_ascii_to_lower, false);
    if (keywords[k].type.len) intl_put_subtag(out, keywords[k].type, ant_ascii_to_lower, false);
  }

  free(parts);
  free(keywords);
  return true;
}

static int intl_extension_cmp(const void *a, const void *b) {
  return (int)((const intl_span_t *)a)->p[0] - (int)((const intl_span_t *)b)->p[0];
}

static bool intl_emit(intl_out_t *out, intl_locale_t *loc) {
  intl_put(out, loc->language.p, loc->language.len, ant_ascii_to_lower, false);
  if (loc->script.len) intl_put_subtag(out, loc->script, ant_ascii_to_lower, true);
  if (loc->region.len) intl_put_subtag(out, loc->region, ant_ascii_to_upper, false);

  qsort(loc->variants, loc->variant_count, sizeof(intl_span_t), intl_span_cmp);
  for (size_t v = 0; v < loc->variant_count; v++) intl_put_subtag(out, loc->variants[v], ant_ascii_to_lower, false);

  bool has_unicode = false;
  for (size_t e = 0; e < loc->extension_count; e++) has_unicode |= loc->extensions[e].p[0] == 'u';
  if (loc->va_posix && !has_unicode) loc->extensions[loc->extension_count++] = intl_span("u");

  qsort(loc->extensions, loc->extension_count, sizeof(intl_span_t), intl_extension_cmp);
  for (size_t e = 0; e < loc->extension_count; e++) {
    intl_span_t ext = loc->extensions[e];
    if (ext.p[0] == 'u') {
      if (!intl_put_unicode_extension(out, ext, loc->va_posix)) return false;
    } else intl_put_subtag(out, ext, ant_ascii_to_lower, false);
  }

  if (loc->private_use.len) intl_put_subtag(out, loc->private_use, ant_ascii_to_lower, false);
  return !out->oom;
}

intl_tag_status_t Ant_Intl_CanonicalizeTag(const char *tag, size_t len, char **out, size_t *out_len) {
  *out = NULL;
  *out_len = 0;
  if (!tag || len == 0) return INTL_TAG_INVALID;

  size_t count = 1;
  for (size_t i = 0; i < len; i++) count += tag[i] == '-';

  char *lower = malloc(len + 1);
  intl_span_t *spans = calloc(count * 3 + 1, sizeof(intl_span_t));
  if (!lower || !spans) {
    free(lower);
    free(spans);
    return INTL_TAG_OOM;
  }
  for (size_t i = 0; i < len; i++) lower[i] = (char)ant_ascii_to_lower((unsigned char)tag[i]);
  lower[len] = '\0';

  intl_span_t *subtags = spans;
  intl_locale_t loc = { .variants = spans + count, .extensions = spans + count * 2 };
  intl_split(lower, len, subtags);

  intl_tag_status_t status = INTL_TAG_INVALID;
  intl_out_t buf = { 0 };
  if (intl_parse(subtags, count, &loc)) {
    intl_apply_aliases(&loc);
    status = intl_emit(&buf, &loc) ? INTL_TAG_OK : INTL_TAG_OOM;
  }

  free(lower);
  free(spans);
  if (status != INTL_TAG_OK) {
    free(buf.buf);
    return status;
  }

  *out = buf.buf;
  *out_len = buf.len;
  return INTL_TAG_OK;
}
