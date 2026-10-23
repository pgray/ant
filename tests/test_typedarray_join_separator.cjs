const assert = require('node:assert');

for (const length of [1, 31, 33, 64, 1000, 100000]) {
  const sep = 'x'.repeat(length);
  assert.strictEqual(Uint8Array.of(1, 2).join(sep), `1${sep}2`);
  assert.strictEqual(Int32Array.of(-7, 8, 9).join(sep), ['-7', '8', '9'].join(sep));
  assert.strictEqual(Float64Array.of(1.5, -2, 3).join(sep), ['1.5', '-2', '3'].join(sep));
}

assert.strictEqual(new Uint8Array(1000).join('ab').length, 1000 + 999 * 2);
assert.strictEqual(Uint8Array.of(5).join('x'.repeat(100000)), '5');

console.log('ok');
