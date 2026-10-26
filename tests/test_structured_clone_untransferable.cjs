const assert = require('node:assert');
const { markAsUntransferable } = require('node:worker_threads');

const dataCloneError = { name: 'DataCloneError', message: 'Cannot transfer object of unsupported type.' };

const ab = new ArrayBuffer(8);
markAsUntransferable(ab);
assert.throws(() => structuredClone(ab, { transfer: [ab] }), dataCloneError);
assert.strictEqual(ab.byteLength, 8);
assert.strictEqual(structuredClone(ab).byteLength, 8);

const view = new Uint8Array([1, 2, 3]);
markAsUntransferable(view.buffer);
assert.throws(() => structuredClone(view, { transfer: [view.buffer] }), dataCloneError);
assert.deepStrictEqual([...view], [1, 2, 3]);

const free = new ArrayBuffer(4);
const moved = structuredClone(free, { transfer: [free] });
assert.strictEqual(moved.byteLength, 4);
assert.strictEqual(free.byteLength, 0);

console.log('structuredClone untransferable: ok');
