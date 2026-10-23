const assert = require('node:assert');

const intTypes = [Int8Array, Uint8Array, Int16Array, Uint16Array, Int32Array, Uint32Array];

for (const Ctor of intTypes) {
  const view = new Ctor(new SharedArrayBuffer(Ctor.BYTES_PER_ELEMENT * 4), Ctor.BYTES_PER_ELEMENT, 2);
  const name = Ctor.name;

  assert.strictEqual(Atomics.store(view, 1, 5), 5, name);
  assert.strictEqual(Atomics.load(view, 1), 5, name);
  assert.strictEqual(Atomics.add(view, 1, 3), 5, name);
  assert.strictEqual(Atomics.sub(view, 1, 2), 8, name);
  assert.strictEqual(Atomics.and(view, 1, 3), 6, name);
  assert.strictEqual(Atomics.or(view, 1, 8), 2, name);
  assert.strictEqual(Atomics.xor(view, 1, 3), 10, name);
  assert.strictEqual(Atomics.exchange(view, 1, 7), 9, name);
  assert.strictEqual(Atomics.compareExchange(view, 1, 1, 4), 7, name);
  assert.strictEqual(Atomics.compareExchange(view, 1, 7, 4), 7, name);
  assert.strictEqual(Atomics.load(view, 1), 4, name);
  assert.strictEqual(Atomics.load(view, 0), 0, name);
  assert.deepStrictEqual([...new Ctor(view.buffer)], [0, 0, 4, 0], name);
}

{
  const view = new Uint32Array(new SharedArrayBuffer(8));
  Atomics.store(view, 0, 0xffffffff);
  assert.strictEqual(Atomics.load(view, 0), 0xffffffff);
  assert.strictEqual(Atomics.add(view, 0, 1), 0xffffffff);
  assert.strictEqual(Atomics.load(view, 0), 0);
  assert.strictEqual(Atomics.sub(view, 0, 1), 0);
  assert.strictEqual(Atomics.exchange(view, 0, 1), 0xffffffff);
}

{
  const view = new Int8Array(new SharedArrayBuffer(1));
  Atomics.store(view, 0, 127);
  assert.strictEqual(Atomics.add(view, 0, 1), 127);
  assert.strictEqual(Atomics.load(view, 0), -128);
}

for (const Ctor of [Uint8ClampedArray, Float32Array, Float64Array]) {
  const view = new Ctor(new SharedArrayBuffer(Ctor.BYTES_PER_ELEMENT * 2));
  assert.throws(() => Atomics.add(view, 0, 1), TypeError, Ctor.name);
  assert.throws(() => Atomics.load(view, 0), TypeError, Ctor.name);
}

assert.throws(() => Atomics.load(new Int32Array(2), 2), RangeError);
assert.throws(() => Atomics.load(new Int32Array(2), -1), RangeError);

{
  const view = new Int32Array(new SharedArrayBuffer(8));
  view[0] = 7;
  view[1] = 9;
  for (const index of [NaN, undefined, -0.5, -0, 0.9]) assert.strictEqual(Atomics.load(view, index), 7, String(index));
  for (const index of [1.5, '1']) assert.strictEqual(Atomics.load(view, index), 9, String(index));
  for (const index of [-1, 2, Infinity, -Infinity]) assert.throws(() => Atomics.load(view, index), RangeError, String(index));
}

{
  const view = new Int8Array(new SharedArrayBuffer(2));
  assert.strictEqual(Atomics.store(view, 0, 300), 300);
  assert.strictEqual(Atomics.load(view, 0), 44);
  assert.strictEqual(Atomics.store(view, 0, -1.5), -1);
  assert.strictEqual(Object.is(Atomics.store(view, 0, -0), 0), true);
  assert.strictEqual(Atomics.store(view, 0, NaN), 0);
  assert.strictEqual(Atomics.store(view, '1', '7'), 7);
  assert.strictEqual(Atomics.load(view, 1.9), 7);
  assert.strictEqual(Atomics.add(view, 1, { valueOf: () => 2 }), 7);
  assert.strictEqual(Atomics.load(view, 1), 9);
}

console.log('ok');
