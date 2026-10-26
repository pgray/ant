#ifndef GENERATOR_H
#define GENERATOR_H

#include "types.h"
#include "silver/engine.h"

void init_generator_module(ant_t *js);
void generator_mark_for_gc(ant_t *js, ant_value_t gen);

bool generator_resume_pending_request(ant_t *js, coroutine_t *coro, ant_value_t result);
void Ant_Generator_SettleYield(ant_t *js, ant_value_t gen, ant_value_t value);
coroutine_t *generator_get_coro_for_gc(ant_value_t gen);

bool js_generator_can_resume_sync(ant_value_t gen, ant_value_t method, sv_resume_kind_t kind);
ant_value_t js_generator_resume_sync(ant_t *js, ant_value_t gen, ant_value_t value, sv_resume_kind_t kind, bool *out_done);

#endif
