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

const shared = new ArrayBuffer(8);
const sharedView = new Uint8Array(shared);
sharedView[1] = 9;
const graph = structuredClone({ buffer: shared, view: sharedView }, { transfer: [shared] });
assert.deepStrictEqual([shared.detached, sharedView.length, graph.view.buffer === graph.buffer, graph.view[1]], [true, 0, true, 9]);
graph.view[1] = 11;
const graphAgain = structuredClone(graph.buffer, { transfer: [graph.buffer] });
assert.deepStrictEqual([graph.buffer.detached, graph.view.length, new Uint8Array(graphAgain)[1]], [true, 0, 11]);

const kept = new ArrayBuffer(4);
assert.throws(() => structuredClone({ kept, fn() {} }, { transfer: [kept] }), isDataCloneError);
assert.deepStrictEqual([kept.detached, kept.byteLength], [false, 4]);

const original = new ArrayBuffer(16);
new Uint8Array(original)[3] = 5;
const sameLength = original.transfer();
const shorter = sameLength.transfer(4);
const longer = shorter.transfer(32);
assert.deepStrictEqual([original.detached, sameLength.detached, shorter.detached], [true, true, true]);
assert.deepStrictEqual([longer.byteLength, new Uint8Array(longer)[3], new Uint8Array(longer)[20]], [32, 5, 0]);

assert.throws(() => structuredClone(1, { transfer: 5 }), TypeError);
assert.throws(() => structuredClone(1, { transfer: 'ab' }), TypeError);
assert.throws(() => structuredClone(1, 5), TypeError);
assert.throws(() => structuredClone(1, { get transfer() { throw new RangeError('boom'); } }), RangeError);
assert.strictEqual(structuredClone(1, null), 1);
const viaSet = new ArrayBuffer(4);
structuredClone(viaSet, { transfer: new Set([viaSet]) });
assert.strictEqual(viaSet.detached, true);
const viaGenerator = new ArrayBuffer(4);
structuredClone(viaGenerator, { transfer: (function* () { yield viaGenerator; })() });
assert.strictEqual(viaGenerator.detached, true);

const memory = new WebAssembly.Memory({ initial: 1 });
assert.throws(() => structuredClone(memory.buffer, { transfer: [memory.buffer] }), isDataCloneError);
assert.strictEqual(memory.buffer.byteLength, 65536);

const big = [];
const before = Ant.stats().external.buffers;
for (let i = 0; i < 8; i++) {
  const buffer = new ArrayBuffer(32 << 20);
  big.push(structuredClone(buffer, { transfer: [buffer] }));
}
assert.ok(Ant.stats().external.buffers - before < 9 * (32 << 20), 'transfer must not copy');

console.log('structured clone detached OK');
