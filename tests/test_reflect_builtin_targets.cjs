const assert = require('node:assert');

assert.deepStrictEqual(Reflect.ownKeys(parseInt), ['length', 'name']);
assert.deepStrictEqual(Reflect.getOwnPropertyDescriptor(parseInt, 'length'), { value: 2, writable: false, enumerable: false, configurable: true });
assert.strictEqual(Reflect.get(parseInt, 'length'), 2);
assert.strictEqual(Reflect.get(Math.max, 'name'), 'max');
assert.strictEqual(Reflect.has(parseInt, 'length'), true);
assert.strictEqual(Reflect.getPrototypeOf(parseInt), Function.prototype);

const target = Math.min;
assert.strictEqual(Reflect.isExtensible(target), true);
assert.strictEqual(Reflect.defineProperty(target, 'extra', { value: 2, configurable: true }), true);
assert.strictEqual(target.extra, 2);
assert.strictEqual(Reflect.deleteProperty(target, 'extra'), true);
assert.strictEqual(Reflect.preventExtensions(target), true);
assert.strictEqual(Reflect.isExtensible(target), false);
assert.strictEqual(Reflect.setPrototypeOf(Math.abs, null), true);
assert.strictEqual(Reflect.getPrototypeOf(Math.abs), null);

for (const method of ['get', 'set', 'has', 'deleteProperty', 'ownKeys', 'getOwnPropertyDescriptor', 'defineProperty', 'getPrototypeOf', 'setPrototypeOf', 'isExtensible', 'preventExtensions'])
  assert.throws(() => Reflect[method](1, 'x', {}), TypeError, method);
assert.throws(() => Reflect.setPrototypeOf({}, 1), TypeError);

assert.strictEqual(Math.max.bind(null).length, 2);
assert.strictEqual(parseInt.bind(null, '1').length, 1);
assert.strictEqual(Object.defineProperty(function (a) {}, 'length', { value: 7 }).bind(null).length, 7);

console.log('reflect builtin targets OK');
