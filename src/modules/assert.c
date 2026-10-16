#include <string.h>

#include "ant.h"
#include "errors.h"
#include "internal.h"
#include "gc/roots.h"

#include "modules/assert.h"
#include "silver/call.h"

typedef struct {
  const char *op;
  ant_value_t actual;
  ant_value_t expected;
} assertion_info_t;

static bool assert_is_error_instance(ant_t *js, ant_value_t value) {
  if (!is_object_type(value)) return false;
  ant_value_t is = do_instanceof(js, value, js->builtins.error_ctor);
  return !is_err(is) && js_truthy(js, is);
}

static ant_value_t assertion_error_make(
  ant_t *js, const char *msg, ant_value_t msg_val, bool generated, assertion_info_t info
) {
  const char *text = vtype(msg_val) == kTypeString ? js_getstr(js, msg_val, NULL) : NULL;

  GC_ROOT_SAVE(root_mark, js);
  GC_ROOT_PIN(js, msg_val);
  GC_ROOT_PIN(js, info.actual);
  GC_ROOT_PIN(js, info.expected);

  ant_value_t props = js_mkobj(js);
  GC_ROOT_PIN(js, props);
  js_set(js, props, "generatedMessage", js_bool(generated && text == NULL));
  js_set(js, props, "code", js_mkstr(js, "ERR_ASSERTION", 13));
  js_set(js, props, "actual", info.actual);
  js_set(js, props, "expected", info.expected);
  js_set(js, props, "operator", info.op ? js_mkstr(js, info.op, strlen(info.op)) : js_mkundef());

  ant_value_t err = Ant_Error_CreateWithProps(js, JS_ERR_GENERIC, props, text ? text : msg);
  if (vtype(err) == kTypeObject && is_object_type(js->builtins.assertion_error_proto))
    js_set_proto_init(err, js->builtins.assertion_error_proto);

  GC_ROOT_RESTORE(js, root_mark);
  return err;
}

static ant_value_t assertion_error_ex(
  ant_t *js, const char *msg, ant_value_t msg_val, bool generated, assertion_info_t info
) {
  if (assert_is_error_instance(js, msg_val)) return js_throw(js, msg_val);
  ant_value_t err = assertion_error_make(js, msg, msg_val, generated, info);
  return is_err(err) ? err : js_throw(js, err);
}

static inline ant_value_t assertion_error(ant_t *js, const char *msg, ant_value_t msg_val, assertion_info_t info) {
  return assertion_error_ex(js, msg, msg_val, true, info);
}

static inline assertion_info_t assert_compared(ant_value_t *args, int nargs, const char *op) {
  return (assertion_info_t){
    .op = op,
    .actual = nargs > 0 ? args[0] : js_mkundef(),
    .expected = nargs > 1 ? args[1] : js_mkundef(),
  };
}

// assert(value, message) / assert.ok
static ant_value_t assert_ok(ant_params_t) {
  if (nargs < 1 || !js_truthy(js, args[0]))
    return assertion_error(
      js, "The expression evaluated to a falsy value", nargs >= 2 ? args[1] : js_mkundef(),
      (assertion_info_t){ "==", nargs > 0 ? args[0] : js_mkundef(), js_true }
    );
  return js_mkundef();
}

// assert.fail(message)
static ant_value_t assert_fail(ant_params_t) {
  return assertion_error(js, "Failed", nargs >= 1 ? args[0] : js_mkundef(), (assertion_info_t){ "fail", js_mkundef(), js_mkundef() });
}

// assert.ifError(value)
static ant_value_t assert_if_error(ant_params_t) {
  if (nargs < 1) return js_mkundef();
  uint8_t t = vtype(args[0]);
  if (t == kTypeNull || t == kTypeUndefined) return js_mkundef();
  if (is_err(args[0])) return args[0];

  char buf[512];
  ant_value_t msg = is_object_type(args[0]) ? js_get(js, args[0], "message") : js_mkundef();
  if (is_err(msg)) return msg;
  if (vtype(msg) == kTypeString) snprintf(buf, sizeof(buf), "ifError got unwanted exception: %s", js_getstr(js, msg, NULL));
  else if (t == kTypeString) snprintf(buf, sizeof(buf), "ifError got unwanted exception: '%s'", js_getstr(js, args[0], NULL));
  else snprintf(buf, sizeof(buf), "ifError got unwanted exception: %s", js_str(js, args[0]));
  return assertion_error_ex(js, buf, js_mkundef(), false, (assertion_info_t){ "ifError", args[0], js_mknull() });
}

// TODO: make into global helper
static bool values_loose_equal(ant_t *js, ant_value_t a, ant_value_t b) {
  uint8_t ta = vtype(a), tb = vtype(b);
  if (ta == tb) return strict_eq_values(js, a, b);
  if ((ta == kTypeNull && tb == kTypeUndefined) || (ta == kTypeUndefined && tb == kTypeNull)) return true;
  if (ta == kTypeNumber && tb == kTypeString) {
    char *sb = js_getstr(js, b, NULL);
    return sb && js_getnum(a) == strtod(sb, NULL);
  }
  if (ta == kTypeString && tb == kTypeNumber) {
    char *sa = js_getstr(js, a, NULL);
    return sa && strtod(sa, NULL) == js_getnum(b);
  }
  return false;
}

static bool deep_equal_impl(ant_t *js, ant_value_t a, ant_value_t b, bool strict, int depth) {
  if (depth > 64) return false;
  uint8_t ta = vtype(a), tb = vtype(b);
  if (js_is_arguments(a) != js_is_arguments(b)) return false;

  if (ta == kTypeArray && tb == kTypeArray) {
    ant_offset_t la = js_arr_len(js, a), lb = js_arr_len(js, b);
    if (la != lb) return false;
    for (ant_offset_t i = 0; i < la; i++) {
      if (!deep_equal_impl(js, js_arr_get(js, a, i), js_arr_get(js, b, i), strict, depth + 1))
        return false;
    }
    return true;
  }

  if (ta == kTypeObject && tb == kTypeObject) {
    if (vdata(a) == vdata(b)) return true;
    ant_iter_t iter = js_prop_iter_begin(js, a);
    const char *key; size_t key_len; ant_value_t va;
    while (js_prop_iter_next(&iter, &key, &key_len, &va)) {
      ant_value_t vb = js_get(js, b, key);
      if (!deep_equal_impl(js, va, vb, strict, depth + 1)) {
        js_prop_iter_end(&iter);
        return false;
      }
    }
    js_prop_iter_end(&iter);
    ant_iter_t iter2 = js_prop_iter_begin(js, b);
    while (js_prop_iter_next(&iter2, &key, &key_len, &va)) {
      ant_value_t va2 = js_get(js, a, key);
      if (vtype(va2) == kTypeUndefined && vtype(va) != kTypeUndefined) {
        js_prop_iter_end(&iter2);
        return false;
      }
    }
    js_prop_iter_end(&iter2);
    return true;
  }

  return strict ? same_value_values(js, a, b) : values_loose_equal(js, a, b);
}

bool js_deep_equal(ant_t *js, ant_value_t a, ant_value_t b, bool strict) {
  return deep_equal_impl(js, a, b, strict, 0);
}

static ant_value_t assert_equal(ant_params_t) {
  if (nargs < 2) return js_mkundef();
  if (!values_loose_equal(js, args[0], args[1]))
    return assertion_error(js, "Expected values to be equal", nargs >= 3 ? args[2] : js_mkundef(), assert_compared(args, nargs, "=="));
  return js_mkundef();
}

static ant_value_t assert_not_equal(ant_params_t) {
  if (nargs < 2) return js_mkundef();
  if (values_loose_equal(js, args[0], args[1]))
    return assertion_error(js, "Expected values to not be equal", nargs >= 3 ? args[2] : js_mkundef(), assert_compared(args, nargs, "!="));
  return js_mkundef();
}

static ant_value_t assert_strict_equal(ant_params_t) {
  if (nargs < 2) return js_mkundef();
  if (!same_value_values(js, args[0], args[1]))
    return assertion_error(js, "Expected values to be strictly equal", nargs >= 3 ? args[2] : js_mkundef(), assert_compared(args, nargs, "strictEqual"));
  return js_mkundef();
}

static ant_value_t assert_not_strict_equal(ant_params_t) {
  if (nargs < 2) return js_mkundef();
  if (same_value_values(js, args[0], args[1]))
    return assertion_error(js, "Expected values to not be strictly equal", nargs >= 3 ? args[2] : js_mkundef(), assert_compared(args, nargs, "notStrictEqual"));
  return js_mkundef();
}

static ant_value_t assert_deep_equal(ant_params_t) {
  if (nargs < 2) return js_mkundef();
  if (!js_deep_equal(js, args[0], args[1], false))
    return assertion_error(js, "Expected values to be deeply equal", nargs >= 3 ? args[2] : js_mkundef(), assert_compared(args, nargs, "deepEqual"));
  return js_mkundef();
}

static ant_value_t assert_not_deep_equal(ant_params_t) {
  if (nargs < 2) return js_mkundef();
  if (js_deep_equal(js, args[0], args[1], false))
    return assertion_error(js, "Expected values to not be deeply equal", nargs >= 3 ? args[2] : js_mkundef(), assert_compared(args, nargs, "notDeepEqual"));
  return js_mkundef();
}

static ant_value_t assert_deep_strict_equal(ant_params_t) {
  if (nargs < 2) return js_mkundef();
  if (!js_deep_equal(js, args[0], args[1], true))
    return assertion_error(js, "Expected values to be deeply strictly equal", nargs >= 3 ? args[2] : js_mkundef(), assert_compared(args, nargs, "deepStrictEqual"));
  return js_mkundef();
}

static ant_value_t assert_not_deep_strict_equal(ant_params_t) {
  if (nargs < 2) return js_mkundef();
  if (js_deep_equal(js, args[0], args[1], true))
    return assertion_error(js, "Expected values to not be deeply strictly equal", nargs >= 3 ? args[2] : js_mkundef(), assert_compared(args, nargs, "notDeepStrictEqual"));
  return js_mkundef();
}

static ant_value_t assert_exception_matches(
  ant_t *js, ant_value_t thrown, ant_value_t expected, ant_value_t message
) {
  if (!is_callable(expected)) return js_mkundef();
  ant_value_t prototype = js_get(js, expected, "prototype");
  if (is_err(prototype)) return prototype;
  
  if (!is_undefined(prototype)) {
    ant_value_t matches = do_instanceof(js, thrown, expected);
    if (is_err(matches)) return matches;
    if (js_truthy(js, matches)) return js_mkundef();
  }

  ant_value_t error_ctor = js->primordial_values[ANT_PRIMORDIAL_Error];
  if (js_is_prototype_of(js, error_ctor, expected))
    return assertion_error(
      js, "The error is expected to be an instance of the specified constructor", message,
      (assertion_info_t){ "throws", thrown, expected }
    );

  ant_value_t receiver = js_mkobj(js);
  if (is_err(receiver)) return receiver;
  ant_value_t valid = sv_vm_call(js->vm, js, expected, receiver, &thrown, 1, NULL, js_mkundef());
  
  if (is_err(valid)) return valid;
  if (valid != js_true)
    return assertion_error(
      js, "The validation function is expected to return true", message,
      (assertion_info_t){ "throws", thrown, expected }
    );
  
  return js_mkundef();
}

static ant_value_t assert_missing_exception(ant_t *js, ant_value_t expected, ant_value_t message) {
  if (assert_is_error_instance(js, message)) return js_throw(js, message);

  char kind[160] = "";
  ant_value_t name = is_callable(expected) ? js_get(js, expected, "name") : js_mkundef();
  if (vtype(name) == kTypeString) snprintf(kind, sizeof(kind), " (%s)", js_getstr(js, name, NULL));

  char buf[512];
  if (vtype(message) == kTypeString) snprintf(buf, sizeof(buf), "Missing expected exception%s: %s", kind, js_getstr(js, message, NULL));
  else snprintf(buf, sizeof(buf), "Missing expected exception%s.", kind);

  return assertion_error_ex(js, buf, js_mkundef(), false, (assertion_info_t){ "throws", js_mkundef(), expected });
}

static ant_value_t assert_throws(ant_params_t) {
  if (nargs < 1 || !is_callable(args[0]))
    return js_mkerr(js, "assert.throws: first argument must be a function");

  GC_ROOT_SAVE(mark, js);
  ant_value_t fn = args[0];
  ant_value_t expected = nargs >= 2 ? args[1] : js_mkundef();
  ant_value_t message = nargs >= 3 ? args[2] : js_mkundef();
  ant_value_t thrown = js_mkundef();
  if (vtype(expected) == kTypeString && nargs < 3) {
    message = expected;
    expected = js_mkundef();
  }
  
  GC_ROOT_PIN(js, fn);
  GC_ROOT_PIN(js, expected);
  GC_ROOT_PIN(js, message);
  GC_ROOT_PIN(js, thrown);

  ant_value_t result = sv_vm_call(js->vm, js, fn, js_mkundef(), NULL, 0, NULL, js_mkundef());
  if (!is_err(result)) result = assert_missing_exception(js, expected, message);
  else {
    thrown = js_take_thrown(js, result);
    result = assert_exception_matches(js, thrown, expected, message);
  } GC_ROOT_RESTORE(js, mark);
  
  return result;
}

static ant_value_t assert_does_not_throw(ant_params_t) {
  if (nargs < 1 || vtype(args[0]) != kTypeFunction)
    return js_mkerr(js, "assert.doesNotThrow: first argument must be a function");

  ant_value_t result = sv_vm_call(js->vm, js, args[0], js_mkundef(), NULL, 0, NULL, js_mkundef());
  if (!is_err(result)) return js_mkundef();

  GC_ROOT_SAVE(mark, js);
  ant_value_t thrown = js_take_thrown(js, result);
  
  GC_ROOT_PIN(js, thrown);
  ant_value_t msg = is_object_type(thrown) ? js_get(js, thrown, "message") : js_mkundef();
  
  if (is_err(msg)) {
    GC_ROOT_RESTORE(js, mark);
    return msg;
  }
  
  ant_value_t user_msg = nargs >= 3 ? args[2] : nargs >= 2 && vtype(args[1]) == kTypeString ? args[1] : js_mkundef();
  char head[256] = "Got unwanted exception.";
  if (vtype(user_msg) == kTypeString) snprintf(head, sizeof(head), "Got unwanted exception: %s", js_getstr(js, user_msg, NULL));

  char buf[512];
  snprintf(
    buf, sizeof(buf), "%s\nActual message: \"%s\"", head,
    vtype(msg) == kTypeString ? js_getstr(js, msg, NULL) : js_str(js, thrown)
  );
  
  result = assertion_error_ex(js, buf, js_mkundef(), false, (assertion_info_t){ "doesNotThrow", thrown, js_mkundef() });
  GC_ROOT_RESTORE(js, mark);
  return result;
}

static ant_value_t assert_rejection_settle(
  ant_t *js, ant_value_t promise, ant_value_t result, bool expect_rejection
) {
  bool rejected = promise_was_rejected(result);
  if (rejected) promise_mark_handled(result);

  if (rejected == expect_rejection) js_resolve_promise(js, promise, js_mkundef());
  else {
    const char *message = expect_rejection ? "Missing expected rejection" : "Got unwanted rejection";
    js_reject_promise(js, promise, Ant_Error_Create(js, JS_ERR_TYPE, message));
  }

  return js_mkundef();
}

static ant_value_t assert_rejection_on_settled(ant_params_t) {
  ant_value_t state = js_get_slot(js_getcurrentfunc(js), SLOT_DATA);
  return assert_rejection_settle(
    js, js_get(js, state, "promise"), js_get(js, state, "result"),
    js_truthy(js, js_get(js, state, "expect_rejection"))
  );
}

static ant_value_t assert_rejection(ant_native_params_t, bool expect_rejection) {
  const char *method = expect_rejection ? "rejects" : "doesNotReject";
  if (nargs < 1) return js_mkerr(js, "assert.%s: first argument required", method);

  GC_ROOT_SAVE(mark, js);
  ant_value_t promise = js_mkpromise(js);
  GC_ROOT_PIN(js, promise);
  ant_value_t result = vtype(args[0]) == kTypeFunction
    ? sv_vm_call(js->vm, js, args[0], js_mkundef(), NULL, 0, NULL, js_mkundef())
    : args[0];
  GC_ROOT_PIN(js, result);

  if (is_err(result)) {
    js_reject_promise(js, promise, result);
  } else if (vtype(result) == kTypePromise) {
    ant_value_t state = js_mkobj(js);
    GC_ROOT_PIN(js, state);
    js_set(js, state, "promise", promise);
    js_set(js, state, "result", result);
    js_set(js, state, "expect_rejection", js_bool(expect_rejection));
    ant_value_t on_settled = js_heavy_mkfun(js, assert_rejection_on_settled, state);
    GC_ROOT_PIN(js, on_settled);
    ant_value_t continuation = js_promise_then(js, result, on_settled, on_settled);
    if (is_err(continuation))
      js_reject_promise(js, promise, continuation);
    else promise_mark_handled(continuation);
  } else assert_rejection_settle(js, promise, result, expect_rejection);

  GC_ROOT_RESTORE(js, mark);
  return promise;
}

static ant_value_t assert_rejects(ant_params_t) {
  return assert_rejection(js, args, nargs, true);
}

static ant_value_t assert_does_not_reject(ant_params_t) {
  return assert_rejection(js, args, nargs, false);
}

static ant_value_t assert_regexp(ant_native_params_t, bool expect_match) {
  if (nargs < 2) return js_mkundef();

  ant_value_t test_fn = js_getprop_fallback(js, args[1], "test");
  if (vtype(test_fn) != kTypeFunction && vtype(test_fn) != kTypeBuiltin) {
    const char *method = expect_match ? "match" : "doesNotMatch";
    return js_mkerr(js, "assert.%s: second argument must be a RegExp", method);
  }

  ant_value_t test_args[1] = {args[0]};
  ant_value_t result = sv_vm_call(js->vm, js, test_fn, args[1], test_args, 1, NULL, js_mkundef());

  if (js_truthy(js, result) != expect_match) {
    const char *message = expect_match
      ? "Value does not match the regular expression"
      : "Value matches the regular expression";
    return assertion_error(
      js, message, nargs >= 3 ? args[2] : js_mkundef(),
      assert_compared(args, nargs, expect_match ? "match" : "doesNotMatch")
    );
  }

  return js_mkundef();
}

static ant_value_t assert_match(ant_params_t) {
  return assert_regexp(js, args, nargs, true);
}

static ant_value_t assert_does_not_match(ant_params_t) {
  return assert_regexp(js, args, nargs, false);
}

static ant_value_t assert_assertion_error_ctor(ant_params_t) {
  ant_value_t options = nargs >= 1 && vtype(args[0]) == kTypeObject ? args[0] : js_mkundef();
  assertion_info_t info = { NULL, js_mkundef(), js_mkundef() };
  ant_value_t fields[4] = { js_mkundef(), js_mkundef(), js_mkundef(), js_mkundef() };
  static const char *const names[] = { "message", "actual", "expected", "operator" };

  for (int i = 0; i < 4 && is_object_type(options); i++) {
    fields[i] = js_get(js, options, names[i]);
    if (is_err(fields[i])) return fields[i];
  }

  GC_ROOT_SAVE(mark, js);
  for (int i = 0; i < 4; i++) GC_ROOT_PIN(js, fields[i]);
  info.actual = fields[1];
  info.expected = fields[2];
  info.op = vtype(fields[3]) == kTypeString ? js_getstr(js, fields[3], NULL) : NULL;

  ant_value_t err = assertion_error_make(js, "Failed", fields[0], true, info);
  if (!is_err(err) && is_object_type(call_new_target)) {
    GC_ROOT_PIN(js, err);
    ant_value_t proto = js_get(js, call_new_target, "prototype");
    if (is_object_type(proto)) js_set_proto_init(err, proto);
  }

  GC_ROOT_RESTORE(js, mark);
  return err;
}

ant_value_t assert_library(ant_t *js) {
  ant_value_t assert_obj = js_mkobj(js);
  js_set_slot(assert_obj, SLOT_CFUNC, Ant_Function_Create(assert_ok));
  js_define_function_length(js, assert_obj, 0);
  js_set_function_name(js, assert_obj, "assert", 6);

  js_set(js, assert_obj, "ok", Ant_Function_Create(assert_ok));
  js_set(js, assert_obj, "fail", Ant_Function_CreateWithArity(assert_fail, 1));
  js_set(js, assert_obj, "ifError", Ant_Function_CreateWithArity(assert_if_error, 1));
  js_set(js, assert_obj, "equal", Ant_Function_CreateWithArity(assert_equal, 2));
  js_set(js, assert_obj, "notEqual", Ant_Function_CreateWithArity(assert_not_equal, 2));
  js_set(js, assert_obj, "strictEqual", Ant_Function_CreateWithArity(assert_strict_equal, 2));
  js_set(js, assert_obj, "notStrictEqual", Ant_Function_CreateWithArity(assert_not_strict_equal, 2));
  js_set(js, assert_obj, "deepEqual", Ant_Function_CreateWithArity(assert_deep_equal, 2));
  js_set(js, assert_obj, "notDeepEqual", Ant_Function_CreateWithArity(assert_not_deep_equal, 2));
  js_set(js, assert_obj, "deepStrictEqual", Ant_Function_CreateWithArity(assert_deep_strict_equal, 2));
  js_set(js, assert_obj, "notDeepStrictEqual", Ant_Function_CreateWithArity(assert_not_deep_strict_equal, 2));
  js_set(js, assert_obj, "throws", Ant_Function_CreateWithArity(assert_throws, 1));
  js_set(js, assert_obj, "doesNotThrow", Ant_Function_CreateWithArity(assert_does_not_throw, 1));
  js_set(js, assert_obj, "rejects", Ant_Function_CreateWithArity(assert_rejects, 1));
  js_set(js, assert_obj, "doesNotReject", Ant_Function_CreateWithArity(assert_does_not_reject, 1));
  js_set(js, assert_obj, "match", Ant_Function_CreateWithArity(assert_match, 2));
  js_set(js, assert_obj, "doesNotMatch", Ant_Function_CreateWithArity(assert_does_not_match, 2));

  ant_value_t ae_fn = Ant_Error_CreateConstructorEx(
    js, "AssertionError", Ant_Function_CreateWithArity(assert_assertion_error_ctor, 1), 
    &js->builtins.assertion_error_proto
  );
  
  js_set(js, assert_obj, "AssertionError", ae_fn);
  ant_value_t lib = js_obj_to_func(js, assert_obj);
  js_set(js, lib, "default", lib);
  
  return lib;
}
