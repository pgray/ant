#include <compat.h> // IWYU pragma: keep

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crprintf.h>

#include "ant.h"
#include "utils.h"
#include "errors.h"
#include "internal.h"
#include "messages.h"
#include "esm/loader.h"
#include "esm/remote.h"
#include "modules/process.h"
#include "cli/preload.h"

char **ant_options_split(const char *src, int *count) {
  *count = 0;
  if (!src || !*src) return NULL;

  size_t len = strlen(src);
  size_t slots = len / 2 + 2;
  
  char **out = try_oom(sizeof(char *) * slots + len + 1);
  char *buf = (char *)(out + slots);
  int n = 0;

  for (const char *p = src; *p;) {
    while (isspace((unsigned char)*p)) p++;
    if (!*p) break;

    out[n++] = buf;
    char quote = 0;
    for (; *p && (quote || !isspace((unsigned char)*p)); p++) {
      if (quote && *p == quote) quote = 0;
      else if (!quote && (*p == '"' || *p == '\'')) quote = *p;
      else if (quote == '"' && *p == '\\' && p[1]) *buf++ = *++p;
      else *buf++ = *p;
    }
    
    *buf++ = '\0';
  }

  out[n] = NULL;
  if (n == 0) {
    free(out);
    return NULL;
  }

  *count = n;
  return out;
}

static ant_value_t preload_specifier(ant_t *js, const char *file) {
  if (esm_is_data_url(file)) return js_mkstr(js, file, strlen(file));

  char *resolved = resolve_js_file(file);
  if (!resolved) {
    crfprintf(stderr, msg.preload_not_found, file);
    return js_mkundef();
  }

  char *real = esm_is_url(resolved) ? NULL : realpath(resolved, NULL);
  ant_value_t specifier = esm_is_url(resolved)
    ? js_mkstr(js, resolved, strlen(resolved))
    : js_esm_make_file_url(js, real ? real : resolved);

  free(real);
  free(resolved);
  
  return specifier;
}

static int preload_one(ant_t *js, const char *file) {
  ant_value_t specifier = preload_specifier(js, file);
  if (vtype(specifier) == kTypeUndefined) return EXIT_FAILURE;

  ant_value_t ns = js_esm_import_sync(js, specifier);
  if (Ant_Exception_Pending(js) && process_has_event_listeners(js, "uncaughtException")) {
    process_report_uncaught_exception(js);
    return EXIT_SUCCESS;
  }

  if (print_uncaught_throw(js)) return EXIT_FAILURE;
  
  if (vtype(ns) == kTypeError) {
    fprintf(stderr, "%s\n", js_str(js, ns));
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int ant_preload_run(ant_t *js, const char *const *files, int count) {
  for (int i = 0; i < count; i++)
    if (preload_one(js, files[i]) != EXIT_SUCCESS) return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
