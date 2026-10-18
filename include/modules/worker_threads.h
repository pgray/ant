#ifndef ANT_WORKER_THREADS_H
#define ANT_WORKER_THREADS_H

#include "types.h"

ant_value_t worker_threads_library(ant_t *js);
ant_value_t ant_worker_exit(ant_t *js, int code);

void cleanup_worker_threads_module(ant_t *js);
bool ant_worker_is_worker(ant_t *js);
bool ant_worker_report_uncaught(ant_t *js, ant_value_t reason);

#endif
