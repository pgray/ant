const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const { inspect } = require('node:util');

const poisoned = spawnSync(process.execPath, ['-e', `
  let setterRuns = 0;
  Object.defineProperty(Object.prototype, 'value', { __proto__: null, value: 'PROTO', writable: false, configurable: true });
  Object.defineProperty(Object.prototype, 'enumerable', { __proto__: null, value: 'PROTO', writable: false, configurable: true });
  Object.defineProperty(Object.prototype, 'get', { __proto__: null, set() { setterRuns++; }, configurable: true });
  Object.defineProperty(Object.prototype, 'configurable', { __proto__: null, set() { setterRuns++; }, configurable: true });
  const d = Object.getOwnPropertyDescriptor({ a: 1 }, 'a');
  const g = Object.getOwnPropertyDescriptor({ get x() { return 1; } }, 'x');
  const out = JSON.stringify([Object.keys(d), d.value, d.enumerable, Object.keys(g), setterRuns]);
  for (const k of ['value', 'enumerable', 'get', 'configurable']) delete Object.prototype[k];
  console.log(out);
`], { encoding: 'utf8' });
assert.strictEqual(poisoned.status, 0, poisoned.stderr);
assert.deepStrictEqual(JSON.parse(poisoned.stdout), [
  ['value', 'writable', 'enumerable', 'configurable'], 1, true,
  ['get', 'set', 'enumerable', 'configurable'], 0,
]);

const gopd = (trapResult, target = {}) => Object.getOwnPropertyDescriptor(
  new Proxy(target, { getOwnPropertyDescriptor: () => trapResult }), 'q'
);
assert.throws(() => gopd({ configurable: true, get() {}, value: 1 }), TypeError);
assert.throws(() => gopd({ configurable: true, set() {}, writable: true }), TypeError);
assert.throws(() => gopd({ configurable: true, get: 5 }), TypeError);

const inherited = gopd(Object.create({ configurable: true, value: 7, writable: true, enumerable: true }));
assert.deepStrictEqual(inherited, { value: 7, writable: true, enumerable: true, configurable: true });
const inheritedGetter = function () { return 1; };
assert.strictEqual(gopd(Object.create({ configurable: true, get: inheritedGetter })).get, inheritedGetter);

const frozen = Object.defineProperty({}, 'q', { value: 1, writable: false, configurable: false });
assert.throws(() => gopd({ value: 2, writable: false, configurable: false }, frozen), TypeError);
assert.deepStrictEqual(gopd({ value: 1, configurable: false }, frozen), { value: 1, writable: false, enumerable: false, configurable: false });
const writableFixed = Object.defineProperty({}, 'q', { value: 1, writable: true, configurable: false });
assert.throws(() => gopd({ value: 1, writable: false, configurable: false }, writableFixed), TypeError);
assert.throws(() => gopd({ value: 1, configurable: false }), TypeError);

assert.throws(() => new Error(Symbol('x')), TypeError);
assert.throws(() => new TypeError(Symbol('x')), TypeError);
assert.throws(() => new AggregateError([], Symbol('x')), TypeError);
assert.throws(() => new SuppressedError(1, 2, Symbol('x')), TypeError);
assert.strictEqual(new AggregateError([], { toString: () => 'm' }).message, 'm');
assert.strictEqual(new AggregateError([], 'm', Object.create({ cause: 4 })).cause, 4);
assert.deepStrictEqual(Object.getOwnPropertyNames(new SuppressedError(1, 2, 'm')).filter(k => k !== 'stack'), ['message', 'error', 'suppressed']);

class A {}
class B extends A { constructor() { super(); } }
class F { x = 1; }
const anon = class {};
assert.strictEqual(inspect(A), '[class A]');
assert.strictEqual(inspect(B), '[class B extends A]');
assert.strictEqual(inspect(F), '[class F]');
assert.strictEqual(inspect(anon), '[class anon]');
assert.strictEqual(inspect(class extends A {}), '[class (anonymous) extends A]');
assert.strictEqual(inspect(class extends Array {}), '[class (anonymous) extends Array]');
assert.strictEqual(inspect(Object.assign(class D {}, { x: 1 })), '[class D] { x: 1 }');
assert.strictEqual(inspect(function f() {}), '[Function: f]');
assert.strictEqual(inspect({ class() {} }.class), '[Function: class]');

const s = Symbol('s');
assert.strictEqual(inspect({ [s]: 2, [Symbol()]: 3 }), '{ Symbol(s): 2, Symbol(): 3 }');
assert.strictEqual(inspect({ get [s]() { return 1; } }), '{ Symbol(s): [Getter] }');

console.log('descriptor proxy error inspect OK');
