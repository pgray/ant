const assert = require('node:assert');

const ctors = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array, BigInt64Array, BigUint64Array];

for (const C of ctors) {
  const big = C === BigInt64Array || C === BigUint64Array;
  const v = big ? [4n, 9n] : [4, 9];
  const a = new C(v);
  const forIn = [];
  for (const k in a) forIn.push(k);

  assert.deepStrictEqual(Object.keys(a), ['0', '1'], C.name);
  assert.deepStrictEqual(Object.values(a), v, C.name);
  assert.deepStrictEqual(Object.entries(a), [['0', v[0]], ['1', v[1]]], C.name);
  assert.deepStrictEqual(Object.getOwnPropertyNames(a), ['0', '1'], C.name);
  assert.deepStrictEqual(forIn, ['0', '1'], C.name);
  assert.deepStrictEqual(Object.getOwnPropertyDescriptor(a, '0'), { value: v[0], writable: true, enumerable: true, configurable: true }, C.name);

  assert.strictEqual(a['01'], undefined, C.name);
  assert.strictEqual(a['1.0'], undefined, C.name);
  assert.strictEqual(a['-0'], undefined, C.name);
  a['01'] = 7;
  assert.strictEqual(a['01'], 7, C.name);
  assert.deepStrictEqual(Object.keys(a), ['0', '1', '01'], C.name);

  for (const name of ['buffer', 'byteLength', 'byteOffset', 'length']) {
    assert.strictEqual(Object.hasOwn(a, name), false, `${C.name} ${name}`);
  }

  assert.strictEqual(a.subarray(1).buffer, a.buffer, C.name);
  const ab = new ArrayBuffer(16);
  assert.strictEqual(new C(ab).buffer, ab, C.name);
  assert.strictEqual(new C(ab).buffer, new C(ab).buffer, C.name);

  const bpe = { value: C.BYTES_PER_ELEMENT, writable: false, enumerable: false, configurable: false };
  assert.deepStrictEqual(Object.getOwnPropertyDescriptor(C, 'BYTES_PER_ELEMENT'), bpe, C.name);
  assert.deepStrictEqual(Object.getOwnPropertyDescriptor(C.prototype, 'BYTES_PER_ELEMENT'), bpe, C.name);
  assert.throws(() => { 'use strict'; C.BYTES_PER_ELEMENT = 99; }, TypeError, C.name);
}

{
  const a = new Uint8Array(4);
  a.buffer.transfer();
  assert.deepStrictEqual(Object.keys(a), []);
  assert.deepStrictEqual(Object.getOwnPropertyNames(a), []);
}

{
  const a = new Uint8Array([1, 2, 3]);
  Object.defineProperty(a, 'x', { get() { a.buffer.transfer(); return 'x'; }, enumerable: true });
  assert.deepStrictEqual(Object.entries(a), [['0', 1], ['1', 2], ['2', 3], ['x', 'x']]);
}

console.log('typed array index properties ok');
