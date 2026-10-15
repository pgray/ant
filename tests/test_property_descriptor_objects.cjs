const assert = require('node:assert');

const keys = d => Object.keys(d);
const sizeDesc = Object.getOwnPropertyDescriptor(Map.prototype, 'size');
assert.deepStrictEqual(keys(sizeDesc), ['get', 'set', 'enumerable', 'configurable']);
assert.ok('set' in sizeDesc);
assert.strictEqual(sizeDesc.set, undefined);
assert.strictEqual(Object.getPrototypeOf(sizeDesc), Object.prototype);

const setterOnly = Object.getOwnPropertyDescriptor({ set x(v) {} }, 'x');
assert.deepStrictEqual(keys(setterOnly), ['get', 'set', 'enumerable', 'configurable']);
assert.strictEqual(setterOnly.get, undefined);

const flagsDesc = Object.getOwnPropertyDescriptor(RegExp.prototype, 'flags');
assert.ok('set' in flagsDesc);

for (const desc of [
  Object.getOwnPropertyDescriptor({ a: 1 }, 'a'),
  Object.getOwnPropertyDescriptor([5], 0),
  Object.getOwnPropertyDescriptor([5], 'length'),
  Object.getOwnPropertyDescriptor('ab', 1),
  Reflect.getOwnPropertyDescriptor(Math.max, 'name'),
  Object.getOwnPropertyDescriptors({ a: 1 }).a,
]) {
  assert.deepStrictEqual(keys(desc), ['value', 'writable', 'enumerable', 'configurable']);
  assert.strictEqual(Object.getPrototypeOf(desc), Object.prototype);
}

const first = Object.getOwnPropertyDescriptor({ a: 1 }, 'a');
first.extra = true;
assert.deepStrictEqual(keys(Object.getOwnPropertyDescriptor({ b: 2 }, 'b')), ['value', 'writable', 'enumerable', 'configurable']);

const trapResult = { configurable: true, value: 3 };
const proxied = new Proxy({}, { getOwnPropertyDescriptor: () => trapResult });
const completed = Object.getOwnPropertyDescriptor(proxied, 'q');
assert.notStrictEqual(completed, trapResult);
assert.deepStrictEqual(completed, { value: 3, writable: false, enumerable: false, configurable: true });
assert.deepStrictEqual(trapResult, { configurable: true, value: 3 });
const accessorProxy = new Proxy({}, { getOwnPropertyDescriptor: () => ({ configurable: true, get() {} }) });
assert.ok(Object.hasOwn(Object.getOwnPropertyDescriptor(accessorProxy, 'q'), 'set'));

const sym = Symbol('s');
const defined = Object.defineProperties({}, { [sym]: { value: 1, enumerable: true }, k: { value: 2 } });
assert.strictEqual(defined[sym], 1);
assert.strictEqual(defined.k, 2);
assert.strictEqual(Object.create({}, { [sym]: { value: 3 } })[sym], 3);
assert.strictEqual(Object.create(null, { [Symbol.toStringTag]: { value: 'N' } })[Symbol.toStringTag], 'N');
const hiddenSym = Object.defineProperty({}, sym, { value: { value: 9 }, enumerable: false });
assert.strictEqual(Object.getOwnPropertySymbols(Object.defineProperties({}, hiddenSym)).length, 0);

for (const name of ['JSON', 'Reflect', 'Atomics', 'WebAssembly'])
  assert.strictEqual(Object.getPrototypeOf(globalThis[name]), Object.prototype, name);
for (const name of ['crypto', 'performance', 'navigator'])
  assert.notStrictEqual(Object.getPrototypeOf(globalThis[name]), null, name);
assert.strictEqual(String(JSON), '[object JSON]');
assert.strictEqual(String(WebAssembly), '[object WebAssembly]');

assert.ok(Object.hasOwn(TypeError.prototype, 'message'));
assert.strictEqual(TypeError.prototype.message, '');
assert.strictEqual(Object.hasOwn(new Error(undefined), 'message'), false);
assert.strictEqual(new Error({}).message, '[object Object]');
assert.strictEqual(new Error('x', { get cause() { return 7; } }).cause, 7);
assert.strictEqual(new Error('x', Object.create({ cause: 3 })).cause, 3);
assert.strictEqual(Object.hasOwn(new Error('x', {}), 'cause'), false);
assert.throws(() => new Error({ toString() { throw new RangeError('no'); } }), RangeError);

console.log('property descriptor objects OK');
