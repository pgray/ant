#include <compat.h> // IWYU pragma: keep

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crprintf.h>

#include "common.h"
#include "crash.h"
#include "debug.h"
#include "messages.h"
#include "modules/io.h"
#include "cli/startup.h"

#ifdef _WIN32
#include "output.h"
#endif

typedef struct {
  const char *key;
  const char *val;
  uint32_t flag;
  bool all;
} ant_debug_option_t;

static const ant_debug_option_t ant_debug_options[] = {
#define SV_DEBUG(name, key, val, all) {key, val, SV_DEBUG_##name, all},
#include "debug/list.h"
};

static void ant_debug_apply(const char *key, const char *val) {
  bool all = strcmp(val, "all") == 0;
  for (size_t i = 0; i < countof(ant_debug_options); i++) {
    const ant_debug_option_t *opt = &ant_debug_options[i];
    if (!opt->key || strcmp(key, opt->key) != 0) continue;
    if ((opt->all && all) || strcmp(val, opt->val) == 0) sv_debug_enable(opt->flag);
  }
}

static void parse_ant_debug_flags(void) {
  const char *env = getenv("ANT_DEBUG");
  if (!env || !*env) return;

  char *buf = strdup(env);
  if (!buf) return;

  char *sp = NULL, *vp = NULL;
  for (char *e = strtok_r(buf, " ", &sp); e; e = strtok_r(NULL, " ", &sp)) {
    char *sep = strchr(e, ':');
    if (!sep) { 
      crfprintf(stderr, msg.unknown_flag_warn, e); 
      continue; 
    }
    
    *sep++ = '\0';
    for (char *v = strtok_r(sep, ",", &vp); v; v = strtok_r(NULL, ",", &vp))
      ant_debug_apply(e, v);
  }

  free(buf);
  
  crprintf_set_debug(sv_debug_enabled(SV_DEBUG_CRPRINTF_BC));
  crprintf_set_debug_hex(sv_debug_enabled(SV_DEBUG_CRPRINTF_HEX));
}

static void setup_console_colors(void) {
  crprintf_var("version", ANT_VERSION);
  crprintf_var("fatal", "<bold+red>FATAL</bold>");
  crprintf_var("error", "<red>Error</red>");
  crprintf_var("warn", "<yellow>Warning</yellow>");
}

#ifdef ANT_PGO_TRAINING
extern int __llvm_profile_write_file(void);

static void pgo_flush_on_signal(int sig) {
  __llvm_profile_write_file();
  signal(sig, SIG_DFL);
  raise(sig);
}

static void pgo_ignore_sigterm_on_exit(void) {
  signal(SIGTERM, SIG_IGN);
}

static void pgo_install_flush_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = pgo_flush_on_signal;
  sa.sa_flags = SA_RESTART;
  sigfillset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  atexit(pgo_ignore_sigterm_on_exit);
}
#endif

void ant_cli_process_init(int argc, char **argv, bool crash_report) {
  if (!crash_report && !getenv("ANT_NO_CRASH_HANDLER")) ant_crash_init(argc, argv);

  #ifdef _WIN32
  ant_output_init_console();
  #else
  signal(SIGPIPE, SIG_IGN);
  #ifdef ANT_PGO_TRAINING
  pgo_install_flush_handler();
  #endif
  #endif

  setup_console_colors();
  crprintf_set_color(io_should_color_fd(STDERR_FILENO));
  parse_ant_debug_flags();
}
