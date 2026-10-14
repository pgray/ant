const assert = require('node:assert');

const detached = (Ctor = Uint8Array, length = 8, byteOffset = 0) => {
  const buffer = new ArrayBuffer(byteOffset + length * Ctor.BYTES_PER_ELEMENT);
  const view = new Ctor(buffer, byteOffset, length);
  buffer.transfer();
  return view;
};
const detachedBuffer = () => {
  const buffer = Buffer.alloc(8);
  structuredClone(buffer.buffer, { transfer: [buffer.buffer] });
  return buffer;
};

const throwsType = {
  slice: () => detached().slice(0),
  fill: () => detached().fill(1),
  with: () => detached().with(0, 1),
  toSorted: () => detached().toSorted(),
  toReversed: () => detached().toReversed(),
  subarray: () => detached().subarray(0),
  set: () => detached().set([1]),
  setFromDetached: () => new Uint8Array(8).set(detached()),
  copyWithin: () => detached().copyWithin(0, 1),
  indexOf: () => detached().indexOf(0),
  lastIndexOf: () => detached().lastIndexOf(0),
  includes: () => detached().includes(0),
  join: () => detached().join(),
  values: () => detached().values(),
  from: () => Uint8Array.from(detached()),
  constructFrom: () => new Uint8Array(detached()),
  atomicsLoad: () => Atomics.load(detached(Int32Array, 2), 0),
  atomicsStore: () => Atomics.store(detached(Int32Array, 2), 0, 1),
  atomicsAdd: () => Atomics.add(detached(Int32Array, 2), 0, 1),
  bufferConcat: () => Buffer.concat([detachedBuffer(), Buffer.from('x')]),
  blob: () => new Blob([detached()]),
  bigOffset: () => detached(Uint8Array, 8, 1 << 20).slice(0),
};
for (const [name, op] of Object.entries(throwsType)) assert.throws(op, TypeError, name);

try {
  assert.strictEqual(crypto.getRandomValues(detached()).length, 0);
} catch (e) {
  assert.ok(e instanceof TypeError, 'getRandomValues');
}

assert.strictEqual(detached().length, 0);
assert.strictEqual(detached()[0], undefined);
assert.strictEqual(detachedBuffer().equals(Buffer.alloc(8)), false);
assert.strictEqual(detachedBuffer().compare(Buffer.alloc(8)), -1);
assert.strictEqual(Buffer.compare(detachedBuffer(), Buffer.alloc(8)), -1);
assert.strictEqual(detachedBuffer().copy(Buffer.alloc(8)), 0);
assert.strictEqual(Buffer.alloc(8).copy(detachedBuffer()), 0);
assert.strictEqual(detachedBuffer().toString(), '');
assert.strictEqual(detachedBuffer().write('hi'), 0);
assert.strictEqual(detachedBuffer().indexOf('a'), -1);
assert.throws(() => detachedBuffer().readUInt8(0), RangeError);
assert.deepStrictEqual(new TextEncoder().encodeInto('ab', detached()), { read: 0, written: 0 });
assert.strictEqual(new TextDecoder().decode(detached()), '');

console.log('detached views OK');
