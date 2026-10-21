const assert = require('node:assert/strict');
const { MessageChannel, Worker } = require('node:worker_threads');

function roundtrip(value) {
  return new Promise(resolve => {
    const { port1, port2 } = new MessageChannel();
    port2.once('message', message => {
      port1.close();
      resolve(message);
    });
    port1.postMessage(value);
  });
}

async function main() {
  const text = 'blob payload '.repeat(4096);
  const blob = new Blob([text], { type: 'text/plain' });
  const got = await roundtrip({ a: blob, b: blob, empty: new Blob([]) });
  assert.equal(got.a instanceof Blob, true);
  assert.equal(got.a, got.b);
  assert.equal(got.a.size, blob.size);
  assert.equal(got.a.type, 'text/plain');
  assert.equal(await got.a.text(), text);
  assert.equal(got.empty.size, 0);
  assert.equal(await got.empty.text(), '');
  assert.equal(await blob.text(), text);
  assert.equal(await got.a.slice(0, 4).text(), 'blob');

  const file = new File(['file body'], 'name.txt', { type: 'text/x', lastModified: 1234 });
  const fileCopy = await roundtrip(file);
  assert.equal(await fileCopy.text(), 'file body');
  assert.equal(fileCopy.type, 'text/x');

  const worker = new Worker(`
    const { parentPort } = require('node:worker_threads');
    parentPort.on('message', async blob => {
      parentPort.postMessage({ text: await blob.text(), echo: blob });
    });
  `, { eval: true });
  const reply = new Promise(resolve => worker.once('message', resolve));
  worker.postMessage(blob);
  const back = await reply;
  assert.equal(back.text, text);
  assert.equal(await back.echo.text(), text);
  await worker.terminate();

  console.log('ok');
}

main();
