const assert = require('node:assert/strict');
const { Worker, postMessageToThread, threadId } = require('node:worker_threads');

const body = `
  const { postMessageToThread, parentPort } = require('worker_threads');
  process.on('workerMessage', (value, source) => {
    if (value === 'throw') throw new Error('listener threw');
    if (value === 'slow') { const end = Date.now() + 200; while (Date.now() < end) {} }
    parentPort.postMessage({ got: value, source });
  });
  parentPort.on('message', async m => {
    if (m === 'tomain') parentPort.postMessage({ resolved: await postMessageToThread(0, { hi: 1 }) === undefined });
    if (m === 'bye') { process.removeAllListeners('workerMessage'); parentPort.close(); }
  });
  parentPort.postMessage('ready');
`;

const code = async (promise, expected) => {
  await assert.rejects(promise, err => err.code === expected);
};

assert.equal(typeof postMessageToThread, 'function');
assert.equal(postMessageToThread.length, 4);

const fromWorker = [];
process.on('workerMessage', (value, source) => fromWorker.push({ value, source }));

const w = new Worker(body, { eval: true });
const quiet = new Worker('require("worker_threads").parentPort.postMessage("ready")', { eval: true });
const got = [];
const quietReady = new Promise(resolve => quiet.once('message', resolve));

w.on('message', m => { if (m !== 'ready') got.push(m); });
w.once('message', async () => {
  assert.equal(await postMessageToThread(w.threadId, { n: 1 }), undefined);
  await code(postMessageToThread(threadId, 1), 'ERR_WORKER_MESSAGING_SAME_THREAD');
  await code(postMessageToThread(9999, 1), 'ERR_WORKER_MESSAGING_FAILED');
  await code(postMessageToThread('x', 1), 'ERR_WORKER_MESSAGING_FAILED');
  await code(postMessageToThread(w.threadId, 'throw'), 'ERR_WORKER_MESSAGING_ERRORED');
  await code(postMessageToThread(w.threadId, 'slow', 20), 'ERR_WORKER_MESSAGING_TIMEOUT');
  await quietReady;
  await code(postMessageToThread(quiet.threadId, 1), 'ERR_WORKER_MESSAGING_FAILED');
  await assert.rejects(postMessageToThread(w.threadId, () => {}), { name: 'DataCloneError' });
  await code(postMessageToThread(w.threadId, 1, 'soon'), 'ERR_INVALID_ARG_TYPE');
  quiet.terminate();

  w.on('message', m => {
    if (!m.resolved) return;
    assert.deepEqual(fromWorker, [{ value: { hi: 1 }, source: w.threadId }]);
    w.postMessage('bye');
  });
  w.postMessage('tomain');
});

w.on('exit', exitCode => {
  assert.equal(exitCode, 0);
  assert.deepEqual(got.filter(m => m.got !== undefined), [
    { got: { n: 1 }, source: 0 },
    { got: 'slow', source: 0 },
  ]);
  console.log('postMessageToThread: ok');
});
