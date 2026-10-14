const assert = require('node:assert');
const EventEmitter = require('node:events');

const attrs = (obj, key) => {
  const d = Object.getOwnPropertyDescriptor(obj, key);
  return `${d.writable ? 'w' : '-'}${d.enumerable ? 'e' : '-'}${d.configurable ? 'c' : '-'}`;
};

const intrinsics = {
  Object, Function, Array, String, Number, Boolean, BigInt, Promise, Date, RegExp,
  Error, TypeError, RangeError, AggregateError, SuppressedError, DisposableStack, AsyncDisposableStack,
  AbortController, AbortSignal, EventTarget, Event, Headers, FormData, DOMException,
};

for (const [name, C] of Object.entries(intrinsics)) {
  assert.strictEqual(attrs(C, 'prototype'), '---', `${name}.prototype`);
  assert.strictEqual(attrs(C.prototype, 'constructor'), 'w-c', `${name}.prototype.constructor`);
  assert.strictEqual(C.prototype.constructor, C, name);
  assert.strictEqual(C.name, name);
  assert.strictEqual(attrs(globalThis, name), 'w-c', `globalThis.${name}`);
}

assert.strictEqual(attrs(EventEmitter, 'prototype'), 'w--');
assert.strictEqual(EventEmitter.prototype.constructor, EventEmitter);
assert.strictEqual(EventEmitter.length, 1);
assert.strictEqual(Date.length, 7);
assert.strictEqual(RegExp.length, 2);

for (const C of [AbortController, Headers, FormData, EventTarget, DOMException]) {
  assert.throws(() => C(), TypeError, C.name);
  assert.ok(new C() instanceof C, C.name);
}
assert.throws(() => new AbortSignal(), TypeError);

assert.deepStrictEqual(Object.keys(RegExp), []);
assert.deepStrictEqual(Object.keys(Date), []);
assert.deepStrictEqual(Object.keys(AbortSignal), []);
assert.strictEqual(attrs(DOMException, 'ABORT_ERR'), '-e-');
assert.strictEqual(DOMException.ABORT_ERR, 20);
assert.strictEqual(new DOMException('m', 'AbortError').code, 20);

class Sub extends AbortController {}
const sub = new Sub();
assert.ok(sub instanceof AbortController && sub.signal instanceof AbortSignal);
assert.strictEqual(new (class extends Date {})(0).getTime(), 0);
assert.strictEqual(new (class extends Headers {})({ a: '1' }).get('a'), '1');

for (const E of [EvalError, RangeError, ReferenceError, SyntaxError, TypeError, URIError, AggregateError, SuppressedError]) {
  assert.strictEqual(Object.getPrototypeOf(E), Error, E.name);
  assert.strictEqual(Object.getPrototypeOf(E.prototype), Error.prototype, E.name);
  assert.strictEqual(attrs(E, 'prototype'), '---', E.name);
}
assert.strictEqual(Object.getPrototypeOf(Error), Function.prototype);

assert.strictEqual(Object.getPrototypeOf(AbortSignal), EventTarget);
assert.strictEqual(Object.getPrototypeOf(AbortSignal.prototype), EventTarget.prototype);
const controller = new AbortController();
assert.ok(controller.signal instanceof EventTarget);
assert.strictEqual(controller.signal, controller.signal);
assert.deepStrictEqual(Object.keys(controller), []);
assert.strictEqual(Object.getOwnPropertyDescriptor(controller, 'signal'), undefined);
const signalDesc = Object.getOwnPropertyDescriptor(AbortController.prototype, 'signal');
assert.strictEqual(typeof signalDesc.get, 'function');
assert.strictEqual(signalDesc.enumerable && signalDesc.configurable, true);
assert.throws(() => AbortController.prototype.signal, TypeError);
assert.throws(() => AbortController.prototype.abort.call({}), TypeError);
controller.abort('why');
assert.strictEqual(controller.signal.aborted, true);
assert.strictEqual(controller.signal.reason, 'why');

console.log('builtin constructor shapes OK');
