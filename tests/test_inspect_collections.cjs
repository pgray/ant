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
const left = {}, right = { left };
left.right = right;
assert.strictEqual(flat([left, right]), '[ <ref *1> { right: { left: [Circular *1] } }, <ref *2> { left: <ref *1> { right: [Circular *2] } } ]');
assert.strictEqual(flat({ a: { b: { c: { d: 1 } } } }), '{ a: { b: { c: [Object] } } }');
assert.strictEqual(flat([[[[1]]]]), '[ [ [ [Array] ] ] ]');
assert.strictEqual(flat({ a: { b: { c: {}, m: new Map(), l: [] } } }), '{ a: { b: { c: {}, m: Map(0) {}, l: [] } } }');
assert.strictEqual(inspect({ a: { b: { c: { d: 1 } } } }, { depth: null }).replace(/\s+/g, ' '), '{ a: { b: { c: { d: 1 } } } }');
assert.strictEqual(inspect({ a: { b: 1 } }, { depth: 0 }).replace(/\s+/g, ' '), '{ a: [Object] }');
let dag = { v: 1 };
for (let i = 0; i < 16; i++) dag = { l: dag, r: dag };
assert.ok(inspect(dag).length < 1024);

const nestedInner = { x: { y: { z: { w: 1 } } } };
nestedInner.self = nestedInner;
const nestedCustom = { [Symbol.inspect]() { return 'C<' + flat(nestedInner) + '>'; } };
const nestedOuter = { a: { b: { c: { d: 1 } } }, custom: nestedCustom };
nestedOuter.loop = nestedOuter;
assert.strictEqual(
  inspect(nestedOuter, { depth: 5 }).replace(/\s+/g, ' '),
  '<ref *1> { a: { b: { c: { d: 1 } } }, custom: C<<ref *1> { x: { y: { z: [Object] } }, self: [Circular *1] }>, loop: [Circular *1] }'
);
const viaProto = {};
viaProto.child = Object.create(viaProto);
assert.strictEqual(flat(viaProto), '{ child: {} }');
const hidden = { x: 1 };
Object.defineProperty(hidden, 'me', { value: hidden, enumerable: false });
assert.strictEqual(flat(hidden), '{ x: 1 }');
const deep = { name: 'deep' };
deep.m = new Map([[1, new Map([[2, new Map([[3, deep]])]])]]);
assert.strictEqual(flat(deep), "{ name: 'deep', m: Map(1) { 1 => Map(1) { 2 => [Map] } } }");

const many = new Map();
for (let i = 0; i < 105; i++) many.set(i, i);
assert.ok(flat(many).endsWith('98 => 98, 99 => 99, ... 5 more items }'));
const manySet = new Set();
for (let i = 0; i < 101; i++) manySet.add(i);
assert.ok(flat(manySet).endsWith('98, 99, ... 1 more item }'));
assert.strictEqual(flat({ m: new Map([[1, new Map([[2, new Map([[3, 4]])]])]]) }), '{ m: Map(1) { 1 => Map(1) { 2 => [Map] } } }');
assert.strictEqual(flat(new Set([new Set([new Set([new Set([1])])])])), 'Set(1) { Set(1) { Set(1) { [Set] } } }');
const huge = new Map();
for (let i = 0; i < 200000; i++) huge.set(i, i);
assert.ok(inspect(huge).length < 4096);

console.log('inspect collections OK');
