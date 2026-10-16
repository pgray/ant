const assert = require('node:assert');
const util = require('node:util');

assert.strictEqual(util.format('%o', [1]), '[ 1, [length]: 1 ]');
assert.strictEqual(util.format('%o', []), '[ [length]: 0 ]');
assert.strictEqual(util.format('%o', { a: 1 }), '{ a: 1 }');
assert.strictEqual(util.format('%o', Object.defineProperty({ v: 1 }, 'h', { value: 2 })), '{ v: 1, [h]: 2 }');
assert.strictEqual(util.format('%O', [1]), '[ 1 ]');
assert.strictEqual(util.format('%o', 'str'), "'str'");
assert.strictEqual(util.inspect([1, 2], { showHidden: true }), '[ 1, 2, [length]: 2 ]');
assert.strictEqual(util.inspect([1], true), '[ 1, [length]: 1 ]');
assert.strictEqual(util.inspect(Object.defineProperty({}, 'g', { get() { return 1; } }), { showHidden: true }), '{ [g]: [Getter] }');
assert.strictEqual(util.inspect(Object.defineProperty([1], 'x', { value: 9 })), '[ 1 ]');
assert.strictEqual(util.inspect(Array.prototype.values, { showHidden: true }), "[Function: values] { [length]: 0, [name]: 'values' }");

const args = util.format('%o', (function () { return arguments; })(1));
assert.ok(args.startsWith("[Arguments] {\n  '0': 1,\n  [length]: 1,\n  [callee]: "), args);
assert.ok(args.includes("[Symbol(Symbol.iterator)]: [Function: values] { [length]: 0, [name]: 'values' }"), args);

const fn = util.format('%o', function f() {});
assert.ok(fn.startsWith("<ref *1> [Function: f] {\n  [length]: 0,\n  [name]: 'f',\n"), fn);
assert.ok(fn.includes('[constructor]: [Circular *1]'), fn);

assert.deepStrictEqual(Reflect.ownKeys(function f() {}).filter(k => k !== 'arguments' && k !== 'caller'), ['length', 'name', 'prototype']);
assert.deepStrictEqual(Reflect.ownKeys(function* g() {}), ['length', 'name', 'prototype']);

console.log('inspect show hidden OK');
