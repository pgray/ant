// meson test -C build ic-identity-wrap
//
// Prototype identities (sv_ic_object_identity) restart at 1 when the counter
// passes SV_IC_IDENTITY_MAX. Live objects must not keep their old numbers
// (sv_ic_identities_reset), or two live prototypes could share one: an add
// case keyed by a young prototype's identity (sv_add_proto_key) recorded
// under P would then also match objects whose prototype is Q, and an add
// through Q's inherited setter would make an own property instead.
#include "internal.h"
#include "silver/engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void run(ant_t *js, const char *source) {
  ant_value_t result = js_eval_bytecode(js, source, strlen(source));
  assert(!is_err(result));
}

static ant_object_t *global_object(ant_t *js, const char *name) {
  ant_value_t value = js_get(js, js->global, name);
  assert(vtype(value) == kTypeObject);
  return js_obj_ptr(value);
}

static double global_number(ant_t *js, const char *name) {
  ant_value_t value = js_get(js, js->global, name);
  assert(vtype(value) == kTypeNumber);
  return js_getnum(value);
}

int main(void) {
  char stack_base;
  ant_t *js = ant_create(nullptr);
  assert(js);
  js_setstackbase(js, &stack_base);

  // two young prototypes: P plain, Q with a setter for z
  run(js,
    "globalThis.hits = 0;"
    "globalThis.P = {};"
    "globalThis.Q = { set z(v) { hits++; } };"
    "globalThis.addW = function (o) { o.w = 1; };"
    "globalThis.addX = function (o) { o.x = 1; };"
    "globalThis.addZ = function (o) { o.z = 1; };");
  ant_object_t *p = global_object(js, "P");
  ant_object_t *q = global_object(js, "Q");
  assert(p->flags.generation == 0 && q->flags.generation == 0);

  // P gets identity 1 before the wrap (an add recorded under it)
  js->ic.next_object_identity = 0;
  run(js, "addW(Object.create(P)); addW(Object.create(P));");
  assert(p->ic_identity == 1);

  // the counter wraps: Q is handed identity 1, and P loses its old one
  js->ic.next_object_identity = SV_IC_IDENTITY_MAX;
  run(js, "addX(Object.create(Q)); addX(Object.create(Q));");
  assert(q->ic_identity == 1 && p->ic_identity == 0);

  // after the wrap: an add of z recorded under P must not apply to Q's
  // objects, whose prototype has a setter for z
  run(js,
    "for (let i = 0; i < 4; i++) addZ(Object.create(P));"
    "globalThis.viaQ = Object.create(Q);"
    "addZ(viaQ);"
    "globalThis.ownZ = Object.prototype.hasOwnProperty.call(viaQ, 'z') ? 1 : 0;");
  assert(p->ic_identity != 0 && p->ic_identity != q->ic_identity);
  assert(global_number(js, "hits") == 1);
  assert(global_number(js, "ownZ") == 0);

  js_destroy(js);
  puts("PASS prototype identities stay unique among live objects when the counter wraps");
}
