const assert = require('node:assert/strict');
const { Worker, isMainThread, parentPort, workerData } = require('node:worker_threads');

const locks = navigator.locks;

function hold(name, options = {}) {
  let release;
  let grant;
  const granted = new Promise(resolve => { grant = resolve; });
  const done = locks.request(name, options, lock => {
    grant(lock);
    return new Promise(r => { release = r; });
  });
  return { granted, done, release: () => release() };
}

function worker(mode) {
  const w = new Worker(__filename, { workerData: mode });
  const messages = [];
  const waiters = [];
  w.on('message', m => {
    const i = waiters.findIndex(x => x.match(m));
    if (i >= 0) waiters.splice(i, 1)[0].resolve(m);
    else messages.push(m);
  });
  w.next = match => {
    const i = messages.findIndex(match);
    if (i >= 0) return Promise.resolve(messages.splice(i, 1)[0]);
    return new Promise(resolve => waiters.push({ match, resolve }));
  };
  w.exited = new Promise(resolve => w.on('exit', resolve));
  return w;
}

async function crossThread() {
  const x = hold('x');
  const s = hold('s', { mode: 'shared' });
  await x.granted;
  await s.granted;

  const w = worker('contend');
  assert.deepEqual(await w.next(m => m.step === 'probe'), { step: 'probe', x: null, s: 'shared' });
  assert.deepEqual(await w.next(m => m.step === 'waiting'), { step: 'waiting', pending: ['x'], held: 0 });

  const q = await locks.query();
  assert.deepEqual(q.pending, []);
  assert.deepEqual(q.held.map(h => [h.name, h.mode]).sort(), [['s', 'shared'], ['x', 'exclusive']]);
  assert.equal(typeof q.held[0].clientId, 'string');

  x.release();
  assert.deepEqual(await w.next(m => m.step === 'granted'), { step: 'granted', name: 'x', mode: 'exclusive' });
  w.postMessage('release');
  assert.equal(await w.exited, 0);
  s.release();
  assert.equal(await locks.request('x', { ifAvailable: true }, l => l && l.name), 'x');
}

async function workerExitReleases() {
  const w = worker('hold-forever');
  await w.next(m => m.step === 'holding');
  assert.equal(await locks.request('held-by-worker', { ifAvailable: true }, l => l), null);
  const after = locks.request('held-by-worker', l => l.name);
  await w.terminate();
  assert.equal(await after, 'held-by-worker');
}

async function queueOrder() {
  const order = [];
  const first = hold('q', { mode: 'shared' });
  await first.granted;
  const ex = locks.request('q', () => { order.push('exclusive'); });
  const sh = locks.request('q', { mode: 'shared' }, () => { order.push('shared'); });
  assert.equal(await locks.request('q', { mode: 'shared', ifAvailable: true }, l => l), null);
  const { pending } = await locks.query();
  assert.deepEqual(pending.filter(p => p.name === 'q').map(p => p.mode), ['exclusive', 'shared']);
  first.release();
  await Promise.all([ex, sh]);
  assert.deepEqual(order, ['exclusive', 'shared']);
}

async function signals() {
  const aborted = AbortSignal.abort();
  await assert.rejects(locks.request('sig', { signal: aborted }, () => 1), e => e === aborted.reason);

  const busy = hold('sig');
  await busy.granted;
  const ac = new AbortController();
  const waiting = locks.request('sig', { signal: ac.signal }, () => 'never');
  ac.abort();
  await assert.rejects(waiting, { name: 'AbortError' });
  busy.release();
  await busy.done;
  await new Promise(r => setTimeout(r, 10));
  assert.equal(await locks.request('sig', { ifAvailable: true }, l => l && l.name), 'sig');
}

async function steal() {
  const victim = hold('st');
  await victim.granted;
  const outcome = victim.done.then(() => 'resolved', e => e.name);
  assert.equal(await locks.request('st', { steal: true }, l => l.mode), 'exclusive');
  assert.equal(await outcome, 'AbortError');
  victim.release();
}

async function validation() {
  await assert.rejects(locks.request('-reserved', () => 1), { name: 'NotSupportedError' });
  await assert.rejects(locks.request('v', { steal: true, ifAvailable: true }, () => 1), { name: 'NotSupportedError' });
  await assert.rejects(locks.request('v', { steal: true, mode: 'shared' }, () => 1), { name: 'NotSupportedError' });
  await assert.rejects(locks.request('v', { ifAvailable: true, signal: new AbortController().signal }, () => 1), { name: 'NotSupportedError' });
}

async function child() {
  const keep = setInterval(() => {}, 1000);
  if (workerData === 'hold-forever') {
    locks.request('held-by-worker', () => {
      parentPort.postMessage({ step: 'holding' });
      return new Promise(() => {});
    });
    return;
  }
  const x = await locks.request('x', { ifAvailable: true }, l => l);
  const s = await locks.request('s', { mode: 'shared', ifAvailable: true }, l => l && l.mode);
  parentPort.postMessage({ step: 'probe', x, s });
  let release;
  const granted = locks.request('x', lock => {
    parentPort.postMessage({ step: 'granted', name: lock.name, mode: lock.mode });
    return new Promise(r => { release = r; });
  });
  const own = await locks.query();
  parentPort.postMessage({ step: 'waiting', pending: own.pending.map(p => p.name), held: own.held.length });
  parentPort.once('message', async () => {
    release();
    await granted;
    clearInterval(keep);
    parentPort.close();
  });
}

if (!isMainThread) child();
else (async () => {
  await crossThread();
  if (process.versions.ant) await workerExitReleases();
  await queueOrder();
  await signals();
  await steal();
  await validation();
  console.log('navigator.locks threads: ok');
})();
