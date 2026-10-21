import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import * as wt from 'node:worker_threads';

const source = code => 'data:text/javascript,' + encodeURIComponent(code);

function run(worker, steps = {}) {
  const seen = [];
  return new Promise(resolve => {
    worker.addEventListener('open', () => seen.push('open'));
    worker.addEventListener('message', e => {
      seen.push(e.data);
      steps.message?.(e, worker);
    });
    worker.addEventListener('error', e => {
      seen.push({ error: e.constructor.name, text: String(e.message).includes(steps.expectError ?? '\0') });
      e.preventDefault();
    });
    worker.addEventListener('close', e => resolve({ seen, code: e.code, kind: e.constructor.name }));
  });
}

assert.equal(typeof Worker, 'function');
assert.notEqual(Worker, wt.Worker);
assert.throws(() => new Worker(), TypeError);

{
  const w = new Worker(source('postMessage([self === globalThis, typeof postMessage, "onmessage" in globalThis]); process.exit(0)'));
  assert.ok(w instanceof EventTarget);
  assert.equal(typeof w.on, 'undefined');
  assert.equal(typeof w.threadId, 'number');
  assert.equal(Object.prototype.toString.call(w), '[object Worker]');
  const r = await run(w);
  assert.deepEqual(r, { seen: ['open', [true, 'function', true]], code: 0, kind: 'CloseEvent' });
}

{
  const w = new Worker(source(`
    const wt = await import('node:worker_threads');
    self.onmessage = e => {
      if (e.data === 'exit') process.exit(7);
      if (e.data === 'port') return e.ports[0].postMessage('via port');
      postMessage({ echo: e.data, type: e.type, wd: wt.workerData, main: wt.isMainThread });
    };
    postMessage('ready');
  `), { workerData: { a: 1 } });
  const r = await run(w, {
    message(e, worker) {
      if (e.data === 'ready') worker.postMessage('hi');
      else if (e.data.echo === 'hi') {
        const { port1, port2 } = new MessageChannel();
        port1.onmessage = ev => { port1.close(); assert.equal(ev.data, 'via port'); worker.postMessage('exit'); };
        worker.postMessage('port', [port2]);
      }
    },
  });
  assert.deepEqual(r, {
    seen: ['open', 'ready', { echo: 'hi', type: 'message', wd: { a: 1 }, main: false }],
    code: 7,
    kind: 'CloseEvent',
  });
}

{
  const r = await run(new Worker(source('setTimeout(() => { throw new TypeError("boom") })')), { expectError: 'boom' });
  assert.deepEqual(r.seen.slice(-1), [{ error: 'ErrorEvent', text: true }]);
  assert.equal(r.code, 1);
}

{
  const w = new Worker(source('self.onmessage = () => {}'));
  const done = run(w, {});
  setTimeout(() => w.terminate(), 50);
  assert.equal((await done).code, 0);
}

{
  const blob = new Blob(['postMessage("from blob"); process.exit(0)'], { type: 'application/javascript' });
  const r = await run(new Worker(URL.createObjectURL(blob)));
  assert.deepEqual(r.seen, ['open', 'from blob']);
}

{
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-web-worker-'));
  const previous = process.cwd();
  try {
    fs.writeFileSync(path.join(dir, 'where.mjs'), 'postMessage("here"); process.exit(0)');
    process.chdir(dir);
    for (const spec of ['./where.mjs', 'where.mjs']) {
      const r = await run(new Worker(spec));
      assert.deepEqual(r.seen, ['open', 'here'], spec);
    }
  } finally {
    process.chdir(previous);
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

console.log('web worker: ok');
