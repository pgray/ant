const assert = require('node:assert');

assert.throws(() => JSON.stringify({ a: 1n }), TypeError);

BigInt.prototype.toJSON = function (key) { return `${this}:${key}`; };
assert.strictEqual(JSON.stringify({ a: 1n, b: [2n], c: { d: -3n } }), '{"a":"1:a","b":["2:0"],"c":{"d":"-3:d"}}');
assert.strictEqual(JSON.stringify(5n), '"5:"');
assert.strictEqual(JSON.stringify({ x: Object(4n) }), '{"x":"4:x"}');

BigInt.prototype.toJSON = function () { return 7n; };
assert.throws(() => JSON.stringify({ a: 1n }), TypeError);

delete BigInt.prototype.toJSON;
Object.defineProperty(BigInt.prototype, 'toJSON', { get() { return () => 'getter'; }, configurable: true });
assert.strictEqual(JSON.stringify([1n]), '["getter"]');
delete BigInt.prototype.toJSON;

console.log('json bigint toJSON OK');
