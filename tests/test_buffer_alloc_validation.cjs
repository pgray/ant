const assert = require('node:assert');

const check = (fn, name, code, message) => assert.throws(fn, e => e.name === name && e.code === code && e.message === message);
const range = got => `The value of "size" is out of range. It must be >= 0 && <= 9007199254740991. Received ${got}`;

for (const alloc of [Buffer.alloc, Buffer.allocUnsafe, Buffer.allocUnsafeSlow]) {
  check(() => alloc(-1), 'RangeError', 'ERR_OUT_OF_RANGE', range('-1'));
  check(() => alloc(NaN), 'RangeError', 'ERR_OUT_OF_RANGE', range('NaN'));
  check(() => alloc(2 ** 53), 'RangeError', 'ERR_OUT_OF_RANGE', range('9_007_199_254_740_992'));
  check(() => alloc('3'), 'TypeError', 'ERR_INVALID_ARG_TYPE', `The "size" argument must be of type number. Received type string ('3')`);
  check(() => alloc(), 'TypeError', 'ERR_INVALID_ARG_TYPE', 'The "size" argument must be of type number. Received undefined');
  assert.strictEqual(alloc(1.5).length, 1);
  assert.strictEqual(alloc(0).length, 0);
}
assert.deepStrictEqual([...Buffer.alloc(3, 'ab')], [97, 98, 97]);

console.log('buffer alloc validation OK');
