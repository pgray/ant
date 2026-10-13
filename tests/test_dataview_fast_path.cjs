const assert = require('node:assert');

const offsets = [3, 3.9, '3', { valueOf() { return 0; } }, -0, NaN, undefined, -1, -0.5, 8, 7.5, 0.5, 2 ** 53, Symbol(), 1n];
const outcome = fn => { try { return fn(); } catch (e) { return e.constructor.name; } };

const run = method => {
  const dv = new DataView(new ArrayBuffer(16), 4, 8);
  dv.setUint8(3, 7);
  return offsets.map(off => outcome(() => method.startsWith('get') ? dv[method](off) : (dv[method](off, 9), dv.getUint8(0))));
};

assert.deepStrictEqual(run('getUint8'), [7, 7, 7, 0, 0, 0, 0, 'RangeError', 0, 'RangeError', 0, 0, 'RangeError', 'TypeError', 'TypeError']);
assert.deepStrictEqual(run('setUint8'), [0, 0, 0, 9, 9, 9, 9, 'RangeError', 9, 'RangeError', 9, 9, 'RangeError', 'TypeError', 'TypeError']);
assert.deepStrictEqual(run('setFloat64'), ['RangeError', 'RangeError', 'RangeError', 64, 64, 64, 64, 'RangeError', 64, 'RangeError', 'RangeError', 64, 'RangeError', 'TypeError', 'TypeError']);

const detached = new DataView(new ArrayBuffer(8));
structuredClone(detached.buffer, { transfer: [detached.buffer] });
assert.strictEqual(outcome(() => detached.getUint8(0)), 'TypeError');
assert.strictEqual(outcome(() => detached.setUint8(0, 1)), 'TypeError');

const late = new DataView(new ArrayBuffer(8));
assert.strictEqual(outcome(() => late.setUint8(0, { valueOf() { structuredClone(late.buffer, { transfer: [late.buffer] }); return 1; } })), 'TypeError');

const big = new DataView(new ArrayBuffer(8));
assert.strictEqual(outcome(() => big.setBigInt64(0, 1)), 'TypeError');

const words = new DataView(new ArrayBuffer(4));
for (const [value, expected] of [[-1, 4294967295], [2 ** 32 + 5, 5], [-(2 ** 31), 2147483648], [3.7, 3], [-0.5, 0], [NaN, 0], [Infinity, 0]]) {
  words.setUint32(0, value);
  assert.strictEqual(words.getUint32(0), expected, String(value));
}

console.log('dataview fast path OK');
