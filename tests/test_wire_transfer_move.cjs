const assert = require('node:assert/strict');
const { MessageChannel, Worker } = require('node:worker_threads');

function roundtrip(value, transfer) {
  return new Promise(resolve => {
    const { port1, port2 } = new MessageChannel();
    port2.once('message', message => {
      port1.close();
      resolve(message);
    });
    port1.postMessage(value, transfer);
  });
}

async function main() {
  const bytes = new Uint8Array(1 << 20);
  for (let i = 0; i < bytes.length; i += 4096) bytes[i] = i & 0xff;
  bytes[bytes.length - 1] = 7;
  const got = await roundtrip(bytes, [bytes.buffer]);
  assert.equal(bytes.byteLength, 0);
  assert.equal(bytes.buffer.byteLength, 0);
  assert.equal(got.byteLength, 1 << 20);
  assert.equal(got[4096], 4096 & 0xff);
  assert.equal(got[got.length - 1], 7);

  const buf = new ArrayBuffer(64);
  new Uint8Array(buf).set([1, 2, 3, 4], 16);
  const views = await roundtrip({ a: new Uint8Array(buf, 16, 4), b: new DataView(buf, 16, 2), raw: buf }, [buf]);
  assert.equal(buf.byteLength, 0);
  assert.deepEqual([...views.a], [1, 2, 3, 4]);
  assert.equal(views.b.getUint8(1), 2);
  assert.equal(views.a.buffer, views.raw);
  assert.equal(views.b.buffer, views.raw);
  assert.equal(views.raw.byteLength, 64);

  const unlisted = new ArrayBuffer(8);
  const detachedOnly = await roundtrip({ ok: 1 }, [unlisted]);
  assert.deepEqual(detachedOnly, { ok: 1 });
  assert.equal(unlisted.byteLength, 0);

  const kept = new Uint8Array([9, 9, 9]);
  assert.throws(() => new MessageChannel().port1.postMessage({ kept, fn() {} }, [kept.buffer]));
  assert.equal(kept.byteLength, 3);
  assert.deepEqual([...kept], [9, 9, 9]);

  const resent = await roundtrip(await roundtrip(new Uint8Array([5, 6]), undefined), undefined);
  assert.deepEqual([...resent], [5, 6]);
  const twice = new Uint8Array([1, 2, 3]);
  const first = await roundtrip(twice, [twice.buffer]);
  const second = await roundtrip(first, [first.buffer]);
  assert.equal(first.byteLength, 0);
  assert.deepEqual([...second], [1, 2, 3]);

  const dropped = new MessageChannel();
  const lost = new Uint8Array(1 << 16);
  dropped.port1.postMessage(lost, [lost.buffer]);
  dropped.port1.close();
  dropped.port2.close();
  assert.equal(lost.byteLength, 0);

  const worker = new Worker(`
    const { parentPort } = require('node:worker_threads');
    parentPort.on('message', view => {
      let sum = 0;
      for (let i = 0; i < view.length; i += 1024) sum += view[i];
      view[0] = 42;
      parentPort.postMessage({ sum, view }, [view.buffer]);
    });
  `, { eval: true });
  const toWorker = new Uint8Array(1 << 20).fill(1);
  const reply = new Promise(resolve => worker.once('message', resolve));
  worker.postMessage(toWorker, [toWorker.buffer]);
  const back = await reply;
  assert.equal(toWorker.byteLength, 0);
  assert.equal(back.sum, (1 << 20) / 1024);
  assert.equal(back.view.byteLength, 1 << 20);
  assert.equal(back.view[0], 42);
  await worker.terminate();

  console.log('ok');
}

main();
