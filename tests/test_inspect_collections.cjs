const assert = require('node:assert');
const { inspect } = require('node:util');

const map = new Map([[1, 2], ['a', 'b']]);
assert.strictEqual(inspect(map), "Map(2) { 1 => 2, 'a' => 'b' }");
map.delete(1);
assert.strictEqual(inspect(map), "Map(1) { 'a' => 'b' }");
assert.strictEqual(inspect(new Map()), 'Map(0) {}');
assert.strictEqual(inspect(new Set()), 'Set(0) {}');
assert.strictEqual(inspect(new Set([1, 'x', 3n])), "Set(3) { 1, 'x', 3n }");
assert.strictEqual(inspect(new Map([[1n, 2n]])), 'Map(1) { 1n => 2n }');

const tagged = new Map([[1, 2]]);
tagged.extra = true;
assert.strictEqual(inspect(tagged), 'Map(1) { 1 => 2, extra: true }');
assert.strictEqual(inspect(new (class Bag extends Set {})([1])), 'Bag(1) [Set] { 1 }');

assert.strictEqual(inspect(5n), '5n');
assert.strictEqual(inspect(-12345678901234567890n), '-12345678901234567890n');
assert.strictEqual(inspect([5n, -1n]), '[ 5n, -1n ]');
assert.strictEqual(inspect({ a: 2n }), '{ a: 2n }');
assert.strictEqual(inspect(new BigInt64Array([1n, -2n])), 'BigInt64Array(2) [ 1n, -2n ]');
assert.strictEqual(String(5n), '5');
assert.strictEqual(`${5n}`, '5');
assert.strictEqual([1n, 2n].join(), '1,2');

console.log('inspect collections OK');
