const assert = require('node:assert');

const view = new Uint8Array([1, 2, 3, 4]);
const fails = (offset, length, code, message) => assert.throws(
  () => Buffer.copyBytesFrom(view, offset, length),
  e => e.code === code && e.message === message,
  `${String(offset)}, ${String(length)}`
);

fails('1', undefined, 'ERR_INVALID_ARG_TYPE', `The "offset" argument must be of type number. Received type string ('1')`);
fails(null, undefined, 'ERR_INVALID_ARG_TYPE', 'The "offset" argument must be of type number. Received null');
fails(0n, undefined, 'ERR_INVALID_ARG_TYPE', 'The "offset" argument must be of type number. Received type bigint (0n)');
fails(1.5, undefined, 'ERR_OUT_OF_RANGE', 'The value of "offset" is out of range. It must be an integer. Received 1.5');
fails(NaN, undefined, 'ERR_OUT_OF_RANGE', 'The value of "offset" is out of range. It must be an integer. Received NaN');
fails(Infinity, undefined, 'ERR_OUT_OF_RANGE', 'The value of "offset" is out of range. It must be an integer. Received Infinity');
fails(-1, undefined, 'ERR_OUT_OF_RANGE', 'The value of "offset" is out of range. It must be >= 0 && <= 9007199254740991. Received -1');
fails(2 ** 53, undefined, 'ERR_OUT_OF_RANGE', 'The value of "offset" is out of range. It must be >= 0 && <= 9007199254740991. Received 9_007_199_254_740_992');
fails(1, '2', 'ERR_INVALID_ARG_TYPE', `The "length" argument must be of type number. Received type string ('2')`);
fails(1, -1, 'ERR_OUT_OF_RANGE', 'The value of "length" is out of range. It must be >= 0 && <= 9007199254740991. Received -1');
fails(1, 1.5, 'ERR_OUT_OF_RANGE', 'The value of "length" is out of range. It must be an integer. Received 1.5');

assert.throws(() => Buffer.copyBytesFrom([1]), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'The "view" argument must be an instance of TypedArray. Received an instance of Array',
});
assert.throws(() => Buffer.copyBytesFrom(new DataView(new ArrayBuffer(1))), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'The "view" argument must be an instance of TypedArray. Received an instance of DataView',
});

assert.deepStrictEqual([...Buffer.copyBytesFrom(view, 10)], []);
assert.deepStrictEqual([...Buffer.copyBytesFrom(view, 1, 10)], [2, 3, 4]);
assert.deepStrictEqual([...Buffer.copyBytesFrom(view, undefined, 2)], [1, 2]);
assert.deepStrictEqual([...Buffer.copyBytesFrom(new Uint16Array([1, 2]), 1)], [2, 0]);

console.log('buffer copyBytesFrom errors OK');
