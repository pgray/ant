#ifndef ANT_CFUNC_H
#define ANT_CFUNC_H

#include "types.h"

void js_define_methods(
  ant_t *js, ant_value_t obj, 
  const ant_cfunc_meta_t *methods, 
  size_t count, uint8_t attrs
);

ant_value_t js_mkfun_dyn(ant_cfunc_t fn);
ant_value_t js_mkfun_meta(const ant_cfunc_meta_t *meta);

ant_value_t js_heavy_mkfun(ant_t *js, ant_value_t (*fn)(ant_params_t), ant_value_t data);
ant_value_t js_heavy_mkfun_meta(ant_t *js, const ant_cfunc_meta_t *meta, ant_value_t data);
ant_value_t js_heavy_mkfun_native(ant_t *js, ant_value_t (*fn)(ant_params_t), void *ptr, uint32_t tag);
ant_value_t js_make_ctor_meta(ant_t *js, const ant_cfunc_meta_t *meta, ant_value_t proto, uint8_t proto_attrs);

#define ANT_CFUNC_META(fn, name, arity, flags)    \
static const ant_cfunc_meta_t _ant_cfunc_meta = { \
  (fn), (name), (arity), (flags)                  \
}

#define js_mkfun_arity_flags(fn, arity, flags) ({ \
  ANT_CFUNC_META(fn, NULL, arity, flags);         \
  js_mkfun_meta(&_ant_cfunc_meta);                \
})

#define js_heavy_mkfun_named(js, fn, name, arity, data) ({ \
  ANT_CFUNC_META(fn, name, arity, 0);                      \
  js_heavy_mkfun_meta((js), &_ant_cfunc_meta, (data));     \
})

#define js_make_ctor_attrs(js, fn, proto, name, length, attrs) ({ \
  ANT_CFUNC_META(fn, name, length, 0);                            \
  js_make_ctor_meta((js), &_ant_cfunc_meta, (proto), (attrs));    \
})

#define js_mkfun(fn)              js_mkfun_arity_flags(fn, 0, 0)
#define js_mkfun_arity(fn, arity) js_mkfun_arity_flags(fn, arity, 0)
#define js_mkfun_flags(fn, flags) js_mkfun_arity_flags(fn, 0, flags)

#define js_make_ctor(js, fn, proto, name, length)           js_make_ctor_attrs(js, fn, proto, name, length, ANT_PROP_ATTR_WRITABLE)
#define js_make_intrinsic_ctor(js, fn, proto, name, length) js_make_ctor_attrs(js, fn, proto, name, length, 0)

#endif
