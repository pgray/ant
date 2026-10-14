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

const flat = value => inspect(value).replace(/\s+/g, ' ');
const self = { a: 1 };
self.self = self;
assert.strictEqual(flat(self), '<ref *1> { a: 1, self: [Circular *1] }');
assert.strictEqual(flat([self, self]), '[ <ref *1> { a: 1, self: [Circular *1] }, <ref *1> { a: 1, self: [Circular *1] } ]');
const parent = { c: {} };
parent.c.parent = parent;
assert.strictEqual(flat(parent), '<ref *1> { c: { parent: [Circular *1] } }');
const list = [1];
list.push(list);
assert.strictEqual(flat(list), '<ref *1> [ 1, [Circular *1] ]');
const loop = new Map();
loop.set('m', loop);
assert.strictEqual(flat(loop), "<ref *1> Map(1) { 'm' => [Circular *1] }");
const shared = { x: 1 };
assert.strictEqual(flat([shared, shared]), '[ { x: 1 }, { x: 1 } ]');

console.log('inspect collections OK');
