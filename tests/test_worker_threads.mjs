import assert from 'node:assert/strict';
import { Worker, MessageChannel, BroadcastChannel, isMainThread, isInternalThread, threadId, threadName, workerData, parentPort, setEnvironmentData, markAsUncloneable, SHARE_ENV } from 'node:worker_threads';

const url = new URL('./fixtures/worker_threads/child.mjs', import.meta.url);
const once = (w, ev) => new Promise(r => w.once(ev, r));
const start = (data, options = {}) => new Worker(url, { ...options, workerData: data });

assert.equal(isMainThread, true);
assert.equal(threadId, 0);
assert.equal(workerData, null);
assert.equal(parentPort, null);
assert.equal(isInternalThread, false);
assert.equal(threadName, '');

{
  const o = { a: 1 };
  markAsUncloneable(o);
  assert.throws(() => structuredClone(o), { name: 'DataCloneError' });
  assert.throws(() => structuredClone({ nested: o }), { name: 'DataCloneError' });
  const { port1 } = new MessageChannel();
  assert.throws(() => port1.postMessage(o), { name: 'DataCloneError' });
  port1.close();
}

setEnvironmentData('k', { v: 1 });
process.env.WT_TEST_PARENT = 'yes';

{
  const w = start({ mode: 'echo', n: [1, 2, 3] });
  const exit = once(w, 'exit');
  let online = false;
  w.on('online', () => { online = true; });
  assert.deepEqual(await once(w, 'message'), { isMainThread: false, threadIdPositive: true, data: [1, 2, 3], env: { v: 1 } });
  w.postMessage({ ping: 1 });
  assert.deepEqual(await once(w, 'message'), { got: { ping: 1 } });
  w.postMessage('quit');
  assert.equal(await exit, 0);
  assert.equal(online, true);
}

{
  const w = start({ mode: 'throw' });
  const exit = once(w, 'exit');
  const err = await once(w, 'error');
  assert.ok(err instanceof TypeError);
  assert.equal(err.message, 'worker boom');
  assert.equal(await exit, 1);
}

{
  const w = start({ mode: 'reject' });
  const exit = once(w, 'exit');
  assert.equal((await once(w, 'error')).message, 'rejected in worker');
  assert.equal(await exit, 1);
}

{
  const w = start({ mode: 'exit', code: 4 });
  const msgs = [];
  w.on('message', m => msgs.push(m));
  assert.equal(await once(w, 'exit'), 4);
  assert.deepEqual(msgs, ['exit:4']);
  assert.equal(await once(start({ mode: 'exit-code' }), 'exit'), 3);
}

for (const mode of ['spin', 'idle', 'wait']) {
  const w = start({ mode });
  await once(w, 'message');
  const t = Date.now();
  assert.equal(await w.terminate(), 1);
  assert.ok(Date.now() - t < 2000, `${mode} terminated promptly`);
  assert.equal(await w.terminate(), undefined);
}

{
  const w = start({ mode: 'env' }, { env: { ONLY: 'this' } });
  assert.deepEqual(await once(w, 'message'), { only: 'this', parent: null });
  const w2 = start({ mode: 'env' });
  assert.deepEqual(await once(w2, 'message'), { only: null, parent: 'yes' });
  const w3 = start({ mode: 'env' }, { env: SHARE_ENV });
  assert.deepEqual(await once(w3, 'message'), { only: null, parent: 'yes' });
}

{
  const sab = new SharedArrayBuffer(4);
  const ia = new Int32Array(sab);
  const w = start({ mode: 'atomics', sab });
  const exit = once(w, 'exit');
  await once(w, 'online');
  const reply = once(w, 'message');
  Atomics.store(ia, 0, 41);
  Atomics.notify(ia, 0);
  assert.equal(await reply, 41);
  assert.equal(Atomics.load(ia, 0), 42);
  await exit;
}

{
  const { port1, port2 } = new MessageChannel();
  const w = start({ mode: 'port', port: port2 }, { transferList: [port2] });
  const exit = once(w, 'exit');
  port1.postMessage('hello port');
  assert.equal(await once(port1, 'message'), 'echo:hello port');
  port1.close();
  await exit;
}

{
  const w = start({ mode: 'nested', depth: 2 });
  assert.deepEqual(await once(w, 'message'), { depth: 2, child: { depth: 1, child: { leaf: true } } });
}

{
  const bc = new BroadcastChannel('wt-test');
  const workers = [1, 2, 3].map(i => start({ mode: 'broadcast', i }));
  const exits = workers.map(w => once(w, 'exit'));
  await Promise.all(workers.map(w => once(w, 'message')));
  const replies = [];
  const done = new Promise(r => { bc.onmessage = e => { replies.push(e.data); if (replies.length === 3) r(); }; });
  bc.postMessage('roll call');
  await done;
  assert.deepEqual(replies.sort(), ['w1:roll call', 'w2:roll call', 'w3:roll call']);
  bc.close();
  await Promise.all(exits);
}

{
  const ab = new ArrayBuffer(8);
  new Uint8Array(ab).fill(5);
  const w = start({ mode: 'sum', ab }, { transferList: [ab] });
  assert.equal(ab.byteLength, 0);
  assert.equal(await once(w, 'message'), 40);
}

{
  const w = new Worker('const { parentPort, workerData } = require("worker_threads"); parentPort.postMessage(workerData * 2);', { eval: true, workerData: 21 });
  assert.equal(await once(w, 'message'), 42);
}

{
  const body = 'const wt = require("worker_threads"); wt.parentPort.postMessage([wt.threadName, wt.isInternalThread]);';
  assert.deepEqual(await once(new Worker(body, { eval: true, name: 'named' }), 'message'), ['named', false]);
  assert.deepEqual(await once(new Worker(body, { eval: true }), 'message'), ['', false]);
}

{
  const workers = Array.from({ length: 24 }, (_, i) => start({ mode: 'compute', n: 20000 + i }));
  const exits = workers.map(w => once(w, 'exit'));
  const results = await Promise.all(workers.map(w => once(w, 'message')));
  assert.ok(results.every((r, i) => r === (20000 + i) * (20000 + i - 1) / 2));
  assert.deepEqual(await Promise.all(exits), Array(24).fill(0));
}

{
  const w = new Worker("console.log('captured A'); process.stdout.write('captured B\\n');", { eval: true, stdout: true });
  let out = '';
  w.stdout.on('data', d => { out += d.toString(); });
  const ended = new Promise(r => w.stdout.on('end', r));
  await once(w, 'exit');
  await ended;
  assert.equal(out, 'captured A\ncaptured B\n');
}

{
  const w = new Worker("console.error('err captured');", { eval: true, stderr: true });
  let err = '';
  w.stderr.on('data', d => { err += d.toString(); });
  const ended = new Promise(r => w.stderr.on('end', r));
  await once(w, 'exit');
  await ended;
  assert.equal(err, 'err captured\n');
}

{
  const limits = { maxOldGenerationSizeMb: 16 };
  const churn = new Worker('let s = 0; for (let i = 0; i < 2e6; i++) { const o = { i, a: [i] }; s += o.a[0]; } require("worker_threads").parentPort.postMessage(s > 0);', { eval: true, resourceLimits: limits });
  const churnExit = once(churn, 'exit');
  assert.equal(await once(churn, 'message'), true);
  assert.equal(await churnExit, 0);

  const hog = new Worker('const keep = []; for (;;) keep.push({ n: keep.length });', { eval: true, resourceLimits: limits });
  const hogExit = once(hog, 'exit');
  const err = await once(hog, 'error');
  assert.equal(err.code, 'ERR_WORKER_OUT_OF_MEMORY');
  assert.equal(await hogExit, 1);

  const probe = new Worker('const wt = require("worker_threads"); wt.parentPort.postMessage(wt.resourceLimits.maxOldGenerationSizeMb);', { eval: true, resourceLimits: { maxOldGenerationSizeMb: 64 } });
  assert.equal(await once(probe, 'message'), 64);
}

assert.throws(() => new Worker('child.mjs'), TypeError);

{
  const w = start({ mode: 'idle' });
  w.unref();
}

console.log('worker threads: ok');
