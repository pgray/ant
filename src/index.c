#include <math.h>
#include <stdint.h>

#include "index.h"
#include "internal.h"

static inline ant_value_t js_to_integer_number(ant_t *js, ant_value_t value, double *out) {
  if (__builtin_expect(vtype(value) == kTypeNumber, 1)) {
    *out = Ant_Value_AsNumberUnchecked(value);
    return Ant_Value_Undefined;
  }

  ant_value_t primitive = value;
  if (is_object_type(value) || vtype(value) == kTypeBuiltin) {
    primitive = js_to_primitive(js, value, 2);
    if (is_err(primitive)) return primitive;
  }

  if (vtype(primitive) == kTypeBigInt || vtype(primitive) == kTypeSymbol)
    return js_mkerr_typed(js, JS_ERR_TYPE, "Cannot convert value to an index");

  *out = js_to_number(js, primitive);
  return Ant_Value_Undefined;
}

ant_value_t js_to_index_slow(ant_t *js, ant_value_t value, size_t *out) {
  double number; ant_value_t result = js_to_integer_number(js, value, &number);
  if (is_err(result)) return result;

  if (isnan(number) || (number < 0.0 && number > -1.0)) {
    *out = 0;
    return Ant_Value_Undefined;
  }

  if (
    !isfinite(number) || number < 0.0 || 
    number > MAX_SAFE_INTEGER || number > (double)SIZE_MAX
  ) return js_mkerr_typed(js, JS_ERR_RANGE, "Index out of range");

  *out = (size_t)number;
  return Ant_Value_Undefined;
}

ant_value_t js_to_length_slow(ant_t *js, ant_value_t value, size_t *out) {
  double number; ant_value_t result = js_to_integer_number(js, value, &number);
  if (is_err(result)) return result;

  if (isnan(number) || number <= 0.0) {
    *out = 0;
    return Ant_Value_Undefined;
  }

  if (number >= MAX_SAFE_INTEGER) number = MAX_SAFE_INTEGER;
  if (number > (double)SIZE_MAX) return js_mkerr_typed(js, JS_ERR_RANGE, "Length out of range");

  *out = (size_t)number;
  return Ant_Value_Undefined;
}
