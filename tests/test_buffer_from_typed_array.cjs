const assert = require('node:assert');

const src = Buffer.from([1, 2, 3, 250]);
const copy = Buffer.from(src);
assert.deepStrictEqual([...copy], [1, 2, 3, 250]);
copy[0] = 9;
assert.strictEqual(src[0], 1);

const backing = new Uint8Array([0, 1, 2, 3, 4, 5, 6, 7]);
assert.deepStrictEqual([...Buffer.from(backing.subarray(2, 5))], [2, 3, 4]);
assert.deepStrictEqual([...Buffer.from(new Int8Array([-1, -128, 127]))], [255, 128, 127]);
assert.deepStrictEqual([...Buffer.from(new Uint8ClampedArray([0, 255, 300]))], [0, 255, 255]);
assert.deepStrictEqual([...Buffer.from(new Uint16Array([1, 256, 65535]))], [1, 0, 255]);
assert.deepStrictEqual([...Buffer.from(new Int32Array([-1, 513]))], [255, 1]);
assert.deepStrictEqual([...Buffer.from(new Float64Array([1.7, -1, NaN, 256.5]))], [1, 255, 0, 0]);
assert.deepStrictEqual([...Buffer.from(new Float32Array(0))], []);
assert.throws(() => Buffer.from(new BigInt64Array([1n])), TypeError);

const big = new Uint8Array(1 << 20).fill(7);
big[12345] = 42;
const bigCopy = Buffer.from(big);
assert.strictEqual(bigCopy.length, 1 << 20);
assert.strictEqual(bigCopy[12345], 42);
assert.strictEqual(bigCopy[0], 7);

console.log('ok');
