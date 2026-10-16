const assert = require('node:assert');
const util = require('node:util');

assert.strictEqual(String.prototype.toString(), '');
assert.strictEqual(String(String.prototype), '');
assert.strictEqual(String.prototype.length, 0);
assert.deepStrictEqual([...String.prototype[Symbol.iterator]()], []);
assert.strictEqual(Number.prototype.valueOf(), 0);
assert.strictEqual(Boolean.prototype.valueOf(), false);
assert.strictEqual(Object.prototype.toString.call(String.prototype), '[object String]');
assert.strictEqual(util.types.isStringObject(String.prototype), true);
assert.strictEqual(util.inspect(String.prototype), '{}');

assert.strictEqual(String.prototype.toString.call('p'), 'p');
assert.strictEqual(String.prototype.valueOf.call(new String('w')), 'w');
assert.throws(() => String({ toString: String.prototype.toString }), {
  name: 'TypeError', message: "String.prototype.toString requires that 'this' be a String",
});
assert.throws(() => String.prototype.valueOf.call({}), {
  name: 'TypeError', message: "String.prototype.valueOf requires that 'this' be a String",
});
assert.throws(() => String.prototype.toString.call(5), TypeError);

assert.throws(() => String({ toString: Object.prototype.toLocaleString }), RangeError);
assert.throws(() => +{ valueOf: Object.prototype.toLocaleString, toString: Object.prototype.toLocaleString }, RangeError);

console.log('primitive prototype values OK');
