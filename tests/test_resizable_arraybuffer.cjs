const assert = require('node:assert/strict');
const { MessageChannel, Worker } = require('node:worker_threads');

const proto = ArrayBuffer.prototype;
assert.equal(typeof proto.resize, 'function');
assert.equal(proto.resize.length, 1);
assert.equal(typeof Object.getOwnPropertyDescriptor(proto, 'resizable').get, 'function');
assert.equal(typeof Object.getOwnPropertyDescriptor(proto, 'maxByteLength').get, 'function');

const fixed = new ArrayBuffer(8);
assert.equal(fixed.resizable, false);
assert.equal(fixed.maxByteLength, 8);
assert.throws(() => fixed.resize(4), TypeError);
assert.equal(new ArrayBuffer(8, {}).resizable, false);
assert.equal(new ArrayBuffer(8, { maxByteLength: undefined }).resizable, false);
assert.throws(() => new ArrayBuffer(16, { maxByteLength: 8 }), RangeError);
assert.throws(() => new ArrayBuffer(0, { maxByteLength: -1 }), RangeError);

const rab = new ArrayBuffer(8, { maxByteLength: 16 });
assert.equal(rab.resizable, true);
assert.equal(rab.maxByteLength, 16);
assert.equal(rab.byteLength, 8);

const tracking = new Uint8Array(rab);
const offsetTracking = new Uint16Array(rab, 4);
const fixedView = new Uint8Array(rab, 2, 4);
const dvTracking = new DataView(rab);
const dvFixed = new DataView(rab, 4, 4);
tracking.set([1, 2, 3, 4, 5, 6, 7, 8]);

rab.resize(12);
assert.equal(rab.byteLength, 12);
assert.equal(tracking.length, 12);
assert.deepEqual([...tracking], [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0]);
assert.equal(offsetTracking.length, 4);
assert.equal(fixedView.length, 4);
assert.equal(dvTracking.byteLength, 12);
assert.equal(dvFixed.byteLength, 4);

rab.resize(3);
assert.equal(tracking.length, 3);
assert.deepEqual([...tracking], [1, 2, 3]);
assert.equal(offsetTracking.length, 0);
assert.equal(offsetTracking.byteOffset, 0);
assert.equal(offsetTracking.byteLength, 0);
assert.equal(fixedView.length, 0);
assert.equal(fixedView.byteOffset, 0);
assert.equal(fixedView[0], undefined);
fixedView[0] = 99;
assert.throws(() => fixedView.fill(1), TypeError);
assert.throws(() => [...fixedView], TypeError);
assert.throws(() => dvFixed.byteLength, TypeError);
assert.throws(() => dvFixed.getUint8(0), TypeError);
assert.equal(dvTracking.byteLength, 3);

rab.resize(16);
assert.deepEqual([...tracking], [1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
assert.equal(fixedView.length, 4);
assert.equal(fixedView.byteOffset, 2);
assert.deepEqual([...fixedView], [3, 0, 0, 0]);
assert.equal(offsetTracking.length, 6);
assert.equal(dvFixed.byteLength, 4);
assert.throws(() => rab.resize(17), RangeError);
assert.equal(rab.byteLength, 16);

const sub = tracking.subarray(4);
rab.resize(10);
assert.equal(sub.length, 6);
const subFixed = tracking.subarray(4, 6);
rab.resize(14);
assert.equal(subFixed.length, 2);

let sum = 0;
for (let round = 0; round < 200; round++) {
  rab.resize(round % 2 ? 16 : 8);
  for (let i = 0; i < tracking.length; i++) sum += tracking[i] === undefined ? 1000 : 1;
}
assert.equal(sum, 100 * 16 + 100 * 8);

const sliced = rab.slice(0, 4);
assert.equal(sliced.resizable, false);
assert.equal(sliced.byteLength, 4);

const moved = rab.transfer();
assert.equal(rab.detached, true);
assert.equal(moved.resizable, true);
assert.equal(moved.maxByteLength, 16);
assert.equal(tracking.length, 0);
assert.throws(() => rab.resize(4), TypeError);
const grownTransfer = moved.transfer(12);
assert.equal(grownTransfer.byteLength, 12);
assert.equal(grownTransfer.resizable, true);
assert.throws(() => grownTransfer.transfer(32), RangeError);
const fixedTransfer = grownTransfer.transferToFixedLength();
assert.equal(fixedTransfer.resizable, false);
assert.equal(fixedTransfer.byteLength, 12);

const cloned = structuredClone(new ArrayBuffer(4, { maxByteLength: 32 }));
assert.equal(cloned.resizable, true);
assert.equal(cloned.maxByteLength, 32);
assert.equal(cloned.byteLength, 4);

const sab = new SharedArrayBuffer(4, { maxByteLength: 8 });
assert.equal(sab.growable, true);
assert.equal(sab.maxByteLength, 8);
assert.equal(new SharedArrayBuffer(4).growable, false);
assert.equal(new SharedArrayBuffer(4).maxByteLength, 4);
const sabView = new Int32Array(sab);
sab.grow(8);
assert.equal(sab.byteLength, 8);
assert.equal(sabView.length, 2);
assert.throws(() => sab.grow(4), RangeError);
assert.throws(() => sab.grow(9), RangeError);
assert.throws(() => new SharedArrayBuffer(4).grow(4), TypeError);

{
  const buf = new ArrayBuffer(8, { maxByteLength: 16 });
  const fixedView = new Uint8Array(buf, 4, 4);
  const view = new Uint8Array(buf);
  view.set([1, 2, 3, 4, 5, 6, 7, 8]);
  buf.resize(4);
  for (const fn of [v => v.map(x => x), v => v.forEach(() => {}), v => v.at(0), v => v.join(), v => [...v.keys()],
    v => v.includes(1), v => v.slice(), v => v.set([1]), v => v.fill(0), v => v.sort()]) {
    assert.throws(() => fn(fixedView), TypeError);
  }
  assert.equal(fixedView.subarray(0).length, 0);
  assert.equal(Object.prototype.toString.call(fixedView), '[object Uint8Array]');
  assert.deepEqual(Object.keys(fixedView), []);
  assert.equal(0 in fixedView, false);
  assert.equal(view.join(), '1,2,3,4');
  view.set([9, 9], 2);
  assert.deepEqual([...view], [1, 2, 9, 9]);
  assert.throws(() => view.set([1, 2, 3, 4, 5]), RangeError);
  const clone = structuredClone(view);
  assert.deepEqual([clone.length, clone.buffer.resizable, clone.buffer.maxByteLength], [4, true, 16]);
  const nodeBuffer = Buffer.from(buf);
  buf.resize(6);
  assert.equal(nodeBuffer.length, 6);
  assert.equal(new Uint32Array(new ArrayBuffer(6, { maxByteLength: 16 })).length, 1);
  assert.throws(() => new Uint32Array(new ArrayBuffer(6)), RangeError);
  const dv = new DataView(buf, 4, 2);
  buf.resize(2);
  assert.throws(() => dv.setUint8(0, 1), TypeError);
}

async function growFromAnotherThread() {
  const shared = new SharedArrayBuffer(8, { maxByteLength: 64 });
  const worker = new Worker(`
    const { parentPort, workerData } = require('node:worker_threads');
    const view = new Int32Array(workerData);
    parentPort.postMessage(view.length);
    parentPort.once('message', () => {
      let count = 0;
      for (const _ of view) count++;
      parentPort.postMessage([view.length, view.byteLength, workerData.byteLength, view[6], count]);
    });
  `, { eval: true, workerData: shared });
  const first = await new Promise(resolve => worker.once('message', resolve));
  assert.equal(first, 2);
  shared.grow(32);
  new Int32Array(shared)[6] = 99;
  const after = new Promise(resolve => worker.once('message', resolve));
  worker.postMessage('go');
  assert.deepEqual(await after, [8, 32, 32, 99, 8]);
  await worker.terminate();
}

async function messages() {
  const { port1, port2 } = new MessageChannel();
  const received = new Promise(resolve => port2.once('message', resolve));
  const sent = new ArrayBuffer(4, { maxByteLength: 64 });
  new Uint8Array(sent)[0] = 7;
  port1.postMessage({ copy: sent, moved: new ArrayBuffer(2, { maxByteLength: 8 }) });
  const got = await received;
  port1.close();
  assert.equal(got.copy.resizable, true);
  assert.equal(got.copy.maxByteLength, 64);
  assert.equal(new Uint8Array(got.copy)[0], 7);
  got.copy.resize(64);
  assert.equal(got.copy.byteLength, 64);
}

messages().then(growFromAnotherThread).then(() => console.log('ok'));
