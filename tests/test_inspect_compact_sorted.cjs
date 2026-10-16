const assert = require('node:assert');
const { inspect } = require('node:util');

const expanded = v => inspect(v, { compact: false, sorted: true, depth: 1000 });

assert.strictEqual(expanded({ b: 1, a: 2 }), '{\n  a: 2,\n  b: 1\n}');
assert.strictEqual(expanded([1, 2]), '[\n  1,\n  2\n]');
assert.strictEqual(expanded({ a: { b: [1, { c: 2 }] } }), '{\n  a: {\n    b: [\n      1,\n      {\n        c: 2\n      }\n    ]\n  }\n}');
assert.strictEqual(expanded(new Map([['x', 1]])), "Map(1) {\n  'x' => 1\n}");
assert.strictEqual(expanded([]), '[]');
assert.strictEqual(expanded({}), '{}');
assert.strictEqual(expanded('str'), "'str'");
assert.strictEqual(inspect({ b: 1, a: 2 }), '{ b: 1, a: 2 }');
assert.strictEqual(inspect({ b: 1, a: 2 }, { sorted: true }), '{ a: 2, b: 1 }');

assert.strictEqual(inspect(-0), '-0');
assert.strictEqual(inspect([-0]), '[ -0 ]');
assert.strictEqual(String(-0), '0');
assert.strictEqual([-0, 1].join(), '0,1');
assert.strictEqual(JSON.stringify([-0]), '[0]');

console.log('inspect compact/sorted OK');
