// detached buffers and views over them must not be cloned or leak their old bytes
const assert = require('node:assert');

const isDataCloneError = e => e && e.name === 'DataCloneError';

function detached() {
  const buffer = new ArrayBuffer(8);
  const bytes = new Uint8Array(buffer);
  const view = new DataView(buffer);
  bytes[0] = 7;
  const moved = structuredClone(buffer, { transfer: [buffer] });
  return { buffer, bytes, view, moved };
}

const { buffer, bytes, view, moved } = detached();
assert.strictEqual(buffer.byteLength, 0);
assert.strictEqual(moved.byteLength, 8);
assert.strictEqual(new Uint8Array(moved)[0], 7);

assert.throws(() => structuredClone(buffer), isDataCloneError);
assert.throws(() => structuredClone(bytes), isDataCloneError);
assert.throws(() => structuredClone(view), isDataCloneError);
assert.throws(() => structuredClone({ nested: [bytes] }), isDataCloneError);

const live = new Uint8Array([1, 2, 3]);
assert.deepStrictEqual(Array.from(structuredClone(live)), [1, 2, 3]);

const unreachedA = new ArrayBuffer(4);
const unreachedB = new ArrayBuffer(4);
structuredClone(unreachedA, { transfer: [unreachedA, unreachedB] });
assert.deepStrictEqual([unreachedA.byteLength, unreachedB.byteLength], [0, 0]);

const duplicate = new ArrayBuffer(4);
assert.throws(() => structuredClone(duplicate, { transfer: [duplicate, duplicate] }), isDataCloneError);
assert.strictEqual(duplicate.byteLength, 4);
assert.throws(() => structuredClone(1, { transfer: [buffer] }), isDataCloneError);
assert.throws(() => structuredClone(1, { transfer: [{}] }), isDataCloneError);
assert.throws(() => structuredClone(1, { transfer: [new Uint8Array(2)] }), isDataCloneError);

const failing = new ArrayBuffer(4);
assert.throws(() => structuredClone({ failing, f() {} }, { transfer: [failing] }), isDataCloneError);
assert.strictEqual(failing.byteLength, 4);

const source = new Uint8Array([1, 2, 3, 4, 5, 6]);
const views = structuredClone({ part: source.subarray(2, 4), whole: source, dv: new DataView(source.buffer, 1, 2) });
assert.strictEqual(views.part.buffer, views.whole.buffer);
assert.strictEqual(views.dv.buffer, views.whole.buffer);
assert.deepStrictEqual([views.part.byteOffset, views.part.length, views.dv.byteOffset], [2, 2, 1]);
assert.deepStrictEqual([...views.part], [3, 4]);

const owner = new Uint8Array([1, 2, 3, 4]);
const transferredView = structuredClone(owner.subarray(1), { transfer: [owner.buffer] });
assert.deepStrictEqual([transferredView.byteOffset, [...transferredView], transferredView.buffer.byteLength, owner.byteLength], [1, [2, 3, 4], 4, 0]);

assert.strictEqual(source.subarray(1).buffer, source.buffer);

console.log('structured clone detached OK');
